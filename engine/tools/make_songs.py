#!/usr/bin/env python3
# OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
# Licensed under the GNU General Public License version 3 (see LICENSE.txt).
"""Writes the engine's own songs, engine/music/*.mid, from the tunes below.

Every tune here was composed for OpenSimFarm and is under the project's licence (GPL-3.0-or-later): free to play,
change and pass on. The build embeds engine/music/*.mid into the program (tools/EmbedMusic.cmake); the radio lists
them after the game's songs. Each file names its title (track name) and the game song whose mood it shares
(a text event "mood:<file>.xmi").

    python3 engine/tools/make_songs.py           # rewrites engine/music/*.mid

Melody notation: notes like G4, F#5, Bb3 or r (rest), each with an optional :length in eighth notes (default 1);
bars are separated by |, and every bar must fill its meter. Chords: one per bar, or two (each half the bar).
"""
import os
import struct

TPQ = 480            # MIDI ticks per quarter note
EIGHTH = TPQ // 2

# General MIDI programs
FIDDLE, BANJO, HARMONICA, ACCORDION, STEEL_GUITAR, NYLON_GUITAR, ACOUSTIC_BASS, STRINGS, PIANO = 40, 105, 22, 21, 25, 24, 32, 48, 0
KICK, SNARE, STICK, HAT = 36, 38, 37, 42

SONGS = [
    dict(file='sunup.mid', title='SUNUP', mood='amain.xmi', meter=8, tempo=96, leads=(HARMONICA, FIDDLE), pad=True, drums=False,
         a='D4:2 G4:2 B4:3 A4 | G4:2 E4:2 C5:4 | B4:2 D5:2 B4:2 G4:2 | A4:6 D4:2 | D4:2 G4:2 B4:3 A4 | C5:2 E5:3 D5 C5:2 | B4:2 G4:2 A4:2 F#4:2 | G4:8',
         ac='G | C | G | D | G | C | G D | G',
         b='E5:3 D5 B4:2 G4:2 | C5:3 B4 A4:2 G4:2 | B4:2 D5:2 G5:4 | F#5:2 E5:2 D5:4 | E5:3 D5 B4:2 G4:2 | C5:2 E5:2 G5:2 E5:2 | D5:3 C5 B4:2 A4:2 | G4:8',
         bc='Em | C | G | D | Em | C | D | G'),
    dict(file='chores.mid', title='MORNING CHORES', mood='genfarm.xmi', meter=8, tempo=104, leads=(BANJO, FIDDLE), pad=False, drums=True,
         a='F#4 A4 D5 A4 F#4 A4 D5 E5 | F#5:2 E5 D5 E5:2 D5:2 | B4 D5 G5 D5 B4 D5 G5 F#5 | E5 D5 A4 F#4 A4:4 | '
           'F#4 A4 D5 A4 F#4 A4 D5 E5 | F#5:2 A5:2 F#5 E5 D5:2 | E5 C#5 A4 C#5 E5:2 G5:2 | F#5:2 D5:2 D5:4',
         ac='D | D | G | D | D | D | A7 | D',
         b='G5:2 G5 F#5 E5 D5 B4:2 | A4:2 D5 E5 F#5:4 | G5:2 B5 A5 G5 F#5 E5 D5 | E5:2 C#5:2 A4:4 | '
           'G5:2 G5 F#5 E5 D5 B4:2 | A4 D5 F#5 A5 F#5 D5 F#5:2 | E5 F#5 E5 C#5 A4:2 C#5:2 | D5:8',
         bc='G | D | G | A | G | D | A7 | D'),
    dict(file='harvest_polka.mid', title='HARVEST POLKA', mood='ffarm.xmi', meter=4, tempo=120, leads=(ACCORDION, FIDDLE), pad=False, drums=True,
         a='D5 B4 G4 B4 | D5:2 G5:2 | F#5 E5 D5 C5 | A4:4 | C5 A4 F#4 A4 | C5:2 F#5:2 | B4 D5 G5 D5 | B4:2 G4:2 | '
           'D5 B4 G4 B4 | D5:2 G5:2 | E5 G5 E5 C5 | C5:2 E5:2 | D5 F#5 A5 F#5 | E5 D5 C5 A4 | A4 B4 C5 A4 | G4:4',
         ac='G | G | D | D | D | D | G | G | G | G | C | C | D | D | D | G',
         b='E5 D5 C5 E5 | G5:2 E5:2 | D5 C5 B4 D5 | B4:2 G4:2 | A4 B4 C5 D5 | F#5 A5 F#5 D5 | G5 D5 B4 D5 | G5:4 | '
           'E5 D5 C5 E5 | G5:2 E5:2 | D5 C5 B4 D5 | B4:2 G4:2 | A4 F#4 A4 C5 | E5 D5 C5 A4 | D5 C5 B4 A4 | G4:4',
         bc='C | C | G | G | D | D | G | G | C | C | G | G | D | D | D | G'),
    dict(file='dry_spell.mid', title='DRY SPELL', mood='dfarm.xmi', meter=6, tempo=84, leads=(HARMONICA, FIDDLE), pad=True, drums=False,
         a='B4:4 E5:2 | G5:3 F#5 E5:2 | E5:2 C5:2 A4:2 | B4:6 | C5:4 E5:2 | D5:3 C5 A4:2 | B4:2 G4:2 E4:2 | F#4:6 | '
           'B4:4 E5:2 | G5:3 F#5 E5:2 | E5:2 C5:2 A4:2 | B4:6 | C5:4 E5:2 | D5:3 C5 A4:2 | A4:2 F#4:2 D#4:2 | E4:6',
         ac='Em | Em | Am | Em | C | D | Em | B7 | Em | Em | Am | Em | C | D | B7 | Em',
         b='D5:4 B4:2 | G5:3 F#5 E5:2 | E5:4 C5:2 | D5:6 | C5:3 B4 A4:2 | B4:3 A4 G4:2 | A4:2 C5:2 E5:2 | D#5:6 | '
           'E5:4 G5:2 | F#5:4 D5:2 | E5:3 D5 C5:2 | B4:6 | A4:2 C5:2 E5:2 | G4:2 B4:2 E5:2 | B4:2 A4:2 F#4:2 | E4:6',
         bc='G | G | C | G | Am | Em | Am | B7 | Em | D | C | G | Am | Em | B7 | Em'),
    dict(file='fairground.mid', title='FAIR TWO-STEP', mood='carnival.xmi', meter=8, tempo=116, leads=(ACCORDION, PIANO), pad=False, drums=True,
         a='C5:2 E5 G5 C6:2 G5:2 | G5 E5 C5 E5 G5:2 E5:2 | F5:2 D5 B4 G4:2 B4:2 | C5:2 E5:2 C5:4 | '
           'A5:2 F5 A5 C6:2 A5:2 | G5:2 E5 G5 C6:4 | B5 A5 G5 F5 D5:2 B4:2 | C5:4 r:4',
         ac='C | C | G7 | C | F | C | G7 | C',
         b='E5:2 A5:2 C6:2 A5:2 | E5 D5 C5 B4 A4:4 | F5:2 A5:2 D6:2 A5:2 | G5 F5 E5 D5 B4:4 | '
           'C5:2 E5:2 G5:2 C6:2 | C#6:2 A5:2 G5:2 E5:2 | F5:2 D5:2 G5:2 B4:2 | C5:4 r:4',
         bc='Am | Am | Dm | G7 | C | A7 | Dm G7 | C'),
    dict(file='dust_and_spurs.mid', title='DUST AND SPURS', mood='rodeo.xmi', meter=8, tempo=132, leads=(FIDDLE, BANJO), pad=False, drums=True,
         a='A4 C#5 E5 A5 E5 C#5 A4 C#5 | E5 F#5 E5 C#5 A4 B4 C#5 E5 | F#5 A5 F#5 D5 A4 D5 F#5 A5 | E5 C#5 A4 C#5 E5:2 A4:2 | '
           'A4 C#5 E5 A5 E5 C#5 A4 C#5 | E5 A5 G#5 F#5 E5 C#5 B4 A4 | B4 E5 G#5 B5 G#5 E5 D5 B4 | A4:2 C#5:2 A4:4',
         ac='A | A | D | A | A | A | E7 | A',
         b='F#5 A5 D6 A5 F#5 D5 F#5 A5 | B5 A5 F#5 D5 E5 F#5 D5 B4 | A4 C#5 E5 A5 C#6 A5 E5 C#5 | E5 F#5 E5 C#5 A4:4 | '
           'F#5 A5 D6 A5 F#5 D5 F#5 A5 | B5 A5 G5 F#5 E5 D5 C#5 B4 | G#4 B4 E5 G#5 B5 G#5 E5 D5 | C#5:2 A4:2 A4:4',
         bc='D | D | A | A | D | D | E7 | A'),
    dict(file='hoedown.mid', title='HARVEST HOEDOWN', mood='cowman.xmi', meter=8, tempo=126, leads=(BANJO, FIDDLE), pad=False, drums=True,
         a='G4 B4 D5 G5 B5:2 G5:2 | A5 G5 F#5 G5 D5:2 B4:2 | C5 E5 G5 C6 E6:2 C6:2 | B5 A5 G5 D5 B4:4 | '
           'G4 B4 D5 G5 B5:2 G5:2 | A5 B5 A5 G5 E5:2 D5:2 | F#5 A5 F#5 D5 E5 F#5 E5 C5 | B4:2 D5:2 G4:4',
         ac='G | G | C | G | G | G | D7 | G',
         b='E5:2 G5:2 C6:2 G5:2 | B5:2 A5 G5 D5:2 B4:2 | A4:2 D5 E5 F#5:2 A5:2 | G5:2 D5:2 B4:4 | '
           'E5:2 G5:2 C6:2 E6:2 | D6:2 B5 A5 G5:2 D5:2 | F#5 E5 D5 C5 A4:2 F#4:2 | G4:4 r:4',
         bc='C | G | D | G | C | G | D7 | G'),
]

NOTE = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def pitch(name):
    """G4 -> 67 (C4 = 60); r -> None"""
    if name == 'r':
        return None
    n = NOTE[name[0]]
    i = 1
    while name[i] in '#b':
        n += 1 if name[i] == '#' else -1
        i += 1
    return 12 * (int(name[i:]) + 1) + n


def melody(text, meter, what):
    """bars of (pitch, eighths)"""
    bars = []
    for k, bar in enumerate(text.split('|')):
        notes = []
        for tok in bar.split():
            name, _, length = tok.partition(':')
            notes.append((pitch(name), int(length) if length else 1))
        total = sum(n[1] for n in notes)
        if total != meter:
            raise SystemExit(f'{what} bar {k + 1}: {total} eighths, the meter has {meter}')
        bars.append(notes)
    return bars


def chord_notes(sym):
    """'Em' -> root and the triad (or seventh chord) as MIDI notes"""
    root = NOTE[sym[0]]
    rest = sym[1:]
    if rest[:1] in ('#', 'b'):
        root += 1 if rest[0] == '#' else -1
        rest = rest[1:]
    minor = rest.startswith('m')
    seventh = rest.endswith('7')
    third = 3 if minor else 4
    tones = [0, third, 7] + ([10] if seventh else [])
    return root, tones


def chords(text, n, what):
    out = [bar.split() for bar in text.split('|')]
    if len(out) != n:
        raise SystemExit(f'{what}: {len(out)} chord bars for {n} melody bars')
    return out


class Track:
    def __init__(self):
        self.ev = []   # (tick, order, bytes)

    def note(self, tick, ch, note, vel, length):
        self.ev.append((tick, 1, bytes([0x90 | ch, note, vel])))
        self.ev.append((tick + max(1, length - 8), 0, bytes([0x80 | ch, note, 0])))

    def raw(self, tick, data, order=0):
        self.ev.append((tick, order, data))

    def bytes(self):
        out = bytearray()
        last = 0
        for tick, _, data in sorted(self.ev, key=lambda e: (e[0], e[1])):
            out += vlq(tick - last) + data
            last = tick
        out += vlq(0) + b'\xff\x2f\x00'
        return bytes(out)


def vlq(v):
    b = [v & 0x7F]
    v >>= 7
    while v:
        b.insert(0, 0x80 | (v & 0x7F))
        v >>= 7
    return bytes(b)


def meta(kind, data):
    return b'\xff' + bytes([kind]) + vlq(len(data)) + data


def accompany(t, start, syms, meter, ch_gtr, ch_bass, drums, ch_pad, last):
    """one bar of rhythm guitar, bass, drums (and a held pad) under the chord symbols"""
    bar = meter * EIGHTH
    part = bar // len(syms)
    for k, sym in enumerate(syms):
        root, tones = chord_notes(sym)
        s = start + k * part
        bass_root = 36 + (root % 12)          # C2 .. B2
        bass_fifth = bass_root + 7 if bass_root + 7 <= 47 else bass_root - 5
        gtr = [48 + ((root + x) % 12) + (12 if (root + x) % 12 < 4 else 0) for x in tones]  # around E3 .. D#4
        if ch_pad is not None:
            for n in gtr:
                t.note(s, ch_pad, n + 12, 40, part)
        if last:
            t.note(s, ch_bass, bass_root, 96, bar)
            for n in gtr:
                t.note(s, ch_gtr, n, 80, bar)
            if drums:
                t.note(s, 9, KICK, 100, EIGHTH)
            return
        if meter == 6:                         # waltz: bass on 1, chord on 2 and 3
            t.note(s, ch_bass, bass_root, 96, 2 * EIGHTH)
            for beat in (1, 2):
                for n in gtr:
                    t.note(s + beat * 2 * EIGHTH, ch_gtr, n, 62, 2 * EIGHTH - 30)
            continue
        beats = part // TPQ                    # 1 (2/4 halves), 2 or 4 quarter beats
        for b in range(beats):
            bt = s + b * TPQ
            if b % 2 == 0:                     # boom: root, then the fifth
                t.note(bt, ch_bass, bass_root if (b // 2) % 2 == 0 else bass_fifth, 100, TPQ - 40)
            else:
                for n in gtr:                  # chick
                    t.note(bt, ch_gtr, n, 70, TPQ // 2)
            if drums:
                t.note(bt, 9, KICK if b % 2 == 0 else SNARE, 90 if b % 2 == 0 else 60, EIGHTH)
                t.note(bt + EIGHTH, 9, HAT, 38, EIGHTH)
        if beats == 1 and meter == 4:          # 2/4 with two chords: offbeat chick
            for n in gtr:
                t.note(s + EIGHTH, ch_gtr, n, 66, EIGHTH)


def build(song):
    meter = song['meter']
    a = melody(song['a'], meter, song['file'] + ' A')
    b = melody(song['b'], meter, song['file'] + ' B')
    ac = chords(song['ac'], len(a), song['file'] + ' A chords')
    bc = chords(song['bc'], len(b), song['file'] + ' B chords')
    t = Track()
    t.raw(0, meta(0x03, song['title'].encode('ascii')))
    t.raw(0, meta(0x01, ('mood:' + song['mood']).encode('ascii')))
    t.raw(0, meta(0x01, b'OpenSimFarm, composed for the game; GPL-3.0-or-later'))
    t.raw(0, meta(0x51, struct.pack('>I', 60000000 // song['tempo'])[1:]))
    t.raw(0, meta(0x58, bytes([meter // 2 if meter != 6 else 3, 2 if meter != 6 else 2, 24, 8])))
    CH_LEAD, CH_LEAD2, CH_GTR, CH_BASS, CH_PAD = 0, 1, 2, 3, 4
    lead1, lead2 = song['leads']
    reed = (HARMONICA, ACCORDION)   # sustained reeds sound louder than plucked or bowed leads at the same volume
    for ch, prog, vol in ((CH_LEAD, lead1, 70 if lead1 in reed else 96), (CH_LEAD2, lead2, 66 if lead2 in reed else 90), (CH_GTR, STEEL_GUITAR if song["drums"] else NYLON_GUITAR, 62),
                          (CH_BASS, ACOUSTIC_BASS, 84), (CH_PAD, STRINGS, 30)):
        t.raw(0, bytes([0xC0 | ch, prog]), -2)
        t.raw(0, bytes([0xB0 | ch, 7, vol]), -1)
    # A A B B twice: the first lead, then the second; a waltz (16-bar strains) plays A B twice
    form = [(a, ac), (b, bc)] if meter == 6 else [(a, ac), (a, ac), (b, bc), (b, bc)]
    bar_len = meter * EIGHTH
    tick = 0
    passes = [(CH_LEAD, form), (CH_LEAD2, form)]
    total_bars = sum(len(m) for _, f in passes for m, _ in f)
    done = 0
    for ch, f in passes:
        for strain, chs in f:
            for notes, syms in zip(strain, chs):
                done += 1
                last = done == total_bars
                pos = tick
                for i, (p, length) in enumerate(notes):
                    dur = length * EIGHTH
                    if p is not None:
                        downbeat = (pos - tick) % (2 * EIGHTH) == 0
                        vel = 100 if pos == tick else (88 if downbeat else 76)
                        if ch == CH_LEAD2 and song['leads'][1] in (FIDDLE, HARMONICA, ACCORDION):
                            vel -= 6
                        t.note(pos, ch, p, vel, dur + (bar_len if last and i == len(notes) - 1 else 0))
                    pos += dur
                accompany(t, tick, syms, meter, CH_GTR, CH_BASS, song['drums'], CH_PAD if song['pad'] else None, last)
                tick += bar_len
    data = t.bytes()
    return b'MThd' + struct.pack('>IHHH', 6, 0, 1, TPQ) + b'MTrk' + struct.pack('>I', len(data)) + data


def main():
    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'music')
    os.makedirs(out_dir, exist_ok=True)
    for song in SONGS:
        data = build(song)
        with open(os.path.join(out_dir, song['file']), 'wb') as f:
            f.write(data)
        print(f"{song['file']:22} {len(data):6} bytes  {song['title']}")


if __name__ == '__main__':
    main()

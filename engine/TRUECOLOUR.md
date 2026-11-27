# Truecolour tile packs

OpenSimFarm's art is 16-colour, 16 x 16 pixels a tile. A *tile pack* replaces the map tiles with full-colour art at
any resolution from 8 to 128 pixels a tile, as PNG files described by JSON. This is the first step towards a total
conversion that needs none of the original game's files; see "What is still indexed" for what is left.

## The idea: logical size and art size

The engine's geometry (cell size, cameras, zoom levels, glides, hit tests) is in units of 16 per cell
(`kLogicalTile`). That does not change. Only the pictures do: a truecolour tile is `size` pixels across and is
resampled to whatever size it is shown at.

* **Map view** (`ui/MapView.cpp`): truecolour tiles are always drawn from the "smooth atlases" Modern Render already
  makes, one per zoom level, each tile resampled to the exact size on screen (area average when shrinking, bilinear
  when growing: `ResampleSmooth` in `ui/PixelArt.cpp`). They never go through the MMPX pixel-art scaler. Modern Render
  stays a separate option: it only adds the movers' glides.
* **Interface tiles** (`ui/Gfx.cpp`: icons, the map editor's tiles): resampled to `16 * max(detail, scale)` pixels (at
  most 64), the size they are drawn at, so the existing source rectangles keep working.
* **Indexed tiles** take the same paths as before; their code only gained `TileSheet::size` and a branch.

## Tile pack format

A folder with `tilepack.json` and the PNG sheets it names.

```json
{
  "format": 1,
  "name": "My Farm",
  "tileSize": 32,
  "sheets": {
    "plain":  { "file": "plain.png",  "count": 848, "columns": 32 },
    "masked": { "file": "masked.png", "count": 200, "columns": 20 },
    "events": { "file": "events.png", "count": 105, "columns": 15 }
  }
}
```

* `tileSize`: 8 to 128, the same for every sheet. A sheet is a grid `columns` tiles across, tile *i* at column
  `i % columns`, row `i / columns`; the PNG must be at least `columns * tileSize` wide and `rows * tileSize` high.
* `plain` (848 tiles, base layer ids 0..847, drawn opaque) and `masked` (200 tiles, overlay ids 0x350..0x417, the
  PNG's alpha channel is the see-through) are required. `events` (105 tiles: the rodeo and fair ground) is optional.
* The tile numbers are the original game's, so the cells keep their meaning (`assets/Graphics.h`).
* `tileSize` and `sheets` are optional together: a pack may bring only pictures (below).
* File names are relative paths in the pack's folder (`plain.png`, `ui/win.png`): `/` between folders, never `..`, a
  drive letter, a backslash or a leading `/`. Only PNG is read (any colour type or bit depth; `stb_image`, built
  PNG-only and from memory, at most 8192 pixels a side, 50 million pixels a pack).

Code: `assets/TilePack.{h,cpp}` (`LoadTilePack`, `ParseTilePack`, `DecodeTileSheetPng`), `assets/Png.{h,cpp}`,
`core/Json.{h,cpp}`.

## Pictures

The interface's own pictures (windows, buttons, gauges, sprites, the full-screen pictures) can come from a pack too,
as truecolour `RgbaPicture`s (`assets/Picture.h`) in a `PicturePack` (`assets/PicturePack.{h,cpp}`). They stand in for
the original's pictures by the *key the interface asks for them with*, the names `Gfx::PictureTexture` uses:

| Key | Picture |
|---|---|
| `buywndo.bmp`, `editeasy.lzs` | a window or other picture |
| `buttons.til$3`, `fldgrph.til$12`, `wethvane.til$1` | face 3 of a face file |
| `alltiles.btl#66` | record 66 of a picture bank |
| `airplane.spr@2` | frame 2 of a sprite |
| `sftitle.int` | a full-screen picture (the palette name the interface passes is ignored) |

A picture has a **logical size**: the size it takes in the interface's own units (the original's 640 x 480 screen),
where the interface positions everything. The PNG may have more pixels (2 or 4 times as many); they make it sharper,
never larger, so a pack's pictures slot into the original's layouts without moving anything. `Gfx` shows each at the
size the screen needs: a picture with more pixels than the screen can show is shrunk once when first drawn.

```json
"pictures": {
  "scale": 2,
  "single": {
    "buywndo.bmp": { "file": "ui/buywndo.png" },
    "popup.bmp":   { "file": "ui/popup.png", "size": [256, 192] },
    "sftitle.int": { "file": "ui/sftitle.png", "scale": 2 }
  },
  "sets": [
    { "key": "buttons.til${n}", "file": "ui/buttons.png", "count": 18, "columns": 6 },
    { "key": "airplane.spr@{n}", "file": "ui/airplane.png", "count": 8, "columns": 4, "first": 0 }
  ]
}
```

* `scale` is pixels per interface unit (0.25 to 16; default 1) for every picture without its own `size` or `scale`; the
  logical size is the PNG's size divided by it. A picture's own `"size": [w, h]` (whole units) wins, then its own
  `"scale"`.
* A **set** cuts equal cells out of one PNG, cell *i* at column `i % columns`, row `i / columns`, under the key with
  `{n}` replaced by `first + i`; give `"cell": [w, h]` for the cells' logical size instead of a scale.
* Keys are matched without regard to case. A key a pack leaves out is drawn from the original's archive as before, so
  a pack can replace pictures one at a time.
* A pack's picture is drawn as it is: the interface's translated-word overlays on the original pictures
  (`PictureText.cpp`) are not painted over it; a pack's pictures carry their own words.
* A bad manifest is rejected whole (nothing of it is used).

## Trying a pack

```
OPENSIMFARM_TILEPACK=/path/to/pack opensimfarm /path/to/SimFarm
```

A development aid for now: the pack's tiles replace the archive's (the rest still comes from the archive). A pack that
cannot be read is reported on stderr and the archive's tiles are used.

## The placeholder pack

`packs/placeholder/` is a complete pack at 32 px, made by `tools/make_placeholder_pack.py` (Python and Pillow; one
second; `--size 8..128`, `--no-labels`). It is drawn from nothing but the engine's own tile tables, so it needs and
contains nothing of the original game:

* Families the engine names are drawn as what they are: the ground ramp (15..30, dry to wet), water with sandy shore
  pieces (39..69), ditches (87..108) and valves, trees by height (128..146), paved and dirt roads (150..171), bridges,
  troughs, fences and gates (172..193), rocks, wrecks, hay, the 16 crops' growth stages, signs and icons (539.., 688..,
  816..), and the rodeo and fair grounds.
* Connected pieces (roads, fences, ditches) have the right arms: the piece number 0..10 is the engine's neighbour-mask
  shape (`kShape` in `game/Editing.cpp`).
* Every other tile is coloured by the tile family it belongs to (`FamilyStart` in `game/Shop.cpp`), so a family is
  recognisable at a glance, and every tile carries its number: the pack is a map of what the engine uses where.
* The interface pictures are blank panels at the sizes the engine expects (`ArchivePictureTest` in `main.cpp`): the 18
  windows with their file name and size, the toolbar with a ruler every 32 units (where the interface cuts it), the
  editor's icons and the button, gauge and weather-vane faces numbered, four full-screen pictures, and numbered
  sprite frames, all at 2 pixels per unit (`--no-pictures` leaves them out). Picture banks (`alltiles.btl#N`) and
  the cursors are not made yet.

`opensimfarm --packpreview packs/placeholder /tmp/shot` draws a pack with the real map view and interface into
`/tmp/shot-plain.bmp` (the 848 plain tiles, 32 to a row), `/tmp/shot-masked.bmp` (the 200 masked tiles over ground) and
`/tmp/shot-pictures.bmp` (every picture, in rows): the quickest way for an artist to see a pack the way the engine
draws it.

## Testing

`opensimfarm --tilepacktest` needs no game files and no display (`SDL_VIDEODRIVER=dummy`). It checks the JSON reader,
the loader on real PNGs made in memory (including bad packs), the map view at 16, 32 and 64 px tiles, classic and
Modern Render, every zoom level, a tile changing, the interface tiles at several scales and details, and the pack
pictures (manifest and error cases, drawing whole, in parts, with alpha, as sprites and full screens, at several
scales and details, and the fall-back to the archive). A
synthetic indexed pack runs the same paths so that the original art's path is covered too. (`--graphicstest` still
needs a SimFarm folder.)

## What is still indexed

Found by reading the code; none of it breaks a truecolour pack, but a pack cannot yet replace it.

| What | Where | Note |
|---|---|---|
| Window pictures, full-screen pictures, sprites, buttons, banks (`.bmp .int .spr .btl .til`) | `assets/Picture.cpp`, `Gfx::PictureTexture`, `Gfx::MakeTexture` | the archive's are still indexed (`Picture`); a pack replaces them by key with truecolour `RgbaPicture`s (see Pictures). Not covered: the cursors and anything the interface draws from `Picture`s other than through `Gfx` |
| Interface colours | `enum Colour` in `ui/Gfx.h`, `_ui` map, `Gfx::Fill/Text` | the UI draws by palette index (0..15) |
| Weather | `SetPlayPalette`, `rainpal.pal`, `frostpal.pal`; the patch block in `main.cpp` | rain and frost are palette swaps of colour 7; a pack's tiles have no weather look yet (that block is skipped for packs) |
| Crop art | `.crp` files, `CropDef::art`, `ApplyCropArt` | 14 indexed tiles a crop patched into the sheet; returns false for a pack. `SpareCrop::icon` and `SpareCropMarket::icon` are 256 indices |
| Overview map colours | `far_mapc.mbm` (`ui/Windows.cpp`, `ui/StartScreen.cpp`) | 2 bytes a tile id, indexed |
| Font | `egafont.fnt`, `Font8` | 8 x 8 bitmap; the modern fonts are already TrueType |
| Self-tests | `MapView::SelfTest`, `Gfx::SelfTest` | indexed tiles, need the archive |

Limits: tile size 128 at most (the native atlas is 32 tiles wide: 4096 px). A truecolour pack still builds the native
atlas besides the per-zoom ones, which costs memory (about 20 MB at 64 px) and could be dropped.

## Next steps

1. Give the interface's hard-coded layout (window contents, the labels `PictureText.cpp` places) a home in the pack's
   JSON, so that a pack can move things; picture banks and cursors in packs.
2. Weather as a tint or overlay for truecolour tiles; crop pictures in the pack (`crops.json`).
3. Put every table the engine reads from the archive (`townly0.cmp`, `events.dat`, `itemtext.dat`, `scenario.odt`,
   `cardN.hc`, `.crp`) behind JSON, so that no original file is needed; a placeholder pack that makes the game run
   with none.

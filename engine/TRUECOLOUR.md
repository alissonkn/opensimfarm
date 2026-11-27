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

## Interface theme

A pack does not need a palette. Everything the interface draws with a colour (fills, frames, bevels, lines, text, the
plot grid, the overview map's legend) asks for one of 16 named colours by number (`enum Colour` in `ui/Gfx.h`); the
original gives each a palette entry, a pack gives each an RGB value, in an optional `theme` block of `tilepack.json`:

```json
"theme": { "black": "#000000", "yellow": "#ffe14d", "sky": "#55aaff", "red": "#d03030", "pink": "#f080c0",
           "navy": "#1c2a7a", "blue": "#3a5fd0", "tan": "#d8c08a", "green": "#3f9f3f", "darkGreen": "#1f6a2e",
           "brown": "#80502a", "colour11": "#70d8d8", "lightGrey": "#c4c4c4", "grey": "#868686",
           "darkGrey": "#484848", "white": "#ffffff" }
```

Colours are `#rrggbb` or `#rgb`; one left out takes the engine's own (`DefaultTheme`), a name that is not one of the 16
is an error (a typo is not a silent default). `colour11` is the one the engine never names. The panels are
`lightGrey` with `white` and `grey` bevel edges and `black` text, so keep those three apart.

How it works: `Gfx::Ui(c)` was the one place a colour number became RGB (through the palette); with a theme set
(`Gfx::SetTheme`) it answers from the theme, and so does `Gfx::Argb16(i)`, the same for the places that draw
pictures pixel by pixel from colour numbers (the map window, the crop icon lists). Without a theme both answer from
the palette exactly as before. Unlike the palette, the theme does not change with the weather: in the original a rainy
day recolours palette entry 7 (tan) and with it everything drawn in it; a theme keeps the interface still (weather on
the map becomes a veil: see Weather).

Code: `assets/Theme.{h,cpp}`. The crop lists' colour numbers are theme colours in a pack. The overview map has its
own colours in RGB (below).

## Weather

The original shows rain and frost by recolouring one palette entry (colour 7, tan: blue in rain, white in frost), so
the ground, and everything else drawn in tan, turns blue or white. A pack has no palette, so its map is laid under a
translucent veil: bluish in rain, pale in frost (a rain day at 32 F or below). Only the map is veiled; the interface
keeps its theme, and so do the pictures. An optional `weather` block of `tilepack.json` sets each day:

```json
"weather": { "rain":  { "colour": "#30508c", "strength": 0.30 },
             "frost": { "colour": "#e8f4ff", "strength": 0.45 } }
```

`colour` is `#rrggbb` or `#rgb`; `strength` is the veil's share, 0 (none) to 1 (the colour alone). Either can be left
out of a day (it keeps the engine's own), either day can be left out, `"strength": 0` is no veil, unknown names are
errors. A pack with no `weather` block gets the engine's own look (a slate-blue 30% in rain, a pale 45% in frost).

How it works: the veil is drawn at the end of `MapView::Draw`, over the part of the world in view (the margin round a
small world is not veiled) and over the movers' glides; `main.cpp` sets it from the day's weather when the tiles are a
pack's (`WeatherTint` and `TintOfDay` in `assets/Weather.{h,cpp}`). With the original's own tiles nothing changes: the
palette swap stays. `opensimfarm --packpreview` also writes `<prefix>-weather.bmp`, the map on a plain day, in rain and
in frost side by side.

## Data tables

Six of the game's data tables can be written as JSON, in the `tables` block of the same `tilepack.json`
(`assets/PackTables.{h,cpp}` has the full description; every table is optional):

| JSON | Replaces | What it holds |
|---|---|---|
| `climate` | `rain0..3.dat`, `temp0..3.dat`, `wind0..3.dat` | 4 climate classes of 240 weeks (or 48, repeated) for rain class 0..7, degrees F, wind |
| `regions` | `scenario.odt` | the 18 x 18 region map: rain, temperature and wind class letters, and where a farm may be chosen |
| `townLayout` | `townly0.cmp` | the 15 town plot templates, 8 x 8 tile ids each |
| `events` | `events.dat` | every message box's title and up to 6 lines, by event id |
| `items` | `itemtext.dat` | every item's name and description, by item id |
| `overview` | `far_mapc.mbm` (the pack adds `overview.rgb`) | the overview map's colours in RGB: each tile's 2 x 2 block, by default, range or tile |

How it works: the engine reads each table as a byte blob by its archive name. `assets/TableFormats.{h,cpp}` holds the
one decoder of each format (it replaced five separate inline readers in the interface and world generation, which now
share it), and `assets/PackTables` makes the same blobs from the JSON, validated and all-or-nothing: ranges, widths
and character sets are checked (the message box takes 27 characters a line, an item name 13, nothing above `z`, no
`@`, `~` or `` ` ``, the file formats' own marks). `Megadata::Override` lays them over the archive's entries, so a
pack's table replaces the archive's of the same name and the consumers do not change. A title is centred for you (the
box centres nothing; the file does it with blanks).

The overview is the one table that is not a byte format of the original's. The original's `far_mapc.mbm` holds 4
palette indices a tile, which cannot say RGB, so a pack makes a second entry, `overview.rgb` (960 tiles x 4 pixels x
R, G, B and a spare byte; `MakeOverview` in `assets/TableFormats.h` picks it when present, else the archive's). A
colour is `"#rrggbb"` for all four pixels of a tile or 4 of them, row by row. The Map window and the start screen's
preview read the overview through that one table type; the window's pixel stage (the picture, its scaling into the
192-pixel box, the XOR highlight of the chosen plot and the view frame) is `ui/OverviewImage`, so that it can be
checked without a window. With the archive's indices the highlight is `index ^ 15`, as the original draws it; with RGB
it inverts the colour.

`opensimfarm --tablestest [pack folder]` needs no game files. It compares each decoder with a word-for-word copy of
the reader it replaced on varied blobs, makes every table from JSON and reads it back with the real decoders and with
the engine's own consumers (`GenerateTerrain` for the climate, `PlaceTownPlotCells` for the town), and checks 36
bad tables. With a folder it also checks that pack's tables.

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
* The interface theme and the weather look: the engine's own 16 colours and rain and frost veils written out in
  `tilepack.json`, ready to change.
* The data tables (`--no-tables` leaves them out): synthetic climate (seasons and weather by class), a region map in
  gradients, a dirt-road cross in each town block, 80 message texts (the engine's own English words for the ids it
  raises, found in `game/*.cpp`), 103 item texts, and overview colours by tile family, in RGB from the theme's own
  colours.

`opensimfarm --packpreview packs/placeholder /tmp/shot` draws a pack with the real map view and interface into
`/tmp/shot-plain.bmp` (the 848 plain tiles, 32 to a row), `/tmp/shot-masked.bmp` (the 200 masked tiles over ground) and
`/tmp/shot-pictures.bmp` (every picture, in rows) and `/tmp/shot-overview.bmp` (the overview colours of every tile id) and `/tmp/shot-weather.bmp` (the map plain, in rain and
in frost): the quickest way for an artist to see a pack the way the engine
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

## What still needs the original's files

With a pack, the map, the interface pictures and six data tables no longer come from the original. To start the game
with none of its files, these remain:

| What | Where it is read | Next |
|---|---|---|
| The archive itself | `Megadata::Open` wants `MEGADATA.IDX/EEA`; `Gfx::Init` wants `egafont.fnt` | open from a pack alone; a font in the pack |
| Palettes: `simfarm.pal`, `rainpal.pal`, `frostpal.pal`, `sftitle.pal`, `scenario.pal`, `prsnt480.pal` | a pack no longer needs any: the interface has its theme, tiles and pictures are RGB, the overview is RGB and the weather a veil. Only the archive's own indexed pictures still use them | nothing on the pack path |
| Picture banks `alltiles.btl`, `wethgrnd.btl`, `wethsky.btl`; `cursors.til` | message box pictures (event ids pick a record), the Expert's faces, weather art | bank sets in `pictures`; cursors |
| Crop files `CROPS/*.crp` | 16 crops: name, 70 parameters, 14 pictures, help card | a `crops` table; the pictures come from the pack |
| Help cards `cardN.hc` | the Farm Expert's scripted cards | a JSON card format |
| Prebuilt farms `SCEN0..N.SSM` | `SCEN0.SSM` is the template every new world starts from (item catalogue, prices, tables) | the template as JSON, a built-in default state |
| Sounds `*.voc` and songs `*.xmi` | effects and music | a sound set in the pack |
| `uicolour.map`, `mskdtile.msk` | the Windows edition's colours and masks | not needed by a pack |

## Next steps

1. Give the interface's hard-coded layout (window contents, the labels `PictureText.cpp` places) a home in the pack's
   JSON, so that a pack can move things; picture banks and cursors in packs.
2. Weather as a tint or overlay for truecolour tiles; crop pictures in the pack (`crops.json`).
3. The rest of the list above, in this order: the crop files, the template farm, picture banks, the help cards,
   sounds, the archive itself; each with placeholder data in the shipped pack until the game runs with none of the
   original's files.

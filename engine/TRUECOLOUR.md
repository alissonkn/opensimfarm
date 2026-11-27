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

## Playing with a pack alone

```
opensimfarm --pack engine/packs/placeholder            # a new game with none of SimFarm's files
opensimfarm --pack engine/packs/placeholder my.sfm     # or a saved game
```

`--pack FOLDER` starts the game from a pack and nothing else: no game folder is searched for or remembered, the archive
is empty (`Megadata::OpenEmpty`), and the pack gives the tiles, pictures, theme, weather, crops, template farm and data
tables (`ApplyPack` in `main.cpp`, the one place a pack is laid over the archive). The pack's folder stands in for the game
folder. What the pack does not give is made by the engine:

* the interface's 8 x 8 font, rasterised from the built-in Press Start 2P (`BuiltinFont8`); the default font is a modern
  one anyway;
* the first farm, a new world from the template (`GenerateWorld`), and the region screen's eight prebuilt farms
  (`GeneratePrebuiltFarm`): farm *n* is a world with the climate of its cell of the region map and a fixed seed, so it is
  the same farm every time (some have a river, some a lake). "Play this region", "Make your own" and "Open a saved game"
  work as they do with the original's files.

`OPENSIMFARM_TILEPACK=folder` still lays a pack over a SimFarm folder (tiles, pictures, ... replaced one by one).

For looking at the game without a display or a person, two test aids (they need `SDL_VIDEODRIVER=dummy`):

* `OPENSIMFARM_SHOT=file.bmp[:frames]` saves the frame after that many frames (default 30) and ends the program;
* `OPENSIMFARM_SCRIPT=file` runs a script of input. A line is `FRAME COMMAND ARGS` (frames count the main loop's passes):
  `click X Y`, `rclick X Y`, `move X Y` (window pixels), `key NAME` (SDL key names), `shot FILE`, `quit`.

```
20 click 500 300      # the intro
50 click 500 300      # the title
75 click 282 366      # PLAY THIS REGION
150 shot /tmp/farm.bmp
160 quit x
```

The placeholder pictures show the buttons and words where the engine expects them because the generator reads the
engine's own layout from its source: the labels and covers of `PictureText.cpp` and `StartScreen.cpp` (a place and the
word), the start screens' button rectangles, and each window's `CloseButton()` (`make_placeholder_pack.py`,
`source_layout`). A picture the pack leaves out is drawn from the archive, or not at all.

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

## Template farm

Every new world starts from a *template farm*: the world generator copies it, then replaces the map, the plots, the
weather and every table of things (animals, machines, fields, buildings, items), and keeps the rest. In the original
the template is the prebuilt farm `SCEN0.SSM`. What survives from it into a new game is small: the **item
catalogue** (103 records of 18 bytes: what each machine, building, animal, seed and supply is, costs and covers), the
save's version byte and a few scalar defaults. A pack gives them in an optional `template` block:

```json
"template": {
  "version": 1,
  "catalogue": [
    { "id": 12, "family": 6, "kind": 64, "price": 800, "size": [1, 1] },
    { "id": 0, "family": 40, "kind": 0, "price": 12000, "size": [1, 1], "turns": 2 }
  ],
  "scalars": { "menu_6": 0 }
}
```

The engine fixes what each record *number* is: 0..8 machines (tractor, plow, planter, sprayer, harvester, trailer, one
unused, truck, crop duster), 12..20 buildings (small and large silo, shed and barn, water pump, windmill, water tower),
21..44 the crops' market records, 45..48 the animals (horse, cow, pig, sheep), 50..65 the crops' stored stacks, 66..81
the seeds, 82..85 the supplies (fertilizer, pesticide, herbicide, fungicide). A record's fields, all optional but `id`:

| Field | Offset | Meaning |
|---|---|---|
| `family` | +0, u16 | the tile family of its picture (`FamilyStart` in `game/Shop.cpp`), which is also the id of its item text (`itemtext.dat`) |
| `kind` | +2, u16 | its ledger code, as `Simulation::Spend` sorts expenses: machines 0..63 (their type), buildings 0x40..0x48, livestock 0xA0..0xA3, seeds 0xC0 + crop, supplies 0xDC..0xDF |
| `own` | +4, u16 | how many the player starts with |
| `price` | +6, u16 | what it costs; the crops' prices are set when a world is made |
| `size` | +8, +9, i8 | cells across and down (a building's picture is that many consecutive tiles, row by row) |
| `turns` | +10 | for a machine: how many of its pictures face one way (the turned ones follow after size x turns) |
| `picture` | +11, i8 | the record of the picture bank shown in the BUY window |
| `extra` | +12..17 | six bytes the engine does not read yet, kept so that nothing is lost |

`scalars` are defaults for the save's scalar tail by name; the cash, the calendar and the speed are not allowed (a
world sets them itself), and most scalars the simulation sets for itself, so a template rarely needs any.

How it works: the codec (`game/Template.{h,cpp}`) builds a blank 96 x 96 farm to the save format's exact size
(`MakeBlankFarm`; the save's tail holds more than the named scalars, so its length is what makes the file the right
size), fills it from the block and writes it as a save file: the entry `template.ssm`, laid into the archive like the
other tables. `LoadTemplateState` gives a new world its template from that entry when a pack has one, else from
`SCEN0.SSM`; `GameState` gained `LoadBytes` and `Serialize` (what `Load` and `Save` now go through) for this. The new
world screen, the map editor's test and the startup check use it.

`opensimfarm --templatetest [pack folder]` needs no game files. It checks the blank farm and the save format, a block
made into a save and read back, 28 bad blocks, and which source wins (a pack's over `SCEN0.SSM`). With a pack folder it
runs the game on that pack's template: a world is generated from it, **every building, machine, animal and supply the
shop offers and a seed bag is bought** (each costing its catalogue price and appearing), and half a year is simulated.

What the shipped placeholder catalogue is: values from the engine's own role tables (families found from each thing's
first tile, ledger codes, building sizes that match the tiles their pictures take) with invented prices, so it is
playable but unbalanced, and unchecked beyond what the smoke test does. The crops' own numbers (prices, seed prices)
come from their crop files, which a pack does not have yet.

**Not covered yet:** the farm a game *starts* in is still loaded from `SCEN0.SSM` (the template only serves new
worlds), as are the other prebuilt farms on the region map (`SCEN1..7.SSM`); without crop files no crop is loaded.

## Crops

The original keeps each crop in a `CROPS/*.CRP` file: a 70-byte parameter record, the crop's 14 pictures, its name and
two help texts. A pack gives them as an optional `crops` block of `tilepack.json` (the full description is in
`game/PackCrops.h`):

```json
"crops": [
  { "file": "corn.crp", "name": "Corn",
    "params": { "weeks": 18, "stages": 8, "baseTemp": 50, "heat": 100, "yield": 300, "price": 300, "seedBase": 100,
                "waterMin": 5, "waterMax": 19, "drainA": 3, "drainB": 2, "drainC": 2,
                "resistPests": 170, "resistWeeds": 150, "resistDisease": 190, "wetPenalty": 4, "dryPenalty": 4 },
    "art": "crops/corn.png",
    "help": ["Warm season crop.", "Rain: Medium."],
    "info": "Corn~Temperature: Warm~Rain: Medium" }
]
```

A pack's crops are all the crops there are (16 to 64): they stand in for the `CROPS` folder through a small registry
(`SetPackCrops` in `game/Crops.h`); `LoadCropFile` finds them by file name in any case, `CropFiles` lists them for the
crop-choosing window, and the first 16 are the ones a new world starts with. The file name is at most 14 characters,
lower case, ending in `.crp`, because a save names its crops by it. `art` is a PNG of the crop's 14 pictures in a row,
each as large as the pack's tiles: the 8 growth stages, the sign's four tiles, and the two icons. When a crop is loaded
into a slot they are patched over the slot's tiles (`ApplyCropArtArgb`), as the original copies its crop files'
pictures; the spare-crop lists show the RGB icon. A crop without `art` uses what the tile sheets hold for that slot.

What the parameters do, found from the growth code (each is the byte `P<n>` of the original's record; the engine reads
only these):

* `weeks` (P36) is the weeks from planting to harvest, `stages` (P46, at most 8) the pictures it passes through.
* `heat` (P10) and `baseTemp` (P47): each week the crop gets 7 heat for every degree F the week is above `baseTemp`;
  below `heat` it falls behind (the heat deficit slows its growth); `heat` 0 means it needs none. `chillHours` (P8) is the
  cold it needs.
* `waterMin`, `waterMax` (P20, P22) are the moisture range. A field's moisture is **twice the soil wetness** (which runs
  0 to 12), so the range lives in 0 to 24, and each full ditch beside the field lowers the minimum by 2 and each empty one
  raises the maximum by 2. A week outside the range adds a large stress penalty.
* `resistPests`, `resistWeeds`, `resistDisease` (P38..P40) are **thresholds, not flags**: each week a random 1..255
  above the threshold adds the excess (at most 64) to that counter, so 0 is the fastest growth there is (a crop with 0
  fills its counters in five weeks and every harvest is worth nothing); any non-zero value also quarters the effect of
  a spray. The placeholder crops use 110 to 230.
* `yield` (P12) and `price` (P14): a field is worth `yield * (price / 10)` before quality, and the seed costs a quarter of
  `seedBase` (P18, at least 5); the market price starts at `price`.
* `drainA..C` (P30..P32) are the weekly drains of the soil's three nutrients, `wetPenalty` and `dryPenalty` (P50, P51)
  the stress a wet or dry week adds, `handPicked` (P37) a harvest by a picking crew and a truck, and `perennial` (P42 bit
  1), `orchard` (bit 4), `harvestWeek` (P56, a week of the 48-week year) and `regrowAge` (the age it goes back to after a
  harvest) the tree crops.

Two things in the placeholder pack came from running the game and not from reading it. **The weather tables must keep the
soil's wetness in range**: the engine keeps an accumulator (a rain day adds 8, a dry day takes off 1.25 to 1.75) and moves
the wetness by `acc >> 3`, so a rain class of 3 or more a week soaks the soil to 12 for good; the generator simulates that
model and steers each week's rain class to hold the wetness near a target (`climate_tables`). **The resistances must be
high** (above). Before these two fixes every harvest was worth nothing.

`opensimfarm --cropstest [pack folder]` needs no game files. It checks the block (the accessors read every parameter, 32
bad blocks), the registry, the truecolour art patched into a tile sheet and the RGB icon. With a pack folder it
generates a world on that pack's crops, saves and loads it (the crops are found again by their names), lists the spare
crops, and runs the autoplayer for three years on **five different worlds**: each must stay solvent, sell at least four
harvests for at least 8,000, keep its soil neither soaked nor parched, and not waste most of its harvests.

## The placeholder pack

`packs/placeholder/` is a complete pack at 32 px, made by `tools/make_placeholder_pack.py` (Python and Pillow; one
second; `--size 8..128`, `--labels`). It is drawn from nothing but the engine's own tile tables, so it needs and
contains nothing of the original game:

* Families the engine names are drawn as what they are: the ground ramp (15..30, dry to wet), water with sandy shore
  pieces (39..69), ditches (87..108) and valves, trees by height (128..146), paved and dirt roads (150..171), bridges,
  troughs, fences and gates (172..193), rocks, wrecks, hay, the 16 crops' growth stages, signs and icons (539.., 688..,
  816..), and the rodeo and fair grounds.
* Connected pieces (roads, fences, ditches) have the right arms: the piece number 0..10 is the engine's neighbour-mask
  shape (`kShape` in `game/Editing.cpp`).
* Every other tile is coloured by the tile family it belongs to (`FamilyStart` in `game/Shop.cpp`), so a family is
  recognisable at a glance. With `--labels` (not in the shipped pack, which is clean to play) every tile also carries its
  number: the pack is then a map of what the engine uses where, for an artist.
* The interface pictures are blank panels at the sizes the engine expects (`ArchivePictureTest` in `main.cpp`): the 18
  windows with their file name and size, the toolbar with a ruler every 32 units (where the interface cuts it), the
  editor's icons and the button, gauge and weather-vane faces numbered, four full-screen pictures, and numbered
  sprite frames, all at 2 pixels per unit (`--no-pictures` leaves them out). The picture banks
  (`alltiles.btl`: 96 labelled records, `wethsky.btl`, `wethgrnd.btl`) are made too; the cursors are not.
* The interface theme and the weather look: the engine's own 16 colours and rain and frost veils written out in
  `tilepack.json`, ready to change. The sound effects (25, synthesised) and the Farm Expert's cards (a welcome card and five
  topics).
* 24 crops with their parameters and pictures (with `--no-tables` left out): the 16 a new world starts with, 8 to choose
  from (4 of them orchards), the weather steered so that they can grow.
* The template farm (with `--no-tables` left out): the item catalogue, made from the engine's own role tables.
* The data tables (`--no-tables` leaves them out): synthetic climate (seasons and weather by class), a region map in
  gradients, a dirt-road cross in each town block, 80 message texts (the engine's own English words for the ids it
  raises, found in `game/*.cpp`), 41 item texts (by tile family, the ids the template catalogue gives), and overview colours by tile family, in RGB from the theme's own
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

## What a pack gives, and what is left

A pack alone makes a whole game (`--pack`). Everything the engine read from SimFarm's files now has a pack form or a
built-in stand-in:

| What the original keeps | In a pack |
|---|---|
| Tile sheets, pictures, banks, sprites, buttons, full screens | PNG: `plain`, `masked`, `events` sheets and the `pictures` block (the banks `alltiles.btl`, `wethsky.btl`, `wethgrnd.btl` are sets of pictures under keys like `alltiles.btl#12`) |
| Palettes (6) | not needed: the theme (16 RGB colours), RGB art, the RGB overview, the weather veil |
| Climate, region map, town layout, message and item texts, overview colours | the `tables` block |
| Crop files | the `crops` block |
| `SCEN0.SSM` as the template for new worlds | the `template` block; the first farm and the region map's eight farms are generated |
| Farm Expert cards | the `cards` block |
| Sound effects (`.voc`) and songs (`.xmi`) | the `sounds` block: `.wav` effects, `.mid` songs; a game song a pack leaves out is played by an engine song of the same mood |
| The interface font | built in (Press Start 2P, rasterised); the modern fonts are built in anyway |
| The archive container (`MEGADATA.IDX/EEA`) | not needed: `--pack` starts from an empty archive |

Left out, and optional: **the six custom mouse cursors** (`cursors.til`): without them the game keeps the system's cursor.

## Sounds

A `sounds` block maps the names the game asks for to files in the pack: an effect (`click.voc`, `uhoh.voc`, ...) to a
`.wav` (PCM, 8 or 16 bits, one or two channels, any rate), a song (`amain.xmi`, `genfarm.xmi`, ...) to a standard `.mid`.
The engine already reads both formats under those names (the Windows edition ships them so), so a pack's files are laid
into the archive as they are (`assets/PackSounds.{h,cpp}` lists the 25 effects and the 8 songs the game uses). The files are
checked for what they claim to be, at most 8 MB each. A game song the pack does not give is played by an engine song
of the same mood (`SoundPlayer::LoadSong`), so a pack with no music still has the radio. The placeholder effects are made
by the generator from tones, sweeps and noise (8 kHz), with nothing recorded or borrowed.

## Farm Expert cards

The Expert's cards are a small script each (`card1.hc`, ...): pictures from the bank, text boxes, buttons and hotspots that
open other cards, tile rows, the information of a crop and sound hotspots. The `cards` block of `tilepack.json` writes
them as JSON (`assets/PackCards.h` has the full description): the Expert opens on card 1, every `goto` must name a card
the pack has, and positions count from the window's top-left corner (the 16 pixel title bar included). The decoder
(`DecodeCard` in `assets/TableFormats.h`) is shared by the window and the tests. The placeholder Expert is a welcome card with
five topics (getting started, fields and crops, machines and storage, weather and water, money and the town), each a
card of plain advice with a way back.

## Testing a whole game

`tools/packplay_test.sh [program] [pack]` plays a game from a pack alone, through the interface, with no display and no
person (`SDL_VIDEODRIVER=dummy`), using the input script `tools/packplay.script`: it starts the program, clicks through
the intro and the title, plays the placeholder region, opens BUY, buys a tractor and clicks it onto the farm, picks Corn
from the PLANT list and makes a field, and opens the Farm Expert from the Windows menu. The script checks the game as it goes with `expect` lines (`expect cash 34000`,
`expect machines 1`, `expect open 6`, ...); a check that fails prints what is wrong and the program ends with exit code 1.
The input script and its expectations are described under Playing with a pack alone. The runner fixes `OSF_WORLDSEED=7`
(the first farm and the region screen's world) and `OSF_SEED=7` (the game's own chances), so that the world is the same
every run and the clicks fit it; with a time-based seed the script failed one run in three, because its fixed click spots
sometimes landed on a rock.

The earlier tests check the parts: `--tilepacktest`, `--tablestest [pack]`, `--templatetest [pack]`, `--cropstest [pack]`.

## Next steps

* Custom mouse cursors in a pack (`cursors`), the one thing a pack still takes from the system.
* Give the interface's hard-coded layout (window contents, the labels `PictureText.cpp` places) a home in the pack's
  JSON, so that a pack can move things instead of only redrawing them.
* Real art, sound and balance in place of the placeholders: the engine and the pack format are done, what the placeholder
  pack holds is only a working stand-in.

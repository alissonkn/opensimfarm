// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "assets/TilePack.h"
#include "game/Crops.h"

#include <string>
#include <vector>

namespace osf {

// A pack's crops: what the original keeps in one CROPS/*.CRP file each (a 70-byte parameter record, the crop's 14
// pictures, its name and two help texts) as JSON, in an optional "crops" block of tilepack.json. A pack's crops are
// all the crops there are: they stand in for the CROPS folder (SetPackCrops in game/Crops.h), the first 16 are the
// ones a new world starts with, and the others can be chosen in the crop window.
//
//   "crops": [
//     { "file": "corn.crp",                   at most 14 characters, lower case, ending in ".crp" (a save names its crops so)
//       "name": "Corn",                       1 to 17 characters
//       "params": { "weeks": 16, "stages": 8, "heat": 150, "baseTemp": 50, "yield": 200, "price": 300, "seedBase": 100,
//                   "waterMin": 6, "waterMax": 18, "drainA": 3, "drainB": 2, "drainC": 2, "wetPenalty": 4, "dryPenalty": 4 },
//       "art": "crops/corn.png",              optional: the 14 pictures in a row, each as large as the pack's tiles
//       "help": [ "Needs warm days.", ... ],  the BUY window's card: up to 7 lines of up to 26 characters
//       "info": "Corn~Temperature: Warm~Rain: Medium" }     the Farm Expert's card: '~' starts a new line
//   ]
//
// params (all optional, 0 or false when left out; the number is the byte of the original's record, P<n>):
//   weeks P36 (1..255)   weeks from planting to harvest        stages P46 (1..8)   growth pictures it passes through
//   firstStage P6        the first of its 8 growth pictures    heat P10, baseTemp P47   weekly heat it needs (7 per degree F
//                        it is above baseTemp, per week) and the temperature it starts counting from; heat 0: none
//   chillHours P8        the cold weeks (below 45 F) it needs  yield P12   what a field gives per unit        price P14   the market price
//   seedBase P18         the seed costs a quarter of it (at least 5)
//   waterMin P20, waterMax P22   the field's moisture range, 0..24 (soil wetness 0..12, doubled; ditches widen it by 2 each)
//   wetPenalty P50, dryPenalty P51   stress a wet or dry week adds       drainA P30, drainB P31, drainC P32   weekly drain of
//                        the soil's three nutrients                       frostShift P52   frost damage
//   resistPests P38, resistWeeds P39, resistDisease P40   non-zero: a spray against it works at a quarter
//   handPicked P37       harvested by a picking crew and a truck, no harvester
//   perennial P42 bit 1  grows on after each harvest, from regrowAge (a number of weeks into its life; the name field's byte
//                        18), harvestWeek P56 the week of the year it is ready; orchard P42 bit 4  a tree crop
// "art": a PNG 14 tiles wide and 1 tile high (14 * tileSize x tileSize): the growth pictures 0..7, the sign's four tiles
// (2 x 2: top left, top right, bottom left, bottom right), the icon with the tan frame, the icon with the green frame.
// When a crop is loaded into a slot these are patched over the slot's tiles (growth 688 + 8 * slot, sign 539 + 4 * slot,
// icons 816 + slot and 832 + slot), as the original copies its crop files' pictures. A crop without "art" uses what the
// tile sheets already hold for that slot.

// Reads the "crops" block of a manifest's text, art through `read`. `present` tells whether the block exists. False with
// `error` set when anything in it is wrong (`out` unchanged then). At least 16 crops, at most 64.
bool ParsePackCrops(const std::string& manifestJson, PackReader read, void* user, std::vector<CropDef>& out, bool& present, std::string& error);

// Reads `dir`/tilepack.json's crops (a folder without a manifest is an error).
bool LoadPackCrops(const std::string& dir, std::vector<CropDef>& out, bool& present, std::string& error);

} // namespace osf

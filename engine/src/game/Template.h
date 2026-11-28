// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "game/GameState.h"

#include <cstdint>
#include <string>
#include <vector>

namespace osf {

class Megadata;

// The template farm: the game state every new world starts from (the world generator copies it, then replaces the map,
// the plots, the weather and every table of things, and keeps the rest). In the original it is the prebuilt farm
// SCEN0.SSM; what survives from it into a new game is small: the item catalogue (103 records of 18 bytes: what each
// machine, building, animal, seed and supply is, costs and covers) and a few scalar defaults. A pack gives them as JSON.
//
// The catalogue's records mean by their number what the engine says: 0..8 machines (tractor, plow, planter, sprayer,
// harvester, trailer, (6 unused), truck, crop duster), 12..20 buildings (small and large silo, small and large shed,
// small and large barn, water pump, windmill, water tower), 21..44 the crops' market records, 45..48 the animals
// (horse, cow, pig, sheep), 50..65 the crops' stored stacks, 66..81 the seeds, 82..85 the supplies (fertilizer,
// pesticide, herbicide, fungicide).
//
// A pack's tilepack.json holds it in an optional "template" block:
//
//   "template": {
//     "version": 1,                           the save's version byte (0..255, default 1)
//     "catalogue": [                          any of the records, by id 0..102; the ones left out are all zero
//       { "id": 12, "family": 6, "kind": 64, "price": 600, "size": [1, 1], "turns": 0, "picture": 0, "own": 0,
//         "extra": [0, 0, 0, 0, 0, 0] } ],    every field but "id" is optional (0, size 0 x 0)
//     "scalars": { "menu_6": 1 }              defaults for the save's scalar tail, by name (see game/GameState.cpp), except
//                                             the cash, the calendar and the speed, which a world sets itself
//   }
//
// family   the tile family of the thing's picture (FamilyStart in game/Shop.cpp); also its item text's id (itemtext.dat)
// kind     what it is: 0x40..0x48 for the buildings (their code in the structure table), as the engine tests it
// price    what it costs (0..65535); own: how many the player starts with
// size     cells across and down (-128..127; the engine takes at least 1)
// turns    for a machine: how many of its pictures face one way (the turned ones follow after size x turns)
// picture  the record of the picture bank (alltiles.btl) shown in the BUY window (-128..127)
// extra    the 6 bytes (offsets 12..17) the engine does not read yet, kept so that nothing is lost

constexpr const char* kTemplateEntry = "template.ssm"; // the archive entry a pack's template is laid into
constexpr int kCatalogueRecords = 103, kCatalogueRecordBytes = 18;

// A 96 x 96 farm with every table empty, the sizes the save format wants.
void MakeBlankFarm(GameState& s);

// Makes the template farm of a manifest's "template" block as the bytes of a save file (the format of SCEN0.SSM).
// `present` tells whether the block exists; false with `error` set when it is wrong (`blob` unchanged then).
bool ParsePackTemplate(const std::string& manifestJson, std::vector<uint8_t>& blob, bool& present, std::string& error);

// Reads `dir`/tilepack.json's template (a folder without a manifest is an error).
bool LoadPackTemplate(const std::string& dir, std::vector<uint8_t>& blob, bool& present, std::string& error);

// The template farm for a new world: a pack's (the archive entry kTemplateEntry) when there is one, else SCEN0.SSM in
// the game folder. False with `error` set when there is neither or it cannot be read.
bool LoadTemplateState(const Megadata& mega, const std::string& gameDir, GameState& out, std::string& error);

// whether LoadTemplateState has a source
bool HasTemplate(const Megadata& mega, const std::string& gameDir);

} // namespace osf

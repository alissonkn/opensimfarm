// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// SimFarm for Windows was also sold on a disc that holds only its setup program: every file on it is packed with
// Microsoft's old installer compression (the files' names end in '_': MEGADATA.EE_, CORN.CR_, ...) and the setup
// program unpacks them into the SimFarm folder. For such a disc the engine does the same once: it unpacks the game's
// own files into a folder of its own and plays from there, so the disc is not needed afterwards.
namespace osf {

// Unpacks one file in that format ("KWAJ"); false when it is not one or is damaged.
bool UnpackKwaj(const std::vector<uint8_t>& packed, std::vector<uint8_t>& out);

// true when `dir` is such a setup disc (SETUP.INF and a packed MEGADATA.EE_)
bool IsSetupDisc(const std::string& dir);

// Unpacks the game's files from the setup disc `disc` into `target` (laid out as the setup program lays them out:
// the game files, CROPS and SOUND), unless they are already there. Returns `target`, or nothing with `error` set.
std::optional<std::string> UnpackSetupDisc(const std::string& disc, const std::string& target, std::string& error);

} // namespace osf

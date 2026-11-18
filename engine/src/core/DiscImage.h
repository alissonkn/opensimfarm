// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include <optional>
#include <string>

// A disc image of the player's SimFarm disc (.iso, or the data track of a .bin/.cue copy): an ISO 9660 file system
// in 2048-byte sectors or in raw 2352-byte ones. The game is found on it as on a disc: its files in the top folder or
// in a folder there (the later discs keep it in SIMFARM), or SimFarm for Windows' setup disc with the files packed
// (see InstallDisc.h).
namespace osf {

// true when `path` is a file holding an ISO 9660 file system
bool IsDiscImage(const std::string& path);

// The game's files from the image copied (or, from the setup disc, unpacked) into the folder `target`, skipping the
// ones already there; returns `target`, or nothing with `error` set when the image holds no SimFarm.
std::optional<std::string> GameFromDiscImage(const std::string& image, const std::string& target, std::string& error);

} // namespace osf

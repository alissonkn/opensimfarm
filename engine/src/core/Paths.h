// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include <optional>
#include <string>

// Locating the player's own copy of the original game. Nothing from the original is shipped with OpenSimFarm:
// the user tells us where SimFarm is installed and the engine reads the files from there.
namespace osf {

// Directory that holds our settings (Linux: $XDG_CONFIG_HOME/opensimfarm, Windows: %APPDATA%\OpenSimFarm).
std::string ConfigDir();

// Returns true when `dir` looks like a SimFarm installation (MEGADATA.IDX/.EEA present).
bool IsGameDirectory(const std::string& dir);

// Case-insensitive lookup of a file inside a directory (the original uses upper-case DOS names;
// Linux file systems are case-sensitive). Returns the full path or nothing.
std::optional<std::string> FindFileNoCase(const std::string& dir, const std::string& name);

// Game directory from (1) the command line, (2) the saved configuration. Saves a valid command-line choice.
std::optional<std::string> ResolveGameDirectory(int argc, char** argv);

} // namespace osf

// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include <optional>
#include <string>
#include <vector>

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

// The remembered game directory as stored, whether or not it still holds the game (empty: none remembered).
std::string RememberedGameDirectory();

// Stores `dir` as the game directory for later starts.
void RememberGameDirectory(const std::string& dir);

// `dir` itself when it holds the game files, else a folder up to two levels below it that does (a player who
// picks the folder above the SimFarm folder), else nothing. Folders in `skip` (already found unusable) don't count.
std::optional<std::string> GameDirectoryIn(const std::string& dir, const std::vector<std::string>& skip = {});

// Looks for a SimFarm folder in the usual places, nearest first: on Windows the root folders of every drive and
// the user's own folders, on Linux the home folder and the usual places for games and removable media. The
// search is bounded (a few levels deep, a fixed number of folders) and skips system and hidden folders.
std::optional<std::string> SearchGameDirectory(const std::vector<std::string>& skip = {});

// Asks the player for the folder with the system's folder dialog (Windows: the shell's folder browser; Linux:
// zenity or kdialog when installed). Nothing when cancelled; `available` is false when there is no dialog.
std::optional<std::string> AskGameDirectory(bool& available);

} // namespace osf

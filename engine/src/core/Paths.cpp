// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "core/Paths.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace osf {

static std::string Lower(std::string s)
{
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string ConfigDir()
{
#ifdef _WIN32
    const char* base = std::getenv("APPDATA");
    fs::path p = base ? fs::path(base) / "OpenSimFarm" : fs::path("OpenSimFarm");
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    fs::path p = xdg && *xdg ? fs::path(xdg) / "opensimfarm" : fs::path(home ? home : ".") / ".config" / "opensimfarm";
#endif
    return p.string();
}

std::optional<std::string> FindFileNoCase(const std::string& dir, const std::string& name)
{
    std::error_code ec;
    fs::path direct = fs::path(dir) / name;
    if (fs::exists(direct, ec))
        return direct.string();
    const std::string want = Lower(name);
    for (const auto& entry : fs::directory_iterator(dir, ec))
    {
        if (Lower(entry.path().filename().string()) == want)
            return entry.path().string();
    }
    return std::nullopt;
}

bool IsGameDirectory(const std::string& dir)
{
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec))
        return false;
    return FindFileNoCase(dir, "MEGADATA.IDX") && FindFileNoCase(dir, "MEGADATA.EEA");
}

std::optional<std::string> ResolveGameDirectory(int argc, char** argv)
{
    const fs::path cfg = fs::path(ConfigDir()) / "gamepath.txt";
    if (argc > 1 && IsGameDirectory(argv[1]))
    {
        std::error_code ec;
        fs::create_directories(cfg.parent_path(), ec);
        std::ofstream(cfg) << argv[1] << "\n";
        return std::string(argv[1]);
    }
    std::ifstream in(cfg);
    std::string saved;
    if (in && std::getline(in, saved) && IsGameDirectory(saved))
        return saved;
    return std::nullopt;
}

} // namespace osf

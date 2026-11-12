// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "core/Paths.h"
#include "core/InstallDisc.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#else
#include <sys/wait.h>
#endif

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

std::string RememberedGameDirectory()
{
    std::ifstream in(fs::path(ConfigDir()) / "gamepath.txt");
    std::string saved;
    if (in)
        std::getline(in, saved);
    while (!saved.empty() && (saved.back() == '\r' || saved.back() == '\n'))
        saved.pop_back();
    return saved;
}

void RememberGameDirectory(const std::string& dir)
{
    const fs::path cfg = fs::path(ConfigDir()) / "gamepath.txt";
    std::error_code ec;
    fs::create_directories(cfg.parent_path(), ec);
    std::ofstream(cfg) << dir << "\n";
}

std::string SetupDiscCopy(const std::string& disc)
{
#ifdef __EMSCRIPTEN__
    return (fs::path(disc) / "SIMFARM").string(); // the browser's own copy of the chosen files, kept or not with them
#else
    (void)disc;
    return (fs::path(ConfigDir()) / "SimFarm").string();
#endif
}

std::optional<std::string> PlayableGameDirectory(const std::string& dir)
{
    if (IsGameDirectory(dir))
        return dir;
    if (!IsSetupDisc(dir))
        return std::nullopt;
    std::string error;
    const auto copy = UnpackSetupDisc(dir, SetupDiscCopy(dir), error);
    if (!copy)
        std::fprintf(stderr, "OpenSimFarm: the SimFarm setup disc in %s could not be unpacked: %s\n", dir.c_str(), error.c_str());
    else if (!IsGameDirectory(*copy))
        return std::nullopt;
    return copy;
}

std::optional<std::string> ResolveGameDirectory(int argc, char** argv)
{
    const fs::path cfg = fs::path(ConfigDir()) / "gamepath.txt";
    if (argc > 1)
        if (const auto dir = PlayableGameDirectory(argv[1]))
        {
            RememberGameDirectory(*dir);
            return dir;
        }
    std::ifstream in(cfg);
    std::string saved;
    if (!in || !std::getline(in, saved))
        return std::nullopt;
    in.close();
    while (!saved.empty() && (saved.back() == '\r' || saved.back() == '\n'))
        saved.pop_back();
    const auto dir = PlayableGameDirectory(saved);
    if (dir && *dir != saved)
        RememberGameDirectory(*dir); // a setup disc remembered: from now on its unpacked copy
    return dir;
}

// one folder's entries: does it hold the game files, and which of its subfolders may be searched further
static bool ScanFolder(const fs::path& dir, std::vector<fs::path>* subfolders)
{
    static const char* const kSkip[] = { "windows", "$recycle.bin", "system volume information", "recovery", "perflogs",
                                         "programdata", "appdata", "winsxs", "node_modules", "proc", "sys", "dev", "run",
                                         "snap", "lost+found", "__pycache__" };
    bool idx = false, eea = false, idxPacked = false, eeaPacked = false, inf = false;
    std::error_code ec;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
    {
        const std::string name = Lower(it->path().filename().string());
        std::error_code ec2;
        if (it->is_directory(ec2))
        {
            if (!subfolders || name.empty() || name[0] == '.' || it->is_symlink(ec2))
                continue;
            bool skip = false;
            for (const char* s : kSkip)
                skip = skip || name == s;
            if (!skip)
                subfolders->push_back(it->path());
        }
        else if (name == "megadata.idx")
            idx = true;
        else if (name == "megadata.eea")
            eea = true;
        else if (name == "megadata.id_")
            idxPacked = true;
        else if (name == "megadata.ee_")
            eeaPacked = true;
        else if (name == "setup.inf")
            inf = true;
    }
    return (idx && eea) || (idxPacked && eeaPacked && inf); // the game, or its setup disc
}

// breadth first from each root (nearest folders first), at most `depth` levels below it; `budget` counts the
// folders read, so the search ends in a few seconds even on a full disk
static std::optional<std::string> SearchFrom(const std::vector<std::pair<fs::path, int>>& roots, int& budget,
                                             const std::vector<std::string>& skip)
{
    for (const auto& [root, depth] : roots)
    {
        std::error_code ec;
        if (!fs::is_directory(root, ec))
            continue;
        std::deque<std::pair<fs::path, int>> queue{ { root, 0 } };
        while (!queue.empty() && budget > 0)
        {
            auto [dir, level] = queue.front();
            queue.pop_front();
            --budget;
            std::vector<fs::path> subs;
            if (ScanFolder(dir, level < depth ? &subs : nullptr))
                if (const auto game = PlayableGameDirectory(dir.string()))
                    if (std::find(skip.begin(), skip.end(), *game) == skip.end())
                        return game;
            for (auto& s : subs)
                queue.emplace_back(std::move(s), level + 1);
        }
    }
    return std::nullopt;
}

std::optional<std::string> GameDirectoryIn(const std::string& dir, const std::vector<std::string>& skip)
{
    int budget = 2000;
    return SearchFrom({ { fs::path(dir), 2 } }, budget, skip);
}

std::optional<std::string> SearchGameDirectory(const std::vector<std::string>& skip)
{
    std::vector<std::pair<fs::path, int>> roots;
#ifdef _WIN32
    if (const char* home = std::getenv("USERPROFILE"))
        roots.emplace_back(fs::path(home), 3); // Desktop, Documents, Downloads, Games ... and two levels below
    const DWORD drives = GetLogicalDrives();
    for (int d = 2; d < 26; ++d) // C: onwards: C:\SIMFARM, C:\GAMES\SIMFARM, D:\DOS\GAMES\SIMFARM
        if (drives & (1u << d))
        {
            const char root[4] = { static_cast<char>('A' + d), ':', '\\', 0 };
            const UINT type = GetDriveTypeA(root);
            if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE || type == DRIVE_CDROM)
                roots.emplace_back(fs::path(root), 3);
        }
#else
    if (const char* home = std::getenv("HOME"))
        roots.emplace_back(fs::path(home), 4);
    for (const char* p : { "/opt", "/usr/local/games", "/usr/share/games", "/srv" })
        roots.emplace_back(fs::path(p), 3);
    for (const char* p : { "/media", "/run/media", "/mnt" })
        roots.emplace_back(fs::path(p), 4);
#endif
    int budget = 40000;
    return SearchFrom(roots, budget, skip);
}

std::optional<std::string> AskGameDirectory(bool& available)
{
    available = true;
#ifdef _WIN32
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    BROWSEINFOW bi{};
    bi.lpszTitle = L"Choose the folder of your SimFarm installation (the one with MEGADATA.IDX and MEGADATA.EEA).";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_NONEWFOLDERBUTTON;
    std::optional<std::string> out;
    if (PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi))
    {
        wchar_t path[MAX_PATH * 4] = {};
        if (SHGetPathFromIDListW(pidl, path))
            out = fs::path(path).string();
        CoTaskMemFree(pidl);
    }
    if (SUCCEEDED(co))
        CoUninitialize();
    return out;
#else
    // the desktop's own folder dialog through the usual helper programs; stdout is the chosen path
    static const char* const kCommands[] = {
        "zenity --file-selection --directory --title='Choose your SimFarm folder (with MEGADATA.IDX and MEGADATA.EEA)' 2>/dev/null",
        "kdialog --getexistingdirectory \"$HOME\" --title 'Choose your SimFarm folder' 2>/dev/null",
    };
    static const char* const kTools[] = { "zenity", "kdialog" };
    for (int i = 0; i < 2; ++i)
    {
        const std::string probe = std::string("command -v ") + kTools[i] + " >/dev/null 2>&1";
        if (std::system(probe.c_str()) != 0)
            continue;
        std::string out;
        int status = -1;
        if (FILE* f = popen(kCommands[i], "r"))
        {
            char buf[4096];
            while (std::fgets(buf, sizeof buf, f))
                out += buf;
            status = pclose(f);
        }
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
            out.pop_back();
        // both helpers exit with 0 for a chosen folder and 1 for Cancel; anything else means the dialog could not
        // be shown (no display, a broken install): try the next helper
        const int code = status != -1 && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        if (code == 0 && !out.empty())
            return out;
        if (code == 1)
            return std::nullopt; // cancelled
    }
    available = false;
    return std::nullopt;
#endif
}

} // namespace osf

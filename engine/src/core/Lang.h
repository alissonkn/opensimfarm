// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
// Translations. The engine's texts are written in English in the code and wrapped in Tr(); a language is a
// dictionary (a gettext .po file: msgid = the English text, msgstr = the translation) built into the program from
// engine/lang/, or read from the "languages" folder in the settings folder (new languages without a rebuild).
// engine/tools/lang_extract.py collects every wrapped text into engine/lang/opensimfarm.pot, the list to translate.
//
// Texts from the player's own game files (crop names) have no English text in the program: they are looked up by
// an id (TrId), for example "crop:corn.crp".
//
// Translations are kept in the font's 8-bit encoding (Latin-1: the font draws accented letters at their Latin-1
// codes), so a translated text has one byte a character like the English ones.
#pragma once
#include <string>
#include <vector>

namespace osf {

struct LanguageInfo
{
    std::string code;   // "en", "pt_BR"
    std::string name;   // the language's own name, Latin-1 ("English", "Portugu\xEAs (Brasil)")
};

// "en" first, then the built-in languages and those in the settings folder, by name
std::vector<LanguageInfo> AvailableLanguages();
// an unknown code means English
void SetLanguage(const std::string& code);
const std::string& CurrentLanguage();
// the system's language when a dictionary for it exists ("pt_BR" for pt_BR.UTF-8; a plain "pt" also picks a
// pt_* dictionary), else "en"
std::string SystemLanguage();

// marks a text for lang_extract.py without translating it (tables of names translated where they are shown)
#define N_(s) s

// the translation of an English text (the text itself when there is none); the pointer stays valid
const char* Tr(const char* english);
std::string Tr(const std::string& english);
// a translated text with {0}, {1}, ... replaced by the arguments (a translation may change their order)
std::string TrFormat(const char* english, const std::vector<std::string>& args);
inline std::string ToText(const std::string& s) { return s; }
inline std::string ToText(const char* s) { return s; }
inline std::string ToText(char c) { return std::string(1, c); }
template <class N>
std::string ToText(N n) { return std::to_string(n); }
template <class... A>
std::string TrF(const char* english, const A&... args)
{
    return TrFormat(english, { ToText(args)... });
}

// a text of the game files by id, or the fallback when the language has none
std::string TrId(const std::string& id, const std::string& fallback);

// for the tests: every built-in dictionary's code and its entries (English, translation) as read from the file
struct Dictionary
{
    std::string code, name;
    std::vector<std::pair<std::string, std::string>> entries; // UTF-8 as in the file
};
std::vector<Dictionary> BuiltInDictionaries();
// a Latin-1 text in lower case without accents, for sorting names alphabetically
std::string SortKey(const std::string& s);
// the font's Latin-1 to UTF-8 (for the system's message boxes and window title)
std::string Latin1ToUtf8(const std::string& s);
// UTF-8 to the font's Latin-1 (typographic quotes and dashes to plain ones; anything else outside Latin-1 to '?')
std::string Utf8ToLatin1(const std::string& s, bool* lossless = nullptr);

} // namespace osf

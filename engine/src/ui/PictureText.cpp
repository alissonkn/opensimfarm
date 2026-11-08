// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "ui/PictureText.h"
#include "core/Lang.h"
#include "ui/Gfx.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <map>
#include <sstream>
#include <vector>

namespace osf {

namespace {

// A word printed into one of the game's pictures, measured on the picture: the area [x, x + room) from y (8 rows a
// line, or wrapH rows for a paragraph) holds only the word and its background, so a translation can paint the
// background over it and print the translated word there. bg -1: the picture's textured plate, repeated from an
// 8 x 8 cell of it (kTexture). Keys "button:..." and "gauge:..." are short labels with little room.
struct PictureLabel
{
    const char* picture;
    int x, y, room;
    const char* english;
    int bg, ink, align; // align 0 left, 1 centred, 2 right
    int lines, lh;      // a label of several lines (English lines split by \n), lh rows apart
    int wrapH;          // > 0: a paragraph, wrapped into the room, wrapH rows high
};

const PictureLabel kLabels[] = {
    { "buywndo.bmp", 168, 112, 80, N_("PRICE"), 12, 0, 0, 1, 8, 0 },
    { "buywndo.bmp", 168, 144, 80, N_("OWN"), 12, 0, 0, 1, 8, 0 },
    { "buywndo.bmp", 171, 193, 26, N_("small button:BUY"), 12, 0, 1, 1, 8, 0 },
    { "buywndo.bmp", 203, 193, 42, N_("button:CLOSE"), 12, 0, 1, 1, 8, 0 },
    { "sellwndo.bmp", 91, 225, 34, N_("button:SELL"), 12, 0, 1, 1, 8, 0 },
    { "sellwndo.bmp", 131, 225, 42, N_("button:CLOSE"), 12, 0, 1, 1, 8, 0 },
    { "cashflow.bmp", 24, 8, 176, N_("ASSETS"), 8, 15, 0, 1, 8, 0 },
    { "cashflow.bmp", 208, 8, 168, N_("EXPENDITURES"), 8, 15, 0, 1, 8, 0 },
    { "cashflow.bmp", 24, 24, 120, N_("Cash"), 12, 0, 0, 1, 8, 0 },
    { "cashflow.bmp", 24, 32, 120, N_("Machinery"), 15, 0, 0, 1, 8, 0 },
    { "cashflow.bmp", 24, 40, 120, N_("Land"), 12, 0, 0, 1, 8, 0 },
    { "cashflow.bmp", 24, 48, 120, N_("Futures"), 15, 0, 0, 1, 8, 0 },
    { "cashflow.bmp", 24, 56, 120, N_("Livestock"), 12, 0, 0, 1, 8, 0 },
    { "cashflow.bmp", 24, 88, 112, N_("TOTAL ASSETS"), 13, 15, 0, 1, 8, 0 },
    { "cashflow.bmp", 24, 104, 176, N_("ANNUAL REVENUE"), 8, 15, 0, 1, 8, 0 },
    { "cashflow.bmp", 24, 120, 120, N_("Crop Sales"), 12, 0, 0, 1, 8, 0 },
    { "cashflow.bmp", 24, 128, 120, N_("Machinery Sales"), 15, 0, 0, 1, 8, 0 },
    { "cashflow.bmp", 24, 136, 120, N_("Land Sales"), 12, 0, 0, 1, 8, 0 },
    { "cashflow.bmp", 24, 144, 120, N_("Futures Sold"), 15, 0, 0, 1, 8, 0 },
    { "cashflow.bmp", 24, 152, 120, N_("Animal Sales"), 12, 0, 0, 1, 8, 0 },
    { "cashflow.bmp", 24, 168, 112, N_("TOTAL REVENUE"), 13, 15, 0, 1, 8, 0 },
    { "cashflow.bmp", 208, 24, 112, N_("Seed"), 12, 0, 0, 1, 8, 0 },
    { "cashflow.bmp", 208, 32, 112, N_("Machinery"), 15, 0, 0, 1, 8, 0 },
    { "cashflow.bmp", 208, 40, 112, N_("Land purchased"), 12, 0, 0, 1, 8, 0 },
    { "cashflow.bmp", 208, 48, 112, N_("Losses on futures"), 15, 0, 0, 1, 8, 0 },
    { "cashflow.bmp", 208, 56, 112, N_("Structures"), 12, 0, 0, 1, 8, 0 },
    { "cashflow.bmp", 208, 64, 112, N_("Livestock"), 15, 0, 0, 1, 8, 0 },
    { "cashflow.bmp", 208, 72, 112, N_("Chemicals"), 12, 0, 0, 1, 8, 0 },
    { "cashflow.bmp", 208, 80, 112, N_("Loan Interest"), 15, 0, 0, 1, 8, 0 },
    { "cashflow.bmp", 208, 88, 112, N_("Machines leased"), 12, 0, 0, 1, 8, 0 },
    { "cashflow.bmp", 208, 96, 112, N_("Taxes"), 15, 0, 0, 1, 8, 0 },
    { "cashflow.bmp", 208, 104, 112, N_("Miscellaneous"), 12, 0, 0, 1, 8, 0 },
    { "cashflow.bmp", 208, 152, 104, N_("TOTAL EXPENSES"), 13, 15, 0, 1, 8, 0 },
    { "cashflow.bmp", 208, 168, 104, N_("PROFIT/LOSS"), 13, 15, 0, 1, 8, 0 },
    { "cashflow.bmp", 24, 184, 168, N_("Tax estimate"), 12, 0, 0, 1, 8, 0 },
    { "cashflow.bmp", 24, 192, 168, N_("Loan payment per quarter"), 15, 0, 0, 1, 8, 0 },
    { "cashflow.bmp", 331, 193, 42, N_("button:CLOSE"), 12, 0, 1, 1, 8, 0 },
    { "bankterm.bmp", 48, 24, 136, N_("CASH ON HAND"), 14, 8, 0, 1, 8, 0 },
    { "bankterm.bmp", 48, 40, 136, N_("TOTAL DEBTS"), 14, 8, 0, 1, 8, 0 },
    { "bankterm.bmp", 48, 56, 136, N_("CREDIT LIMIT"), 14, 8, 0, 1, 8, 0 },
    { "bankterm.bmp", 48, 72, 136, N_("INTEREST RATE"), 14, 8, 0, 1, 8, 0 },
    { "bankterm.bmp", 48, 88, 136, N_("PAYMENT PER QUARTER"), 14, 8, 0, 1, 8, 0 },
    { "bankterm.bmp", 48, 112, 196, N_("Type the amount on the keys"), 14, 8, 0, 1, 8, 0 },
    { "bankterm.bmp", 40, 128, 128, N_("LOAN AMOUNT"), 14, 8, 2, 1, 8, 0 },
    { "bankterm.bmp", 147, 161, 82, N_("button:REPAY"), 12, 0, 1, 1, 8, 0 },
    { "bankterm.bmp", 187, 185, 42, N_("button:CLOSE"), 12, 0, 1, 1, 8, 0 },
    { "weather.bmp", 102, 29, 128, N_("Weather today"), 12, 0, 1, 1, 8, 0 },
    { "weather.bmp", 99, 158, 140, N_("Forecast, next 5 days"), 12, 0, 1, 1, 8, 0 },
    { "weather.bmp", 10, 161, 80, N_("Evaporation"), -1, 0, 1, 1, 8, 0 },
    { "weather.bmp", 259, 151, 62, N_("Wind\nSpeed"), -1, 0, 1, 2, 10, 0 },
    { "weather.bmp", 265, 37, 35, N_("Mean\nrain"), -1, 0, 1, 2, 11, 0 },
    { "weather.bmp", 265, 66, 35, N_("Rain\nso\nfar"), -1, 0, 1, 3, 9, 0 },
    { "futures.bmp", 16, 10, 134, N_("Last 30 weeks"), 12, 0, 1, 1, 8, 0 },
    { "futures.bmp", 157, 147, 40, N_("Crop"), 12, 0, 0, 1, 8, 0 },
    { "futures.bmp", 157, 160, 40, N_("Price"), 12, 0, 0, 1, 8, 0 },
    { "futures.bmp", 13, 181, 100, N_("Futures held"), 12, 0, 0, 1, 8, 0 },
    { "futures.bmp", 261, 182, 42, N_("button:CLOSE"), 12, 0, 1, 1, 8, 0 },
    { "evaluate.bmp", 182, 84, 40, N_("Low"), 12, 0, 0, 1, 8, 0 },
    { "evaluate.bmp", 240, 84, 40, N_("High"), 12, 0, 2, 1, 8, 0 },
    { "evaluate.bmp", 17, 101, 126, N_("Problems"), 12, 0, 1, 1, 8, 0 },
    { "evaluate.bmp", 153, 101, 126, N_("Crops to try"), 12, 0, 1, 1, 8, 0 },
    { "evaluate.bmp", 291, 161, 42, N_("button:CLOSE"), 12, 0, 1, 1, 8, 0 },
    { "mapwndo.bmp", 10, 161, 56, N_("Land\nValue"), 12, 0, 1, 2, 11, 0 },
    { "mapwndo.bmp", 227, 211, 42, N_("button:CLOSE"), 12, 0, 1, 1, 8, 0 },
    { "fdstatus.bmp", 22, 44, 52, N_("Crop"), 12, 0, 1, 1, 8, 0 },
    { "fdstatus.bmp", 256, 44, 64, N_("Tool"), 12, 0, 1, 1, 8, 0 },
    { "fdstatus.bmp", 275, 66, 43, N_("button:TODAY"), 12, 0, 1, 1, 8, 0 },
    { "fdstatus.bmp", 275, 82, 43, N_("button:CUT"), 12, 0, 1, 1, 8, 0 },
    { "fdstatus.bmp", 275, 106, 43, N_("button:CLOSE"), 12, 0, 1, 1, 8, 0 },
    { "fdstatus.bmp", 11, 132, 89, N_("Current Value"), 12, 0, 2, 1, 8, 0 },
    { "fdstatus.bmp", 11, 147, 89, N_("Maturity"), 12, 0, 2, 1, 8, 0 },
    { "fdstatus.bmp", 200, 132, 68, N_("Crop\nQuality"), 12, 0, 1, 2, 15, 0 },
    { "fdstatus.bmp", 15, 203, 56, N_("gauge:Disease"), 12, 0, 1, 1, 8, 0 },
    { "fdstatus.bmp", 72, 203, 56, N_("gauge:Pests"), 12, 0, 1, 1, 8, 0 },
    { "fdstatus.bmp", 129, 203, 56, N_("gauge:Weeds"), 12, 0, 1, 1, 8, 0 },
    { "fdstatus.bmp", 210, 203, 54, N_("gauge:Water"), 12, 0, 1, 1, 8, 0 },
    { "fdstatus.bmp", 266, 203, 56, N_("gauge:Soil"), 12, 0, 1, 1, 8, 0 },
    { "airpwndo.bmp", 88, 42, 74, N_("gauge:Gas"), 12, 0, 1, 1, 8, 0 },
    { "airpwndo.bmp", 88, 90, 74, N_("gauge:Spray"), 12, 0, 1, 1, 8, 0 },
    { "airpwndo.bmp", 88, 138, 74, N_("gauge:Damage"), 12, 0, 1, 1, 8, 0 },
    { "airpwndo.bmp", 200, 132, 38, N_("button:FLY!"), 12, 0, 1, 1, 8, 0 },
    { "airpwndo.bmp", 200, 153, 38, N_("small button:CLOSE"), 12, 0, 1, 1, 8, 0 },
    { "choscrop.bmp", 15, 14, 120, N_("REPLACE"), 12, 0, 0, 1, 8, 0 },
    { "choscrop.bmp", 160, 14, 120, N_("WITH"), 12, 0, 0, 1, 8, 0 },
    { "choscrop.bmp", 187, 193, 58, N_("button:REPLACE"), 12, 0, 1, 1, 8, 0 },
    { "choscrop.bmp", 251, 193, 42, N_("button:CLOSE"), 12, 0, 1, 1, 8, 0 },
    { "filegenr.bmp", 22, 14, 116, N_("FILE NAME"), 12, 0, 0, 1, 8, 0 },
    { "filegenr.bmp", 22, 45, 100, N_("DIRECTORY"), 12, 0, 0, 1, 8, 0 },
    { "filegenr.bmp", 147, 65, 42, N_("button:CLOSE"), 12, 0, 1, 1, 8, 0 },
    { "twnchoic.bmp", 10, 107, 84, N_("Residential"), 12, 0, 1, 1, 8, 0 },
    { "twnchoic.bmp", 98, 107, 84, N_("Commercial"), 12, 0, 1, 1, 8, 0 },
    { "twnchoic.bmp", 187, 107, 84, N_("Airport"), 12, 0, 1, 1, 8, 0 },
    { "twnchoic.bmp", 275, 107, 84, N_("Recreational"), 12, 0, 1, 1, 8, 0 },
    { "twnchoic.bmp", 70, 136, 228, N_("The town is buying another plot of land, and the mayor asks the voters what should be built on it. Click the kind of plot you would like the town to build."), 12, 0, 0, 1, 9, 64 },
    { "cntlbtnh.bmp", 400, 5, 57, N_("Date"), 12, 0, 2, 1, 8, 0 },
    { "cntlbtnh.bmp", 400, 20, 57, N_("Funds"), 12, 0, 2, 1, 8, 0 },
    { "buttons.til$1", 3, 9, 42, N_("button:BUY"), 12, 0, 1, 1, 8, 0 },
    { "buttons.til$2", 3, 9, 42, N_("button:SELL"), 12, 0, 1, 1, 8, 0 },
    { "buttons.til$3", 3, 9, 42, N_("button:CANCEL"), 12, 0, 1, 1, 8, 0 },
    { "buttons.til$4", 3, 9, 42, N_("button:GOTO"), 12, 0, 1, 1, 8, 0 },
    { "buttons.til$5", 3, 9, 42, N_("button:USE"), 12, 0, 1, 1, 8, 0 },
    { "buttons.til$6", 3, 9, 42, N_("button:EXIT"), 12, 0, 1, 1, 8, 0 },
    { "buttons.til$7", 3, 9, 42, N_("button:LOAD"), 12, 0, 1, 1, 8, 0 },
    { "buttons.til$8", 3, 9, 42, N_("button:NEW"), 12, 0, 1, 1, 8, 0 },
    { "buttons.til$9", 3, 9, 42, N_("button:USED"), 12, 0, 1, 1, 8, 0 },
    { "buttons.til$10", 3, 9, 42, N_("button:YES"), 12, 0, 1, 1, 8, 0 },
    { "buttons.til$11", 3, 9, 42, N_("button:NO"), 12, 0, 1, 1, 8, 0 },
    { "buttons.til$12", 3, 9, 42, N_("button:SAVE"), 12, 0, 1, 1, 8, 0 },
    { "buttons.til$13", 3, 9, 42, N_("button:NEXT"), 12, 0, 1, 1, 8, 0 },
    { "buttons.til$14", 3, 9, 42, N_("button:PREV"), 12, 0, 1, 1, 8, 0 },
    { "buttons.til$15", 3, 9, 42, N_("button:CLOSE"), 12, 0, 1, 1, 8, 0 },
    { "buttons.til$16", 3, 9, 42, N_("button:TOP"), 12, 0, 1, 1, 8, 0 },
    { "buttons.til$17", 3, 9, 42, N_("button:RATS"), 12, 0, 1, 1, 8, 0 },
};

// a clean cell of each textured picture, at a position that keeps the pattern's phase (multiples of 8)
struct Texture { const char* picture; int x, y; };
const Texture kTexture[] = { { "weather.bmp", 8, 32 } };

const std::multimap<std::string, const PictureLabel*>& Index()
{
    static std::multimap<std::string, const PictureLabel*> index;
    if (index.empty())
        for (const PictureLabel& l : kLabels)
            index.emplace(l.picture, &l);
    return index;
}

std::vector<std::string> Split(const std::string& s)
{
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string line;
    while (std::getline(in, line))
        out.push_back(line);
    return out;
}

// the words of a text in lines of at most room pixels
std::vector<std::string> WrapWords(const std::string& s, int room)
{
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string word, line;
    while (in >> word)
    {
        const std::string next = line.empty() ? word : line + " " + word;
        if (!line.empty() && Gfx::TextWidth(next) > room)
        {
            out.push_back(line);
            line = word;
        }
        else
            line = next;
    }
    if (!line.empty())
        out.push_back(line);
    return out;
}

void Background(Gfx& g, const PictureLabel& l, int dx, int dy, int h)
{
    if (l.bg >= 0)
    {
        g.Fill(dx + l.x, dy + l.y, l.room, h, l.bg);
        return;
    }
    for (const Texture& t : kTexture)
        if (std::strcmp(t.picture, l.picture) == 0)
            for (int y = l.y; y < l.y + h;)
            {
                const int sy = t.y + (y - t.y) % 8, ph = std::min(8 - (y - t.y) % 8, l.y + h - y);
                for (int x = l.x; x < l.x + l.room;)
                {
                    const int sx = t.x + (x - t.x) % 8, pw = std::min(8 - (x - t.x) % 8, l.x + l.room - x);
                    g.DrawPicturePart(l.picture, sx, sy, pw, ph, dx + x, dy + y);
                    x += pw;
                }
                y += ph;
            }
}

} // namespace

void DrawPictureText(Gfx& g, const std::string& picture, int dx, int dy, int sx, int sy, int sw, int sh)
{
    if (CurrentLanguage() == "en")
        return;
    const auto range = Index().equal_range(picture);
    for (auto it = range.first; it != range.second; ++it)
    {
        const PictureLabel& l = *it->second;
        const std::string tr = Tr(std::string(l.english));
        if (tr == l.english)
            continue; // not translated: the picture's own word stays
        const int h = l.wrapH > 0 ? l.wrapH : (l.lines - 1) * l.lh + 8;
        if (l.x < sx || l.y < sy || l.x + l.room > sx + sw || l.y + h > sy + sh)
            continue; // only part of the picture is drawn, without this label
        const int ox = dx - sx, oy = dy - sy;
        Background(g, l, ox, oy, h);
        const std::vector<std::string> lines = l.wrapH > 0 ? WrapWords(tr, l.room) : Split(tr);
        int y = l.y + (l.wrapH > 0 ? 0 : (l.lines - static_cast<int>(lines.size())) * l.lh / 2);
        for (const std::string& line : lines)
        {
            g.TextFit(ox + l.x, oy + y, l.room, line, l.ink, l.align);
            y += l.lh;
        }
    }
}

std::vector<PictureTextInfo> PictureTexts()
{
    std::vector<PictureTextInfo> out;
    for (const PictureLabel& l : kLabels)
        out.push_back({ l.picture, l.english, l.room, l.lines, l.wrapH > 0 ? l.wrapH / l.lh : 0 });
    return out;
}

} // namespace osf

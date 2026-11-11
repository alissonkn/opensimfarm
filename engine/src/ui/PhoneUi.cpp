// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
//
// The phone layout of the main screen (GameUi::SetPhone): an engine addition for the browser version on phones and
// tablets. The game's mechanics stay as they are; only the way its screen is laid out and reached changes:
// * the farm fills the screen between the top (date, funds, the status text, the speed; the menu bar's titles; the
//   toolbar's eight buttons and Farm Expert) and the tool palette (the twelve tools, Plant, Spray) in a bar at the bottom, or in a
//   column at the right when the phone is turned sideways;
// * a window, an event box, the Examine card or one of the engine's lists (crops, sprays, a menu's items, messages)
//   opens over the dimmed farm, one at a time, magnified to fill the screen. A tap beside a window or a list closes
//   it; an event box waits for its answer as always.
#include "ui/GameUi.h"

#include "assets/Megadata.h"
#include "core/Lang.h"
#include "game/Crops.h"
#include "game/Shop.h"
#include "ui/MapView.h"

#include <algorithm>
#include <cmath>

namespace osf {

extern const char* const kMonthShort[12];
extern const char* const kSprayNames[4];
extern const Tool kPaletteGrid[6][2];

namespace {
constexpr int kTitle = 20;  // a list's title bar and the gap under it
// the icon bar: the toolbar's buttons as the desktop shows them (no EDIT; Market Value in its place), and Farm Expert
// (user 2026-11-11: "add farm expert to the top menu"; its face: the farmer of the Farm Expert window's first card)
constexpr int kIconBarButtons = 9;
constexpr int kIconBarFace[kIconBarButtons] = { 0, 32, 64, 96, 128, 160, 192, 256, -1 };
constexpr int kIconBarWindow[kIconBarButtons] = { kWinBuy, kWinSell, kWinEvaluation, kWinMap, kWinWeather, kWinBalance, kWinBank, kWinMarket, kWinExpert };
constexpr int kExpertFaceX = 32, kExpertFaceY = 4, kExpertFaceSize = 56; // in the picture bank's picture 66
constexpr int kExamineW = 208, kExamineH = 208;
constexpr int kIconPlantButton = 100, kIconSprayButton = 101;
constexpr int kIconCrop = 200, kIconSpare = 300, kIconSpray = 400;
} // namespace

void GameUi::SetPhone(bool on)
{
    _phone = on;
    _openMenu = -1;
    _popup = 0;
    _historyOpen = false;
    _sheet = Sheet::None;
}

// ---------------------------------------------------------------- the lists

std::string GameUi::SheetTitle() const
{
    switch (_sheet)
    {
    case Sheet::Plant: return Tr("Plant Field");
    case Sheet::Spray: return Tr("Spray Field");
    case Sheet::MenuItems: return _menus[static_cast<size_t>(std::clamp(_sheetMenu, 0, static_cast<int>(_menus.size()) - 1))].title;
    case Sheet::Messages: return Tr("Messages");
    default: return "";
    }
}

std::vector<GameUi::SheetItem> GameUi::SheetItems()
{
    std::vector<SheetItem> v;
    switch (_sheet)
    {
    case Sheet::Plant:
        for (size_t i = 0; i < _plantList.size(); ++i)
        {
            const PlantEntry& p = _plantList[i];
            v.push_back({ p.name, "plantrow:" + std::to_string(i), p.slot >= 0 ? kIconCrop + p.slot : kIconSpare + p.spare, true,
                          _tool == Tool::Plant && p.slot >= 0 && p.slot == (_toolParam & 15) });
        }
        break;
    case Sheet::Spray:
        for (int i = 0; i < 4; ++i)
            v.push_back({ Tr(kSprayNames[i]), "sprayrow:" + std::to_string(i), kIconSpray + i, true, _tool == Tool::Spray && _toolParam == 6 + i });
        break;
    case Sheet::MenuItems:
        if (_sheetMenu >= 0 && _sheetMenu < static_cast<int>(_menus.size()))
            for (const auto& it : _menus[static_cast<size_t>(_sheetMenu)].items)
                v.push_back({ it.label, it.command, -1, ItemEnabled(it.command), ItemChecked(it.command) });
        break;
    case Sheet::Messages:
        for (auto it = _history.rbegin(); it != _history.rend(); ++it)
            v.push_back({ it->first + "  " + it->second, "", -1, false });
        if (v.empty())
            v.push_back({ Tr("No messages yet."), "", -1, false });
        break;
    default:
        break;
    }
    return v;
}

GameUi::SheetGeometry GameUi::SheetLayout(int n)
{
    // all the items in one list that scrolls when it is taller than the screen (no pages); the tools, crops and
    // sprays in a narrower drawer at the left, in two columns when the phone is turned sideways
    SheetGeometry g;
    g.rowH = SheetDrawer() ? 28 : SheetRightDrawer() ? 20 : _sheet == Sheet::Messages ? 22 : 20;
    g.cellW = _sheet == Sheet::Messages ? 320 : SheetDrawer() ? 136 : 168;
    const bool wide = _pw > _ph * 6 / 5;
    g.cols = wide && n > 6 && _sheet != Sheet::Messages ? 2 : 1;
    g.rows = std::max(1, (n + g.cols - 1) / g.cols);
    g.w = g.cols * g.cellW + 8;
    g.h = kTitle + g.rows * g.rowH + 4;
    return g;
}

Rect GameUi::SheetCell(const SheetGeometry& g, const Rect& box, int i) const
{
    const int col = i / g.rows, row = i % g.rows;
    return { box.x + 4 + col * g.cellW, box.y + kTitle + row * g.rowH - _sheetScroll, g.cellW - 2, g.rowH - 2 };
}

void GameUi::DrawSheetIcon(int icon, int x, int y)
{
    // a 24 x 24 icon: the tool column's pictures, the crops' and sprays' list pictures
    if (icon >= 0 && icon < 12)
    {
        _g.DrawPicturePart("editeasy.lzs", 24 * (icon % 2), 24 * (icon / 2), 24, 24, x, y);
        return;
    }
    _g.Fill(x, y, 24, 24, kLightGrey);
    _g.Frame(x, y, 24, 24, kBlack);
    if (icon == kIconPlantButton)
        _g.DrawPicturePart("buywndo.bmp", 16, 42, 16, 16, x + 4, y + 4);
    else if (icon == kIconSprayButton)
        _g.DrawPicturePart("buywndo.bmp", 15, 131, 18, 18, x + 3, y + 3);
    else if (icon >= kIconCrop && icon < kIconCrop + 16)
        _g.DrawTile(832 + icon - kIconCrop, x + 4, y + 4);
    else if (icon >= kIconSpare && icon < kIconSpray)
    {
        const size_t k = static_cast<size_t>(icon - kIconSpare);
        if (k < _spareCrops.size())
        {
            const auto& pal = _g.Pal();
            std::vector<uint32_t> img(256);
            for (size_t p = 0; p < 256; ++p)
            {
                const auto& c = pal[static_cast<size_t>((p < _spareCrops[k].icon.size() ? _spareCrops[k].icon[p] : static_cast<uint8_t>(kLightGrey)) & 15)];
                img[p] = 0xFF000000u | (static_cast<uint32_t>(c[0]) << 16) | (static_cast<uint32_t>(c[1]) << 8) | c[2];
            }
            _g.DrawPixels(img, 16, 16, x + 4, y + 4, 16, 16);
        }
    }
    else if (icon >= kIconSpray && icon < kIconSpray + 4)
        _g.DrawAnyTile(848 + 67 - (icon - kIconSpray), x + 4, y + 4);
}

void GameUi::DrawSheet(const Rect& box)
{
    const std::vector<SheetItem> items = SheetItems();
    const SheetGeometry g = SheetLayout(static_cast<int>(items.size()));
    int maxScroll = std::max(0, g.h - box.h);
    if (maxScroll <= 6)
        maxScroll = 0; // (only the padding under the last row hidden)
    _sheetScroll = std::clamp(_sheetScroll, 0, maxScroll);
    if (!SheetDrawer() && !SheetRightDrawer())
        _g.Fill(box.x + 4, box.y + 4, box.w, box.h, kDarkGrey); // drop shadow, as the windows
    _g.Fill(box.x, box.y, box.w, box.h, kLightGrey);
    _g.Frame(box.x, box.y, box.w, box.h, kBlack);
    DrawTitleBar(box.x, box.y, box.w, SheetTitle(), true, true);
    // the items under the title, cut at the list's edges
    SDL_Renderer* rd = _g.Renderer();
    const SDL_Rect clip{ box.x + 1, box.y + kTitle, box.w - 2, box.h - kTitle - 2 };
    SDL_RenderSetClipRect(rd, &clip);
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
    {
        const SheetItem& it = items[static_cast<size_t>(i)];
        const Rect r = SheetCell(g, box, i);
        if (r.y + r.h < clip.y || r.y > clip.y + clip.h)
            continue;
        if (_sheet == Sheet::Messages)
        {
            // a message: its date in the small text, then the message itself (tighter letters when long)
            const auto gap = it.label.find("  ");
            const std::string date = gap == std::string::npos ? "" : it.label.substr(0, gap);
            const std::string text = gap == std::string::npos ? it.label : it.label.substr(gap + 2);
            _g.SmallText(r.x + 2, r.y + 1, date, kGrey);
            if (Gfx::FitWidth(text, r.w - 4) <= r.w - 4)
                _g.TextFit(r.x + 2, r.y + 10, r.w - 4, text, kBlack);
            else
                _g.Text(r.x + 2, r.y + 10, text.substr(0, static_cast<size_t>(std::max(0, (r.w - 4) / 8))), kBlack);
            continue;
        }
        _g.Fill(r.x, r.y, r.w, r.h, it.checked ? kNavy : kLightGrey);
        _g.Bevel(r.x, r.y, r.w, r.h, !it.checked);
        int tx = r.x + 6;
        if (it.icon >= 0)
        {
            DrawSheetIcon(it.icon, r.x + 2, r.y + (r.h - 24) / 2);
            tx = r.x + 30;
        }
        const int ink = !it.enabled ? kGrey : it.checked ? kWhite : kBlack;
        const int room = r.x + r.w - 4 - tx;
        if (Gfx::FitWidth(it.label, room) <= room)
            _g.TextFit(tx, r.y + (r.h - 8) / 2, room, it.label, ink);
        else
            _g.Text(tx, r.y + (r.h - 8) / 2, it.label.substr(0, static_cast<size_t>(std::max(0, room / 8))), ink);
    }
    SDL_RenderSetClipRect(rd, nullptr);
    if (maxScroll > 0)
    {
        // a scroll bar at the right edge: where the shown part is in the whole list
        const int th = clip.h, kh = std::max(12, th * box.h / g.h);
        const int ky = clip.y + (th - kh) * _sheetScroll / maxScroll;
        _g.Fill(box.x + box.w - 4, clip.y, 3, th, kGrey);
        _g.Fill(box.x + box.w - 4, ky, 3, kh, kNavy);
    }
}

void GameUi::SheetClick(int x, int y, const Rect& box)
{
    if (!box.Contains(x, y))
    {
        _sheet = Sheet::None; // a tap beside the list closes it
        return;
    }
    if (y < box.y + 16)
    {
        // the title bar closes the list
        _sheet = Sheet::None;
        _sheetScroll = 0;
        Click();
        return;
    }
    if (y < box.y + kTitle)
        return;
    const std::vector<SheetItem> items = SheetItems();
    const SheetGeometry g = SheetLayout(static_cast<int>(items.size()));
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
        if (SheetCell(g, box, i).Contains(x, y))
        {
            const SheetItem& it = items[static_cast<size_t>(i)];
            if (it.enabled && !it.command.empty())
                SheetCommand(it.command);
            return;
        }
}

void GameUi::SheetCommand(const std::string& cmd)
{
    const auto arg = [&](size_t at) { return std::atoi(cmd.c_str() + at); };
    Click();
    _sheetScroll = 0;
    if (cmd.rfind("tool:", 0) == 0)
    {
        const int i = std::clamp(arg(5), 0, 11);
        _tool = kPaletteGrid[i / 2][i % 2];
        _moveKind = 0;
        _sheet = Sheet::None;
    }
    else if (cmd == "plant" || cmd == "spray")
    {
        // the other tools are let go of (user 2026-11-11: "when i select bulldoze i can select plant or spray as
        // well without deselecting bulldoze"); the crop or spray in use stays until another is chosen
        if (_tool != Tool::Plant && _tool != Tool::Spray)
        {
            _tool = Tool::None;
            _moveKind = 0;
        }
        if (cmd == "plant")
            ListSpareCrops();
        _sheet = cmd == "plant" ? Sheet::Plant : Sheet::Spray;
    }
    else if (cmd.rfind("plantrow:", 0) == 0)
    {
        const size_t i = static_cast<size_t>(arg(9));
        _sheet = Sheet::None;
        if (i >= _plantList.size())
            return;
        if (_plantList[i].slot < 0)
            PlantSpareCrop(_plantList[i].spare);
        else
        {
            _tool = Tool::Plant;
            _toolParam = _plantList[i].slot;
        }
        _moveKind = 0;
    }
    else if (cmd.rfind("sprayrow:", 0) == 0)
    {
        _tool = Tool::Spray;
        _toolParam = 6 + std::clamp(arg(9), 0, 3);
        _moveKind = 0;
        _sheet = Sheet::None;
    }
    else if (cmd.rfind("menu:", 0) == 0)
    {
        _sheetMenu = arg(5);
        _sheet = Sheet::MenuItems;
    }
    else
    {
        // a menu's item: as the menu bar does it (some open a window or an event box, which then shows instead)
        const bool stay = cmd.rfind("opt:", 0) == 0 || cmd == "music" || cmd == "sfx" || cmd == "msgs" || cmd == "smartdrive" ||
                          cmd == "modernsound" || cmd == "modernrender";
        if (!stay)
            _sheet = Sheet::None;
        Execute(cmd);
    }
}

// ---------------------------------------------------------------- the layer over the farm

GameUi::PhoneLayer GameUi::PhoneLayerNow()
{
    PhoneLayer L;
    const int m = 2 * _fs;
    const float fMax = static_cast<float>(_fs);
    const auto fit = [&](int kind, int cw, int ch) {
        L.kind = kind;
        L.f = std::max(0.25f, std::min({ (_pw - 2 * m) / static_cast<float>(cw), (_ph - 2 * m) / static_cast<float>(ch), fMax }));
        L.sw = static_cast<int>(_pw / L.f);
        L.sh = static_cast<int>(_ph / L.f);
        L.box = { (L.sw - cw) / 2, (L.sh - ch) / 2, cw, ch };
    };
    if (!_events.empty())
    {
        if (_events.front().id == kTownVoteEvent)
            fit(1, 368, 224);
        else
            fit(1, 256, 204);
        return L;
    }
    if (!_windows.empty())
    {
        Window& w = *_windows.back();
        fit(3, w.w + 4, w.h + Window::kTitleH + 4); // with its drop shadow
        L.box.w = w.w;
        L.box.h = w.h + Window::kTitleH;
        w.x = L.box.x;
        w.y = L.box.y;
        return L;
    }
    if (_sheet != Sheet::None)
    {
        const SheetGeometry g = SheetLayout(static_cast<int>(SheetItems().size()));
        L.kind = 2;
        if (SheetDrawer() || SheetRightDrawer())
        {
            // a drawer between the bars: the crops and sprays at the left edge, a menu's items dropping down from
            // its title (left of the landscape tool column); as large as the bars' buttons (smaller when that lets the
            // whole list show, but not much), scrolled when still too long
            const int top = PhoneTopH(), bottom = _ph - PhoneBottomH(), right = _pw - PhoneSideW();
            const float avail = static_cast<float>(bottom - top);
            L.f = std::min(static_cast<float>(_fs), std::max(_fs * 0.7f, avail / g.h));
            L.f = std::min(L.f, right * 0.9f / g.w);
            if (SheetRightDrawer() || _sheet == Sheet::Spray)
                L.f = std::min(L.f, _fs * 0.8f); // a menu's items and the sprays a little smaller (user 2026-11-11)
            L.sw = static_cast<int>(_pw / L.f);
            L.sh = static_cast<int>(_ph / L.f);
            const int y0 = static_cast<int>(std::ceil(top / L.f));
            int x0 = static_cast<int>(std::ceil(PhoneLeftW() / L.f));
            if (SheetRightDrawer())
            {
                const Rect t = MenuTitleBox(_sheetMenu);
                x0 = std::clamp(static_cast<int>(t.x * _ms / L.f), 0, std::max(0, static_cast<int>(right / L.f) - g.w));
            }
            L.box = { x0, y0, g.w, std::min(g.h, static_cast<int>(bottom / L.f) - y0) };
            return L;
        }
        // the other lists in the middle, as wide as the screen allows, scrolled when too tall
        L.f = std::max(0.25f, std::min(fMax, (_pw - 2 * m) / static_cast<float>(g.w + 4)));
        L.sw = static_cast<int>(_pw / L.f);
        L.sh = static_cast<int>(_ph / L.f);
        const int h = std::min(g.h, static_cast<int>((_ph - 2 * m) / L.f) - 4);
        L.box = { (L.sw - g.w) / 2, (L.sh - h) / 2, g.w, h };
        return L;
    }
    if ((_exHold || _exPinned) && _exShown)
    {
        int cw = kExamineW, ch = kExamineH;
        _g.PictureSize("examine.bmp", cw, ch);
        fit(4, std::max(1, cw), std::max(1, ch));
    }
    return L;
}

void GameUi::DrawPhoneLayer()
{
    const PhoneLayer L = PhoneLayerNow();
    if (!L.kind)
        return;
    SDL_Renderer* r = _g.Renderer();
    if (L.kind == 2 && (SheetDrawer() || SheetRightDrawer()))
        _g.DimRect(PhoneLeftW(), PhoneTopH(), _w - PhoneSideW() - PhoneLeftW(), _h - PhoneTopH() - PhoneBottomH(), 150); // the bars stay live
    else
        _g.DimRect(0, 0, _w, _h, 150);
    SDL_RenderSetScale(r, L.f, L.f);
    const int ow = _w, oh = _h;
    _w = L.sw;
    _h = L.sh;
    switch (L.kind)
    {
    case 1: DrawEventBox(); break;
    case 2: DrawSheet(L.box); break;
    case 3: DrawWindow(*_windows.back()); break;
    case 4: DrawExamineCard(_g, _ctx.mega, _exCard, L.box.x, L.box.y); break;
    default: break;
    }
    _w = ow;
    _h = oh;
    SDL_RenderSetScale(r, 1.0f, 1.0f);
}

// ---------------------------------------------------------------- the bars

Rect GameUi::PhoneButton(int k) const
{
    // the tool palette: two rows of seven across the width (portrait), seven rows of two down the right (landscape)
    const int bw = _w / _bs, bh = _h / _bs;
    if (_side)
    {
        const int top = (PhoneTopH() + _bs - 1) / _bs, bx = (_w - PhoneSideW()) / _bs;
        const int slot = std::max(34, (bh - top) / 7);
        return { bx + 4 + 34 * (k % 2), top + (k / 2) * slot + (slot - 32) / 2, 32, 32 };
    }
    const int by = (_h - PhoneBottomH()) / _bs;
    const int slot = std::max(34, bw / 7);
    return { (k % 7) * slot + (slot - 32) / 2, by + 3 + 34 * (k / 7), 32, 32 };
}

Rect GameUi::MenuTitleBox(int m) const
{
    // the menu bar's titles side by side across the width (SetScreenSize places them), in the menu rows' scale
    if (m < 0 || m >= static_cast<int>(_menuBox.size()))
        return { 0, MenuRowY(), 0, kMenuRowH };
    Rect r = _menuBox[static_cast<size_t>(m)];
    r.y += MenuRowY();
    return r;
}

Rect GameUi::IconBarButton(int k) const
{
    // the toolbar's 32 x 32 faces spread across the width under the menu titles, in the icon bar's scale
    const int slot = std::max(34, (_w / _ts) / kIconBarButtons);
    if (_side)
    {
        // sideways: a column at the left under the top
        const int top = (PhoneTopH() + _ts - 1) / _ts, slotH = std::max(33, (_h / _ts - top) / kIconBarButtons);
        return { 2, top + k * slotH + (slotH - 32) / 2, 32, 32 };
    }
    return { k * slot + (slot - 32) / 2, IconRowY() + 1, 32, 32 };
}

void GameUi::DrawPhoneBars()
{
    if (!_ctx.state || !_ctx.sim)
        return;
    const GameState& s = *_ctx.state;
    _g.SetScale(_fs);
    int bw = _w / _fs + 1, bh = _h / _fs + 1;

    // the top bar: date and funds; the status text and the speed at the bottom of the top, under the toolbar (user
    // 2026-11-11: "message bar can be moved to bottom of top menu now, together with speed")
    const int sy = StatusRowY();
    _g.Fill(0, 0, bw, kDateRowH, kLightGrey);
    _g.Fill(0, sy - 1, bw, kStatusRowH + 1, kLightGrey);
    _g.Fill(0, sy + kStatusRowH - 1, bw, 1, kBlack);
    const bool lost = _ctx.sim->Bankrupt();
    const std::string date = TrF("Week {0} of {1} {2}", s.cal.week + 1, Tr(kMonthShort[s.cal.month % 12]), s.cal.year);
    if ((_events.empty() && !lost) || _shownDate.empty())
        _shownDate = date;
    if (!lost)
        _shownCash = s.cash;
    // everything in the top bar can be tapped, and shows it as the speed does: navy with a small ">" after it
    // (user 2026-11-11: "funds and weather and message highlights the same way"); the date opens the weather, the
    // funds the Funds window, the status line the messages, the speed the Speed menu
    const std::string cash = "$" + std::to_string(_shownCash);
    const int arrow = Gfx::SmallTextWidth(">") + 2;
    std::string shownDate = _shownDate;
    if (Gfx::TextWidth(shownDate) + Gfx::TextWidth(cash) + 2 * arrow + 20 > bw)
        shownDate = TrF("W{0} {1} {2}", s.cal.week + 1, Tr(kMonthShort[s.cal.month % 12]), s.cal.year);
    _g.Text(4, 3, shownDate, kNavy);
    _g.SmallText(4 + Gfx::TextWidth(shownDate) + 2, 4, ">", kNavy);
    _g.TextRight(bw - 5 - arrow, 3, cash, _shownCash < 0 ? kRed : kNavy);
    _g.SmallText(bw - 5 - arrow + 2, 4, ">", kNavy);
    static const char* const kSpeedName[5] = { N_("Ultra"), N_("Fast"), N_("Normal"), N_("Slow"), N_("Pause") };
    const std::string speed = std::string(Tr(kSpeedName[std::clamp(static_cast<int>(_ctx.sim->GetSpeed()), 0, 4)])) + " >";
    const int sx = bw - 5 - Gfx::SmallTextWidth(speed);
    _g.SmallText(sx, sy + 2, speed, kNavy);
    std::string text = StatusText();
    if (text.empty())
        text = Tr("Messages"); // (no tool: the line still opens the messages)
    if (Gfx::SmallTextWidth(text + " >") > sx - 10)
    {
        while (!text.empty() && Gfx::SmallTextWidth(text + ".. >") > sx - 10)
            text.pop_back();
        text += "..";
    }
    _g.SmallText(4, sy + 2, text + " >", kNavy);

    // under it the desktop's top as it is (user 2026-11-11: "put the normal top menu all in the top bar"): the menu
    // bar's titles, each opening its items under it, and the toolbar's buttons, each opening its window
    _g.SetScale(1);
    _g.Fill(0, kDateRowH * _fs, _w, StatusRowY() * _fs - kDateRowH * _fs, kLightGrey);
    _g.Fill(0, kDateRowH * _fs, _w, std::max(1, _fs / 2), kGrey);
    {
        // the titles at their own (fractional) scale, a little larger than the icons
        SDL_Renderer* rd = _g.Renderer();
        SDL_RenderSetScale(rd, _ms, _ms);
        _g.Fill(0, MenuRowY() + kMenuRowH * _menuRows - 1, static_cast<int>(_w / _ms) + 1, 1, kGrey);
        for (int m = 0; m < static_cast<int>(_menus.size()); ++m)
        {
            const Rect t = MenuTitleBox(m);
            if (t.w <= 0)
                continue; // (not shown on the phone)
            const bool open = _sheet == Sheet::MenuItems && _sheetMenu == m;
            if (open)
                _g.Fill(t.x + 1, t.y + 1, t.w - 2, t.h - 3, kNavy);
            _g.TextFit(t.x + 1, t.y + 2, t.w - 2, _menus[static_cast<size_t>(m)].title, open ? kWhite : kBlack, 1);
        }
        SDL_RenderSetScale(rd, 1.0f, 1.0f);
    }
    _g.SetScale(_ts);
    {
        const int bw2 = _w / _ts + 1;
        if (_side)
        {
            const int top = PhoneTopH() / _ts;
            _g.Fill(0, top, 36, _h / _ts + 1 - top, kLightGrey);
            _g.Fill(35, top, 1, _h / _ts + 1 - top, kBlack);
        }
        else
            _g.Fill(0, IconRowY() + kIconBarH - 1, bw2, 1, kGrey);
        for (int k = 0; k < kIconBarButtons; ++k)
        {
            const Rect b = IconBarButton(k);
            if (kIconBarFace[k] >= 0)
            {
                _g.DrawPicturePart("cntlbtnh.bmp", kIconBarFace[k], 0, 32, 32, b.x, b.y);
                continue;
            }
            // Farm Expert: a button as the toolbar's, the farmer's face shrunk into it
            _g.Fill(b.x, b.y, 32, 32, kLightGrey);
            _g.Bevel(b.x + 1, b.y + 1, 30, 30, true);
            _g.Frame(b.x, b.y, 32, 32, kBlack);
            SDL_Renderer* rd = _g.Renderer();
            const float f = _ts * 26.0f / kExpertFaceSize;
            _g.SetScale(1);
            SDL_RenderSetScale(rd, f, f);
            _g.DrawPicturePart("alltiles.btl#66", kExpertFaceX, kExpertFaceY, kExpertFaceSize, kExpertFaceSize,
                               static_cast<int>(std::lround((b.x + 3) * _ts / f)), static_cast<int>(std::lround((b.y + 3) * _ts / f)));
            SDL_RenderSetScale(rd, 1.0f, 1.0f);
            _g.SetScale(_ts);
        }
    }

    // the tool palette at the bottom (or in the right column): the twelve tools, Plant and Spray (user 2026-11-11:
    // "tools menu as the bottom"); the chosen one pressed
    _g.SetScale(_bs);
    bw = _w / _bs + 1;
    bh = _h / _bs + 1;
    if (_side)
    {
        const int bx = (_w - PhoneSideW()) / _bs, top = PhoneTopH() / _bs;
        _g.Fill(bx, top, bw - bx, bh - top, kLightGrey);
        _g.Fill(bx, top, 1, bh - top, kBlack);
    }
    else
    {
        const int by = (_h - PhoneBottomH()) / _bs;
        _g.Fill(0, by, bw, bh - by, kLightGrey);
        _g.Fill(0, by, bw, 1, kBlack);
    }
    for (int k = 0; k < kPhoneButtons; ++k)
    {
        const Rect b = PhoneButton(k);
        bool on;
        int icon;
        if (k < 12)
        {
            on = kPaletteGrid[k / 2][k % 2] == _tool;
            icon = k;
        }
        else
        {
            // the crop or spray in use, else the BUY window's planting / spraying picture
            const bool plant = k == 12;
            on = plant ? _tool == Tool::Plant : _tool == Tool::Spray;
            icon = !on ? (plant ? kIconPlantButton : kIconSprayButton)
                       : plant ? kIconCrop + (_toolParam & 15) : kIconSpray + std::clamp(_toolParam - 6, 0, 3);
            on = on || _sheet == (plant ? Sheet::Plant : Sheet::Spray);
        }
        _g.Fill(b.x, b.y, 32, 32, on ? kGrey : kLightGrey);
        DrawSheetIcon(icon, b.x + 4, b.y + 4);
        _g.Frame(b.x, b.y, 32, 32, kBlack);
        _g.Bevel(b.x + 1, b.y + 1, 30, 30, !on);
    }
    _g.SetScale(1);
}

bool GameUi::PhoneBarClick(int x, int y)
{
    // a tap on the bars; with a list open it is closed first, and the tap then does what it does (a tap on the
    // button or title that opened the list only closes it)
    const Sheet was = _sheet;
    const int wasMenu = _sheetMenu;
    _sheet = Sheet::None;
    _sheetScroll = 0;
    const int statusY = StatusRowY() * _fs;
    const auto toolbar = [&]() {
        // the toolbar's buttons (under the menu titles, or in the left column sideways)
        const int bx = x / _ts, by = y / _ts;
        for (int k = 0; k < kIconBarButtons; ++k)
        {
            const Rect b = IconBarButton(k);
            if (bx >= b.x - 2 && bx < b.x + b.w + 2 && by >= b.y - 1 && by < b.y + b.h + 1)
            {
                Execute("win:" + std::to_string(kIconBarWindow[k]));
                break;
            }
        }
        return true;
    };
    if (y >= kDateRowH * _fs && y < statusY)
    {
        if (y < MenuRowBottom())
        {
            const int mx = static_cast<int>(x / _ms);
            // a menu's title: its items drop down under it
            for (int m = 0; m < static_cast<int>(_menus.size()); ++m)
            {
                const Rect t = MenuTitleBox(m);
                const int my = static_cast<int>(y / _ms);
                if (mx >= t.x && mx < t.x + t.w && my >= t.y && my < t.y + t.h)
                {
                    Click();
                    if (!(was == Sheet::MenuItems && wasMenu == m))
                        SheetCommand("menu:" + std::to_string(m));
                    return true;
                }
            }
            return true;
        }
        return toolbar();
    }
    if (y < PhoneTopH())
    {
        const int bx = x / _fs, bw = _w / _fs;
        Click();
        if (y < statusY)
        {
            if (bx >= bw / 2)
                OpenWindow(kWinFunds);
            else
                Execute("win:" + std::to_string(kWinWeather));
        }
        else if (bx >= bw * 2 / 3)
        {
            if (!(was == Sheet::MenuItems && wasMenu == 2))
            {
                _sheetMenu = 2; // the Speed menu
                _sheet = Sheet::MenuItems;
            }
        }
        else if (was != Sheet::Messages)
            _sheet = Sheet::Messages;
        return true;
    }
    if (x < PhoneLeftW())
        return toolbar();
    // the tool palette
    if (_side ? x < _w - PhoneSideW() : y < _h - PhoneBottomH())
        return false;
    const int bx = x / _bs, by = y / _bs;
    for (int k = 0; k < kPhoneButtons; ++k)
    {
        const Rect b = PhoneButton(k);
        if (bx < b.x - 1 || bx >= b.x + b.w + 1 || by < b.y - 1 || by >= b.y + b.h + 1)
            continue;
        // a tap on the chosen tool lets go of it, back to the plain pointer (user 2026-11-11: "if i click again i
        // should cancel that action"); Plant and Spray the same, also with their list open (as the desktop's buttons)
        const Tool t = k < 12 ? kPaletteGrid[k / 2][k % 2] : k == 12 ? Tool::Plant : Tool::Spray;
        const bool listWas = was == (k == 12 ? Sheet::Plant : Sheet::Spray);
        if (_tool == t && (k < 12 || listWas || was == Sheet::None))
        {
            Click();
            _tool = Tool::None;
            _moveKind = 0;
        }
        else if (k < 12)
            SheetCommand("tool:" + std::to_string(k));
        else if (listWas)
            Click();
        else
            SheetCommand(k == 12 ? "plant" : "spray");
        return true;
    }
    return true;
}

// ---------------------------------------------------------------- input

bool GameUi::PhoneEvent(const SDL_Event& e, bool& used)
{
    const bool down = e.type == SDL_MOUSEBUTTONDOWN, up = e.type == SDL_MOUSEBUTTONUP, motion = e.type == SDL_MOUSEMOTION;
    if (!down && !up && !motion && e.type != SDL_MOUSEWHEEL)
        return false;
    int x = 0, y = 0;
    if (motion)
        x = e.motion.x, y = e.motion.y;
    else if (down || up)
        x = e.button.x, y = e.button.y;
    else
        MouseState(&x, &y);
    const PhoneLayer L = PhoneLayerNow();
    if (L.kind)
    {
        used = true;
        const int qx = static_cast<int>(std::floor(x / L.f)), qy = static_cast<int>(std::floor(y / L.f));
        const bool inside = L.box.Contains(qx, qy);
        const bool left = (down || up) && e.button.button == SDL_BUTTON_LEFT;
        if (L.kind == 4)
        {
            if (down) // any tap puts the Examine card away
                _exPinned = _exHold = _exShown = false;
            return true;
        }
        if (L.kind == 2)
        {
            // a tap beside the list closes it at once; on the list a finger drags it up and down, and a tap (a
            // press let go where it went down) chooses
            if (down && left && !inside)
            {
                _sheetDown = false;
                if (y < PhoneTopH() || y >= _h - PhoneBottomH() || x >= _w - PhoneSideW() || x < PhoneLeftW())
                    PhoneBarClick(x, y); // on the bars: closes the list and does what the tap there does
                else
                    SheetClick(qx, qy, L.box);
            }
            else if (down && left)
            {
                _sheetDown = true;
                _sheetDragged = false;
                _sheetDownY = qy;
                _sheetDownScroll = _sheetScroll;
            }
            else if (motion && _sheetDown)
            {
                if (std::abs(qy - _sheetDownY) > 4)
                    _sheetDragged = true;
                if (_sheetDragged)
                    _sheetScroll = std::max(0, _sheetDownScroll - (qy - _sheetDownY)); // (DrawSheet keeps it in range)
            }
            else if (up && left && _sheetDown)
            {
                _sheetDown = false;
                if (!_sheetDragged)
                    SheetClick(qx, _sheetDownY, L.box);
            }
            else if (e.type == SDL_MOUSEWHEEL)
                _sheetScroll = std::max(0, _sheetScroll - 24 * e.wheel.y);
            return true;
        }
        if (L.kind == 3 && !inside)
        {
            if (down && left)
            {
                _windows.back()->closed = true; // a tap beside the window closes it
                Click();
            }
            return true;
        }
        if (e.type == SDL_MOUSEWHEEL)
        {
            if (L.kind == 3)
                _windows.back()->Wheel(e.wheel.y);
            return true;
        }
        if ((down || up) && !left)
            return true; // no right clicks in a window or a box
        // inside the window or the event box: the usual handling, in the layer's units
        SDL_Event c = e;
        if (motion)
        {
            c.motion.x = qx;
            c.motion.y = qy;
            c.motion.xrel = static_cast<Sint32>(e.motion.xrel / L.f);
            c.motion.yrel = static_cast<Sint32>(e.motion.yrel / L.f);
        }
        else
        {
            c.button.x = qx;
            c.button.y = qy;
        }
        const int ow = _w, oh = _h;
        _w = L.sw;
        _h = L.sh;
        HandleEventCore(c);
        _w = ow;
        _h = oh;
        return true;
    }
    // the bars; a tool being dragged over the farm keeps its motion and release
    if (down && (y < PhoneTopH() || y >= _h - PhoneBottomH() || x >= _w - PhoneSideW() || x < PhoneLeftW()))
    {
        used = true;
        if (e.button.button == SDL_BUTTON_LEFT)
            PhoneBarClick(x, y);
        return true;
    }
    return false;
}

} // namespace osf

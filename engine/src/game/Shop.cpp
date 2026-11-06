// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "game/Shop.h"

#include "game/Editing.h"
#include "game/Machines.h"
#include "game/Simulation.h"

#include <algorithm>
#include <cctype>

namespace osf {

namespace {
// building tiles: first id of the picture stamped row by row
int BuildingTile(int cat)
{
    switch (cat)
    {
    case 0x40: return 440;  // small silo
    case 0x41: return 441;  // large silo
    case 0x42: return 445;  // small shed
    case 0x43: return 451;  // large shed
    case 0x44: return 460;  // small barn
    case 0x45: return 469;  // large barn
    case 0x46: return 120;  // water pump
    case 0x47: return 1010; // windmill (masked, drawn over the ground)
    case 0x48: return 481;  // water tower
    default: return -1;
    }
}
int CatalogByte(const GameState& s, int rec, int off) { return Rd8(s.catalog, static_cast<size_t>(rec) * 18 + off); }
int CatalogId(const GameState& s, int rec) { return Rd16(s.catalog, static_cast<size_t>(rec) * 18 + 2); }
// a building or supply goes on plain ground (15-30); with AutoDoze also on rocks and trees (bulldozed
// first) and on roads (150-171), fences (177-187), ditches (87-108) and valves (122-125), which it simply covers
bool Buildable(int id, bool autodoze)
{
    if (id >= 15 && id <= 30)
        return true;
    return autodoze && (AutoDozable(id) || (id >= 150 && id <= 171) || (id >= 177 && id <= 187) || (id >= 87 && id <= 108) ||
                        (id >= 122 && id <= 125));
}
// placing with AutoDoze: every footprint cell is put through the bulldozer (rocks and trees only, 25 each)
void AutoDoze(GameState& s, Simulation& sim, int x, int y, int w, int h)
{
    if (!s.scalars["opt_autodoze"])
        return;
    for (int cx = x; cx < x + w && cx < s.world.size; ++cx)
        for (int cy = y; cy < y + h && cy < s.world.size; ++cy)
            if (AutoDozable(s.world.At(cx, cy).BaseTile()) && s.cash >= 25 && sim.Spend(-1, 25))
            {
                MapCell& c = s.world.At(cx, cy);
                c.base = static_cast<uint16_t>((c.base & 0xF800) | GroundTile(s, sim.SoilWetness(), cx, cy));
                ReshapeAround(s, cx, cy);
            }
}
constexpr int kMachinePicture[9] = { 34, 35, 36, 37, 38, 39, 40, 40, 41 };
constexpr int kBuildingPicture[9] = { 42, 42, 43, 43, 44, 44, 51, 45, 50 };
constexpr int kAnimalRecord[4] = { 45, 46, 47, 48 };   // horse, cow, pig, sheep
constexpr int kAnimalPicture[4] = { 30, 32, 28, 26 };
constexpr const char* kAnimalName[4] = { "Horse", "Cow", "Pig", "Sheep" };
constexpr const char* kMachineName[9] = { "Tractor", "Plow", "Planter", "Sprayer", "Harvester", "Trailer", "", "Truck", "Crop Duster" };
constexpr const char* kBuildingName[9] = { "Small Silo", "Large Silo", "Small Shed", "Large Shed", "Small Barn", "Large Barn", "Water Pump", "Windmill", "Water Tower" };
constexpr const char* kSupplyName[4] = { "Fertilizer", "Pesticide", "Herbicide", "Fungicide" };

// picture of a crop by its file name (alltiles.btl records 0-23)
int CropPicture(const std::string& file, int slot)
{
    static const char* const names[24] = { "corn", "wheat", "barley", "potatoes", "oats", "rice", "sorghum", "soybeans", "cotton", "onions", "tomatoes", "lettuce",
                                           "carrots", "strawber", "sugar", "peanuts", "gladiolu", "sweet", "tobacco", "sunflowe", "grapes", "oranges", "almonds", "apples" };
    std::string f;
    for (char c : file)
        f += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (int i = 0; i < 24; ++i)
        if (f.rfind(names[i], 0) == 0)
            return i;
    return slot;
}
} // namespace

std::vector<int> ShopItems(int category)
{
    switch (category)
    {
    case kShopMachinery: return { 0, 1, 2, 3, 4, 5, 7, 8 };
    case kShopSeeds: return { 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36 };
    case kShopLivestock: return { 46, 47, 48, 45 }; // record order from the cow (the window opens on it); down goes back: cow, horse, sheep, pig
    case kShopBuildings: return { 12, 13, 14, 15, 16, 17, 18, 19, 20 };
    default: return { 82, 83, 84, 85 };
    }
}

static int BuildingCovering(const GameState& s, int x, int y, bool& open);
namespace {
bool StoreIn(GameState& s, Simulation& sim, int st, int type, int record, int picture);
}

// the record that holds a shop item's Own count: a seed's is the seed record (66 + crop, the original's catalogue entry
// for type 0xC0 + crop), not the crop's market record, whose count is the crop's contracts
int ShopOwnRecord(int rec) { return rec >= 21 && rec <= 36 ? 66 + rec - 21 : rec; }

int ShopPrice(const GameState& s, int rec)
{
    if (rec >= 21 && rec <= 36)
        return s.CatalogPrice(66 + rec - 21); // the seed record's own price: max(5, P18 / 4) at load, then max(5, market / 4) weekly
    return s.CatalogPrice(rec);
}

int ShopPicture(const GameState& s, int rec)
{
    if (rec <= 8)
        return kMachinePicture[rec];
    if (rec >= 12 && rec <= 20)
        return kBuildingPicture[rec - 12];
    if (rec >= 21 && rec <= 36)
        return CropPicture(s.crops[rec - 21].fileName, rec - 21);
    if (rec >= 45 && rec <= 48)
        return kAnimalPicture[rec - 45];
    if (rec >= 82 && rec <= 85)
        return 52 + (rec - 82);
    return 0;
}

int ShopTextId(const GameState& s, int rec) { return Rd16(s.catalog, static_cast<size_t>(rec) * 18); }

int ShopMapTile(int rec)
{
    if (rec <= 8)
        return rec == 8 ? 1028 : Machines::TileFor(rec, 0);
    if (rec >= 12 && rec <= 20)
        return BuildingTile(0x40 + rec - 12);
    if (rec >= 21 && rec <= 36)
        return 816 + rec - 21;
    if (rec >= 45 && rec <= 48)
        return Animals::TileFor(rec - 45, 4, 0);
    if (rec >= 82 && rec <= 85)
        return 915 - (rec - 82);
    return -1;
}

bool ShopNeedsPlacing(int rec) { return rec != 8; } // seed too: its bag goes into a building

std::string ShopName(const GameState& s, int rec)
{
    if (rec <= 8)
        return kMachineName[rec];
    if (rec >= 12 && rec <= 20)
        return kBuildingName[rec - 12];
    if (rec >= 21 && rec <= 36)
        return s.crops[rec - 21].name + " seed";
    if (rec >= 45 && rec <= 48)
        return kAnimalName[rec - 45];
    if (rec >= 82 && rec <= 85)
        return kSupplyName[rec - 82];
    return "";
}

void ShopFootprint(const GameState& s, int rec, int& w, int& h)
{
    w = h = 1;
    if (rec >= 12 && rec <= 20) // buildings: their catalogue size, as ShopBuy checks it
    {
        w = std::max(1, CatalogByte(s, rec, 8));
        h = std::max(1, CatalogByte(s, rec, 9));
    }
    else if (rec >= 0 && rec < 8) // machines: the long ones stand on two cells downwards (Machines::FootprintFree)
        h = Machines::Cells(rec);
}

EditResult ShopBuy(GameState& s, Simulation& sim, int rec, int x, int y)
{
    const int price = ShopPrice(s, rec);
    const int cat = CatalogId(s, rec);
    auto own = [&] { s.SetCatalogOwn(ShopOwnRecord(rec), s.CatalogOwn(ShopOwnRecord(rec)) + 1); };
    if (s.cash < price)
        return { false, "Not enough money.", "uhoh.voc" };

    if (rec == 8)
    {
        const std::string why = sim.Plane().Buy();
        return { why.empty(), why.empty() ? "The crop duster waits at the town airport." : why, why.empty() ? "plopitem.voc" : "uhoh.voc" };
    }
    if (x < 0 || y < 0 || x >= s.world.size || y >= s.world.size)
        return { false, "", "uhoh.voc" };
    if (s.PlotOwner(x / 8, y / 8) != 1)
        return { false, "You do not own this land.", "uhoh.voc" };
    if (rec >= 21 && rec <= 36)
    {
        // seed: a bag (item 0xC0 + crop, 10 in it) placed into a free slot of a building, open or closed, of any
        // kind; bare ground is refused (seen live on MVB: corn seed into an open and a closed small silo)
        const int crop = rec - 21;
        bool open = false;
        const int st = BuildingCovering(s, x, y, open);
        if (!st)
            return { false, "Seed is kept in a building.", "uhoh.voc" };
        const int bag = sim.NewItem(0xC0 + crop, 10, x, y);
        if (!bag)
            return { false, "There is no room there.", "uhoh.voc" };
        if (!StoreIn(s, sim, st, 0xC0 + crop, bag, 0x340 + crop))
        {
            sim.RemoveItem(bag);
            return { false, "There is no room there.", "uhoh.voc" };
        }
        sim.Spend(192 + crop, price);
        own();
        return { true, "", "plopitem.voc" };
    }

    if (rec <= 8)
    {
        if (rec == 8)
        {
            const std::string why = sim.Plane().Buy();
            return { why.empty(), why.empty() ? "The crop duster waits at the town airport." : why, why.empty() ? "plopitem.voc" : "uhoh.voc" };
        }
        if (!sim.Fleet().FootprintFree(rec, x, y))
            return { false, "There is no room here.", "uhoh.voc" };
        if (!sim.Fleet().Create(rec, x, y))
            return { false, "You have too many machines.", "uhoh.voc" };
        sim.Spend(rec, price);
        s.scalars["asset_machinery"] += price;
        own();
        return { true, "", "plopitem.voc" };
    }
    if (rec >= 45 && rec <= 48)
    {
        if (!sim.Herds().Place(x, y, rec - 45))
            return { false, "There is no room here.", "uhoh.voc" };
        sim.Spend(cat, price);
        s.scalars["asset_livestock"] += price;
        own();
        return { true, "", "plopitem.voc" };
    }
    if (rec >= 82 && rec <= 85)
    {
        MapCell& c = s.world.At(x, y);
        if (c.HasObject() || !Buildable(c.BaseTile(), s.scalars["opt_autodoze"] != 0))
            return { false, "There is no room here.", "uhoh.voc" };
        AutoDoze(s, sim, x, y, 1, 1);
        for (int i = 1; i < 127; ++i)
        {
            const size_t b = static_cast<size_t>(i) * 20;
            if (Rd8(s.objects20, b + 2) & 0x20)
                continue;
            for (size_t o = 0; o < 20; ++o)
                s.objects20[b + o] = 0;
            Wr16(s.objects20, b, cat);
            Wr8(s.objects20, b + 2, 0x20);
            Wr8(s.objects20, b + 5, x);
            Wr8(s.objects20, b + 6, y);
            s.object20Count++;
            c.base |= 0x800;
            c.overlay = static_cast<uint16_t>((c.overlay & 0xF800) | ShopMapTile(rec));
            sim.Spend(cat, price);
            own();
            return { true, "", "plopitem.voc" };
        }
        return { false, "There is no room for more supplies.", "uhoh.voc" };
    }
    if (rec >= 12 && rec <= 20)
    {
        const int w = std::max(1, CatalogByte(s, rec, 8)), h = std::max(1, CatalogByte(s, rec, 9));
        for (int cx = x; cx < x + w; ++cx)
            for (int cy = y; cy < y + h; ++cy)
                if (cx >= s.world.size || cy >= s.world.size || s.PlotOwner(cx / 8, cy / 8) != 1 || s.world.At(cx, cy).HasObject() ||
                    !Buildable(s.world.At(cx, cy).BaseTile(), s.scalars["opt_autodoze"] != 0) || sim.FieldIndexAt(cx, cy))
                    return { false, "There is no room here.", "uhoh.voc" };
        AutoDoze(s, sim, x, y, w, h);
        int index = 0;
        for (int i = 1; i < 127 && !index; ++i)
            if (!(Rd8(s.structures, static_cast<size_t>(i) * 70 + 2) & 0x20))
                index = i;
        if (!index)
            return { false, "You have too many buildings.", "uhoh.voc" };
        const size_t b = static_cast<size_t>(index) * 70;
        for (size_t o = 0; o < 70; ++o)
            s.structures[b + o] = 0;
        Wr16(s.structures, b, cat);
        Wr8(s.structures, b + 2, 0x20);
        Wr8(s.structures, b + 4, x);
        Wr8(s.structures, b + 5, y);
        for (int k = 0; k < 12; ++k)
        {
            Wr16(s.structures, b + 0x0A + 2 * k, 0xD9);   // empty storage slots: picture,
            Wr16(s.structures, b + 0x22 + 2 * k, 0xFFFF); // content (and item index 0)
        }
        s.structureCount++;
        const int base = BuildingTile(cat);
        for (int r = 0; r < h; ++r)
            for (int c = 0; c < w; ++c)
            {
                MapCell& m = s.world.At(x + c, y + r);
                if (base >= 848)
                {
                    m.base |= 0x800;
                    m.overlay = static_cast<uint16_t>((m.overlay & 0xF800) | (base + r * w + c));
                }
                else
                    m.base = static_cast<uint16_t>((m.base & 0xF800) | (base + r * w + c));
            }
        sim.Spend(cat, price);
        own();
        if (cat == 0x46 || cat == 0x47)
            sim.Water().AddSource(x, y, cat == 0x46 ? 0x40 : 0x30); // irrigation source: pump power 64, windmill 48
        return { true, "", "plopitem.voc" };
    }
    return { false, "", "uhoh.voc" };
}

int FamilyStart(int family)
{
    static const int kStart[160] = { 15, 39, 87, 232, 150, 161, 440, 441, 445, 451, 460, 469, 518, 273, 282, 529, 300, 309, 688, 696, 704, 712, 720, 728,
        736, 744, 752, 760, 768, 776, 784, 792, 800, 808, 962, 978, 994, 946, 109, 110, 848, 852, 852, 880, 856, 856, 904, 904, 904, 904, 868, 912, 860,
        860, 876, 876, 122, 504, 12, 12, 1010, 120, 131, 132, 133, 134, 2, 2, 2, 2, 518, 892, 896, 320, 915, 914, 913, 912, 177, 175, 188, 1020, 1028,
        87, 98, 539, 248, 832, 833, 834, 835, 836, 837, 838, 839, 840, 841, 842, 843, 844, 845, 846, 847, 481, 1010, 540, 1025, 1026, 69, 816, 817,
        818, 819, 820, 821, 822, 823, 824, 825, 826, 827, 828, 829, 830, 831, 916, 1025, 1026, 172, 122, 815, 816, 817, 818, 819, 820, 821, 839, 840,
        841, 842, 843, 844, 845, 846, 535, 218, 483 };
    return family >= 0 && family < 160 ? kStart[family] : 0;
}

std::vector<SaleItem> SaleList(GameState& s, Simulation& sim, int kind)
{
    std::vector<SaleItem> out;
    if (kind == 0)
    {
        // harvested crop stacks in storage (item records 0xA8 + crop): value = amount x price/10 x (65535 - age) / 65536
        for (int i = 1; i < 512; ++i)
        {
            const size_t b = static_cast<size_t>(i) * 12;
            const int type = Rd16(s.items, b);
            if (!(Rd8(s.items, b + 10) & 0x20) || type < 0xA8 || type > 0xB7)
                continue;
            const int crop = type - 0xA8;
            const int age = Rd16(s.items, b + 4);
            const int64_t value = age > 51000 ? 0 : static_cast<int64_t>(Rd16(s.items, b + 2)) * (s.MarketPrice(crop) / 10) * (65535 - age) / 65536;
            out.push_back({ 0, i, s.crops[crop].name, value, CropPicture(s.crops[crop].fileName, crop), 3 - std::min(3, age >> 14), 50 + crop });
        }
    }
    else if (kind == 1)
    {
        for (int i = 1; i < 127; ++i)
        {
            Machine m(s.machines, i);
            if (!m.InUse() || m.Leased() || m.Type() > 8 || m.Type() == 6) // the crop duster is listed too (seen live)
                continue;
            const int64_t value = s.CatalogPrice(m.Type()) >> (m.Wear() >> 6);
            out.push_back({ 1, i, kMachineName[m.Type()], value, kMachinePicture[m.Type()], 3 - (m.Wear() >> 6), m.Type() });
        }
    }
    else
    {
        for (int i = 1; i < 256; ++i)
        {
            const size_t b = static_cast<size_t>(i) * 32;
            if (!(Rd8(s.animals, b + 2) & 0x20))
                continue;
            const int sp = (Rd16(s.animals, b + 6) - 0xA0) & 3;
            // the original's livestock value: (price + factor x weight) >> (3 - condition/64), factors horse 3, cow 5,
            // pig 3, sheep 2 (weight = record word +0x0A)
            static const int factor[4] = { 3, 5, 3, 2 };
            const int64_t raw = s.CatalogPrice(kAnimalRecord[sp]) + static_cast<int64_t>(factor[sp]) * Rd16(s.animals, b + 0x0A);
            const int64_t value = raw >> (3 - (Rd8(s.animals, b + 8) >> 6));
            out.push_back({ 2, i, kAnimalName[sp], value, kAnimalPicture[sp], Rd8(s.animals, b + 8) >> 6, kAnimalRecord[sp] });
        }
    }
    (void)sim;
    return out;
}

EditResult ShopSell(GameState& s, Simulation& sim, const SaleItem& it)
{
    if (it.kind == 0)
    {
        const size_t b = static_cast<size_t>(it.index) * 12;
        const int crop = Rd16(s.items, b) - 0xA8;
        Wr8(s.items, b + 10, Rd8(s.items, b + 10) & ~0x20);
        // free its storage slot
        sim.FreeStorageSlot(it.index);
        s.itemCount = std::max(0, s.itemCount - 1);
        sim.Credit(168 + (crop & 15), it.value);
        // the original's SELL window books the sale a second time on the Crop Sales line (cash and yearly income once)
        s.scalars["rev_crops"] += it.value;
        // the crop's stored-stack count (byte 4 of its second catalogue record) goes down
        s.SetCatalogOwn(50 + (crop & 15), std::max(0, s.CatalogOwn(50 + (crop & 15)) - 1));
    }
    else if (it.kind == 1)
    {
        Machine m(s.machines, it.index);
        if (m.Flags() & 0x0B)
            return { false, "That machine is busy.", "uhoh.voc" };
        const int type = m.Type();
        sim.Fleet().Remove(it.index);
        s.scalars["asset_machinery"] = std::max<int64_t>(0, s.scalars["asset_machinery"] - s.CatalogPrice(type));
        s.SetCatalogOwn(type, std::max(0, s.CatalogOwn(type) - 1));
        sim.Credit(type, it.value);
        s.scalars["yearly_income"] += it.value; // the sell routine counts the sale as income
    }
    else
    {
        const size_t b = static_cast<size_t>(it.index) * 32;
        const int sp = (Rd16(s.animals, b + 6) - 0xA0) & 3;
        sim.RemoveAnimal(it.index);
        // (the original leaves the livestock asset unchanged on a sale)
        s.SetCatalogOwn(kAnimalRecord[sp], std::max(0, s.CatalogOwn(kAnimalRecord[sp]) - 1));
        sim.Credit(0xA0 + sp, it.value);
        s.scalars["yearly_income"] += it.value;
    }
    return { true, it.label + " sold for " + std::to_string(it.value) + ".", "plopitem.voc" };
}

// the building covering a cell: its record index, and whether its inside is shown (flag bit 0)
static int BuildingCovering(const GameState& s, int x, int y, bool& open)
{
    open = false;
    for (int i = 1; i < 127; ++i)
    {
        const size_t b = static_cast<size_t>(i) * 70;
        if (!(Rd8(s.structures, b + 2) & 0x20))
            continue;
        const int rec = Rd16(s.structures, b) - 52;
        if (rec < 0 || rec >= 103)
            continue;
        const int bx = Rd8(s.structures, b + 4), by = Rd8(s.structures, b + 5);
        const size_t cr = static_cast<size_t>(rec) * 18;
        const int w = std::max(1, static_cast<int>(static_cast<int8_t>(cr + 8 < s.catalog.size() ? s.catalog[cr + 8] : 1)));
        const int h = std::max(1, static_cast<int>(static_cast<int8_t>(cr + 9 < s.catalog.size() ? s.catalog[cr + 9] : 1)));
        if (x >= bx && y >= by && x < bx + w && y < by + h)
        {
            open = (Rd8(s.structures, b + 2) & 1) != 0;
            return i;
        }
    }
    return 0;
}

// The Move tool's pick, in the original's order: a machine (by footprint, also one parked in a building),
// a barrel record at the cell, then by the cell's ground picture a seed bag (832-847) or a crop stack (816-831) with an
// item record at the cell, a water trough (175-176), and last a feed pile by its overlay picture (1020-1024) with an
// item record. Animals cannot be moved. Seen live on ACT1/MRKC: a cow is refused, a closed building's stack is not seen.
bool PickObject(GameState& s, Simulation& sim, int x, int y, int& kind, int& index, std::string& why)
{
    if (x < 0 || y < 0 || x >= s.world.size || y >= s.world.size)
        return false;
    if (const int i = sim.Fleet().MachineAt(x, y))
    {
        Machine m(s.machines, i);
        bool open = false;
        if (m.Flags() & 0x03)
        {
            why = "That machine is busy.";
            return false;
        }
        if (BuildingCovering(s, x, y, open) && !open)
        {
            why = "Open the building first: right click on it.";
            return false;
        }
        kind = kMoveMachine;
        index = i;
        return true;
    }
    if (const int a = sim.Herds().AnimalAt(x, y))
    {
        // an engine addition (user finding Q20): the original's Move tool does not pick animals
        kind = kMoveAnimal;
        index = a;
        return true;
    }
    for (int i = 1; i < 127; ++i)
    {
        const size_t b = static_cast<size_t>(i) * 20;
        if ((Rd8(s.objects20, b + 2) & 0x20) && Rd8(s.objects20, b + 5) == x && Rd8(s.objects20, b + 6) == y)
        {
            kind = kMoveBarrel;
            index = i;
            return true;
        }
    }
    const MapCell& c = s.world.At(x, y);
    const int t = c.BaseTile();
    const int item = ItemAt(s, x, y);
    if ((t >= 832 && t <= 847) || (t >= 816 && t <= 831))
    {
        if (!item)
            return false;
        kind = kMoveItem;
        index = item;
        return true;
    }
    if (t == 175 || t == 176)
    {
        kind = kMoveTrough;
        index = item; // the original does not ask for a record here
        return true;
    }
    const int o = c.overlay & 0x7FF;
    if (o >= 1020 && o <= 1024 && item)
    {
        kind = kMoveFeed;
        index = item;
        return true;
    }
    why = "Choose a machine, an animal, a barrel or a stored thing to move.";
    return false;
}

// the item record at a cell: the first one in use there
int ItemAt(const GameState& s, int x, int y)
{
    for (int i = 1; i < 512; ++i)
    {
        const size_t b = static_cast<size_t>(i) * 12;
        if (b + 12 > s.items.size())
            break;
        if ((Rd8(s.items, b + 10) & 0x20) && Rd8(s.items, b + 8) == x && Rd8(s.items, b + 9) == y)
            return i;
    }
    return 0;
}

namespace {

// a building's slot block shown again as it is
void Redraw(const GameState& s, Simulation& sim, int st)
{
    const int bx = Rd8(s.structures, static_cast<size_t>(st) * 70 + 4), by = Rd8(s.structures, static_cast<size_t>(st) * 70 + 5);
    sim.ToggleInside(bx, by);
    sim.ToggleInside(bx, by);
}

// a slot freed: the block at the cell is empty again and the building is drawn again
void FreeSlotAt(GameState& s, Simulation& sim, int st, int x, int y)
{
    const size_t b = static_cast<size_t>(st) * 70;
    const int k = (x - Rd8(s.structures, b + 4)) * 3 + (y - Rd8(s.structures, b + 5));
    if (k >= 0 && k < 12)
    {
        Wr16(s.structures, b + 0x0A + 2 * k, 0xD9);
        Wr16(s.structures, b + 0x22 + 2 * k, 0xFFFF);
        Wr8(s.structures, b + 0x3A + k, 0);
    }
    Redraw(s, sim, st);
}

// a thing put into a building's first free slot, columns first. A silo takes crop stacks and seed bags,
// a barn or shed (0x42-0x45) takes anything; a seed bag goes into any building. The slot shows the thing's own
// picture. An item stored by the silo rule takes the slot's cell; a barrel takes it and the stored flag. A crop stack
// put into a barn keeps its old cell in its record (seen live: the potato stack moved into the machine shed)
bool StoreIn(GameState& s, Simulation& sim, int st, int type, int record, int picture)
{
    const size_t tb = static_cast<size_t>(st) * 70;
    const int cat = Rd16(s.structures, tb);
    const bool stack = type >= 0xA8 && type < 0xC0, seed = type >= 0xC0 && type < 0xD8;
    const bool siloRule = ((cat == 0x40 || cat == 0x41) && stack) || seed;
    if (!siloRule && (cat < 0x42 || cat > 0x45))
        return false;
    const size_t rec = static_cast<size_t>(cat - 52) * 18;
    const int w = rec + 9 < s.catalog.size() ? std::max(1, static_cast<int>(static_cast<int8_t>(s.catalog[rec + 8]))) : 1;
    const int h = rec + 9 < s.catalog.size() ? std::max(1, static_cast<int>(static_cast<int8_t>(s.catalog[rec + 9]))) : 1;
    for (int c = 0; c < w; ++c)
        for (int r = 0; r < h; ++r)
        {
            const size_t k = static_cast<size_t>(c * 3 + r);
            if (k >= 12 || Rd16(s.structures, tb + 0x22 + 2 * k) != 0xFFFF)
                continue;
            Wr16(s.structures, tb + 0x22 + 2 * k, type);
            Wr16(s.structures, tb + 0x0A + 2 * k, picture);
            Wr8(s.structures, tb + 0x3A + k, record);
            const int sx = Rd8(s.structures, tb + 4) + c, sy = Rd8(s.structures, tb + 5) + r;
            if (type >= 0xDC && type <= 0xDF)
            {
                const size_t ob = static_cast<size_t>(record) * 20;
                Wr8(s.objects20, ob + 5, sx);
                Wr8(s.objects20, ob + 6, sy);
                Wr8(s.objects20, ob + 2, Rd8(s.objects20, ob + 2) | 0x04);
            }
            else if (siloRule && record)
            {
                Wr8(s.items, static_cast<size_t>(record) * 12 + 8, sx);
                Wr8(s.items, static_cast<size_t>(record) * 12 + 9, sy);
            }
            Redraw(s, sim, st);
            return true;
        }
    return false;
}

// bare ground for the drops (family 0: the original's test takes base .. base + count, one past the family)
bool BareGround(int t) { return t >= 15 && t <= 31; }

} // namespace

// The Move tool's drop, only on your own land. Seen live on ACT1 and MRKC, paused: a trough, a feed pile,
// a crop stack and a barrel each moved at once (a stack and a barrel also out of and into open buildings); a machine
// drives there by itself.
EditResult DropObject(GameState& s, Simulation& sim, int kind, int index, int fromX, int fromY, int x, int y)
{
    if (x < 0 || y < 0 || x >= s.world.size || y >= s.world.size || s.PlotOwner(x / 8, y / 8) != 1)
        return { false, "You can only move things onto your own land.", "uhoh.voc" };
    const EditResult refused{ false, "There is no room there.", "uhoh.voc" };
    const EditResult done{ true, "", "plopitem.voc" };
    MapCell& to = s.world.At(x, y);
    MapCell& from = s.world.At(fromX, fromY);
    bool openTo = false, openFrom = false;
    const int target = BuildingCovering(s, x, y, openTo);
    const int source = BuildingCovering(s, fromX, fromY, openFrom);
    const int ground = GroundTile(s, sim.SoilWetness(), fromX, fromY);
    switch (kind)
    {
    case kMoveMachine:
        if (Machine(s.machines, index).Flags() & 0x03)
            return { false, "That machine is busy.", "uhoh.voc" };
        sim.Fleet().PlayerMove(index, x, y);
        return done;
    case kMoveAnimal:
        if (target || !sim.Herds().MoveTo(index, x, y))
            return refused;
        return done;
    case kMoveFeed:
    {
        if (target || !BareGround(to.BaseTile()) || to.HasObject())
            return refused;
        // the old cell: no object; its overlay word gets the terrain's own ground picture, as in the original
        from.base &= static_cast<uint16_t>(~0x800);
        from.overlay = static_cast<uint16_t>((from.overlay & 0xF800) | (15 + (from.terrain & 15)));
        const size_t b = static_cast<size_t>(index) * 12;
        Wr8(s.items, b + 8, x);
        Wr8(s.items, b + 9, y);
        const int amount = Rd16(s.items, b + 2);
        if (amount == 0)
        {
            // an empty pile is dropped as plain ground and its record let go
            const uint16_t g = static_cast<uint16_t>(15 + (to.terrain & 15));
            to.base = static_cast<uint16_t>((to.base & 0xF000) | g);
            to.overlay = static_cast<uint16_t>((to.overlay & 0xF800) | g);
            sim.RemoveItem(index);
            return done;
        }
        const int level = amount < 0x66 ? 4 : amount < 0xCE ? 3 : amount < 0x134 ? 2 : amount <= 0x199 ? 1 : 0;
        to.overlay = static_cast<uint16_t>((to.overlay & 0xF800) | (1020 + level));
        to.base |= 0x800;
        return done;
    }
    case kMoveTrough:
    {
        if (!BareGround(to.BaseTile()))
            return refused;
        const bool water = index && Rd16(s.items, static_cast<size_t>(index) * 12 + 2) != 0;
        to.base = static_cast<uint16_t>((to.base & 0xF800) | (water ? 176 : 175));
        from.base = static_cast<uint16_t>((from.base & 0xF800) | ground);
        if (index)
        {
            Wr8(s.items, static_cast<size_t>(index) * 12 + 8, x);
            Wr8(s.items, static_cast<size_t>(index) * 12 + 9, y);
        }
        return done;
    }
    case kMoveItem:
    case kMoveBarrel:
    {
        const bool barrel = kind == kMoveBarrel;
        const int type = barrel ? Rd16(s.objects20, static_cast<size_t>(index) * 20) : Rd16(s.items, static_cast<size_t>(index) * 12);
        const int picture = barrel ? 915 - (type - 0xDC)
                          : type >= 0xC0 ? 0x340 + (type - 0xC0) : 0x330 + (type - 0xA8);
        if (target)
        {
            // the old place is let go first, so the same building can take it into another slot
            const size_t sb = static_cast<size_t>(source) * 70;
            const int k = source ? (fromX - Rd8(s.structures, sb + 4)) * 3 + (fromY - Rd8(s.structures, sb + 5)) : -1;
            const bool slot = k >= 0 && k < 12;
            const uint16_t keep[3] = { slot ? static_cast<uint16_t>(Rd16(s.structures, sb + 0x0A + 2 * k)) : uint16_t(0),
                                       slot ? static_cast<uint16_t>(Rd16(s.structures, sb + 0x22 + 2 * k)) : uint16_t(0),
                                       slot ? static_cast<uint16_t>(Rd8(s.structures, sb + 0x3A + k)) : uint16_t(0) };
            if (slot)
            {
                Wr16(s.structures, sb + 0x0A + 2 * k, 0xD9);
                Wr16(s.structures, sb + 0x22 + 2 * k, 0xFFFF);
                Wr8(s.structures, sb + 0x3A + k, 0);
            }
            if (!StoreIn(s, sim, target, type, index, picture))
            {
                if (slot)
                {
                    Wr16(s.structures, sb + 0x0A + 2 * k, keep[0]);
                    Wr16(s.structures, sb + 0x22 + 2 * k, keep[1]);
                    Wr8(s.structures, sb + 0x3A + k, static_cast<uint8_t>(keep[2]));
                }
                return refused;
            }
            if (source)
                Redraw(s, sim, source);
            else if (barrel)
            {
                from.base = static_cast<uint16_t>((from.base & 0xF000) | ground);
                from.overlay = static_cast<uint16_t>((from.overlay & 0xF800) | ground);
            }
            else
                from.base = static_cast<uint16_t>((from.base & 0xF800) | ground);
            return done;
        }
        if (!BareGround(to.BaseTile()) || to.HasObject())
            return refused;
        if (source)
            FreeSlotAt(s, sim, source, fromX, fromY);
        else if (barrel)
        {
            from.base = static_cast<uint16_t>((from.base & 0xF000) | ground);
            from.overlay = static_cast<uint16_t>((from.overlay & 0xF800) | ground);
        }
        else
            from.base = static_cast<uint16_t>((from.base & 0xF800) | ground);
        if (barrel)
        {
            Wr8(s.objects20, static_cast<size_t>(index) * 20 + 5, x);
            Wr8(s.objects20, static_cast<size_t>(index) * 20 + 6, y);
            to.overlay = static_cast<uint16_t>((to.overlay & 0xF800) | picture);
            to.base |= 0x800;
        }
        else
        {
            Wr8(s.items, static_cast<size_t>(index) * 12 + 8, x);
            Wr8(s.items, static_cast<size_t>(index) * 12 + 9, y);
            to.base = static_cast<uint16_t>((to.base & 0xF800) | picture);
        }
        return done;
    }
    default:
        return refused;
    }
}

// The original's card content routine, in its order: wreck and water-tower-inside tiles, town area, the
// homestead, then a building, machine, animal or item record at the cell, a town plot, and the terrain tile's family.
bool ExamineCardAt(const GameState& s, int x, int y, ExamineCard& card)
{
    card = ExamineCard{};
    if (x < 0 || y < 0 || x >= s.world.size || y >= s.world.size)
        return false;
    const MapCell& c = s.world.At(x, y);
    const int base = c.BaseTile();
    const int tile = c.HasObject() ? (c.overlay & 0x7FF) : base;
    if (base == 0x49 || base == 0x4A || base == 0x71 || base == 0x72)
    {
        card.picture = 0x29;
        card.text = "A wrecked aircraft, damaged beyond any repair.";
        return true;
    }
    if (base >= 0x53 && base <= 0x56)
    {
        card.picture = 0x32;
        card.text = "The inside of a water tower: the blue part shows how much water it holds.";
        return true;
    }
    if (c.overlay & 0x4000)
    {
        card.picture = 0x4A;
        card.text = "Part of the town, where all sorts of things go on.";
        return true;
    }
    auto scalar = [&](const char* k) {
        const auto it = s.scalars.find(k);
        return it == s.scalars.end() ? 0 : static_cast<int>(it->second);
    };
    const int ox = scalar("view_x"), oy = scalar("view_y");
    if (x >= ox + 1 && x <= ox + 3 && y >= oy + 1 && y <= oy + 3)
    {
        card.picture = 0x43;
        card.textId = 12;
        return true;
    }
    // buildings (any cell of the footprint); a record with bit 0 of its flags is passed over
    for (int i = 1; i < 127; ++i)
    {
        const size_t b = static_cast<size_t>(i) * 70;
        if (!(Rd8(s.structures, b + 2) & 0x20))
            continue;
        const int type = Rd16(s.structures, b), rec = type - 52;
        if (rec < 0 || rec >= 103)
            continue;
        const int bx = Rd8(s.structures, b + 4), by = Rd8(s.structures, b + 5);
        if (x < bx || y < by || x >= bx + static_cast<int8_t>(CatalogByte(s, rec, 8)) || y >= by + static_cast<int8_t>(CatalogByte(s, rec, 9)))
            continue;
        if (Rd8(s.structures, b + 2) & 1)
            break;
        card.kind = ExamineCard::kStructure;
        card.picture = static_cast<int8_t>(CatalogByte(s, rec, 0x0B));
        card.textId = CatalogByte(s, rec, 0);
        card.damage = Rd8(s.structures, b + 6);
        card.slots = type != 0x46 && type != 0x47; // not the pump and the windmill
        if (card.slots)
        {
            card.cols = static_cast<int8_t>(CatalogByte(s, rec, 8));
            card.rows = static_cast<int8_t>(CatalogByte(s, rec, 9));
            for (int cc = 0; cc < card.cols; ++cc)
                for (int r = 0; r < card.rows; ++r)
                {
                    int t = Rd16(s.structures, b + 0x0A + 2 * (cc * 3 + r));
                    if (type == 0x48)
                        t = (Rd16(s.structures, b + 0x0A) >> 6) + 0x53; // a water tower: its level picture
                    card.tiles.push_back(t ? t : 0xD9);
                }
        }
        return true;
    }
    // machines (the footprint, turned with the heading)
    for (int i = 1; i < 127; ++i)
    {
        const size_t b = static_cast<size_t>(i) * 56;
        if (!(Rd8(s.machines, b + 2) & 0x20))
            continue;
        const int type = Rd16(s.machines, b);
        if (type < 0 || type >= 103)
            continue;
        int w = static_cast<int8_t>(CatalogByte(s, type, 8)), h = static_cast<int8_t>(CatalogByte(s, type, 9));
        if (Rd8(s.machines, b + 0x0F) == 2 || Rd8(s.machines, b + 0x0F) == 6)
            std::swap(w, h);
        const int mx = Rd8(s.machines, b + 5), my = Rd8(s.machines, b + 6);
        if (x < mx || x > mx + w - 1 || y < my || y > my + h - 1)
            continue;
        const int shift = Rd8(s.machines, b + 0x11) >> 6;
        card.kind = ExamineCard::kMachine;
        card.picture = static_cast<int8_t>(CatalogByte(s, type, 0x0B));
        card.textId = CatalogByte(s, type, 0);
        card.cols = static_cast<int8_t>(CatalogByte(s, type, 8));
        card.rows = static_cast<int8_t>(CatalogByte(s, type, 9));
        card.firstTile = FamilyStart(card.textId);
        card.quality = 3 - shift;
        card.value = static_cast<uint16_t>(s.CatalogPrice(type)) >> shift;
        return true;
    }
    // animals
    for (int i = 1; i < 256; ++i)
    {
        const size_t b = static_cast<size_t>(i) * 32;
        const int fl = Rd8(s.animals, b + 2);
        if (!(fl & 0x20) || Rd8(s.animals, b) != x || Rd8(s.animals, b + 1) != y)
            continue;
        const int rec = Rd16(s.animals, b + 6) - 115;
        if (rec < 0 || rec >= 103)
            break;
        card.kind = ExamineCard::kAnimal;
        card.picture = static_cast<int8_t>(CatalogByte(s, rec, 0x0B)) + (fl & 1); // a pregnant animal's picture follows
        card.textId = CatalogByte(s, rec, 0);
        card.cols = card.rows = 1;
        card.firstTile = FamilyStart(card.textId);
        // the SELL value: (price + factor x weight) >> (3 - condition / 64); "$%d" shows its low 16 bits
        const int sp = (Rd16(s.animals, b + 6) - 0xA0) & 3;
        static const int factor[4] = { 3, 5, 3, 2 };
        const int64_t raw = s.CatalogPrice(rec) + static_cast<int64_t>(factor[sp]) * Rd16(s.animals, b + 0x0A);
        card.quality = Rd8(s.animals, b + 8) >> 6;
        card.value = static_cast<int16_t>(static_cast<uint16_t>(raw >> (3 - card.quality)));
        return true;
    }
    // items: barrels and the other 20-byte objects
    for (int i = 1; i < 127; ++i)
    {
        const size_t b = static_cast<size_t>(i) * 20;
        if (!(Rd8(s.objects20, b + 2) & 0x20) || Rd8(s.objects20, b + 5) != x || Rd8(s.objects20, b + 6) != y)
            continue;
        const int rec = Rd16(s.objects20, b) - 138;
        if (rec < 0 || rec >= 103)
            break;
        card.kind = ExamineCard::kItem;
        card.picture = static_cast<int8_t>(CatalogByte(s, rec, 0x0B));
        card.textId = CatalogByte(s, rec, 0);
        card.cols = card.rows = 1;
        card.firstTile = FamilyStart(card.textId);
        card.quality = 3 - (Rd8(s.objects20, b + 9) >> 6);
        card.value = static_cast<int16_t>(s.CatalogPrice(rec));
        return true;
    }
    // a town plot's building tiles: rows 8-10 x columns 0-29 and rows 6-7 x columns 0-11 of the 40-wide
    // tile sheet, residential tiles excepted
    const bool residential = (base >= 0xFE && base <= 0x117) || (base >= 0x126 && base <= 0x13F);
    const int row = base / 40, col = base % 40;
    if (!residential && ((row >= 8 && row <= 10 && col < 30) || (row >= 6 && row <= 7 && col < 12)))
    {
        card.kind = ExamineCard::kLine;
        card.picture = 0x4A;
        card.text = "A town building";
        return true;
    }
    // the terrain: the tile family holding the tile
    card.picture = 0x4A;
    static const uint8_t kSize[160] = { 16, 30, 11, 8, 11, 11, 1, 4, 6, 9, 9, 12, 12, 9, 9, 9, 9, 9, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
        4, 4, 4, 16, 16, 16, 16, 2, 2, 4, 16, 4, 24, 4, 24, 8, 8, 16, 16, 8, 4, 8, 24, 8, 24, 4, 32, 12, 12, 4, 2, 16, 2, 2, 2, 2, 2, 2, 11, 25,
        4, 16, 3, 1, 1, 1, 1, 11, 2, 4, 5, 4, 11, 11, 127, 16, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 4, 9, 1, 1, 8, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 16, 1, 1, 2, 4, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 4, 17, 35 };
    int family = -1;
    for (int f = 0; f < 160 && family < 0; ++f)
        if (FamilyStart(f) <= tile && tile < FamilyStart(f) + kSize[f])
            family = f;
    if (tile == 0xD9)
        card.kind = ExamineCard::kLine, card.text = "A free storage slot";
    else if (tile >= FamilyStart(125) && tile <= FamilyStart(125) + 8)
        card.text = "One of the game's programmers walking home after a long night of work on the farm.";
    else if (family < 0)
        card.kind = ExamineCard::kLine, card.text = "No description";
    else if (((family >= 87 && family <= 102) || (family >= 109 && family <= 124)) && s.crops[(family - (family >= 109 ? 109 : 87)) & 15].loaded)
    {
        // a crop slot's icon (a stored stack or seed bag): the text of the crop loaded in that slot. The records of
        // itemtext.dat hold the default 16 crops; the original shows a crop loaded into another slot under its own
        // name (seen live: Oranges loaded over Peanuts is "Oranges" in BUY). User finding F30.
        const CropDef& crop = s.crops[(family - (family >= 109 ? 109 : 87)) & 15];
        card.text = crop.name + "~~";
        for (const std::string& line : crop.help)
            card.text += line + " ";
    }
    else
        card.textId = family;
    return true;
}

int TownBuildingAt(int id)
{
    static const int kBlock[9] = { 320, 323, 326, 329, 332, 335, 338, 352, 355 };
    for (int k = 0; k < 9; ++k)
    {
        const int t = kBlock[k];
        if ((id >= t && id <= t + 2) || (id >= t + 40 && id <= t + 42) || (id >= t + 80 && id <= t + 82))
            return k + 1;
    }
    return 0;
}

} // namespace osf

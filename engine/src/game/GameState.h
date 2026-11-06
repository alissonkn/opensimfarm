// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include "assets/SaveGame.h"
#include "game/Crops.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace osf {

// Little-endian access to raw record bytes.
inline int Rd8(const std::vector<uint8_t>& v, size_t o) { return o < v.size() ? v[o] : 0; }
inline int Rd16(const std::vector<uint8_t>& v, size_t o) { return Rd8(v, o) | (Rd8(v, o + 1) << 8); }
inline void Wr8(std::vector<uint8_t>& v, size_t o, int x) { if (o < v.size()) v[o] = static_cast<uint8_t>(x); }
inline void Wr16(std::vector<uint8_t>& v, size_t o, int x) { Wr8(v, o, x & 0xFF); Wr8(v, o + 1, (x >> 8) & 0xFF); }

// A field record. Offsets are those of the field layout used throughout the engine;
// in the save file every field record starts two bytes earlier, which the accessors handle.
class Field
{
public:
    Field(std::vector<uint8_t>& table, int index) : _t(table), _base(static_cast<size_t>(index) * 68 + 2), _index(index) {}
    int Index() const { return _index; }
    int Get8(int o) const { return Rd8(_t, _base + o); }
    int Get16(int o) const { return Rd16(_t, _base + o); }
    void Set8(int o, int v) { Wr8(_t, _base + o, v); }
    void Set16(int o, int v) { Wr16(_t, _base + o, v); }

    int State() const { return Get8(0x00); }      void SetState(int v) { Set8(0x00, v); }
    int Sub() const { return Get8(0x01); }        void SetSub(int v) { Set8(0x01, v); }
    bool InUse() const { return (Get8(0x05) & 1) != 0; }
    int X() const { return Get8(0x08); }
    int Y() const { return Get8(0x09); }
    int W() const { return Get8(0x0A); }
    int H() const { return Get8(0x0B); }
    int Crop() const { return Get8(0x0C); }       void SetCrop(int v) { Set8(0x0C, v); }
    int Stress() const { return Get16(0x10); }    void SetStress(int v) { Set16(0x10, v); }
    int Pests() const { return Get8(0x12); }
    int Weeds() const { return Get8(0x13); }
    int Disease() const { return Get8(0x14); }
    int Moisture() const { return Get16(0x16); }  void SetMoisture(int v) { Set16(0x16, v); }
    int Fertility() const { return Get8(0x1A); }
    int NutrientA() const { return Get8(0x1B); }
    int Toxicity() const { return Get8(0x1E); }
    int Phase() const { return Get8(0x1F); }      void SetPhase(int v) { Set8(0x1F, v); }
    int Age() const { return Get8(0x20); }        void SetAge(int v) { Set8(0x20, v); }
    int HeatDeficit() const { return Get16(0x24); } void SetHeatDeficit(int v) { Set16(0x24, v); }
    int Chill() const { return Get16(0x26); }     void SetChill(int v) { Set16(0x26, v); }
    int Contract() const { return Get16(0x2E); }  void SetContract(int v) { Set16(0x2E, v); }
    int FullDitches() const { return Get8(0x31); }
    int EmptyDitches() const { return Get8(0x32); }
    int Status() const { return Get16(0x34); }     // problem bits stored by the status refresh

private:
    std::vector<uint8_t>& _t;
    size_t _base;
    int _index;
};

// Calendar.
struct Calendar
{
    int day = 0, week = 0, month = 0, year = 1993;
    int AbsWeek() const { return month * 4 + week; }
    int Season() const;            // 0 winter, 1 spring, 2 summer, 3 autumn
};

// The complete game state as loaded from an original save or prebuilt farm. Tables are kept as raw records so a save can be written back unchanged.
struct GameState
{
    // raw sections in file order
    std::vector<uint8_t> version, animals, objects20, machines, fields, structures, items, traffic, timeline, weather,
        catalog, irrigationFlag, irrigationLists, cropNames, zones, eventGrounds, tail;
    int animalCount = 0, object20Count = 0, machineCount = 0, fieldCount = 0, structureCount = 0, itemCount = 0, carCount = 0;
    World world;

    // decoded scalars of the tail
    std::map<std::string, int64_t> scalars;
    Calendar cal;
    int64_t cash = 0;
    int speedDelay = 256;

    // crops in the 16 market slots
    std::array<CropDef, 16> crops;

    Field FieldAt(int i) { return Field(fields, i); }

    // weather of the active 5-year table: 240 weeks x (wind, rain class, temperature)
    int WeatherWind(int idx) const { return Rd8(weather, static_cast<size_t>(idx % 240) * 3 + 0); }
    int WeatherRain(int idx) const { return Rd8(weather, static_cast<size_t>(idx % 240) * 3 + 1); }
    int WeatherTemp(int idx) const { return Rd8(weather, static_cast<size_t>(idx % 240) * 3 + 2); }

    // item catalogue: 103 records of 18 bytes (own count +4, price +6); crop market records are 21..44
    int CatalogOwn(int rec) const { return Rd16(catalog, static_cast<size_t>(rec) * 18 + 4); }
    int CatalogPrice(int rec) const { return Rd16(catalog, static_cast<size_t>(rec) * 18 + 6); }
    void SetCatalogOwn(int rec, int v) { Wr16(catalog, static_cast<size_t>(rec) * 18 + 4, v); }
    void SetCatalogPrice(int rec, int v) { Wr16(catalog, static_cast<size_t>(rec) * 18 + 6, v); }
    static constexpr int kCropRecord0 = 21;
    int MarketPrice(int crop) const { return CatalogPrice(kCropRecord0 + crop); }
    void SetMarketPrice(int crop, int v) { SetCatalogPrice(kCropRecord0 + crop, v); }
    int Contracts(int crop) const { return CatalogOwn(kCropRecord0 + crop); }

    // plot grid: Plots() x Plots() plots (144 on the original's map) x 7 bytes (type|level<<4, ownership, zone maps...)
    int Plots() const { return world.Plots(); }
    size_t PlotByte(int col, int row, int k) const { return static_cast<size_t>(col * Plots() + row) * 7 + k; }
    int PlotType(int col, int row) const { return Rd8(zones, PlotByte(col, row, 0)); }
    int PlotOwner(int col, int row) const { return Rd8(zones, PlotByte(col, row, 1)); }
    void SetPlotType(int col, int row, int v) { Wr8(zones, PlotByte(col, row, 0), v); }
    void SetPlotOwner(int col, int row, int owner) { Wr8(zones, PlotByte(col, row, 1), owner); }
    // a new, empty map of n x n cells with its plot table (the larger worlds of world creation)
    void ResizeMap(int n) { world.Resize(n); zones.assign(static_cast<size_t>(world.Plots()) * world.Plots() * 7, 0); }

    // schedule timeline: 240 rows (weeks) x 98 fields of u16 codes
    int ScheduleCode(int row, int field) const { return Rd16(timeline, (static_cast<size_t>(row) * 98 + (field - 1)) * 2); }
    void SetScheduleCode(int row, int field, int v) { Wr16(timeline, (static_cast<size_t>(row) * 98 + (field - 1)) * 2, v); }

    bool Load(const std::string& gameDir, const std::string& path, std::string& error, bool resetPrices = true);
    bool Save(const std::string& path, std::string& error) const;
    void StoreScalarsToTail();
};

} // namespace osf

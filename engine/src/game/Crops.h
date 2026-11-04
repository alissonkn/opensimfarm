// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include <algorithm>
#include <array>
#include <vector>
#include <cstdint>
#include <string>

namespace osf {

// One crop definition read from an original .CRP file.
// The 70-byte parameter record is kept as is; the accessors name the parameters that have gameplay meaning.
struct CropDef
{
    std::string name;     // display name from the file
    std::string fileName; // e.g. "corn.crp"
    std::array<uint8_t, 70> p{};
    // the crop's own pictures in tile-sheet format (128 bytes per tile): 8 growth stages (file bytes 0..1023), the
    // 32x32 sign as 4 tiles (4608..5119), a small icon with a tan frame (5120..5247) and one with a green frame
    // (5248..5375); the game copies them over the slot's tiles 688 + 8s, 539 + 4s, 816 + s and 832 + s
    std::vector<uint8_t> art;
    std::vector<std::string> help; // the item help card's text lines (title line left out), shown in the BUY window
    std::string info;              // the Crop Information card text ('~' = new line), shown by the Farm Expert
    int regrowAge = 0;             // name-field byte 18: a perennial's age after each harvest (apples, oranges 1)
    bool loaded = false;

    int B(int off) const { return p[off]; }
    int P6() const { return p[6]; }               // first stage picture of the timeline
    int W(int off) const { return p[off] | (p[off + 1] << 8); }

    int ChillNeed() const { return W(8); }       // P8
    int HeatNeed() const { return W(10); }       // P10 weekly heat requirement
    int Yield() const { return W(12); }          // P12
    int BasePrice() const { return W(14); }      // P14
    int SeedBase() const { return W(18); }       // P18: the seed price is max(5, P18 / 4) when the crop is loaded
    int SeedPrice() const { return std::max(5, SeedBase() >> 2); }
    int WaterMin() const { return W(20); }       // P20
    int WaterMax() const { return W(22); }       // P22
    int DrainA() const { return B(30); }         // P30..P32 weekly drain of counters A, B, C
    int DrainB() const { return B(31); }
    int DrainC() const { return B(32); }
    int Weeks() const { return B(36); }          // P36 weeks to maturity
    int ResistPests() const { return B(38); }    // P38..P40
    int ResistWeeds() const { return B(39); }
    int ResistDisease() const { return B(40); }
    bool Perennial() const { return (B(42) & 1) != 0; }
    bool HandPicked() const { return B(37) == 1; }    // P37: harvested by a picking crew and a truck, no harvester
    bool Orchard() const { return (B(42) & 4) != 0; }
    int Stages() const { return B(46); }         // P46
    int BaseTemp() const { return B(47); }       // P47
    int WetPenalty() const { return B(50); }     // P50
    int DryPenalty() const { return B(51); }     // P51
    int FrostShift() const { return B(52); }     // P52
    int HarvestWeek() const { return B(56); }    // P56 (perennials)
};

// Reads CROPS/<fileName> from the game folder. Returns false if missing or malformed.
bool LoadCropFile(const std::string& gameDir, const std::string& fileName, CropDef& out);

} // namespace osf

// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "assets/SaveGame.h"

namespace osf {

namespace {
constexpr uint8_t kSignature[4] = { 'O', 'S', 'F', 'M' };
constexpr uint8_t kFormatVersion = 1;
bool UsableMapSize(int n) { return n >= kOriginalMapSize && n <= 192 && n % 16 == 0; }
}

size_t SaveBodySize(int n)
{
    const size_t plots = static_cast<size_t>(n / kPlotCells) * (n / kPlotCells);
    const size_t originalPlots = static_cast<size_t>(kOriginalMapSize / kPlotCells) * (kOriginalMapSize / kPlotCells);
    return kOriginalSaveSize - static_cast<size_t>(kOriginalMapSize) * kOriginalMapSize * 5 - originalPlots * 7 +
           static_cast<size_t>(n) * n * 5 + plots * 7;
}

int SaveFileMapSize(const std::vector<uint8_t>& d, size_t& body)
{
    if (d.size() >= kSaveSignatureSize)
    {
        const uint8_t* t = &d[d.size() - kSaveSignatureSize];
        if (t[0] == kSignature[0] && t[1] == kSignature[1] && t[2] == kSignature[2] && t[3] == kSignature[3] && t[4] >= 1)
        {
            const int n = t[5] | (t[6] << 8);
            body = d.size() - kSaveSignatureSize;
            return UsableMapSize(n) && SaveBodySize(n) == body ? n : 0;
        }
    }
    body = d.size();
    return d.size() == kOriginalSaveSize ? kOriginalMapSize : 0; // an original save: no signature
}

void AppendSaveSignature(std::vector<uint8_t>& d, int n)
{
    d.insert(d.end(), kSignature, kSignature + 4);
    d.push_back(kFormatVersion);
    d.push_back(static_cast<uint8_t>(n & 0xFF));
    d.push_back(static_cast<uint8_t>((n >> 8) & 0xFF));
    d.push_back(0);
}

} // namespace osf

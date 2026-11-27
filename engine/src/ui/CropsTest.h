// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once

namespace osf {

// Headless check of a pack's crops (--cropstest [pack folder]): the "crops" block (parameters, art, help, errors), the
// registry that makes a pack's crops stand in for the CROPS folder, the truecolour art patched into a tile sheet, and
// with a pack folder the game on that pack's crops: a world is generated, a save is written and read back, the spare
// crops are listed, and the autoplayer farms for three years. Needs no game files. 0 when all is well.
int CropsSelfTest(const char* packFolder = nullptr);

} // namespace osf

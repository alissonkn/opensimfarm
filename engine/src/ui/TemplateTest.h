// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once

namespace osf {

// Headless check of the template farm (--templatetest [pack folder]): the blank farm and the save format, a pack's
// "template" block made into a save and read back, its errors, and which source a new world's template comes from
// (a pack's, else SCEN0.SSM). With a pack folder it also runs the game on that pack's template: a world is generated
// from it, every shop item is bought, and half a year is simulated. Needs no game files. 0 when all is well.
int TemplateSelfTest(const char* packFolder = nullptr);

} // namespace osf

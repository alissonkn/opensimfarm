// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once

namespace osf {

// Headless check of the data tables (--tablestest): the shared decoders against the readers they replaced, the JSON
// tables of a pack made into blobs and read back by the engine's own decoders and consumers (world generation, the
// town plots), and the errors a bad table gives. Needs no game files. With `packFolder` it also checks that pack's
// own tables (the shipped placeholder pack, say). 0 when all is well.
int TablesSelfTest(const char* packFolder = nullptr);

} // namespace osf

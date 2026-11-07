// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once

namespace osf {

class Megadata;

// --langtest: checks every built-in language (characters, {0} {1} ..., texts that must fit a picture or a box);
// returns the process exit code
int LangSelfTest(const Megadata& mega);

} // namespace osf

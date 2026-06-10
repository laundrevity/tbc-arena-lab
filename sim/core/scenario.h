/*
 * This file is part of tbc-arena-lab.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#pragma once

#include <string>

#include "sim/core/unit_state.h"

namespace arena {

struct Scenario {
    std::string name;
    std::string ruleset;
    int64_t duration_ms = 0;
    UnitSpec attacker;
    UnitSpec defender;
    std::string source_path;  // as loaded; recorded in trace headers
};

// Hand-rolled parser for the tiny YAML subset used by scenarios/: top-level
// `key: value` pairs plus the `attacker:` / `defender:` sections with
// two-space-indented `key: value` pairs, `#` comments. Every stat must be
// pinned explicitly — any missing key is a load error (CLAUDE.md: no
// defaults hidden in code). The attacker must attack; the defender may
// (M1 mutual scenarios) or may not (M0 fixtures). Levels other than 70 are
// rejected: the rage conversion constant is only defined for level 70
// (spec M-006). Returns false and fills `err` on failure; no exceptions.
bool load_scenario(const std::string& path, Scenario& out, std::string& err);

} // namespace arena

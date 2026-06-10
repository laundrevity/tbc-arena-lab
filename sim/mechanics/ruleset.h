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

#include <cstdint>
#include <string>

namespace arena {

// Pinned ruleset identifier (Anniversary TBC 2.5.x; see divergence ledger
// D-003 for its relationship to the cmangos-tbc bootstrap oracle).
const char* ruleset_id();

// Canonical manifest of every mechanics constant, as a string; its FNV-1a
// hash goes in trace headers so replays fail loudly when formulas change.
std::string ruleset_manifest();
uint64_t ruleset_hash();

} // namespace arena

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

struct ReplayResult {
    bool ok = false;
    std::string error;            // first mismatch / failure, empty when ok
    uint64_t checkpoints_checked = 0;
    uint64_t swings_in_trace = 0;
};

// Re-runs the scenario named in the trace header from the header seed and
// verifies: the trace ruleset_hash matches this build's constants, the init
// hash, every checkpoint hash, the swing-line count, and the end line.
// `base_dir` resolves the header's scenario path (it is stored relative to
// the repo root); pass "" to use it as-is from the current directory.
ReplayResult verify_trace_file(const std::string& trace_path, const std::string& base_dir);

} // namespace arena

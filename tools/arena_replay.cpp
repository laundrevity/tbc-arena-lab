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

// arena_replay <trace.jsonl> [--base-dir DIR]
// Re-runs the scenario from the trace header seed and verifies every hash
// checkpoint. Exit 0 iff all checkpoints (and the end line) verify.
// The header's scenario path is relative to the repo root; --base-dir
// resolves it when running from elsewhere.

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <string>

#include "sim/mechanics/replay.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: arena_replay <trace.jsonl> [--base-dir DIR]\n");
        return 2;
    }
    std::string base_dir;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--base-dir") && i + 1 < argc) {
            base_dir = argv[++i];
        } else {
            fprintf(stderr, "usage: arena_replay <trace.jsonl> [--base-dir DIR]\n");
            return 2;
        }
    }

    const arena::ReplayResult res = arena::verify_trace_file(argv[1], base_dir);
    if (!res.ok) {
        fprintf(stderr, "arena_replay: FAIL: %s\n", res.error.c_str());
        return 1;
    }
    printf("arena_replay: OK — %" PRIu64 " checkpoints verified, %" PRIu64 " swings\n",
           res.checkpoints_checked, res.swings_in_trace);
    return 0;
}

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

// Integer-only trigonometry for facing math (spec M-008, ledger D-007).
// Bhaskara-I sine approximation, max relative error ~0.2%; only the SIGN of
// the frontal-arc dot product feeds combat outcomes. No floats anywhere.

namespace arena {

constexpr int64_t TRIG_SCALE = 1000000;  // sin/cos outputs are scaled by 1e6
constexpr int64_t PI_MRAD    = 3142;     // pi in milliradians, rounded (D-007)

constexpr int64_t sin_mrad(int64_t x) {
    const int64_t two_pi = 2 * PI_MRAD;
    x %= two_pi;
    if (x < 0) x += two_pi;
    int64_t sign = 1;
    if (x >= PI_MRAD) {
        x -= PI_MRAD;
        sign = -1;
    }
    const int64_t q   = x * (PI_MRAD - x);
    const int64_t num = 16 * q;
    const int64_t den = 5 * PI_MRAD * PI_MRAD - 4 * q;
    return sign * (num * TRIG_SCALE) / den;
}

constexpr int64_t cos_mrad(int64_t x) {
    return sin_mrad(x + PI_MRAD / 2);
}

// True iff (att_x, att_y) lies in the frontal arc (total width pi, i.e.
// within +/- pi/2 of facing) of a unit at (def_x, def_y) facing
// def_facing_mrad. Ties (attacker exactly on the shoulder line) count as
// front (spec M-008).
constexpr bool in_frontal_arc(int64_t def_x, int64_t def_y, int32_t def_facing_mrad,
                              int64_t att_x, int64_t att_y) {
    const int64_t fx = cos_mrad(def_facing_mrad);
    const int64_t fy = sin_mrad(def_facing_mrad);
    const int64_t dx = att_x - def_x;
    const int64_t dy = att_y - def_y;
    return fx * dx + fy * dy >= 0;
}

} // namespace arena

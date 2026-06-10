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

#include "sim/mechanics/abilities.h"

#include "sim/mechanics/observation.h"

namespace arena {

const char* action_name(Action a) {
    switch (a) {
        case Action::None: return "none";
        case Action::CastMortalStrike: return "cast_mortal_strike";
        case Action::QueueHeroicStrike: return "queue_heroic_strike";
        case Action::UnqueueHeroicStrike: return "unqueue_heroic_strike";
    }
    return "?";
}

const char* yellow_outcome_name(YellowOutcome o) {
    switch (o) {
        case YellowOutcome::Miss: return "miss";
        case YellowOutcome::Dodge: return "dodge";
        case YellowOutcome::Parry: return "parry";
        case YellowOutcome::Hit: return "hit";
    }
    return "?";
}

} // namespace arena

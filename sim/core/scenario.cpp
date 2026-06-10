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

#include "sim/core/scenario.h"

#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace arena {

namespace {

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r");
    return s.substr(b, e - b + 1);
}

struct KV {
    std::map<std::string, std::string> values;

    bool get_str(const std::string& key, std::string& out, std::string& err) {
        auto it = values.find(key);
        if (it == values.end()) {
            err = "missing key: " + key;
            return false;
        }
        out = it->second;
        return true;
    }

    bool get_i64(const std::string& key, int64_t& out, std::string& err) {
        std::string raw;
        if (!get_str(key, raw, err)) return false;
        std::istringstream in(raw);
        in >> out;
        if (in.fail() || !(in >> std::ws).eof()) {
            err = "key '" + key + "' is not an integer: " + raw;
            return false;
        }
        return true;
    }

    bool get_i32(const std::string& key, int32_t& out, std::string& err) {
        int64_t v = 0;
        if (!get_i64(key, v, err)) return false;
        if (v < INT32_MIN || v > INT32_MAX) {
            err = "key '" + key + "' out of int32 range";
            return false;
        }
        out = static_cast<int32_t>(v);
        return true;
    }

    bool get_bool(const std::string& key, bool& out, std::string& err) {
        std::string raw;
        if (!get_str(key, raw, err)) return false;
        if (raw == "true") {
            out = true;
        } else if (raw == "false") {
            out = false;
        } else {
            err = "key '" + key + "' is not true/false: " + raw;
            return false;
        }
        return true;
    }
};

bool parse_unit(KV& kv, const std::string& section, UnitSpec& u, std::string& err) {
    bool ok = kv.get_i32("entity_id", u.entity_id, err) &&
              kv.get_i32("level", u.level, err) &&
              kv.get_i32("weapon_skill", u.weapon_skill, err) &&
              kv.get_i32("defense_skill", u.defense_skill, err) &&
              kv.get_i32("attack_power", u.attack_power, err) &&
              kv.get_i32("crit_pm", u.crit_pm, err) &&
              kv.get_i32("hit_pm", u.hit_pm, err) &&
              kv.get_i32("weapon_min", u.weapon_min, err) &&
              kv.get_i32("weapon_max", u.weapon_max, err) &&
              kv.get_i32("weapon_speed_ms", u.weapon_speed_ms, err) &&
              kv.get_i32("weapon_norm_ms", u.weapon_norm_ms, err) &&
              kv.get_i32("armor", u.armor, err) &&
              kv.get_i32("dodge_pm", u.dodge_pm, err) &&
              kv.get_i32("parry_pm", u.parry_pm, err) &&
              kv.get_i32("block_pm", u.block_pm, err) &&
              kv.get_i32("block_value", u.block_value, err) &&
              kv.get_bool("has_shield", u.has_shield, err) &&
              kv.get_bool("attacks", u.attacks, err) &&
              kv.get_i32("max_hp", u.max_hp, err) &&
              kv.get_i32("initial_rage_deci", u.initial_rage_deci, err) &&
              kv.get_i64("pos_x_cm", u.pos_x_cm, err) &&
              kv.get_i64("pos_y_cm", u.pos_y_cm, err) &&
              kv.get_i32("facing_mrad", u.facing_mrad, err) &&
              kv.get_bool("use_mortal_strike", u.use_mortal_strike, err) &&
              kv.get_i32("heroic_strike_min_rage_deci", u.hs_min_rage_deci, err);
    if (!ok) {
        err = section + ": " + err;
        return false;
    }
    if (u.level != 70) {
        err = section + ": level must be 70 in M0 (rage constant pinned at 70, spec M-006)";
        return false;
    }
    if (u.weapon_min <= 0 || u.weapon_max < u.weapon_min || u.weapon_speed_ms <= 0 ||
        u.weapon_norm_ms <= 0 || u.max_hp <= 0) {
        err = section + ": weapon_min/weapon_max/weapon_speed_ms/weapon_norm_ms/max_hp out of "
                        "range";
        return false;
    }
    if (u.initial_rage_deci < 0 || u.initial_rage_deci > 1000) {
        err = section + ": initial_rage_deci must be in [0, 1000]";
        return false;
    }
    // 150 deci = Heroic Strike cost (spec M-015); keep in sync with
    // HS_RAGE_COST_DECI in sim/mechanics/abilities.h.
    if (u.hs_min_rage_deci != 0 && u.hs_min_rage_deci < 150) {
        err = section + ": heroic_strike_min_rage_deci must be 0 or >= 150 (M-015 cost)";
        return false;
    }
    return true;
}

} // namespace

bool load_scenario(const std::string& path, Scenario& out, std::string& err) {
    std::ifstream in(path);
    if (!in) {
        err = "cannot open scenario file: " + path;
        return false;
    }

    KV top;
    std::map<std::string, KV> sections;
    std::string current_section;

    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        if (trim(line).empty()) continue;

        const bool indented = line[0] == ' ' || line[0] == '\t';
        const std::string t = trim(line);
        const size_t colon = t.find(':');
        if (colon == std::string::npos) {
            err = path + ":" + std::to_string(lineno) + ": expected 'key: value'";
            return false;
        }
        const std::string key = trim(t.substr(0, colon));
        const std::string value = trim(t.substr(colon + 1));

        if (!indented && value.empty()) {
            current_section = key;
            sections[current_section];  // create
            continue;
        }
        if (value.empty()) {
            err = path + ":" + std::to_string(lineno) + ": empty value for '" + key + "'";
            return false;
        }
        if (indented) {
            if (current_section.empty()) {
                err = path + ":" + std::to_string(lineno) + ": indented key outside a section";
                return false;
            }
            sections[current_section].values[key] = value;
        } else {
            current_section.clear();
            top.values[key] = value;
        }
    }

    if (!top.get_str("name", out.name, err) ||
        !top.get_str("ruleset", out.ruleset, err) ||
        !top.get_i64("duration_ms", out.duration_ms, err)) {
        err = path + ": " + err;
        return false;
    }
    if (out.duration_ms <= 0) {
        err = path + ": duration_ms must be positive";
        return false;
    }
    if (sections.find("attacker") == sections.end() || sections.find("defender") == sections.end()) {
        err = path + ": scenario needs 'attacker:' and 'defender:' sections";
        return false;
    }
    if (!parse_unit(sections["attacker"], "attacker", out.attacker, err) ||
        !parse_unit(sections["defender"], "defender", out.defender, err)) {
        err = path + ": " + err;
        return false;
    }
    if (out.attacker.entity_id == out.defender.entity_id) {
        err = path + ": attacker and defender must have distinct entity_id";
        return false;
    }
    if (!out.attacker.attacks) {
        err = path + ": attacker must have attacks: true";
        return false;
    }
    out.source_path = path;
    return true;
}

} // namespace arena

/* -*- C++ -*-
 *
 *  This file is part of ART.
 *
 *  Copyright 2023 Alberto Griggio <alberto.griggio@gmail.com>
 *
 *  ART is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  ART is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with ART.  If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#include <string>
#include <glibmm/ustring.h>
#include <vector>
#include <map>
#include <array>
#include "cJSON.h"

namespace rtengine {

enum class CLUTParamType {
    PT_INT,
    PT_FLOAT,
    PT_BOOL,
    PT_CHOICE,
    PT_CURVE,
    PT_FLATCURVE,
    PT_FLATCURVE_PERIODIC
};

struct CLUTParamDescriptor {
    std::string name;
    CLUTParamType type;
    double value_min;
    double value_max;
    std::vector<double> value_default;
    std::vector<Glib::ustring> choices;
    Glib::ustring gui_name;
    Glib::ustring gui_group;
    double gui_step;
    Glib::ustring gui_tooltip;
    std::vector<std::array<float, 4>> gui_bottom_gradient;
    std::vector<std::array<float, 4>> gui_left_gradient;

    bool fill_from_json(cJSON *root);
};

typedef std::map<std::string, std::vector<double>> CLUTParamValueMap;

} // namespace rtengine

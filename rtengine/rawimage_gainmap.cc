/* -*- C++ -*-
 *
 *  This file is part of ART.
 *
 *  Copyright 2020 Alberto Griggio <alberto.griggio@gmail.com>
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

#include <exiv2/exiv2.hpp>
#include <vector>
#include "rawimage.h"
#include "rawimagesource.h"
#include "array2D.h"
#include "rescale.h"


namespace rtengine {

namespace {

struct GainMap {
    uint32_t top;
    uint32_t left;
    uint32_t bottom;
    uint32_t right;
    uint32_t plane;
    uint32_t planes;
    uint32_t row_pitch;
    uint32_t col_pitch;
    uint32_t map_points_v;
    uint32_t map_points_h;
    double map_spacing_v;
    double map_spacing_h;
    double map_origin_v;
    double map_origin_h;
    uint32_t map_planes;
    std::vector<float> map_gain;

    GainMap() = default;
};


struct OutOfBounds: public std::exception {
    const char *what() const throw() { return "out of bounds"; }
};


GainMap read_gain_map(const Exiv2::byte *data, size_t limit, size_t idx)
{
    GainMap ret;

    const auto get_ulong =
        [&]() -> uint32_t
        {
            if (idx + 4 > limit) {
                throw OutOfBounds();
            }
            uint32_t ret = Exiv2::getULong(data+idx, Exiv2::bigEndian);
            idx += 4;
            return ret;
        };

    const auto get_double =
        [&]() -> double
        {
            if (idx + 8 > limit) {
                throw OutOfBounds();
            }
            double ret = Exiv2::getDouble(data+idx, Exiv2::bigEndian);
            idx += 8;
            return ret;
        };

    const auto get_float =
        [&]() -> float
        {
            if (idx + 4 > limit) {
                throw OutOfBounds();
            }
            float ret = Exiv2::getFloat(data+idx, Exiv2::bigEndian);
            idx += 4;
            return ret;
        };
    
    ret.top = get_ulong();
    ret.left = get_ulong();
    ret.bottom = get_ulong();
    ret.right = get_ulong();
    ret.plane = get_ulong();
    ret.planes = get_ulong();
    ret.row_pitch = get_ulong();
    ret.col_pitch = get_ulong();
    ret.map_points_v = get_ulong();
    ret.map_points_h = get_ulong();
    ret.map_spacing_v = get_double();
    ret.map_spacing_h = get_double();
    ret.map_origin_v = get_double();
    ret.map_origin_h = get_double();
    ret.map_planes = get_ulong();
    
    size_t n = ret.map_points_v * ret.map_points_h * ret.map_planes;
    ret.map_gain.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        ret.map_gain.push_back(get_float());
    }
    return ret;
}


bool check_gain_map(const Exiv2::byte *data, size_t limit,
                    size_t &idx, size_t &size)
{
    if (idx + 16 > limit) {
        return false;
    }
    uint32_t opid = Exiv2::getULong(data+idx, Exiv2::bigEndian);
    idx += 4;
    idx += 4; // version
    idx += 4; // flags
    size = Exiv2::getULong(data+idx, Exiv2::bigEndian);
    idx += 4;
    return opid == 9;
}


std::vector<GainMap> extract_gain_maps(const std::vector<Exiv2::byte> &buf)
{
    std::vector<GainMap> ret;
    if (buf.size() < 4) {
        return ret;
    }
    const Exiv2::byte *data = &buf[0];
    try {
        uint32_t num_entries = Exiv2::getULong(data, Exiv2::bigEndian);
        size_t idx = 4;
        for (size_t i = 0; i < num_entries; ++i) {
            size_t size = 0;
            if (check_gain_map(data, buf.size(), idx, size)) {
                ret.push_back(read_gain_map(data, buf.size(), idx));
            }
            idx += size;
            if (idx > buf.size()) {
                ret.clear();
                break;
            }
        }
    } catch (std::exception &exc) {
        ret.clear();
    }
    return ret;
}

} // namespace


void RawImage::apply_gain_map()
{
    if (!(isBayer() && DNGVERSION()/*load_raw == &RawImage::lossless_dng_load_raw*/ && RT_OpcodeList2_len > 0)) {
        return;
    }
    
    std::vector<Exiv2::byte> buf(RT_OpcodeList2_len);
    fseek(ifp, RT_OpcodeList2_start, SEEK_SET);
    if (fread(&buf[0], 1, RT_OpcodeList2_len, ifp) != RT_OpcodeList2_len) {
        return;
    }

    auto maps = extract_gain_maps(buf);
    if (maps.size() != 4) {
        return;
    }
    for (auto &m : maps) {
        if (m.bottom + 1 < raw_height || m.right + 1 < raw_width ||
            m.plane != 0 || m.planes != 1 || m.map_planes != 1 ||
            m.row_pitch != 2 || m.col_pitch != 2 ||
            m.map_origin_v != 0 || m.map_origin_h != 0) {
            return; // not something we can handle yet
        }
    }

    float black[4];
    get_colorsCoeff(nullptr, nullptr, black, false);

    // now we can apply each gain map to raw_data
    array2D<float> mvals;
    for (auto &m : maps) {
        mvals(m.map_points_h, m.map_points_v, &(m.map_gain[0]), 0);

        const float col_scale = float(m.map_points_h) / float(raw_width / m.col_pitch);
        const float row_scale = float(m.map_points_v) / float(raw_height / m.row_pitch);

#ifdef _OPENMP
#       pragma omp parallel for
#endif
        for (unsigned y = m.top; y < m.bottom; y += m.row_pitch) {
            float ys = y / m.row_pitch * row_scale;
            for (unsigned x = m.left; x < m.right; x += m.col_pitch) {
                float xs = x / m.col_pitch * col_scale;
                float f = getBilinearValue(mvals, xs, ys);
                int i = y * raw_width + x;
                float b = black[FC(y, x)];
                raw_image[i] = CLIP((raw_image[i] - b) * f + b);
            }
        }
    }
}

} // namespace rtengine

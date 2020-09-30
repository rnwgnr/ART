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

#include "rawimage.h"
#include "gainmap.h"

namespace rtengine {

bool RawImage::has_gain_map(std::vector<uint8_t> *out_buf) const
{
    if (!(isBayer() && DNGVERSION() && RT_OpcodeList2_len > 0)) {
        return false;
    }
    
    std::vector<uint8_t> buf(RT_OpcodeList2_len);
    fseek(ifp, RT_OpcodeList2_start, SEEK_SET);
    if (fread(&buf[0], 1, RT_OpcodeList2_len, ifp) != RT_OpcodeList2_len) {
        return false;
    }

    if (out_buf) {
        out_buf->swap(buf);
    }
    return true;
}

} // namespace rtengine

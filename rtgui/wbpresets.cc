/** -*- C++ -*-
 *  
 *  This file is part of ART.
 *
 *  Copyright (c) 2020 Alberto Griggio <alberto.griggio@gmail.com>
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

#include "toolpanelcoord.h"
#include "../rtengine/rawimagesource.h"
#include "wb_presets.c"

namespace {

inline std::string upcase(const std::string &s)
{
    std::string ret(s);
    for (auto &c : ret) {
        c = std::toupper(c);
    }
    return ret;
}

} // namespace


std::vector<WBProvider::Preset> ToolPanelCoordinator::getWBPresets() const
{
    std::vector<WBProvider::Preset> ret;
    if (ipc) {
        const rtengine::FramesMetaData *md = ipc->getInitialImage()->getMetaData();
        rtengine::RawImageSource *src = dynamic_cast<rtengine::RawImageSource *>(ipc->getInitialImage());
        if (md && src) {
            std::string make = upcase(md->getMake());
            std::string model = upcase(md->getModel());

            auto imatrices = src->getImageMatrices();
            
            for (int i = 0; i < wb_preset_count; ++i) {
                if (make == upcase(wb_preset[i].make) && model == upcase(wb_preset[i].model) && wb_preset[i].tuning == 0 && wb_preset[i].channel[3] == 0) {
                    double r = src->get_pre_mul(0) / wb_preset[i].channel[0];
                    double g = src->get_pre_mul(1) / wb_preset[i].channel[1];
                    double b = src->get_pre_mul(2) / wb_preset[i].channel[2];

                    if (imatrices) {
                        double rr = imatrices->rgb_cam[0][0] * r + imatrices->rgb_cam[0][1] * g + imatrices->rgb_cam[0][2] * b;
                        double gg = imatrices->rgb_cam[1][0] * r + imatrices->rgb_cam[1][1] * g + imatrices->rgb_cam[1][2] * b;
                        double bb = imatrices->rgb_cam[2][0] * r + imatrices->rgb_cam[2][1] * g + imatrices->rgb_cam[2][2] * b;
                        r = rr;
                        g = gg;
                        b = bb;
                    }
                    
                    ret.push_back(WBProvider::Preset(wb_preset[i].name, {1.0/r, 1.0/g, 1.0/b}));
                }
            }
        }
    }
    return ret;
}



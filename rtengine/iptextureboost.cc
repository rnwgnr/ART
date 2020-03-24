/* -*- C++ -*-
 *
 *  This file is part of RawTherapee.
 *
 *  Copyright 2018 Alberto Griggio <alberto.griggio@gmail.com>
 *
 *  RawTherapee is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  RawTherapee is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with RawTherapee.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef _OPENMP
#include <omp.h>
#endif

#include "improcfun.h"
#include "labmasks.h"
#include "array2D.h"
#include "guidedfilter.h"

namespace rtengine {

namespace {

void texture_boost(array2D<float> &Y, const rtengine::procparams::TextureBoostParams::Region &pp, double scale, bool multithread)
{
    int radius = int(pp.edgeStopping * 3.5f / scale + 0.5f);
    float epsilon = 0.001f;
    float strength = pp.strength >= 0 ? 1.f + pp.strength: 1.f / (1.f - pp.strength);

    const int W = Y.width();
    const int H = Y.height();
    
#ifdef _OPENMP
#   pragma omp parallel for if (multithread)
#endif
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            Y[y][x] /= 65535.f;
        }
    }

    array2D<float> base(W, H, Y, 0);
    for (int i = 0; i < pp.iterations; ++i) {
        guidedFilterLog(Y, 100.f, base, radius, epsilon, multithread);
        guidedFilter(base, base, base, radius * 4, epsilon * 10.f, multithread);
        
#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                float d = Y[y][x] - base[y][x];
                d *= strength;
                float blend = std::sqrt(LIM01(Y[y][x] * 10.f));
                Y[y][x] = intp(blend, base[y][x] + d, Y[y][x]);
                base[y][x] = Y[y][x];
            }
        }
    }

#ifdef _OPENMP
#   pragma omp parallel for if (multithread)
#endif
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            Y[y][x] *= 65535.f;
        }
    }
}

} // namespace


bool ImProcFunctions::textureBoost(Imagefloat *rgb)
{
    PlanarWhateverData<float> *editWhatever = nullptr;
    EditUniqueID eid = pipetteBuffer ? pipetteBuffer->getEditID() : EUID_None;

    if ((eid == EUID_LabMasks_H4 || eid == EUID_LabMasks_C4 || eid == EUID_LabMasks_L4) && pipetteBuffer->getDataProvider()->getCurrSubscriber()->getPipetteBufferType() == BT_SINGLEPLANE_FLOAT) {
        editWhatever = pipetteBuffer->getSinglePlaneBuffer();
    }
    
    if (eid == EUID_LabMasks_DE4) {
        if (getDeltaEColor(rgb, deltaE.x, deltaE.y, offset_x, offset_y, full_width, full_height, scale, deltaE.L, deltaE.C, deltaE.H)) {
            deltaE.ok = true;
        }
    }
    
    if (params->textureBoost.enabled) {
        if (editWhatever) {
            LabMasksEditID id = static_cast<LabMasksEditID>(int(eid) - EUID_LabMasks_H4);
            fillPipetteLabMasks(rgb, editWhatever, id, multiThread);
        }
        
        int n = params->textureBoost.regions.size();
        int show_mask_idx = params->textureBoost.showMask;
        if (show_mask_idx >= n || (cur_pipeline != Pipeline::PREVIEW && cur_pipeline != Pipeline::OUTPUT)) {
            show_mask_idx = -1;
        }
        std::vector<array2D<float>> mask(n);
        if (!generateLabMasks(rgb, params->textureBoost.labmasks, offset_x, offset_y, full_width, full_height, scale, multiThread, show_mask_idx, &mask, nullptr)) {
            return true; // show mask is active, nothing more to do
        }

        // LabImage lab(rgb->getWidth(), rgb->getHeight());
        // rgb2lab(*rgb, lab);
        rgb->setMode(Imagefloat::Mode::YUV, multiThread);

        // array2D<float> L(lab.W, lab.H, lab.L, 0);
        // array2D<float> a(lab.W, lab.H, lab.a, 0);
        // array2D<float> b(lab.W, lab.H, lab.b, 0);
        const int W = rgb->getWidth();
        const int H = rgb->getHeight();
        array2D<float> Y(W, H, rgb->g.ptrs, 0);

        for (int i = 0; i < n; ++i) {
            if (!params->textureBoost.labmasks[i].enabled) {
                continue;
            }
            
            auto &r = params->textureBoost.regions[i];
            //EPD(&lab, r, scale, multiThread);
            texture_boost(Y, r, scale, multiThread);
            const auto &blend = mask[i];

#ifdef _OPENMP
#           pragma omp parallel for if (multiThread)
#endif
            // for (int y = 0; y < lab.H; ++y) {
            //     for (int x = 0; x < lab.W; ++x) {
            //         float &l = lab.L[y][x];
            //         float &aa = lab.a[y][x];
            //         float &bb = lab.b[y][x];
            //         l = intp(blend[y][x], l, L[y][x]);
            //         aa = intp(blend[y][x], aa, a[y][x]);
            //         bb = intp(blend[y][x], bb, b[y][x]);
            //         L[y][x] = l;
            //         a[y][x] = aa;
            //         b[y][x] = bb;
            //     }
            // }
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    float &YY = rgb->g(y, x);
                    YY = intp(blend[y][x], Y[y][x], YY);
                    Y[y][x] = YY;
                }
            }
        }

        // lab2rgb(lab, *rgb);
    } else if (editWhatever) {
        editWhatever->fill(0.f);
    }

    return false;
}

} // namespace rtengine

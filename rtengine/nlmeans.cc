/** -*- C++ -*-
 *  
 *  This file is part of ART.
 *
 *  Copyright (c) 2021 Alberto Griggio <alberto.griggio@gmail.com>
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

#include "improcfun.h"
#include "sleef.h"
#include "alignedbuffer.h"
#include "settings.h"
#include "gauss.h"
#include "rescale.h"
#include "ipdenoise.h"
#ifdef _OPENMP
#include <omp.h>
#endif
#include <iostream>

#define BENCHMARK
#include "StopWatch.h"

namespace rtengine { 

extern const Settings *settings;

namespace denoise {

// basic idea taken from Algorithm 3 in the paper:
// "Parameter-Free Fast Pixelwise Non-Local Means Denoising"
// by Jacques Froment

void NLMeans(Imagefloat *img, int strength, int detail_thresh, float scale, bool multithread)
{
    if (!strength) {
        return;
    }
    
    BENCHFUN

    constexpr int max_patch_radius = 2;
    constexpr int max_search_radius = 5;
    
    img->setMode(Imagefloat::Mode::YUV, multithread);
    
    const int search_radius = int(std::ceil(float(max_search_radius) / scale));
    const int patch_radius = int(std::ceil(float(max_patch_radius) / scale));

    const int W = img->getWidth();
    const int H = img->getHeight();

    const float h2 = SQR(std::sqrt(float(strength) / 100.f) / 30.f / scale);

    float amount = LIM(float(detail_thresh)/100.f, 0.f, 0.99f);
    array2D<float> mask(W, H, ARRAY2D_ALIGNED);
    {
        array2D<float> LL(W, H, img->g.ptrs, ARRAY2D_BYREFERENCE);
        detail_mask(LL, mask, 1e-3f, 1.f, amount, true, 10.f / scale, multithread);
    }

#ifdef _OPENMP
#   pragma omp parallel for if (multithread)
#endif
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            mask[y][x] = -1.f / (mask[y][x] * h2);
        }
    }

    Imagefloat *dst = img;
    const int border = search_radius + patch_radius;
    const int WW = W + border * 2;
    const int HH = H + border * 2;

    array2D<float> src(WW, HH, ARRAY2D_ALIGNED);
#ifdef _OPENMP
#   pragma omp parallel for if (multithread)
#endif
    for (int y = 0; y < HH; ++y) {
        int yy = y <= border ? 0 : y >= H ? H-1 : y - border;
        for (int x = 0; x < WW; ++x) {
            int xx = x <= border ? 0 : x >= W ? W-1 : x - border;
            float Y = img->g(yy, xx) / 65535.f;
            src[y][x] = Y;
        }
    }

#ifdef _OPENMP
#   pragma omp parallel for if (multithread)
#endif
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            dst->g(y, x) = 0.f;
        }
    }

    // process by tiles to avoid numerical accuracy errors in the computation
    // of the integral image
    const int tile_size = 150;
    const int ntiles_x = int(std::ceil(float(WW) / (tile_size-2*border)));
    const int ntiles_y = int(std::ceil(float(HH) / (tile_size-2*border)));
    const int ntiles = ntiles_x * ntiles_y;

#ifdef __SSE2__
    vfloat zerov = F2V(0.0);
#endif

#ifdef _OPENMP
#   pragma omp parallel for if (multithread) schedule(dynamic)
#endif
    for (int tile = 0; tile < ntiles; ++tile) {
        const int tile_y = tile / ntiles_x;
        const int tile_x = tile % ntiles_x;

        const int start_y = tile_y * (tile_size - 2*border);
        const int end_y = std::min(start_y + tile_size, HH);
        const int TH = end_y - start_y;

        const int start_x = tile_x * (tile_size - 2*border);
        const int end_x = std::min(start_x + tile_size, WW);
        const int TW = end_x - start_x;

        const auto Y = [=](int y) -> int { return LIM(y+start_y, 0, HH-1); };
        const auto X = [=](int x) -> int { return LIM(x+start_x, 0, WW-1); };

        const auto score =
            [&](int tx, int ty, int zx, int zy) -> float
            {
                return SQR(src[Y(zy)][X(zx)] - src[Y(zy + ty)][X(zx + tx)]);
            };

        array2D<float> St(TW, TH, ARRAY2D_ALIGNED);
        array2D<float> SW(TW, TH, ARRAY2D_ALIGNED|ARRAY2D_CLEAR_DATA);

        for (int ty = -search_radius; ty <= search_radius; ++ty) {
            for (int tx = -search_radius; tx <= search_radius; ++tx) {
                // Step 1 — Compute the integral image St
                St[0][0] = 0.f;
                for (int xx = 1; xx < TW; ++xx) {
                    St[0][xx] = St[0][xx-1] + score(tx, ty, xx, 0);
                }
                for (int yy = 1; yy < TH; ++yy) {
                    St[yy][0] = St[yy-1][0] + score(tx, ty, 0, yy);
                }
                for (int yy = 1; yy < TH; ++yy) {
                    for (int xx = 1; xx < TW; ++xx) {
                        St[yy][xx] = St[yy][xx-1] + St[yy-1][xx] - St[yy-1][xx-1] + score(tx, ty, xx, yy);
                    }
                }
                // Step 2 — Compute weight and estimate for patches
                // V(x), V(y) with y = x + t
                for (int yy = start_y+border; yy < end_y-border; ++yy) {
                    int y = yy - border;
                    int xx = start_x+border;
#ifdef __SSE2__
                    for (; xx < end_x-border-3; xx += 4) {
                        int x = xx - border;
                        int sx = xx + tx;
                        int sy = yy + ty;

                        int sty = yy - start_y;
                        int stx = xx - start_x;
                    
                        vfloat dist2 = LVFU(St[sty + patch_radius][stx + patch_radius]) + LVFU(St[sty - patch_radius][stx - patch_radius]) - LVFU(St[sty + patch_radius][stx - patch_radius]) - LVFU(St[sty - patch_radius][stx + patch_radius]);
                        dist2 = vmaxf(dist2, zerov);
                        vfloat d = dist2 * LVFU(mask[y][x]);
                        vfloat weight = xexpf(d);
                        STVFU(SW[y-start_y][x-start_x], LVFU(SW[y-start_y][x-start_x]) + weight);
                        vfloat Y = weight * LVFU(src[sy][sx]);
                        STVFU(dst->g(y, x), LVFU(dst->g(y, x)) + Y);
                    }
#endif
                    for (; xx < end_x-border; ++xx) {
                        int x = xx - border;
                        int sx = xx + tx;
                        int sy = yy + ty;

                        int sty = yy - start_y;
                        int stx = xx - start_x;
                    
                        float dist2 = St[sty + patch_radius][stx + patch_radius] + St[sty - patch_radius][stx - patch_radius] - St[sty + patch_radius][stx - patch_radius] - St[sty - patch_radius][stx + patch_radius];
                        dist2 = std::max(dist2, 0.f);
                        float d = dist2 * mask[y][x];
                        float weight = xexpf(d);
                        SW[y-start_y][x-start_x] += weight;
                        float Y = weight * src[sy][sx];
                        dst->g(y, x) += Y;

                        assert(!xisinff(dst->g(y, x)));
                        assert(!xisnanf(dst->g(y, x)));
                    }
                }
            }
        }

        // Compute final estimate at pixel x = (x1, x2)
        for (int yy = start_y+border; yy < end_y-border; ++yy) {
            int y = yy - border;
            for (int xx = start_x+border; xx < end_x-border; ++xx) {
                int x = xx - border;
            
                const float Y = dst->g(y, x);
                const float f = (1e-5f + float(SW[y-start_y][x-start_x]));
                dst->g(y, x) = (Y / f) * 65535.f;

                assert(!xisnanf(dst->g(y, x)));
            }
        }
    }

    dst->setMode(Imagefloat::Mode::RGB, multithread);
}


}} // namespace rtengine::denoise

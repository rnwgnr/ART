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
#include "sleef.h"
#include "imagesource.h"
#include "rt_algo.h"
#include "curves.h"
#include "guidedfilter.h"

namespace rtengine {

extern const Settings *settings;

namespace {

float find_gray(float source_gray, float target_gray)
{
    // find a base such that log2lin(base, source_gray) = target_gray
    // log2lin is (base^source_gray - 1) / (base - 1), so we solve
    //
    //  (base^source_gray - 1) / (base - 1) = target_gray, that is
    //
    //  base^source_gray - 1 - base * target_gray + target_gray = 0
    //
    // use a bisection method (maybe later change to Netwon)

    if (source_gray <= 0.f) {
        return 0.f;
    }

    const auto f =
        [=](float x) -> float
        {
            return std::pow(x, source_gray) - 1 - target_gray * x + target_gray;
        };

    // first find the interval we are interested in

    float lo = 1.f;
    while (f(lo) <= 0.f) {
        lo *= 2.f;
    }

    float hi = lo * 2.f;
    while (f(hi) >= 0.f) {
        hi *= 2.f;
    }

    if (std::isinf(hi)) {
        return 0.f;
    }

    // now search for a zero
    for (int iter = 0; iter < 100; ++iter) {
        float mid = lo + (hi - lo) / 2.f;
        float v = f(mid);
        if (std::abs(v) < 1e-4f || (hi - lo) / lo <= 1e-4f) {
            return mid;
        }
        if (v > 0.f) {
            lo = mid;
        } else {
            hi = mid;
        }
    }

    return 0.f; // not found
}


// taken from darktable
inline float power_norm(float r, float g, float b)
{
    r = std::abs(r);
    g = std::abs(g);
    b = std::abs(b);

    float r2 = SQR(r);
    float g2 = SQR(g);
    float b2 = SQR(b);
    float d = r2 + g2 + b2;
    float n = r*r2 + g*g2 + b*b2;

    return n / std::max(d, 1e-12f);
}


inline float norm(float r, float g, float b, TMatrix ws)
{
    return (power_norm(r, g, b) + Color::rgbLuminance(r, g, b, ws)) / 2.f;
}


// basic log encoding taken from ACESutil.Lin_to_Log2, from
// https://github.com/ampas/aces-dev
// (as seen on pixls.us)
void log_encode(Imagefloat *rgb, const ProcParams *params, float scale, int full_width, int full_height, bool multithread)
{
    if (!params->logenc.enabled) {
        return;
    }

    const float gray = params->logenc.sourceGray / 100.f;
    const float shadows_range = params->logenc.blackEv;
    const float dynamic_range = std::max(params->logenc.whiteEv - params->logenc.blackEv, 0.5);
    const float noise = pow_F(2.f, -16.f);
    const float log2 = xlogf(2.f);
    const float b = params->logenc.targetGray > 1 && params->logenc.targetGray < 100 && dynamic_range > 0 ? find_gray(std::abs(params->logenc.blackEv) / dynamic_range, params->logenc.targetGray / 100.f) : 0.f;
    const float linbase = max(b, 0.f);
    TMatrix ws = ICCStore::getInstance()->workingSpaceMatrix(params->icm.workingProfile);

    const auto apply =
        [=](float x, bool scale=true) -> float
        {
            if (scale) {
                x /= 65535.f;
            }
            x = max(x, noise);
            x = max(x / gray, noise);
            x = max((xlogf(x)/log2 - shadows_range) / dynamic_range, noise);
            assert(x == x);
            if (linbase > 0.f) {
                x = xlog2lin(x, linbase);
            }
            if (scale) {
                return x * 65535.f;
            } else {
                return x;
            }
        };

    const int W = rgb->getWidth(), H = rgb->getHeight();
    
    if (params->logenc.regularization == 0) {
#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                float r = rgb->r(y, x);
                float g = rgb->g(y, x);
                float b = rgb->b(y, x);
                float m = norm(r, g, b, ws);
                if (m > noise) {
                    float mm = apply(m);
                    float f = mm / m;
                    r *= f;
                    b *= f;
                    g *= f;
                }
            
                assert(r == r);
                assert(g == g);
                assert(b == b);

                rgb->r(y, x) = r;
                rgb->g(y, x) = g;
                rgb->b(y, x) = b;
            }
        }
    } else {
        array2D<float> Y(W, H);
        {
            constexpr float base_posterization = 20.f;
            array2D<float> Y2(W, H);
        
#ifdef _OPENMP
#           pragma omp parallel for if (multithread)
#endif
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    Y2[y][x] = norm(rgb->r(y, x), rgb->g(y, x), rgb->b(y, x), ws) / 65535.f;
                    float l = xlogf(std::max(Y2[y][x], 1e-9f));
                    float ll = round(l * base_posterization) / base_posterization;
                    Y[y][x] = xexpf(ll);
                    assert(std::isfinite(Y[y][x]));
                }
            }
            const float radius = max(max(full_width, W), max(full_height, H)) / 30.f;
            const float epsilon = 0.005f;
            rtengine::guidedFilter(Y2, Y, Y, radius, epsilon, multithread);
        }
        const float blend = LIM01(float(params->logenc.regularization) / 100.f);
        
#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                float &r = rgb->r(y, x);
                float &g = rgb->g(y, x);
                float &b = rgb->b(y, x);
                float t = Y[y][x];
                float t2;
                if (t > noise && (t2 = norm(r, g, b, ws)) > noise) {
                    float c = apply(t, false);
                    float f = c / t;
                    float f2 = apply(t2) / t2;
                    f = intp(blend, f, f2);
                    assert(std::isfinite(f));
                    r *= f;
                    g *= f;
                    b *= f;
                    assert(std::isfinite(r));
                    assert(std::isfinite(g));
                    assert(std::isfinite(b));                    
                }
            }
        }
    }
}

} // namespace


void ImProcFunctions::getAutoLog(ImageSource *imgsrc, LogEncodingParams &lparams)
{
    constexpr int SCALE = 10;
    int fw, fh, tr = TR_NONE;
    imgsrc->getFullSize(fw, fh, tr);
    PreviewProps pp(0, 0, fw, fh, SCALE);
    Imagefloat img(int(fw / SCALE + 0.5), int(fh / SCALE + 0.5));
    ProcParams neutral;
    neutral.exposure.enabled = true;
    // neutral.exposure.clampOOG = false;
    imgsrc->getImage(imgsrc->getWB(), tr, &img, pp, neutral.exposure, neutral.raw);
    imgsrc->convertColorSpace(&img, params->icm, imgsrc->getWB());
    TMatrix ws = ICCStore::getInstance()->workingSpaceMatrix(params->icm.workingProfile);

    float vmin = RT_INFINITY;
    float vmax = -RT_INFINITY;

    constexpr float noise = 1e-5;
    const int w = img.getWidth();
    const int h = img.getHeight();

    array2D<float> Y(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float r = img.r(y, x), g = img.g(y, x), b = img.b(y, x);
            Y[y][x] = norm(r, g, b, ws) / 65535.f;
        }
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float l = Y[y][x];
            if (l > noise) {
                vmin = min(vmin, l);
                vmax = max(vmax, l);
            }
        }
    }
//    vmin /= 1.5f;
    vmax *= 1.5f;

    // for (int y = 0, h = fh / SCALE; y < h; ++y) {
    //     for (int x = 0, w = fw / SCALE; x < w; ++x) {
    //         float r = img.r(y, x), g = img.g(y, x), b = img.b(y, x);
    //         float m = max(0.f, r, g, b) / 65535.f;
    //         if (m > noise) {
    //             float l = min(r, g, b) / 65535.f;
    //             vmin = min(vmin, l > noise ? l : m);
    //             vmax = max(vmax, m);
    //         }
    //     }
    // }

    if (vmax > vmin) {
        const float log2 = xlogf(2.f);
        float dynamic_range = -xlogf(vmin / vmax) / log2;
        if (settings->verbose) {
            std::cout << "AutoLog: min = " << vmin << ", max = " << vmax
                      << ", DR = " << dynamic_range << std::endl;
        }

        if (lparams.autogray) {
            double tot = 0.f;
            int n = 0;
            float gmax = std::min(vmax / 2.f, 0.25f);
            float gmin = std::max(vmin * std::pow(2.f, std::max((dynamic_range - 1.f) / 2.f, 1.f)), 0.05f);
            if (settings->verbose) {
                std::cout << "         gray boundaries: " << gmin << ", " << gmax << std::endl;
            }
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    float l = Y[y][x];
                    if (l >= gmin && l <= gmax) {
                        tot += l;
                        ++n;
                    }
                }
            }
            // for (int y = 0, h = fh / SCALE; y < h; ++y) {
            //     for (int x = 0, w = fw / SCALE; x < w; ++x) {
            //         float l = img.g(y, x) / 65535.f;
            //         if (l >= gmin && l <= gmax) {
            //             tot += l;
            //             ++n;
            //         }
            //     }
            // }
            if (n > 0) {
                lparams.sourceGray = tot / n * 100.f;
                if (settings->verbose) {
                    std::cout << "         computed gray point from " << n << " samples: " << lparams.sourceGray << std::endl;
                }
            } else if (settings->verbose) {
                std::cout << "         no samples found in range, resorting to default gray point value" << std::endl;
                lparams.sourceGray = LogEncodingParams().sourceGray;
            }
        }
        
        float gray = float(lparams.sourceGray) / 100.f;
        lparams.whiteEv = xlogf(vmax / gray) / log2;
        lparams.blackEv = lparams.whiteEv - dynamic_range;
    }
}


void ImProcFunctions::logEncoding(Imagefloat *rgb)
{
    if (params->logenc.enabled) {
        rgb->setMode(Imagefloat::Mode::RGB, multiThread);
        log_encode(rgb, params, scale, full_width, full_height, multiThread);
    }
}


} // namespace rtengine

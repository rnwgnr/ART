/* -*- C++ -*-
 *
 *  This file is part of ART.
 *
 *  Copyright 2019 Alberto Griggio <alberto.griggio@gmail.com>
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
// matrix mixing extracted and adapted from ImProcFunctions::rgbProc
// (improcfun.cc) of RawTherapee

#include "improcfun.h"

namespace rtengine {

using namespace procparams;

//
// Computes the color correction matrix corresponding to the desired tweak of
// the primaries in terms of hue and saturation.
// Analogous to the "camera calibration" tool of Lightroom.
// Uses the four-color method described in the paper:
//
// Four-Color Matrix Method for Correction of Tristimulus Colorimeters
// by Yoshihiro Ohno and Jonathan E. Hardis
//    National Institute of Standards and Technology
// published in Proc., IS&T Fifth Color Imaging Conference, 301-305 (1997)
//
// implemented by Alberto Griggio
//
void get_mixer_matrix(const ChannelMixerParams &chmix, const Glib::ustring &workingProfile,
                      float &rr, float &rg, float &rb,
                      float &gr, float &gg, float &gb,
                      float &br, float &bg, float &bb)
{
    typedef std::array<float, 3> A3;
    typedef std::array<A3, 3> M33;
    
    TMatrix m = ICCStore::getInstance()->workingSpaceMatrix(workingProfile);
    const M33 ws = {
        A3{ float(m[0][0]), float(m[0][1]), float(m[0][2]) },
        A3{ float(m[1][0]), float(m[1][1]), float(m[1][2]) },
        A3{ float(m[2][0]), float(m[2][1]), float(m[2][2]) }
    };

    constexpr float D65_x = 0.3127266146810121f;
    constexpr float D65_y = 0.32902313032606195f;
    
    const A3 white = { D65_x, D65_y, 1.f - D65_x - D65_y };

    const auto rgb2xy =
        [&](const A3 &rgb) -> A3
        {
            A3 xyz = dotProduct(ws, rgb);
            float sum = xyz[0] + xyz[1] + xyz[2];
            if (sum == 0.f) {
                return white;
            }
            float x = xyz[0] / sum;
            float y = xyz[1] / sum;
            return { x, y, 1.f - x - y };
        };
    
    const auto get_matrix =
        [&](const A3 &r, const A3 &g, const A3 &b) -> M33
        {
            auto r_xy = rgb2xy(r);
            auto g_xy = rgb2xy(g);
            auto b_xy = rgb2xy(b);

            M33 m = {
                A3{ r_xy[0], g_xy[0], b_xy[0] },
                A3{ r_xy[1], g_xy[1], b_xy[1] },
                A3{ r_xy[2], g_xy[2], b_xy[2] }
            };

            M33 mi;
            invertMatrix(m, mi);

            A3 kr = dotProduct(mi, white);

            M33 kr_m = {
                A3{ kr[0], 0.f, 0.f },
                A3{ 0.f, kr[1], 0.f },
                A3{ 0.f, 0.f, kr[2] }
            };

            M33 ret = dotProduct(m, kr_m);
            return ret;
        };

    A3 red = {1.f, 0.f, 0.f};
    A3 green = {0.f, 1.f, 0.f};
    A3 blue = {0.f, 0.f, 1.f};

    M33 M = get_matrix(red, green, blue);

    const auto tweak =
        [](const A3 &c, int hue, int sat, float hrange) -> A3
        {
            float h, s, l;
            Color::rgb2hsl(c[0] * 65535.f, c[1] * 65535.f, c[2] * 65535.f, h, s, l);
            float dh = float(hue)/100.f * hrange;
            h += dh;
            if (h > 1.f) {
                h -= 1.f;
            } else if (h < 0.f) {
                h += 1.f;
            }
            float ds = 1.f + (float(sat) / 100.f * 0.3f);
            s *= ds;
            A3 ret;
            Color::hsl2rgb(h, s, l, ret[0], ret[1], ret[2]);
            ret[0] /= 65535.f;
            ret[1] /= 65535.f;
            ret[2] /= 65535.f;

            return ret;
        };

    M33 N = get_matrix(tweak(red, chmix.hue_tweak[0], chmix.sat_tweak[0], 0.05f),
                       tweak(green, chmix.hue_tweak[1], chmix.sat_tweak[1], 0.15f),
                       tweak(blue, chmix.hue_tweak[2], chmix.sat_tweak[2], 0.15f));

    M33 Minv;
    if (!invertMatrix(M, Minv)) {
        rr = 100.f;
        rg = 0.f;
        rb = 0.f;

        gr = 0.f;
        gg = 100.f;
        gb = 0.f;

        br = 0.f;
        bg = 0.f;
        bb = 100.f;
    } else {
        M33 res = dotProduct(N, Minv);

        rr = res[0][0] * 100.f;
        rg = res[0][1] * 100.f;
        rb = res[0][2] * 100.f;

        gr = res[1][0] * 100.f;
        gg = res[1][1] * 100.f;
        gb = res[1][2] * 100.f;

        br = res[2][0] * 100.f;
        bg = res[2][1] * 100.f;
        bb = res[2][2] * 100.f;
    }
}


void ImProcFunctions::channelMixer(Imagefloat *img)
{
    if (params->chmixer.enabled) {
        img->setMode(Imagefloat::Mode::RGB, multiThread);
        
        float chMixRR = float(params->chmixer.red[0])/10.f;
        float chMixRG = float(params->chmixer.red[1])/10.f;
        float chMixRB = float(params->chmixer.red[2])/10.f;
        float chMixGR = float(params->chmixer.green[0])/10.f;
        float chMixGG = float(params->chmixer.green[1])/10.f;
        float chMixGB = float(params->chmixer.green[2])/10.f;
        float chMixBR = float(params->chmixer.blue[0])/10.f;
        float chMixBG = float(params->chmixer.blue[1])/10.f;
        float chMixBB = float(params->chmixer.blue[2])/10.f;

        if (params->chmixer.mode == ChannelMixerParams::Mode::PRIMARIES_CHROMA){
            get_mixer_matrix(params->chmixer, params->icm.workingProfile,
                             chMixRR, chMixRG, chMixRB,
                             chMixGR, chMixGG, chMixGB,
                             chMixBR, chMixBG, chMixBB);
            if (options.rtSettings.verbose) {
                printf("Channel mixer matrix:\n"
                       "   %.1f %.1f %.1f\n"
                       "   %.1f %.1f %.1f\n"
                       "   %.1f %.1f %.1f\n",
                       chMixRR, chMixRG, chMixRB,
                       chMixGR, chMixGG, chMixGB,
                       chMixBR, chMixBG, chMixBB);
                fflush(stdout);
            }
        }

        
#ifdef _OPENMP
#       pragma omp parallel for if (multiThread)
#endif
        for (int y = 0; y < img->getHeight(); ++y) {
            for (int x = 0; x < img->getWidth(); ++x) {
                float r = img->r(y, x);
                float g = img->g(y, x);
                float b = img->b(y, x);

                float rmix = (r * chMixRR + g * chMixRG + b * chMixRB) / 100.f;
                float gmix = (r * chMixGR + g * chMixGG + b * chMixGB) / 100.f;
                float bmix = (r * chMixBR + g * chMixBG + b * chMixBB) / 100.f;

                img->r(y, x) = rmix;
                img->g(y, x) = gmix;
                img->b(y, x) = bmix;
            }
        }
    }
}

} // namespace rtengine

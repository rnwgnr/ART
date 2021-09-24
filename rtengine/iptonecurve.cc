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

#include "improcfun.h"
#include "curves.h"
#include "color.h"
#include "sleef.h"
#include "curves.h"

namespace rtengine {

namespace {

// template <class Curve>
// inline void apply_batch(const Curve &c, Imagefloat *rgb, int W, int H, bool multithread)
// {
// #ifdef _OPENMP
//     #pragma omp parallel for if (multithread)
// #endif
//     for (int y = 0; y < H; ++y) {
//         c.BatchApply(0, W, rgb->r.ptrs[y], rgb->g.ptrs[y], rgb->b.ptrs[y]);
//     }
// }


template <class Curve>
inline void apply(const Curve &c, Imagefloat *rgb, int W, int H, bool multithread)
{
#ifdef _OPENMP
    #pragma omp parallel for if (multithread)
#endif
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            c.Apply(rgb->r(y, x), rgb->g(y, x), rgb->b(y, x));
        }
    }
}


void apply_tc(Imagefloat *rgb, const ToneCurve &tc, ToneCurveParams::TcMode curveMode, const Glib::ustring &working_profile, int perceptual_strength, bool multithread)
{
    const int W = rgb->getWidth();
    const int H = rgb->getHeight();
    
    if (curveMode == ToneCurveParams::TcMode::PERCEPTUAL) {
        const PerceptualToneCurve &c = static_cast<const PerceptualToneCurve&>(tc);
        PerceptualToneCurveState state;
        c.initApplyState(state, working_profile);
        state.strength = LIM01(float(perceptual_strength) / 100.f);

#ifdef _OPENMP
        #pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < H; ++y) {
            c.BatchApply(0, W, rgb->r.ptrs[y], rgb->g.ptrs[y], rgb->b.ptrs[y], state);
        }
    } else if (curveMode == ToneCurveParams::TcMode::STD) {
        const StandardToneCurve &c = static_cast<const StandardToneCurve &>(tc);
        apply(c, rgb, W, H, multithread);
    } else if (curveMode == ToneCurveParams::TcMode::WEIGHTEDSTD) {
        const WeightedStdToneCurve &c = static_cast<const WeightedStdToneCurve &>(tc);
        apply(c, rgb, W, H, multithread);
    } else if (curveMode == ToneCurveParams::TcMode::FILMLIKE) {
        const AdobeToneCurve &c = static_cast<const AdobeToneCurve &>(tc);
        apply(c, rgb, W, H, multithread);
    } else if (curveMode == ToneCurveParams::TcMode::SATANDVALBLENDING) {
        const SatAndValueBlendingToneCurve &c = static_cast<const SatAndValueBlendingToneCurve &>(tc);
        apply(c, rgb, W, H, multithread);
    } else if (curveMode == ToneCurveParams::TcMode::LUMINANCE) {
        TMatrix ws = ICCStore::getInstance()->workingSpaceMatrix(working_profile);
        const LuminanceToneCurve &c = static_cast<const LuminanceToneCurve &>(tc);
//        apply(c, rgb, W, H, multithread);
#ifdef _OPENMP
#       pragma omp parallel for if (multithread)
#endif
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                c.Apply(rgb->r(y, x), rgb->g(y, x), rgb->b(y, x), ws);
            }
        }
    }
}


class ContrastCurve: public Curve {
public:
    ContrastCurve(double a, double b): a_(a), b_(b) {}
    void getVal(const std::vector<double>& t, std::vector<double>& res) const {}
    bool isIdentity () const { return false; }
    
    double getVal(double x) const
    {
        return lin2log(std::pow(x, a_), b_);
    }

private:
    double a_;
    double b_;
};


void apply_contrast(Imagefloat *rgb, const ImProcData &im, int contrast, bool legacy, ToneCurveParams::TcMode curveMode, const Glib::ustring &working_profile, float whitept)
{
    const int W = rgb->getWidth();
    const int H = rgb->getHeight();
    const float Lmax = 65535.f * whitept;

#ifdef _OPENMP
#   pragma omp parallel for if (im.multiThread)
#endif
    for (int i = 0; i < H; ++i) {
        for (int j = 0; j < W; ++j) {
            float &r = rgb->r(i, j);
            float &g = rgb->g(i, j);
            float &b = rgb->b(i, j);
            Color::filmlike_clip(&r, &g, &b, Lmax);
        }
    }
    
    if (!contrast) {
        return;
    }

    ToneCurve tc;
    std::unique_ptr<Curve> ccurve;
    auto &curve = tc.lutToneCurve;
    curve(65536);

    if (im.params->logenc.enabled || !legacy) {
        const double pivot = im.params->logenc.enabled ? im.params->logenc.targetGray / 100.0 : 0.18;
        const double b = contrast > 0 ? (1 + contrast * 0.125) : 1.0 / (1 - contrast * 0.125);
        const double a = std::log((std::exp(std::log(b) * pivot) - 1) / (b - 1)) / std::log(pivot);

        ccurve.reset(new ContrastCurve(a, b));
        tc.Set(*ccurve, 0, 65535.f * whitept);

        // const auto scurve =
        //     [a,b](double x) -> double
        //     {
        //         return lin2log(std::pow(x, a), b);
        //     };

        // for (int i = 0; i < 65536; ++i) {
        //     double x = i / 65535.0;
        //     double y = scurve(x);
        //     assert(y == y);
        //     curve[i] = y * 65535.f;
        // }        
    } else {
        ccurve.reset(new DiagonalCurve({DCT_Empty}));
        tc.Set(*ccurve);
        
        LUTf curve1(65536);
        LUTf curve2(65536);
        LUTu dummy;
        LUTu hist16(65536);
        ToneCurve customToneCurve1, customToneCurve2;

        ImProcFunctions ipf(im.params, im.multiThread);
        ipf.firstAnalysis(rgb, *im.params, hist16);
        CurveFactory::complexCurve(0, 0, 0, 0, 0, 0, contrast,
                                   { DCT_Linear }, { DCT_Linear },
                                   hist16, curve1, curve2, curve, dummy,
                                   customToneCurve1, customToneCurve2,
                                   max(im.scale, 1.0));
    }

    apply_tc(rgb, tc, legacy ? ToneCurveParams::TcMode::STD : curveMode, working_profile, 100, im.multiThread);
}


inline float satcurve_logenc(float x, float whitept)
{
    static const float log2 = std::log(2.f);
    const float dr = xlogf(65535.f * whitept) / log2;
    constexpr float black = -13.5f;
    constexpr float gray = 0.18f;
    const float p = std::log(0.18f) / std::log(-black / dr);
    
    return pow_F((xlogf(x / 65535.f / gray) / log2 - black) / dr, p);
}


void satcurve_lut(const FlatCurve &curve, LUTf &sat, float whitept)
{
    sat(65536, LUT_CLIP_BELOW);
    sat[0] = curve.getVal(0) * 2.f;
    const bool uselog = whitept > 1.f;
    for (int i = 1; i < 65536; ++i) {
        float x = uselog ? LIM01(satcurve_logenc(float(i), whitept)) : Color::gamma2curve[i] / 65535.f;
        float v = curve.getVal(x);
        sat[i] = v * 2.f;
    }
}


void apply_satcurve(Imagefloat *rgb, const FlatCurve &curve, const Glib::ustring &working_profile, float whitept, bool multithread)
{
    LUTf sat;
    satcurve_lut(curve, sat, whitept);

    TMatrix dws = ICCStore::getInstance()->workingSpaceMatrix(working_profile);
    float ws[3][3];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            ws[i][j] = dws[i][j];
        }
    }

    TMatrix diws = ICCStore::getInstance()->workingSpaceInverseMatrix(working_profile);
    float iws[3][3];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            iws[i][j] = diws[i][j];
        }
    }
    
#ifdef _OPENMP
#   pragma omp parallel for if (multithread)
#endif
    for (int y = 0; y < rgb->getHeight(); ++y) {
        for (int x = 0; x < rgb->getWidth(); ++x) {
            float R = rgb->r(y, x), G = rgb->g(y, x), B = rgb->b(y, x);
            float Y = Color::rgbLuminance(R, G, B, ws);
            float s = sat[Y];
            // rgb->r(y, x) = Y + s * (r - Y);
            // rgb->g(y, x) = Y + s * (g - Y);
            // rgb->b(y, x) = Y + s * (b - Y);
            float L, a, b;
            Color::rgb2lab(R, G, B, L, a, b, ws);
            Color::lab2rgb(L, a * s, b * s, rgb->r(y, x), rgb->g(y, x), rgb->b(y, x), iws);
        }
    }
}


void fill_satcurve_pipette(Imagefloat *rgb, PlanarWhateverData<float>* editWhatever, const Glib::ustring &working_profile, float whitept, bool multithread)
{
    TMatrix ws = ICCStore::getInstance()->workingSpaceMatrix(working_profile);
    const bool uselog = whitept > 1.f;

#ifdef _OPENMP
#   pragma omp parallel for if (multithread)
#endif
    for (int y = 0; y < rgb->getHeight(); ++y) {
        for (int x = 0; x < rgb->getWidth(); ++x) {
            float r = rgb->r(y, x), g = rgb->g(y, x), b = rgb->b(y, x);
            float Y = Color::rgbLuminance(r, g, b, ws);
            float s = uselog ? satcurve_logenc(Y, whitept) : Color::gamma2curve[Y] / 65535.f;
            editWhatever->v(y, x) = LIM01(s);
        }
    }
}


void update_tone_curve_histogram(Imagefloat *img, LUTu &hist, const Glib::ustring &profile, bool multithread)
{
    hist.clear();
    const int compression = log2(65536 / hist.getSize());

    TMatrix ws = ICCStore::getInstance()->workingSpaceMatrix(profile);

#ifdef _OPENMP
#   pragma omp parallel for if (multithread)
#endif
    for (int y = 0; y < img->getHeight(); ++y) {
        for (int x = 0; x < img->getWidth(); ++x) {
            float r = CLIP(img->r(y, x));
            float g = CLIP(img->g(y, x));
            float b = CLIP(img->b(y, x));

            int y = CLIP<int>(Color::gamma2curve[Color::rgbLuminance(r, g, b, ws)]);//max(r, g, b)]);
            hist[y >> compression]++;
        }
    }

    // we make this log encoded
    int n = hist.getSize();
    float f = float(n);
    for (int i = 0; i < n; ++i) {
        hist[i] = xlin2log(float(hist[i]) / f, 2.f) * f;
    }
}

void fill_pipette(Imagefloat *img, Imagefloat *pipette, bool multithread)
{
    const int W = img->getWidth();
    const int H = img->getHeight();
    
#ifdef _OPENMP
#    pragma omp parallel for if (multithread)
#endif
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            pipette->r(y, x) = Color::gamma2curve[CLIP(img->r(y, x))] / 65535.f;
            pipette->g(y, x) = Color::gamma2curve[CLIP(img->g(y, x))] / 65535.f;
            pipette->b(y, x) = Color::gamma2curve[CLIP(img->b(y, x))] / 65535.f;
        }
    }
}

} // namespace


void ImProcFunctions::toneCurve(Imagefloat *img)
{
    if (histToneCurve && *histToneCurve) {
        img->setMode(Imagefloat::Mode::RGB, multiThread);
        update_tone_curve_histogram(img, *histToneCurve, params->icm.workingProfile, multiThread);
    }

    Imagefloat *editImgFloat = nullptr;
    PlanarWhateverData<float> *editWhatever = nullptr;
    EditUniqueID editID = pipetteBuffer ? pipetteBuffer->getEditID() : EUID_None;

    if ((editID == EUID_ToneCurve1 || editID == EUID_ToneCurve2) && pipetteBuffer->getDataProvider()->getCurrSubscriber()->getPipetteBufferType() == BT_IMAGEFLOAT) {
        editImgFloat = pipetteBuffer->getImgFloatBuffer();
    } else if (editID == EUID_ToneCurveSaturation && pipetteBuffer->getDataProvider()->getCurrSubscriber()->getPipetteBufferType() == BT_SINGLEPLANE_FLOAT) {
        editWhatever = pipetteBuffer->getSinglePlaneBuffer();
    }

    if (params->toneCurve.enabled) {
        img->setMode(Imagefloat::Mode::RGB, multiThread);

        const float whitept = params->toneCurve.hasWhitePoint() ? params->toneCurve.whitePoint : 1.f;

        ImProcData im(params, scale, multiThread);
        apply_contrast(img, im, params->toneCurve.contrast, params->toneCurve.contrastLegacyMode, params->toneCurve.curveMode, params->icm.workingProfile, whitept);

        if (editImgFloat && editID == EUID_ToneCurve1) {
            fill_pipette(img, editImgFloat, multiThread);
        }
        
        ToneCurve tc;
        const DiagonalCurve tcurve1(params->toneCurve.curve, CURVES_MIN_POLY_POINTS / max(int(scale), 1));

        if (!tcurve1.isIdentity()) {
            tc.Set(tcurve1, Color::sRGBGammaCurve, 65535.f * whitept);
            apply_tc(img, tc, params->toneCurve.curveMode, params->icm.workingProfile, params->toneCurve.perceptualStrength, multiThread);
        }

        if (editImgFloat && editID == EUID_ToneCurve2) {
            fill_pipette(img, editImgFloat, multiThread);
        }

        const DiagonalCurve tcurve2(params->toneCurve.curve2, CURVES_MIN_POLY_POINTS / max(int(scale), 1));

        if (!tcurve2.isIdentity()) {
            tc.Set(tcurve2, Color::sRGBGammaCurve, 65535.f * whitept);
            apply_tc(img, tc, params->toneCurve.curveMode2, params->icm.workingProfile, params->toneCurve.perceptualStrength, multiThread);
        }

        if (editWhatever) {
            fill_satcurve_pipette(img, editWhatever, params->icm.workingProfile, whitept, multiThread);
        }

        const FlatCurve satcurve(params->toneCurve.saturation, false, CURVES_MIN_POLY_POINTS / max(int(scale), 1));
        if (!satcurve.isIdentity()) {
            apply_satcurve(img, satcurve, params->icm.workingProfile, whitept, multiThread);
        }
    } else if (editImgFloat) {
        const int W = img->getWidth();
        const int H = img->getHeight();

#ifdef _OPENMP
#       pragma omp parallel for if (multiThread)
#endif
        for (int y = 0; y < H; ++y) {
            std::fill(editImgFloat->r(y), editImgFloat->r(y)+W, 0.f);
            std::fill(editImgFloat->g(y), editImgFloat->g(y)+W, 0.f);
            std::fill(editImgFloat->b(y), editImgFloat->b(y)+W, 0.f);
        }
    } else if (editWhatever) {
        editWhatever->fill(0.f);
    }
}

} // namespace rtengine

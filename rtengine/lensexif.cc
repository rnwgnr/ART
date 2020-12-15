/* -*- C++ -*-
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

// adapted from mlens.c in PR #7092 of darktable
// original author: Freddie Witherden (https://freddie.witherden.org/)
// copyright of original code follows
/*
    This file is part of darktable,
    Copyright (C) 2010-2020 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "lensexif.h"
#include "metadata.h"
#include <array>
#include <iostream>

namespace rtengine {

namespace {

class SonyCorrectionData: public ExifLensCorrection::CorrectionData {
public:
    int nc;
    std::array<short, 16> distortion;
    std::array<short, 16> ca_r;
    std::array<short, 16> ca_b;
    std::array<short, 16> vignetting;

    void get_coeffs(std::vector<float> &knots, std::vector<float> &dist, std::vector<float> &vig, std::array<std::vector<float>, 3> &ca) const override
    {
        const float scale = 1.f;
        
        knots.resize(nc);
        for (int i = 0; i < 3; ++i) {
            ca[i].resize(nc);
        }
        dist.resize(nc);
        vig.resize(nc);

        for (int i = 0; i < nc; i++) {
            knots[i] = float(i) / (nc - 1);

            dist[i] = (distortion[i] * std::pow(2.f, -14.f) + 1) * scale;

            ca[0][i] = ca[1][i] = ca[2][i] = 1.f;
            ca[0][i] *= ca_r[i] * std::pow(2.f, -21.f) + 1;
            ca[2][i] *= ca_b[i] * std::pow(2.f, -21.f) + 1;

            vig[i] = std::pow(2.f, 0.5f - std::pow(2.f, vignetting[i] * std::pow(2.f, -13.f)  -1));
        }
    }    
};


class FujiCorrectionData: public ExifLensCorrection::CorrectionData {
public:
    float cropf;
    std::array<float, 9> knots;
    std::array<float, 9> distortion;
    std::array<float, 9> ca_r;
    std::array<float, 9> ca_b;
    std::array<float, 9> vignetting;

    void get_coeffs(std::vector<float> &knots, std::vector<float> &dist, std::vector<float> &vig, std::array<std::vector<float>, 3> &ca) const override
    {
        knots.resize(9);
        dist.resize(9);
        vig.resize(9);
        for (int i = 0; i < 3; ++i) {
            ca[i].resize(9);
        }
        
        for (int i = 0; i < 9; i++) {
            knots[i] = cropf * this->knots[i];

            dist[i] = distortion[i] / 100 + 1;

            ca[0][i] = ca[1][i] = ca[2][i] = 1.f;

            ca[0][i] *= ca_r[i] + 1;
            ca[2][i] *= ca_b[i] + 1;
            
            vig[i] = 1 - (1 - vignetting[i] / 100);
        }
    }
};


float interpolate(const std::vector<float> &xi, const std::vector<float> &yi, float x)
{
    if (x < xi[0]) {
        return yi[0];
    }

    for (size_t i = 1; i < xi.size(); i++) {
        if (x >= xi[i - 1] && x <= xi[i]) {
            float dydx = (yi[i] - yi[i - 1]) / (xi[i] - xi[i - 1]);

            return yi[i - 1] + (x - xi[i - 1]) * dydx;
        }
    }

    return yi[yi.size() - 1];
}

} // namespace


ExifLensCorrection::ExifLensCorrection(const FramesMetaData *meta, int width, int height, const CoarseTransformParams &coarse, int rawRotationDeg):
    data_(),
    swap_xy_(false)
{
    if (rawRotationDeg >= 0) {
        int rot = (coarse.rotate + rawRotationDeg) % 360;
        swap_xy_ = (rot == 90 || rot == 270);
        if (swap_xy_) {
            std::swap(width, height);
        }
    }

    w2_ = width * 0.5f;
    h2_ = height * 0.5f;
    r_ = 1 / std::sqrt(SQR(w2_) + SQR(h2_));

    auto make = meta->getMake();
    if (make != "SONY" && make != "FUJIFILM") {
        return;
    }
    
    try {
        auto mn = Exiv2Metadata(meta->getFileName()).getMakernotes();

        const auto gettag =
            [&](const char *name) -> std::string
            {
                auto it = mn.find(name);
                if (it != mn.end()) {
                    return it->second;
                }
                return "";
            };

        const auto getvec =
            [&](const char *tag) -> std::vector<float>
            {
                std::istringstream src(gettag(tag));
                std::vector<float> ret;
                float val;
                while (src >> val) {
                    ret.push_back(val);
                }
                return ret;
            };

    
        if (make == "SONY") {
            auto posd = getvec("DistortionCorrParams");
            auto posc = getvec("ChromaticAberrationCorrParams");
            auto posv = getvec("VignettingCorrParams");

            if (!posd.empty() && !posc.empty() && !posv.empty() &&
                posd[0] <= 16 && posc[0] == 2 * posd[0] && posv[0] == posd[0]) {
                SonyCorrectionData *sony = new SonyCorrectionData();
                data_.reset(sony);
                sony->nc = posd[0];
                for (int i = 0; i < sony->nc; ++i) {
                    sony->distortion[i] = posd[i+1];
                    sony->ca_r[i] = posc[i+1];
                    sony->ca_b[i] = posc[sony->nc + i+1];
                    sony->vignetting[i] = posv[i+1];
                }
            }
        } else if (make == "FUJIFILM") {
            auto posd = getvec("GeometricDistortionParams");
            auto posc = getvec("ChromaticAberrationParams");
            auto posv = getvec("VignettingParams");

            if (posd.size() == 19 && posc.size() == 29 && posv.size() == 19) {
                FujiCorrectionData *fuji = new FujiCorrectionData();
                data_.reset(fuji);
                
                for(int i = 0; i < 9; i++) {
                    float kd = posd[i+1], kc = posc[i + 1], kv = posv[i + 1];
                    if (kd != kc || kd != kv) {
                        data_.reset(nullptr);
                        break;
                    }

                    fuji->knots[i] = kd;
                    fuji->distortion[i] = posd[i + 10];
                    fuji->ca_r[i] = posc[i + 10];
                    fuji->ca_b[i] = posc[i + 19];
                    fuji->vignetting[i] = posv[i + 10];
                }

                if (data_) {
                    // Account for the 1.25x crop modes in some Fuji cameras
                    std::string val = gettag("CropMode");
                    if (val == "2" || val == "4") {
                        fuji->cropf = 1.25f;
                    } else {
                        fuji->cropf = 1;
                    }
                }
            }
        }
    } catch (std::exception &exc) {
        data_.reset(nullptr);
    }

    if (data_) {
        data_->get_coeffs(knots_, dist_, vig_, ca_);
    }
}


bool ExifLensCorrection::ok() const
{
    return data_.get();
}


bool ExifLensCorrection::ok(const FramesMetaData *meta)
{
    ExifLensCorrection corr(meta, -1, -1, CoarseTransformParams(), -1);
    return corr.ok();
}


void ExifLensCorrection::correctDistortion(double &x, double &y, int cx, int cy, double scale) const
{
    if (data_) {
        float xx = x + cx;
        float yy = y + cy;
        if (swap_xy_) {
            std::swap(xx, yy);
        }

        float ccx = xx - w2_;
        float ccy = yy - h2_;
        float dr = interpolate(knots_, dist_, r_ * std::sqrt(SQR(ccx) + SQR(ccy)));

        x = dr * ccx + w2_;
        y = dr * ccy + h2_;
        if (swap_xy_) {
            std::swap(x, y);
        }
        x -= cx;
        y -= cy;

        x *= scale;
        y *= scale;
    }
}


bool ExifLensCorrection::isCACorrectionAvailable() const
{
    return data_.get();
}


void ExifLensCorrection::correctCA(double &x, double &y, int cx, int cy, int channel) const
{
    if (data_) {
        float xx = x + cx;
        float yy = y + cy;
        if (swap_xy_) {
            std::swap(xx, yy);
        }

        float ccx = xx - w2_;
        float ccy = yy - h2_;
        float dr = interpolate(knots_, ca_[channel], r_ * std::sqrt(SQR(ccx) + SQR(ccy)));

        x = dr * ccx + w2_;
        y = dr * ccy + h2_;
        if (swap_xy_) {
            std::swap(x, y);
        }
        x -= cx;
        y -= cy;
    }
}


void ExifLensCorrection::processVignette(int width, int height, float** rawData) const
{
    if (data_) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float cx = x - w2_;
                float cy = y - h2_;
                float sf = interpolate(knots_, vig_, r_ * std::sqrt(SQR(cx) + SQR(cy)));
                rawData[y][x] /= SQR(sf);
            }
        }
    }
}

} // namespace rtengine

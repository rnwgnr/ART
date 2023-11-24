/** -*- C++ -*-
 *  
 *  This file is part of ART
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

#include <memory>
#include <cstdint>

#include <gtkmm.h>

#include "cache.h"
#include "alignedbuffer.h"
#include "noncopyable.h"
#include "iccstore.h"
#include "imagefloat.h"

#ifdef ART_USE_OCIO
#  include <OpenColorIO/OpenColorIO.h>
namespace OCIO = OCIO_NAMESPACE;
#endif // ART_USE_OCIO

#ifdef ART_USE_CTL
#  include <CtlSimdInterpreter.h>
#endif


namespace rtengine {

class CLUT final: public NonCopyable {
public:
    CLUT();
    ~CLUT();

    bool load(const Glib::ustring& filename);

    explicit operator bool() const;

    Glib::ustring getFilename() const;
    Glib::ustring getProfile() const;

    void getRGB(
        float strength,
        std::size_t line_size,
        const float* r,
        const float* g,
        const float* b,
        float* out_rgbx
    ) const;

    static void splitClutFilename(
        const Glib::ustring& filename,
        Glib::ustring& name,
        Glib::ustring& extension,
        Glib::ustring& profile_name
    );

private:
    AlignedBuffer<std::uint16_t> clut_image;
    unsigned int clut_level;
    float flevel_minus_one;
    float flevel_minus_two;
    Glib::ustring clut_filename;
    Glib::ustring clut_profile;
};


class CLUTStore final: public NonCopyable {
public:
    static CLUTStore& getInstance();

    std::shared_ptr<CLUT> getClut(const Glib::ustring& filename) const;
#ifdef ART_USE_OCIO
    OCIO::ConstProcessorRcPtr getOCIOLut(const Glib::ustring &filename) const;
#endif // ART_USE_OCIO
#ifdef ART_USE_CTL
    std::vector<Ctl::FunctionCallPtr> getCTLLut(const Glib::ustring &filename, int num_threads, int &chunk_size) const;
#endif // ART_USE_CTL

    void clearCache();

private:
    CLUTStore();

    mutable Cache<Glib::ustring, std::shared_ptr<CLUT>> cache;
#ifdef ART_USE_OCIO
    typedef std::pair<OCIO::ConstProcessorRcPtr, std::string> OCIOCacheEntry;
    mutable Cache<Glib::ustring, OCIOCacheEntry> ocio_cache_;
#endif // ART_USE_OCIO
#ifdef ART_USE_CTL
    typedef std::pair<std::shared_ptr<Ctl::Interpreter>, std::string> CTLCacheEntry;
    mutable Cache<Glib::ustring, CTLCacheEntry> ctl_cache_;
#endif // ART_USE_CTL
    mutable MyMutex mutex_;
};


class CLUTApplication {
public:
    CLUTApplication(const Glib::ustring &clut_filename, const Glib::ustring &working_profile, float strength, int num_threads);
    void operator()(Imagefloat *img);
    void apply_single(int thread_id, float &r, float &g, float &b);
#ifdef __SSE2__
    void apply_vec(int thread_id, vfloat &r, vfloat &g, vfloat &b);
#endif // __SSE2__
    operator bool() const { return ok_; }

private:
    void init(int num_threads);
    void apply_tile(float *r, float *g, float *b, int istart, int jstart, int tW, int tH);
    Glib::ustring clut_filename_;
    Glib::ustring working_profile_;
    bool ok_;
    bool clut_and_working_profiles_are_same_;
    bool multiThread_;
    float strength_;
    std::shared_ptr<CLUT> hald_clut_;
    TMatrix wprof_;
    TMatrix wiprof_;
    TMatrix xyz2clut_;
    TMatrix clut2xyz_;
#ifdef __SSE2__
    vfloat v_work2xyz_[3][3] ALIGNED16;
    vfloat v_xyz2clut_[3][3] ALIGNED16;
    vfloat v_clut2xyz_[3][3] ALIGNED16;
    vfloat v_xyz2work_[3][3] ALIGNED16;
#endif // __SSE2__

#ifdef ART_USE_OCIO
    bool OCIO_init();
    void OCIO_apply(Imagefloat *img);

    OCIO::ConstCPUProcessorRcPtr ocio_processor_;
#endif // ART_USE_OCIO
#ifdef ART_USE_CTL
    bool CTL_init(int num_threads);
    void CTL_apply(Imagefloat *img);
    std::vector<Ctl::FunctionCallPtr> ctl_func_;
    int ctl_chunk_size_;
#endif // ART_USE_CTL

    void init_matrices();
    
    float conv_[3][3];
    float iconv_[3][3];
};

} // namespace rtengine

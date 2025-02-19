/* -*- C++ -*-
 *
 *  This file is part of ART.
 *
 *  Copyright 2025 Alberto Griggio <alberto.griggio@gmail.com>
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
#include "extclut.h"
#include "cJSON.h"
#include "subprocess.h"
#include "settings.h"

#include "../rtgui/multilangmgr.h"
#include "../rtgui/pathutils.h"
#include "../rtgui/options.h"

#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <glib/gstdio.h>
#include <unistd.h>

namespace rtengine {

extern const Settings *settings;

namespace {

bool add_param(std::vector<CLUTParamDescriptor> &params, cJSON *elem)
{
    params.emplace_back();
    auto &p = params.back();
    static CLUTParamType pt[4] = {
        CLUTParamType::PT_INT,
        CLUTParamType::PT_FLOAT,
        CLUTParamType::PT_BOOL,
        CLUTParamType::PT_CURVE
    };
    for (int i = 0; i < 4; ++i) {
        p.type = pt[i];
        if (p.fill_from_json(elem)) {
            return true;
        }
    }
    return false;
}


std::string get_params_json(const std::vector<CLUTParamDescriptor> &params,
                            const CLUTParamValueMap &values)
{
    cJSON *root = cJSON_CreateObject();
    const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> del_root(root, cJSON_Delete);

    for (const auto &p : params) {
        auto it = values.find(p.name);
        if (it == values.end()) {
            return "";
        }
        const auto &vv = it->second;
        auto v = vv[0];
        
        cJSON *val = nullptr;
        switch (p.type) {
        case CLUTParamType::PT_FLOAT:
        case CLUTParamType::PT_INT:
        case CLUTParamType::PT_CHOICE:
            val = cJSON_CreateNumber(v);
            break;
        case CLUTParamType::PT_BOOL:
            val = cJSON_CreateBool(bool(v));
            break;
        default:
            return ""; // TODO
        }
        if (val) {
            cJSON_AddItemToObject(root, p.name.c_str(), val);
        } else {
            return "";
        }
    }

    char *data = cJSON_PrintUnformatted(root);
    std::string ret = data;
    free(data);
    return ret;
}


std::string generate_params(const std::vector<CLUTParamDescriptor> &params,
                            const CLUTParamValueMap &values)
{
    std::string fn = "";
    std::string json = get_params_json(params, values);
    if (!json.empty()) {
        fn = Glib::build_filename(Glib::get_tmp_dir(), "ART-extclut-params-XXXXXX");
        int fd = Glib::mkstemp(fn);
        if (fd < 0) {
            return "";
        }
        close(fd);

        FILE *out = g_fopen(fn.c_str(), "wb");
        fputs(json.c_str(), out);
        fclose(out);
    }
    
    return fn;
}


Glib::ustring get_cache_key(const Glib::ustring &filename, const std::vector<CLUTParamDescriptor> &params, const CLUTParamValueMap &values)
{
    auto md5 = getMD5(filename, true);
    return filename + "\n" + md5 + "\n" + get_params_json(params, values);
}


std::string recompute_lut(const Glib::ustring &workdir, const std::vector<Glib::ustring> &argv, const std::string &params)
{
    if (params.empty()) {
        return "";
    }
    
    std::string fn = Glib::build_filename(Glib::get_tmp_dir(), "ART-extclut-params-XXXXXX");
    int fd = Glib::mkstemp(fn);
    if (fd < 0) {
        return "";
    }
    close(fd);

    try {
        std::vector<Glib::ustring> args = argv;
        args.push_back(params);
        args.push_back(fn);
        std::string sout, serr;
        if (settings->verbose > 1) {
            std::cout << "executing:";
            for (auto &a : args) {
                std::cout << " " << a;
            }
            std::cout << "\nworkdir: " << workdir << std::endl;
        }
        subprocess::exec_sync(workdir, args, true, &sout, &serr);
        if (settings->verbose > 1) {
            std::cout << "  stdout: " << sout << "\n  stderr: " << serr << std::endl;
        }
    } catch (subprocess::error &err) {
        if (settings->verbose) {
            std::cout << "  exec error: " << err.what() << std::endl;
        }
    }

    return fn;
}

} // namespace


Cache<Glib::ustring, OCIO::ConstProcessorRcPtr> ExternalLUT3D::cache_(options.clutCacheSize * 4);


ExternalLUT3D::ExternalLUT3D():
    ok_(false)
{
}


ExternalLUT3D::ExternalLUT3D(const Glib::ustring &filename):
    ok_(false),
    filename_(filename)
{
    init(filename);
}


bool ExternalLUT3D::init(const Glib::ustring &filename)
{
    ok_ = false;
    filename_ = filename;
    
    const std::unique_ptr<FILE, std::function<void (FILE*)>> file(
        g_fopen(filename.c_str(), "rb"),
        [](FILE *file) { fclose(file); }
        );

    if (!file) {
        return false;
    }

    fseek(file.get(), 0, SEEK_END);
    const long length = ftell(file.get());
    if (length <= 0) {
        return false;
    }

    std::unique_ptr<char[]> buffer(new char[length + 1]);
    fseek(file.get(), 0, SEEK_SET);
    const size_t rd = fread(buffer.get(), 1, length, file.get());
    buffer[rd] = 0;

    cJSON_Minify(buffer.get());
    const std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root_p(cJSON_Parse(buffer.get()), cJSON_Delete);
    cJSON *root = root_p.get();
    if (!root || !cJSON_IsObject(root)) {
        return false;
    }

    root = cJSON_GetObjectItem(root, "ART-lut3d");
    if (!root || !cJSON_IsObject(root)) {
        return false;
    }

    cJSON *cmd = cJSON_GetObjectItem(root, "command");
    if (!cmd || !cJSON_IsString(cmd)) {
        return false;
    }
    auto cmdline = cJSON_GetStringValue(cmd);
    workdir_ = Glib::path_get_dirname(filename);
    argv_ = subprocess::split_command_line(cmdline);

    cJSON *params = cJSON_GetObjectItem(root, "params");
    if (params) {
        if (!cJSON_IsArray(params)) {
            return false;
        }
        for (size_t i = 0, n = cJSON_GetArraySize(params); i < n; ++i) {
            if (!add_param(params_, cJSON_GetArrayItem(params, i))) {
                return false;
            }
        }
    }

    cJSON *label = cJSON_GetObjectItem(root, "label");
    if (label) {
        if (!cJSON_IsString(label)) {
            return false;
        }
        gui_name_ = cJSON_GetStringValue(label);
        if (!gui_name_.empty() && gui_name_[0] == '$') {
            auto pos = gui_name_.find(';');
            if (pos != Glib::ustring::npos) {
                auto key = gui_name_.substr(1, pos-1);
                auto dflt = gui_name_.substr(pos+1);
                auto res = M(key);
                gui_name_ = (res == key) ? dflt : res;
            } else {
                gui_name_ = M(gui_name_.c_str()+1);
            }
        }
    }
    if (gui_name_.empty()) {
        gui_name_ = removeExtension(Glib::path_get_basename(filename_));
    }

    ok_ = true;
    return true;
}


bool ExternalLUT3D::set_param_values(const CLUTParamValueMap &values)
{
    if (!ok_) {
        return false;
    }
    bool success = true;
    OCIO::ConstProcessorRcPtr lut;
    Glib::ustring key = get_cache_key(filename_, params_, values);
    bool found = cache_.get(key, lut);
    if (!found) {
        if (settings->verbose) {
            std::cout << "computing 3dlut for " << filename_ << std::endl;
        }
        std::string pn = generate_params(params_, values);
        std::string fn = recompute_lut(workdir_, argv_, pn);

        try {
            OCIO::ConstConfigRcPtr config = OCIO::Config::CreateRaw();
            OCIO::FileTransformRcPtr t = OCIO::FileTransform::Create();
            t->setSrc(fn.c_str());
            t->setInterpolation(OCIO::INTERP_BEST);
            lut = config->getProcessor(t);
            cache_.set(key, lut);
        } catch (...) {
            ok_ = false;
            success = false;
        }
        
        if (!pn.empty()) {
            g_remove(pn.c_str());
        }
        if (!fn.empty()) {
            g_remove(fn.c_str());
        }
    }
    if (lut) {
        try {
            proc_ = lut->getOptimizedCPUProcessor(OCIO::BIT_DEPTH_F32, 
                                                  OCIO::BIT_DEPTH_F32,
                                                  OCIO::OPTIMIZATION_DEFAULT);
        } catch (...) {
            ok_ = false;
            success = false;
        }
    } else {
        ok_ = success = false;
    }
    return success;
}

} // namespace rtengine

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

#include "imgiomanager.h"
#include "subprocess.h"
#include "utils.h"
#include "settings.h"
#include "imagefloat.h"
#include <iostream>
#include <glib/gstdio.h>

namespace rtengine {

extern const Settings *settings;

namespace {

ImageIOManager instance;

class S: public Glib::ustring {
public:
    explicit S(const Glib::ustring &s): Glib::ustring(s) {}
};

std::ostream &operator<<(std::ostream &out, const S &s)
{
    try {
        return out << std::string(s);
    } catch (Glib::ConvertError &e) {
        return out << s.raw();
    }
}

} // namespace


ImageIOManager *ImageIOManager::getInstance()
{
    return &instance;
}


void ImageIOManager::init(const Glib::ustring &dirname)
{
    loaders_.clear();

    if (!Glib::file_test(dirname, Glib::FILE_TEST_IS_DIR)) {
        return;
    }

    dir_ = dirname;

    try {
        Glib::Dir dir(dirname);
        std::vector<std::string> dirlist(dir.begin(), dir.end());
        std::sort(dirlist.begin(), dirlist.end());

        for (auto &filename : dirlist) {
            auto ext = getFileExtension(filename).lowercase();
            if (ext != "txt") {
                continue;
            }
            
            const Glib::ustring pth = Glib::build_filename(dirname, filename);

            if (!Glib::file_test(pth, Glib::FILE_TEST_IS_REGULAR)) {
                continue;
            }

            try {
                const Glib::ustring group = "ART ImageIO";
                Glib::KeyFile kf;
                if (!kf.load_from_file(pth)) {
                    continue;
                }

                Glib::ustring cmd;
                if (kf.has_key(group, "ReadCommand")) {
                    cmd = kf.get_string(group, "ReadCommand");
                } else {
                    continue;
                }

                std::string ext;
                if (kf.has_key(group, "Extension")) {
                    ext = kf.get_string(group, "Extension").lowercase();
                } else {
                    continue;
                }

                loaders_[ext] = cmd;

                if (settings->verbose) {
                    std::cout << "Found loader for extension \"" << ext << "\": " << S(cmd) << std::endl;
                }
            } catch (Glib::Exception &exc) {
                std::cout << "ERROR loading " << S(pth) << ": " << S(exc.what())
                          << std::endl;
            }
        }
    } catch (Glib::Exception &exc) {
        std::cout << "ERROR scanning " << S(dirname) << ": " << S(exc.what()) << std::endl;
    }

    if (settings->verbose) {
        std::cout << "Loaded " << loaders_.size() << " custom loaders"
                  << std::endl;
    }
}


bool ImageIOManager::load(const Glib::ustring &fileName, ProgressListener *plistener, ImageIO *&img, int maxw_hint, int maxh_hint)
{
    auto ext = std::string(getFileExtension(fileName).lowercase());
    auto it = loaders_.find(ext);
    if (it == loaders_.end()) {
        return false;
    }
    if (plistener) {
        plistener->setProgressStr("PROGRESSBAR_LOADING");
        plistener->setProgress(0.0);
    }

    std::string templ = Glib::build_filename(Glib::get_tmp_dir(), Glib::ustring::compose("ART-load-%1-XXXXXX", Glib::path_get_basename(fileName)));
    int fd = Glib::mkstemp(templ);
    if (fd < 0) {
        return false;
    }
    Glib::ustring outname = Glib::filename_to_utf8(templ) + ".tif";
    // int exit_status = -1;
    std::vector<Glib::ustring> argv = subprocess::split_command_line(it->second);
    argv.push_back(fileName);
    argv.push_back(outname);
    argv.push_back(std::to_string(maxw_hint));
    argv.push_back(std::to_string(maxh_hint));
    std::string out, err;
    bool ok = true;
    if (settings->verbose) {
        std::cout << "loading " << fileName << " with " << it->second << std::endl;
    }
    try {
        subprocess::exec_sync(dir_, argv, true, &out, &err);
    } catch (subprocess::error &err) {
        if (settings->verbose) {
            std::cout << "  exec error: " << err.what() << std::endl;
        }
        ok = false;
    }
    close(fd);
    g_remove(templ.c_str());
    if (settings->verbose) {
        if (!out.empty()) {
            std::cout << "  stdout: " << out << std::flush;
        }
        if (!err.empty()) {
            std::cout << "  stderr: " << err << std::flush;
        }
    }
    if (!ok) {
        if (Glib::file_test(outname, Glib::FILE_TEST_EXISTS)) {
            g_remove(outname.c_str());
        }
        return false;
    }

    IIOSampleFormat sFormat;
    IIOSampleArrangement sArrangement;
    if (ImageIO::getTIFFSampleFormat(outname, sFormat, sArrangement) != IMIO_SUCCESS) {
        if (Glib::file_test(outname, Glib::FILE_TEST_EXISTS)) {
            g_remove(outname.c_str());
        }
        return false;
    }
    
    Imagefloat *fimg = new Imagefloat();
    fimg->setProgressListener(plistener);
    fimg->setSampleFormat(sFormat);
    fimg->setSampleArrangement(sArrangement);

    bool ret = true;
    
    if (fimg->load(outname)) {
        delete fimg;
        ret = false;
    } else {
        img = fimg;
    }

    if (Glib::file_test(outname, Glib::FILE_TEST_EXISTS)) {
        g_remove(outname.c_str());
    }
    return ret;
}

} // namespace rtengine

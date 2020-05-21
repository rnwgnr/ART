/* -*- C++ -*-
*  
*  This file is part of RawTherapee.
*
*  Copyright (c) 2012 Oliver Duis <www.oliverduis.de>
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

#pragma once

#include <vector>
#include <glibmm/ustring.h>
#include <glibmm/regex.h>
#include "threadutils.h"
#include "thumbnail.h"


class UserCommand {
public:
    Glib::ustring command;
    Glib::ustring label;
    
    Glib::ustring make;
    Glib::ustring model;
    Glib::ustring extension;
    size_t min_args;
    size_t max_args;
    enum FileType {
        RAW,
        NONRAW,
        ANY
    };
    FileType filetype;
    bool match_make;
    bool match_model;
    bool match_lens;
    bool match_shutter;
    bool match_iso;
    bool match_aperture;
    bool match_focallen;

    UserCommand();
    bool matches(const std::vector<Thumbnail *> &args) const;
    void execute(const std::vector<Thumbnail *> &args) const;
};


class UserCommandStore {
public:
    static UserCommandStore *getInstance();
    void init(const Glib::ustring &dir);

    std::vector<UserCommand> getCommands(const std::vector<Thumbnail *> &sel) const;
    const std::string &dir() const { return dir_; }

private:
    MyMutex mtx_;  // covers actions
    std::string dir_;
    std::vector<UserCommand> commands_;
};


namespace ExtProg {

bool spawnCommandAsync(const Glib::ustring &cmd);
bool spawnCommandSync(const Glib::ustring &cmd);

bool openInGimp(const Glib::ustring &fileName);
bool openInPhotoshop(const Glib::ustring &fileName);
bool openInCustomEditor(const Glib::ustring &fileName);

} // namespace ExtProg

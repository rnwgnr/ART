/* -*- C++ -*-
 *  
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2011 Jan Rinze Peterzon (janrinze@gmail.com)
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

/*
 *  Declaration of flexible 2D arrays
 *
 *  Usage:
 *
 *      array2D<type> name (X-size,Y-size);
 *      array2D<type> name (X-size,Y-size,type ** data);
 *
 *      creates an array which is valid within the normal C/C++ scope "{ ... }"
 *
 *      access to elements is a simple as:
 *
 *          array2D<float> my_array (10,10); // creates 10x10 array of floats
 *          value =  my_array[3][5];
 *          my_array[4][6]=value;
 *
 *      or copy an existing 2D array
 *
 *          float ** mydata;
 *          array2D<float> my_array (10,10,mydata);
 *
 *
 *      Useful extra pointers
 *
 *          <type> ** my_array      gives access to the pointer for access with [][]
 *          <type> *  my_array      gives access to the flat stored data.
 *
 *      Advanced usage:
 *          array2D<float> my_array             ; // empty container.
 *          my_array(10,10)                     ; // resize to 10x10 array
 *          my_array(10,10,ARRAY2D_CLEAR_DATA)  ; // resize to 10x10 and clear data
 *          my_array(10,10,ARRAY2D_CLEAR_DATA|ARRAY2D_LOCK_DATA)  ; same but set a lock on changes
 *
 *          !! locked arrays cannot be resized and cannot be unlocked again !!
 */
#pragma once
#include <csignal>  // for raise()
#include <cassert>
#include <cstring>
#include <cstdio>

#include "noncopyable.h"
#include "alignedbuffer.h"

namespace rtengine {

// flags for use
constexpr unsigned int ARRAY2D_CLEAR_DATA = 2;
constexpr unsigned int ARRAY2D_BYREFERENCE = 4;
constexpr unsigned int ARRAY2D_ALIGNED = 16;


template <typename T>
class array2D: public rtengine::NonCopyable {
private:
    int width_;
    int height_;
    unsigned int flags_ : 31;
    bool owner_ : 1;
    T **ptr_;
    AlignedBuffer<T> buf_;
    
    void ar_realloc(int w, int h, int offset=0)
    {
        if (ptr_ && ((h > height_) || (4 * h < height_))) {
            delete[] ptr_;
            ptr_ = nullptr;
        }

        size_t cursz = width_ * height_;
        size_t reqsz = w * h;

        bool ok = true;
        if ((reqsz > cursz) || (reqsz < (cursz / 4))) {
            ok = buf_.resize(reqsz + offset);
        }

        if (!ok) {
            if (ptr_) {
                delete[] ptr_;
                ptr_ = nullptr;
            }
            width_ = height_ = 0;
            return;
        }

        if (ptr_ == nullptr) {
            ptr_ = new T*[h];
        }

        width_ = w;
        height_ = h;
        
        for (int i = 0; i < h; ++i) {
            ptr_[i] = buf_.data + offset + w * i;
        }

        owner_ = true;
    }
    
public:

    // use as empty declaration, resize before use!
    // very useful as a member object
    array2D():
        width_(0), height_(0), flags_(0), owner_(false), ptr_(nullptr), buf_()
    {
    }

    explicit array2D(unsigned int flgs):
        width_(0), height_(0), flags_(flgs), owner_(false), ptr_(nullptr),
        buf_(0, flgs & ARRAY2D_ALIGNED ? 16 : 0)
    {
    }

    // creator type1
    array2D(int w, int h, unsigned int flgs=0):
        buf_(0, flgs & ARRAY2D_ALIGNED ? 16 : 0)
    {
        flags_ = flgs;
        owner_ = true;

        buf_.resize(w * h);
        width_ = w;
        height_ = h;
        ptr_ = new T*[h];

        for (int i = 0; i < h; i++) {
            ptr_[i] = buf_.data + i * w;
        }

        if (flags_ & ARRAY2D_CLEAR_DATA) {
            memset(buf_.data, 0, buf_.getSize() * sizeof(T));
        }
    }

    // creator type 2
    array2D(int w, int h, T **source, unsigned int flgs=0):
        buf_(0, (flgs & ARRAY2D_ALIGNED) && !(flgs & ARRAY2D_BYREFERENCE) ? 16 : 0)
    {
        flags_ = flgs;
        owner_ = !(flags_ & ARRAY2D_BYREFERENCE);

        if (owner_) {
            buf_.resize(w * h);
        }

        width_ = w;
        height_ = h;
        ptr_ = new T*[h];

        for (int i = 0; i < h; ++i) {
            if (owner_) {
                ptr_[i] = buf_.data + i * w;

                for (int j = 0; j < w; ++j) {
                    ptr_[i][j] = source[i][j];
                }
            } else {
                ptr_[i] = source[i];
            }
        }
    }

    // destructor
    ~array2D()
    {
        if (ptr_) {
            delete[] ptr_;
        }
    }

    void fill(const T val)
    {
        for (int i = 0; i < height_; ++i) {
            for (int j = 0; j < width_; ++j) {
                ptr_[i][j] = val;
            }
        }
    }

    void free()
    {
        if (owner_) {
            buf_.resize(0);
        }

        if (ptr_) {
            delete [] ptr_;
            ptr_ = nullptr;
        }
    }

    // use with indices
    T * operator[](int index) const
    {
        assert((index >= 0) && (index < height_));
        return ptr_[index];
    }

    // use as pointer to T**
    operator T**()
    {
        return ptr_;
    }

    // use as pointer to data
    operator T*()
    {
        // only if owner this will return a valid pointer
        return owner_ ? buf_.data : nullptr;
    }


    // useful within init of parent object
    // or use as resize of 2D array
    void operator()(int w, int h, unsigned int flgs=0, int offset=0)
    {
        flags_ = flgs & ~(ARRAY2D_BYREFERENCE|ARRAY2D_ALIGNED);
        ar_realloc(w, h, offset);

        if (flags_ & ARRAY2D_CLEAR_DATA) {
            memset(buf_.data + offset, 0, w * h * sizeof(T));
        }
    }

    // import from flat data
    void operator()(int w, int h, T* copy, unsigned int flgs=0)
    {
        flags_ = flgs;

        ar_realloc(w, h);
        memcpy(buf_.data, copy, w * h * sizeof(T));
    }
    
    int width() const { return width_; }
    int height() const { return height_; }

    operator bool()
    {
        return (width_ > 0 && height_ > 0);
    }
};



template <typename T, const size_t num>
class multi_array2D {
private:
    array2D<T> list[num];

public:
    multi_array2D(int x, int y, int flags = 0, int offset = 0)
    {
        for (size_t i = 0; i < num; i++) {
            list[i](x, y, flags, (i + 1) * offset);
        }
    }

    ~multi_array2D()
    {
        //printf("trying to delete the list of array2D objects\n");
    }

    array2D<T> &operator[](int index)
    {
        assert(static_cast<size_t>(index) < num);
        return list[index];
    }
};

} // namespace rtengine

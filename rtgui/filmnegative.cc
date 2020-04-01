/*
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2019 Alberto Romei <aldrop8@gmail.com>
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
 *  along with RawTherapee.  If not, see <https://www.gnu.org/licenses/>.
 */
#include <iomanip>

#include "filmnegative.h"

#include "eventmapper.h"
#include "options.h"
#include "rtimage.h"

#include "../rtengine/procparams.h"

namespace
{

Adjuster* createExponentAdjuster(AdjusterListener* listener, const Glib::ustring& label, double minV, double maxV, double defaultVal)
{
    Adjuster* const adj = Gtk::manage(new Adjuster(label, minV, maxV, 0.001, defaultVal));
    adj->setAdjusterListener(listener);
    adj->setLogScale(6, 1, true);

    if (adj->delay < options.adjusterMaxDelay) {
        adj->delay = options.adjusterMaxDelay;
    }

    adj->show();
    return adj;
}

Glib::ustring formatBaseValues(const std::array<float, 3>& rgb)
{
    if (rgb[0] <= 0.f && rgb[1] <= 0.f && rgb[2] <= 0.f) {
        return "- - -";
    } else {
        return Glib::ustring::format(std::fixed, std::setprecision(4), rgb[0]) + " " +
               Glib::ustring::format(std::fixed, std::setprecision(4), rgb[1]) + " " +
               Glib::ustring::format(std::fixed, std::setprecision(4), rgb[2]);
    }
}

}

FilmNegative::FilmNegative() :
    FoldableToolPanel(this, "filmnegative", M("TP_FILMNEGATIVE_LABEL"), false, true, true),
    EditSubscriber(ET_OBJECTS),
    evFilmNegativeExponents(ProcEventMapper::getInstance()->newEvent(FIRST, "HISTORY_MSG_FILMNEGATIVE_VALUES")),
    evFilmNegativeEnabled(ProcEventMapper::getInstance()->newEvent(FIRST, "HISTORY_MSG_FILMNEGATIVE_ENABLED")),
    evFilmBaseValues(ProcEventMapper::getInstance()->newEvent(FIRST, "HISTORY_MSG_FILMNEGATIVE_FILMBASE")),
    // filmBaseValues({0.f, 0.f, 0.f}),
    fnp(nullptr),
    greenExp(createExponentAdjuster(this, M("TP_FILMNEGATIVE_GREEN"), 0.3, 4, 1.5)),  // master exponent (green channel)
    redRatio(createExponentAdjuster(this, M("TP_FILMNEGATIVE_RED"), 0.3, 3, (2.04 / 1.5))), // ratio of red exponent to master exponent
    blueRatio(createExponentAdjuster(this, M("TP_FILMNEGATIVE_BLUE"), 0.3, 3, (1.29 / 1.5))), // ratio of blue exponent to master exponent
    //spotgrid(Gtk::manage(new Gtk::Grid())),
    spotbutton(Gtk::manage(new Gtk::ToggleButton(M("TP_FILMNEGATIVE_PICK")))),
    // filmBaseLabel(Gtk::manage(new Gtk::Label(M("TP_FILMNEGATIVE_FILMBASE_VALUES"), Gtk::ALIGN_START))),
    // filmBaseValuesLabel(Gtk::manage(new Gtk::Label("- - -"))),
    filmBaseSpotButton(Gtk::manage(new Gtk::ToggleButton(M("TP_FILMNEGATIVE_FILMBASE_PICK"))))
{
    EvToolReset.set_action(FIRST);
    
    // spotgrid->get_style_context()->add_class("grid-spacing");
    // setExpandAlignProperties(spotgrid, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_CENTER);

    setExpandAlignProperties(spotbutton, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_CENTER);
    spotbutton->get_style_context()->add_class("independent");
    spotbutton->set_tooltip_text(M("TP_FILMNEGATIVE_GUESS_TOOLTIP"));
    //spotbutton->set_image(*Gtk::manage(new RTImage("color-picker-small.png")));

    filmBaseSpotButton->set_tooltip_text(M("TP_FILMNEGATIVE_FILMBASE_TOOLTIP"));
    // setExpandAlignProperties(filmBaseValuesLabel, false, false, Gtk::ALIGN_START, Gtk::ALIGN_CENTER);

    // TODO make spot size configurable ?

    // Gtk::Label* slab = Gtk::manage (new Gtk::Label (M("TP_WBALANCE_SIZE")));
    // setExpandAlignProperties(slab, false, false, Gtk::ALIGN_START, Gtk::ALIGN_CENTER);

    // Gtk::Grid* wbsizehelper = Gtk::manage(new Gtk::Grid());
    // wbsizehelper->set_name("WB-Size-Helper");
    // setExpandAlignProperties(wbsizehelper, false, false, Gtk::ALIGN_START, Gtk::ALIGN_CENTER);

    // spotsize = Gtk::manage (new MyComboBoxText ());
    // setExpandAlignProperties(spotsize, true, false, Gtk::ALIGN_FILL, Gtk::ALIGN_CENTER);
    // spotsize->append ("2");
    // spotsize->set_active(0);
    // spotsize->append ("4");

    //spotgrid->attach(*spotbutton, 0, 1, 1, 1);
    // spotgrid->attach (*slab, 1, 0, 1, 1);
    // spotgrid->attach (*wbsizehelper, 2, 0, 1, 1);

    pack_start(*greenExp, Gtk::PACK_SHRINK, 0);
    pack_start(*redRatio, Gtk::PACK_SHRINK, 0);
    pack_start(*blueRatio, Gtk::PACK_SHRINK, 0);
    pack_start(*spotbutton, Gtk::PACK_SHRINK, 0);

    Gtk::HSeparator* const sep = Gtk::manage(new  Gtk::HSeparator());
    sep->get_style_context()->add_class("grid-row-separator");
    pack_start(*sep, Gtk::PACK_SHRINK, 0);

    Gtk::Frame *base_frame = Gtk::manage(new Gtk::Frame(""));
    filmBaseCheck = Gtk::manage(new Gtk::CheckButton(M("TP_FILMNEGATIVE_FILMBASE_VALUES")));
    base_frame->set_label_widget(*filmBaseCheck);
    Gtk::VBox *hb = Gtk::manage(new Gtk::VBox());
    std::array<const char *, 3> icons = {
        "circle-red-small.png",
        "circle-green-small.png",
        "circle-blue-small.png"
    };
    for (int i = 0; i < 3; ++i) {
        filmBase[i] = Gtk::manage(new Adjuster("", 0, 1e6, 0.1, 0, Gtk::manage(new RTImage(icons[i]))));
        filmBase[i]->setAdjusterListener(this);
        filmBase[i]->setLogScale(100, 0);
        hb->pack_start(*filmBase[i]);
    }
    // Gtk::Grid* const fbGrid = Gtk::manage(new Gtk::Grid());
    // fbGrid->attach(*filmBaseLabel, 0, 0, 1, 1);
    // fbGrid->attach(*filmBaseValuesLabel, 1, 0, 1, 1);
    // pack_start(*fbGrid, Gtk::PACK_SHRINK, 0);

    hb->pack_start(*filmBaseSpotButton, Gtk::PACK_SHRINK, 0);
    base_frame->add(*hb);
    pack_start(*base_frame);

    spotbutton->signal_toggled().connect(sigc::mem_fun(*this, &FilmNegative::editToggled));
    // spotsize->signal_changed().connect( sigc::mem_fun(*this, &WhiteBalance::spotSizeChanged) );
    filmBaseCheck->signal_toggled().connect(sigc::mem_fun(*this, &FilmNegative::baseCheckToggled));

    filmBaseSpotButton->signal_toggled().connect(sigc::mem_fun(*this, &FilmNegative::baseSpotToggled));

    // Editing geometry; create the spot rectangle
    Rectangle* const spotRect = new Rectangle();
    spotRect->filled = false;

    visibleGeometry.push_back(spotRect);

    // Stick a dummy rectangle over the whole image in mouseOverGeometry.
    // This is to make sure the getCursor() call is fired everywhere.
    Rectangle* const imgRect = new Rectangle();
    imgRect->filled = true;

    mouseOverGeometry.push_back(imgRect);
}

FilmNegative::~FilmNegative()
{
    for (auto geometry : visibleGeometry) {
        delete geometry;
    }

    for (auto geometry : mouseOverGeometry) {
        delete geometry;
    }
}

void FilmNegative::read(const rtengine::procparams::ProcParams* pp)
{
    disableListener();

    setEnabled(pp->filmNegative.enabled);
    redRatio->setValue(pp->filmNegative.redRatio);
    greenExp->setValue(pp->filmNegative.greenExp);
    blueRatio->setValue(pp->filmNegative.blueRatio);

    filmBase[0]->setValue(pp->filmNegative.redBase);
    filmBase[1]->setValue(pp->filmNegative.greenBase);
    filmBase[2]->setValue(pp->filmNegative.blueBase);

    // If base values are not set in params, estimated values will be passed in later
    // (after processing) via FilmNegListener
    // filmBaseValuesLabel->set_text(formatBaseValues(filmBaseValues));

    filmBaseCheck->set_active(pp->filmNegative.redBase >= 0);
    baseCheckToggled();

    enableListener();
}

void FilmNegative::write(rtengine::procparams::ProcParams* pp)
{
    pp->filmNegative.redRatio = redRatio->getValue();
    pp->filmNegative.greenExp = greenExp->getValue();
    pp->filmNegative.blueRatio = blueRatio->getValue();

    pp->filmNegative.enabled = getEnabled();

    if (filmBaseCheck->get_active()) {
        pp->filmNegative.redBase = filmBase[0]->getValue();
        pp->filmNegative.greenBase = filmBase[1]->getValue();
        pp->filmNegative.blueBase = filmBase[2]->getValue();
    } else {
        pp->filmNegative.redBase = -1;
        pp->filmNegative.greenBase = -1;
        pp->filmNegative.blueBase = -1;
    }
}

void FilmNegative::setDefaults(const rtengine::procparams::ProcParams* defParams)
{
    redRatio->setValue(defParams->filmNegative.redRatio);
    greenExp->setValue(defParams->filmNegative.greenExp);
    blueRatio->setValue(defParams->filmNegative.blueRatio);

    initial_params = defParams->filmNegative;
}

void FilmNegative::adjusterChanged(Adjuster* a, double newval)
{
    if (listener && getEnabled()) {
        if (a == redRatio || a == greenExp || a == blueRatio) {
            listener->panelChanged(
                evFilmNegativeExponents,
                Glib::ustring::compose(
                    "Ref=%1\nR=%2\nB=%3",
                    greenExp->getValue(),
                    redRatio->getValue(),
                    blueRatio->getValue()
                    )
                );
        } else if (a == filmBase[0] || a == filmBase[1] || a == filmBase[2]) {
            std::array<float, 3> l;
            for (int i = 0; i < 3; ++i) {
                l[i] = filmBase[i]->getValue();
            }
            auto vs = formatBaseValues(l);
            listener->panelChanged(evFilmBaseValues, vs);
        }
    }
}

void FilmNegative::enabledChanged()
{
    if (listener) {
        if (getEnabled()) {
            listener->panelChanged(evFilmNegativeEnabled, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged(evFilmNegativeEnabled, M("GENERAL_DISABLED"));
        }
    }
}

void FilmNegative::filmBaseValuesChanged(std::array<float, 3> rgb)
{
    // filmBaseValues = rgb;
    // filmBaseValuesLabel->set_text(formatBaseValues(filmBaseValues));
    disableListener();
    for (int i = 0; i < 3; ++i) {
        filmBase[i]->setValue(rgb[i]);
    }
    enableListener();
}

void FilmNegative::setFilmNegProvider(FilmNegProvider* provider)
{
    fnp = provider;
}

void FilmNegative::setEditProvider(EditDataProvider* provider)
{
    EditSubscriber::setEditProvider(provider);
}

CursorShape FilmNegative::getCursor(int objectID)
{
    return CSSpotWB;
}

bool FilmNegative::mouseOver(int modifierKey)
{
    EditDataProvider* const provider = getEditProvider();
    Rectangle* const spotRect = static_cast<Rectangle*>(visibleGeometry.at(0));
    spotRect->setXYWH(provider->posImage.x - 16, provider->posImage.y - 16, 32, 32);

    return true;
}

bool FilmNegative::button1Pressed(int modifierKey)
{
    EditDataProvider* const provider = getEditProvider();

    EditSubscriber::action = EditSubscriber::ES_ACTION_NONE;

    if (listener) {
        if (spotbutton->get_active()) {

            refSpotCoords.push_back(provider->posImage);

            if (refSpotCoords.size() == 2) {
                // User has selected 2 reference gray spots. Calculating new exponents
                // from channel values and updating parameters.

                std::array<float, 3> newExps;

                if (fnp->getFilmNegativeExponents(refSpotCoords[0], refSpotCoords[1], newExps)) {
                    disableListener();
                    // Leaving green exponent unchanged, setting red and blue exponents based on
                    // the ratios between newly calculated exponents.
                    redRatio->setValue(newExps[0] / newExps[1]);
                    blueRatio->setValue(newExps[2] / newExps[1]);
                    enableListener();

                    if (listener && getEnabled()) {
                        listener->panelChanged(
                            evFilmNegativeExponents,
                            Glib::ustring::compose(
                                "Ref=%1\nR=%2\nB=%3",
                                greenExp->getValue(),
                                redRatio->getValue(),
                                blueRatio->getValue()
                            )
                        );
                    }
                }

                switchOffEditMode();
            }

        } else if (filmBaseSpotButton->get_active()) {

            std::array<float, 3> newBaseLev;

            if (fnp->getImageSpotValues(provider->posImage, 32, newBaseLev)) {
                disableListener();

                // filmBaseValues = newBaseLev;
                for (int i = 0; i < 3; ++i) {
                    filmBase[i]->setValue(newBaseLev[i]);
                }

                enableListener();

                const Glib::ustring vs = formatBaseValues(newBaseLev);

                // filmBaseValuesLabel->set_text(vs);

                if (listener && getEnabled()) {
                    listener->panelChanged(evFilmBaseValues, vs);
                }
            }

            switchOffEditMode();
        }
    }

    return true;
}

bool FilmNegative::button1Released()
{
    EditSubscriber::action = EditSubscriber::ES_ACTION_NONE;
    return true;
}

void FilmNegative::switchOffEditMode()
{
    refSpotCoords.clear();
    unsubscribe();
    spotbutton->set_active(false);
    filmBaseSpotButton->set_active(false);
}

void FilmNegative::editToggled()
{
    if (spotbutton->get_active()) {

        filmBaseSpotButton->set_active(false);
        refSpotCoords.clear();

        subscribe();

        int w, h;
        getEditProvider()->getImageSize(w, h);

        // Stick a dummy rectangle over the whole image in mouseOverGeometry.
        // This is to make sure the getCursor() call is fired everywhere.
        Rectangle* const imgRect = static_cast<Rectangle*>(mouseOverGeometry.at(0));
        imgRect->setXYWH(0, 0, w, h);
    } else {
        refSpotCoords.clear();
        unsubscribe();
    }
}

void FilmNegative::baseSpotToggled()
{
    if (filmBaseSpotButton->get_active()) {

        spotbutton->set_active(false);
        refSpotCoords.clear();

        subscribe();

        int w, h;
        getEditProvider()->getImageSize(w, h);

        // Stick a dummy rectangle over the whole image in mouseOverGeometry.
        // This is to make sure the getCursor() call is fired everywhere.
        Rectangle* const imgRect = static_cast<Rectangle*>(mouseOverGeometry.at(0));
        imgRect->setXYWH(0, 0, w, h);
    } else {
        refSpotCoords.clear();
        unsubscribe();
    }
}


void FilmNegative::baseCheckToggled()
{
    bool en = filmBaseCheck->get_active();
    for (int i = 0; i < 3; ++i) {
        filmBase[i]->set_sensitive(en);
    }
    filmBaseSpotButton->set_sensitive(en);

    if (listener && getEnabled()) {
        // std::array<float, 3> vals = { -1.f, -1.f, -1.f };
        // if (en) {
        //     for (int i = 0; i < 3; ++i) {
        //         vals[i] = filmBase[i]->getValue();
        //     }
        // }
        listener->panelChanged(evFilmBaseValues, en ? M("GENERAL_ENABLED") : M("GENERAL_DISABLED"));
    }
}


void FilmNegative::toolReset(bool to_initial)
{
    rtengine::procparams::ProcParams pp;
    if (to_initial) {
        pp.filmNegative = initial_params;
    }
    pp.filmNegative.enabled = getEnabled();
    read(&pp);
}

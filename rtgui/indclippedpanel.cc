/*
 *  This file is part of RawTherapee.
 *
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
#include "indclippedpanel.h"
#include "options.h"
#include "multilangmgr.h"
#include "imagearea.h"
#include "rtimage.h"

IndicateClippedPanel::IndicateClippedPanel (ImageArea* ia) : imageArea(ia)
{

    iFon  = new RTImage ("focusscreen-on.png");
    iFoff = new RTImage ("focusscreen-off.png");

    // for previewSharpMask, needs to be replaced with different icons
    iSon  = new RTImage ("contrastmask-on.png");
    iSoff = new RTImage ("contrastmask-off.png");

    falseColorsOff = new RTImage("false-colors-off.png");
    falseColorsOn = new RTImage("false-colors.png");

    previewFocusMask = Gtk::manage (new Gtk::ToggleButton ());
    previewFocusMask->set_relief(Gtk::RELIEF_NONE);
    previewFocusMask->set_tooltip_markup (M("MAIN_TOOLTIP_PREVIEWFOCUSMASK"));
    previewFocusMask->set_image(*iFoff);

    previewSharpMask = Gtk::manage (new Gtk::ToggleButton ());
    previewSharpMask->set_relief(Gtk::RELIEF_NONE);
    previewSharpMask->set_tooltip_markup (M("MAIN_TOOLTIP_PREVIEWSHARPMASK"));
    previewSharpMask->set_image(*iSoff);

    falseColors = Gtk::manage(new Gtk::ToggleButton());
    falseColors->set_relief(Gtk::RELIEF_NONE);
    const std::array<const char *, 12> colors = {
        "#FFFFFF",
        "#0000FF",
        "#2290FF",
        "#4B4B4B",
        "#FF11FC",
        "#7B7B7B",
        "#00FF00",
        "#ADADAD",
        "#AEAE00",
        "#FFFF00",
        "#FF7F00",
        "#FF0000"
    };
    Glib::ustring color_shadows;
    Glib::ustring color_midtones;
    Glib::ustring color_skin;
    Glib::ustring color_highlights;
    for (int i = 0; i < 4; ++i) {
        color_shadows += Glib::ustring("<span foreground=\"") + colors[i] + "\">&#9724;</span>";
    }
    for (int i = 4; i < 7; ++i) {
        color_midtones += Glib::ustring("<span foreground=\"") + colors[i] + "\">&#9724;</span>";
    }
    color_skin += Glib::ustring("<span foreground=\"") + colors[7] + "\">&#9724;</span>";
    for (int i = 8; i < 12; ++i) {
        color_highlights += Glib::ustring("<span foreground=\"") + colors[i] + "\">&#9724;</span>";
    }    
    falseColors->set_tooltip_markup(Glib::ustring::compose(M("MAIN_TOOLTIP_FALSECOLORS"), color_shadows, color_midtones, color_skin, color_highlights));
    falseColors->set_image(*falseColorsOff);
    
    Glib::ustring tt;

    indClippedH = Gtk::manage (new Gtk::ToggleButton ());
    indClippedH->set_relief(Gtk::RELIEF_NONE);
    indClippedH->add (*Gtk::manage (new RTImage ("warning-highlights.png")));
    tt = Glib::ustring::compose("%1\n%2 = %3", M("MAIN_TOOLTIP_INDCLIPPEDH"), M("MAIN_TOOLTIP_THRESHOLD"), options.highlightThreshold);

    if (tt.find("&lt;") == Glib::ustring::npos && tt.find("&gt;") == Glib::ustring::npos) {
        indClippedH->set_tooltip_text (tt);
    } else {
        indClippedH->set_tooltip_markup (tt);
    }

    indClippedS = Gtk::manage (new Gtk::ToggleButton ());
    indClippedS->set_relief(Gtk::RELIEF_NONE);
    indClippedS->add (*Gtk::manage (new RTImage ("warning-shadows.png")));
    tt = Glib::ustring::compose("%1\n%2 = %3", M("MAIN_TOOLTIP_INDCLIPPEDS"), M("MAIN_TOOLTIP_THRESHOLD"), options.shadowThreshold);

    if (tt.find("&lt;") == Glib::ustring::npos && tt.find("&gt;") == Glib::ustring::npos) {
        indClippedS->set_tooltip_text (tt);
    } else {
        indClippedS->set_tooltip_markup (tt);
    }

    falseColors->set_active(false);
    previewFocusMask->set_active (false);
    previewSharpMask->set_active (false);
    indClippedH->set_active (options.showClippedHighlights);
    indClippedS->set_active (options.showClippedShadows);

    pack_start(*falseColors, Gtk::PACK_SHRINK, 0);
    pack_start (*previewFocusMask, Gtk::PACK_SHRINK, 0);
    pack_start (*previewSharpMask, Gtk::PACK_SHRINK, 0);
    pack_start (*indClippedS, Gtk::PACK_SHRINK, 0);
    pack_start (*indClippedH, Gtk::PACK_SHRINK, 0);

    connFalseColors = falseColors->signal_toggled().connect(sigc::bind(sigc::mem_fun(*this, &IndicateClippedPanel::buttonToggled), falseColors));
    connSharpMask = previewSharpMask->signal_toggled().connect( sigc::bind(sigc::mem_fun(*this, &IndicateClippedPanel::buttonToggled), previewSharpMask) );
    connFocusMask = previewFocusMask->signal_toggled().connect( sigc::bind(sigc::mem_fun(*this, &IndicateClippedPanel::buttonToggled), previewFocusMask) );
    connClippedS = indClippedS->signal_toggled().connect( sigc::bind(sigc::mem_fun(*this, &IndicateClippedPanel::buttonToggled), indClippedS) );
    connClippedH = indClippedH->signal_toggled().connect( sigc::bind(sigc::mem_fun(*this, &IndicateClippedPanel::buttonToggled), indClippedH) );

    show_all ();
}

// inverts a toggle programmatically
void IndicateClippedPanel::toggleClipped (bool highlights)
{
    if (highlights) {
        indClippedH->set_active(!indClippedH->get_active());
    } else {
        indClippedS->set_active(!indClippedS->get_active());
    }
}

void IndicateClippedPanel::toggleFocusMask ()
{
    previewFocusMask->set_active(!previewFocusMask->get_active());
}

void IndicateClippedPanel::silentlyDisableSharpMask ()
{
    ConnectionBlocker conBlocker(connSharpMask);
    previewSharpMask->set_active(false);
    previewSharpMask->set_image(*iSoff);

}

void IndicateClippedPanel::toggleSharpMask ()
{
    previewSharpMask->set_active(!previewSharpMask->get_active());
}


void IndicateClippedPanel::toggleFalseColors()
{
    falseColors->set_active(!falseColors->get_active());
}


void IndicateClippedPanel::buttonToggled (Gtk::ToggleButton* tb)
{
    ConnectionBlocker b1(connFalseColors);
    ConnectionBlocker b2(connFocusMask);
    ConnectionBlocker b3(connSharpMask);
    ConnectionBlocker b4(connClippedS);
    ConnectionBlocker b5(connClippedH);

    bool shm = previewSharpMask->get_active();

    if (tb == previewFocusMask) {
        if (indClippedS->get_active()) {
            indClippedS->set_active(false);
        }
        if (indClippedH->get_active()) {
            indClippedH->set_active(false);
        }
        previewSharpMask->set_active(false);
        falseColors->set_active(false);
    } else if (tb == previewSharpMask) {
        if (indClippedS->get_active()) {
            indClippedS->set_active(false);
        }
        if (indClippedH->get_active()) {
            indClippedH->set_active(false);
        }
        previewFocusMask->set_active(false);
        falseColors->set_active(false);
    } else if (tb == falseColors) {
        indClippedH->set_active(false);
        indClippedS->set_active(false);
        previewFocusMask->set_active(false);
        previewSharpMask->set_active(false);
    } else {
        previewFocusMask->set_active(false);
        previewSharpMask->set_active(false);
        falseColors->set_active(false);
    }

    if (tb == previewSharpMask || previewSharpMask->get_active() != shm) {
        imageArea->sharpMaskSelected(previewSharpMask->get_active());
    }
    previewFocusMask->set_image(previewFocusMask->get_active() ? *iFon : *iFoff);
    previewSharpMask->set_image(previewSharpMask->get_active() ? *iSon : *iSoff);
    falseColors->set_image(falseColors->get_active() ? *falseColorsOn : *falseColorsOff);

    // connFocusMask.block(false);
    // connSharpMask.block(false);
    // connClippedS.block(false);
    // connClippedH.block(false);

    imageArea->queue_draw ();

    // this will redraw the linked Before image area
    // which is set when before/after view is enabled
    if (imageArea->iLinkedImageArea != nullptr) {
        imageArea->iLinkedImageArea->queue_draw ();
    }
}

IndicateClippedPanel::~IndicateClippedPanel ()
{
    delete iFon;
    delete iFoff;
    delete iSon;
    delete iSoff;
    delete falseColorsOn;
    delete falseColorsOff;
}

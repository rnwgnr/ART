/** -*- C++ -*-
 *  
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2018 Alberto Griggio <alberto.griggio@gmail.com>
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
#include "smoothing.h"
#include "eventmapper.h"
#include <iomanip>
#include <cmath>

using namespace rtengine;
using namespace rtengine::procparams;


//-----------------------------------------------------------------------------
// SmoothingMasksContentProvider
//-----------------------------------------------------------------------------

class SmoothingMasksContentProvider: public LabMasksContentProvider {
public:
    SmoothingMasksContentProvider(Smoothing *parent):
        parent_(parent)
    {
    }

    Gtk::Widget *getWidget() override
    {
        return parent_->box;
    }

    void getEvents(rtengine::ProcEvent &mask_list, rtengine::ProcEvent &parametric_mask, rtengine::ProcEvent &h_mask, rtengine::ProcEvent &c_mask, rtengine::ProcEvent &l_mask, rtengine::ProcEvent &blur, rtengine::ProcEvent &show, rtengine::ProcEvent &area_mask, rtengine::ProcEvent &deltaE_mask, rtengine::ProcEvent &contrastThreshold_mask, rtengine::ProcEvent &drawn_mask) override
    {
        mask_list = parent_->EvList;
        parametric_mask = parent_->EvParametricMask;
        h_mask = parent_->EvHueMask;
        c_mask = parent_->EvChromaticityMask;
        l_mask = parent_->EvLightnessMask;
        blur = parent_->EvMaskBlur;
        show = parent_->EvShowMask;
        area_mask = parent_->EvAreaMask;
        deltaE_mask = parent_->EvDeltaEMask;
        contrastThreshold_mask = parent_->EvContrastThresholdMask;
        drawn_mask = parent_->EvDrawnMask;
    }

    ToolPanelListener *listener() override
    {
        if (parent_->getEnabled()) {
            return parent_->listener;
        }
        return nullptr;
    }

    void selectionChanging(int idx) override
    {
        parent_->regionGet(idx);
    }

    void selectionChanged(int idx) override
    {
        parent_->regionShow(idx);
    }

    bool addPressed() override
    {
        parent_->data.push_back(SmoothingParams::Region());
        return true;
    }

    bool removePressed(int idx) override
    {
        parent_->data.erase(parent_->data.begin() + idx);
        return true;
    }
    
    bool copyPressed(int idx) override
    {
        parent_->data.push_back(parent_->data[idx]);
        return true;
    }

    bool resetPressed(int idx) override
    {
        parent_->data[idx] = SmoothingParams::Region();
        //parent_->labMasks->setMasks({ Mask() }, -1);
        return true;
    }
    
    bool moveUpPressed(int idx) override
    {
        auto r = parent_->data[idx];
        parent_->data.erase(parent_->data.begin() + idx);
        --idx;
        parent_->data.insert(parent_->data.begin() + idx, r);
        return true;
    }
    
    bool moveDownPressed(int idx) override
    {
        auto r = parent_->data[idx];
        parent_->data.erase(parent_->data.begin() + idx);
        ++idx;
        parent_->data.insert(parent_->data.begin() + idx, r);
        return true;
    }

    int getColumnCount() override
    {
        return 1;
    }
    
    Glib::ustring getColumnHeader(int col) override
    {
        return M("TP_SMOOTHING_LIST_TITLE");
    }
    
    Glib::ustring getColumnContent(int col, int row) override
    {
        auto &r = parent_->data[row];

        return Glib::ustring::compose(
            "%1 %2 %4 [%3]", r.radius, r.epsilon, 
            r.channel == SmoothingParams::Region::Channel::LUMINANCE ? "L" :
            (r.channel == SmoothingParams::Region::Channel::CHROMINANCE ? "C" : "RGB"), r.iterations);
    }

    void getEditIDs(EditUniqueID &hcurve, EditUniqueID &ccurve, EditUniqueID &lcurve, EditUniqueID &deltaE) override
    {
        hcurve = EUID_LabMasks_H3;
        ccurve = EUID_LabMasks_C3;
        lcurve = EUID_LabMasks_L3;
        deltaE = EUID_LabMasks_DE3;
    }

private:
    Smoothing *parent_;
};


//-----------------------------------------------------------------------------
// Smoothing
//-----------------------------------------------------------------------------

Smoothing::Smoothing(): FoldableToolPanel(this, "smoothing", M("TP_SMOOTHING_LABEL"), false, true, true)
{
    auto m = ProcEventMapper::getInstance();
    auto EVENT = LUMINANCECURVE | M_LUMACURVE;
    EvEnabled = m->newEvent(EVENT, "HISTORY_MSG_SMOOTHING_ENABLED");
    EvChannel = m->newEvent(EVENT, "HISTORY_MSG_SMOOTHING_CHANNEL");
    EvRadius = m->newEvent(EVENT, "HISTORY_MSG_SMOOTHING_RADIUS");
    EvEpsilon = m->newEvent(EVENT, "HISTORY_MSG_SMOOTHING_EPSILON");
    EvIterations = m->newEvent(EVENT, "HISTORY_MSG_SMOOTHING_ITERATIONS");
    EvMode = m->newEvent(EVENT, "HISTORY_MSG_SMOOTHING_MODE");
    EvSigma = m->newEvent(EVENT, "HISTORY_MSG_SMOOTHING_SIGMA");
    EvFalloff = m->newEvent(EVENT, "HISTORY_MSG_SMOOTHING_FALLOFF");
    EvNLStrength = m->newEvent(EVENT, "HISTORY_MSG_SMOOTHING_NLSTRENGTH");
    EvNLDetail = m->newEvent(EVENT, "HISTORY_MSG_SMOOTHING_NLDETAIL");

    EvList = m->newEvent(EVENT, "HISTORY_MSG_SMOOTHING_LIST");
    EvParametricMask = m->newEvent(EVENT, "HISTORY_MSG_SMOOTHING_PARAMETRICMASK");
    EvHueMask = m->newEvent(EVENT, "HISTORY_MSG_SMOOTHING_HUEMASK");
    EvChromaticityMask = m->newEvent(EVENT, "HISTORY_MSG_SMOOTHING_CHROMATICITYMASK");
    EvLightnessMask = m->newEvent(EVENT, "HISTORY_MSG_SMOOTHING_LIGHTNESSMASK");
    EvMaskBlur = m->newEvent(EVENT, "HISTORY_MSG_SMOOTHING_MASKBLUR");
    EvShowMask = m->newEvent(EVENT, "HISTORY_MSG_SMOOTHING_SHOWMASK");
    EvAreaMask = m->newEvent(EVENT, "HISTORY_MSG_SMOOTHING_AREAMASK");
    EvDeltaEMask = m->newEvent(EVENT, "HISTORY_MSG_SMOOTHING_DELTAEMASK");
    EvContrastThresholdMask = m->newEvent(EVENT, "HISTORY_MSG_SMOOTHING_CONTRASTTHRESHOLDMASK");
    EvDrawnMask = m->newEvent(EVENT, "HISTORY_MSG_SMOOTHING_DRAWNMASK");

    EvToolEnabled.set_action(EVENT);
    EvToolReset.set_action(EVENT);

    box = Gtk::manage(new Gtk::VBox());

    Gtk::HBox *hb = Gtk::manage(new Gtk::HBox());
    hb->pack_start(*Gtk::manage(new Gtk::Label(M("TP_SMOOTHING_CHANNEL") + ":")), Gtk::PACK_SHRINK, 1);
    channel = Gtk::manage(new MyComboBoxText());
    channel->append(M("TP_SMOOTHING_CHANNEL_L"));
    channel->append(M("TP_SMOOTHING_CHANNEL_C"));
    channel->append(M("TP_SMOOTHING_CHANNEL_RGB"));
    channel->set_active(2);
    channel->signal_changed().connect(sigc::mem_fun(*this, &Smoothing::channelChanged));
    hb->pack_start(*channel, Gtk::PACK_EXPAND_WIDGET, 1);
    box->pack_start(*hb, Gtk::PACK_SHRINK, 1);

    mode = Gtk::manage(new MyComboBoxText());
    mode->append(M("TP_SMOOTHING_MODE_GUIDED"));
    mode->append(M("TP_SMOOTHING_MODE_GAUSSIAN"));
    mode->append(M("TP_SMOOTHING_MODE_GAUSSIAN_GLOW"));
    mode->append(M("TP_SMOOTHING_MODE_NLMEANS"));
    mode->set_active(0);
    mode->signal_changed().connect(sigc::mem_fun(*this, &Smoothing::modeChanged));
    hb = Gtk::manage(new Gtk::HBox());
    hb->pack_start(*Gtk::manage(new Gtk::Label(M("TP_SMOOTHING_MODE") + ":")), Gtk::PACK_SHRINK, 1);
    hb->pack_start(*mode, Gtk::PACK_EXPAND_WIDGET, 1);
    box->pack_start(*hb, Gtk::PACK_SHRINK, 1);

    guided_box = Gtk::manage(new Gtk::VBox());
    gaussian_box = Gtk::manage(new Gtk::VBox());
    
    radius = Gtk::manage(new Adjuster(M("TP_SMOOTHING_RADIUS"), 0, 1000, 1, 0));
    radius->setLogScale(100, 0);
    radius->setAdjusterListener(this);
    guided_box->pack_start(*radius);
    
    epsilon = Gtk::manage(new Adjuster(M("TP_SMOOTHING_EPSILON"), -10, 10, 0.1, 0));
    epsilon->setAdjusterListener(this);
    guided_box->pack_start(*epsilon);

    sigma = Gtk::manage(new Adjuster(M("TP_SMOOTHING_SIGMA"), 0, 500, 0.01, 0));
    sigma->setLogScale(100, 0);
    sigma->setAdjusterListener(this);
    gaussian_box->pack_start(*sigma);

    nl_box = Gtk::manage(new Gtk::VBox());
    nlstrength = Gtk::manage(new Adjuster(M("TP_SMOOTHING_NLSTRENGTH"), 0, 100, 1, 0));
    nldetail = Gtk::manage(new Adjuster(M("TP_SMOOTHING_NLDETAIL"), 1, 100, 1, 50));
    nldetail->setAdjusterListener(this);
    nlstrength->setAdjusterListener(this);
    nl_box->pack_start(*nlstrength);
    nl_box->pack_start(*nldetail);
    
    box->pack_start(*guided_box);
    box->pack_start(*gaussian_box);
    box->pack_start(*nl_box);

    iterations = Gtk::manage(new Adjuster(M("TP_SMOOTHING_ITERATIONS"), 1, 10, 1, 1));
    iterations->setAdjusterListener(this);

    box->pack_start(*iterations);

    falloff = Gtk::manage(new Adjuster(M("TP_SMOOTHING_FALLOFF"), 0.5, 2, 0.01, 1));
    falloff->setLogScale(2, 1, true);
    falloff->setAdjusterListener(this);
    box->pack_start(*falloff);
    
    radius->delay = options.adjusterMaxDelay;
    epsilon->delay = options.adjusterMaxDelay;
    iterations->delay = options.adjusterMaxDelay;
    sigma->delay = options.adjusterMaxDelay;
    falloff->delay = options.adjusterMaxDelay;
    nlstrength->delay = options.adjusterMaxDelay;
    nldetail->delay = options.adjusterMaxDelay;

    labMasksContentProvider.reset(new SmoothingMasksContentProvider(this));
    labMasks = Gtk::manage(new LabMasksPanel(labMasksContentProvider.get()));
    pack_start(*labMasks, Gtk::PACK_EXPAND_WIDGET, 4);   

    show_all_children();
}


void Smoothing::read(const ProcParams *pp)
{
    disableListener();

    setEnabled(pp->smoothing.enabled);
    data = pp->smoothing.regions;
    auto m = pp->smoothing.labmasks;
    if (data.empty()) {
        data.emplace_back(rtengine::procparams::SmoothingParams::Region());
        m.emplace_back(rtengine::procparams::Mask());
    }
    labMasks->setMasks(m, pp->smoothing.showMask);
    modeChanged();

    enableListener();
}


void Smoothing::write(ProcParams *pp)
{
    pp->smoothing.enabled = getEnabled();

    regionGet(labMasks->getSelected());
    pp->smoothing.regions = data;

    labMasks->getMasks(pp->smoothing.labmasks, pp->smoothing.showMask);
    assert(pp->smoothing.regions.size() == pp->smoothing.labmasks.size());

    labMasks->updateSelected();
}

void Smoothing::setDefaults(const ProcParams *defParams)
{
    radius->setDefault(defParams->smoothing.regions[0].radius);
    epsilon->setDefault(defParams->smoothing.regions[0].epsilon);
    iterations->setDefault(defParams->smoothing.regions[0].iterations);
    sigma->setDefault(defParams->smoothing.regions[0].sigma);
    falloff->setDefault(defParams->smoothing.regions[0].falloff);
    nlstrength->setDefault(defParams->smoothing.regions[0].nlstrength);
    nldetail->setDefault(defParams->smoothing.regions[0].nldetail);

    initial_params = defParams->smoothing;
}


void Smoothing::adjusterChanged(Adjuster* a, double newval)
{
    if (listener && getEnabled()) {
        labMasks->setEdited(true);
        
        if (a == radius) {
            listener->panelChanged(EvRadius, a->getTextValue());
        } else if (a == epsilon) {
            listener->panelChanged(EvEpsilon, a->getTextValue());
        } else if (a == iterations) {
            listener->panelChanged(EvIterations, a->getTextValue());
        } else if (a == sigma) {
            listener->panelChanged(EvSigma, a->getTextValue());
        } else if (a == falloff) {
            listener->panelChanged(EvFalloff, a->getTextValue());
        } else if (a == nlstrength) {
            listener->panelChanged(EvNLStrength, a->getTextValue());
        } else if (a == nldetail) {
            listener->panelChanged(EvNLDetail, a->getTextValue());
        }
    }
}


void Smoothing::enabledChanged ()
{
    if (listener) {
        if (get_inconsistent()) {
            listener->panelChanged(EvEnabled, M("GENERAL_UNCHANGED"));
        } else if (getEnabled()) {
            listener->panelChanged(EvEnabled, M("GENERAL_ENABLED"));
        } else {
            listener->panelChanged(EvEnabled, M("GENERAL_DISABLED"));
        }
    }

    if (listener && !getEnabled()) {
        labMasks->switchOffEditMode();
    }
}


void Smoothing::setEditProvider(EditDataProvider *provider)
{
    labMasks->setEditProvider(provider);
}


void Smoothing::procParamsChanged(
    const rtengine::procparams::ProcParams* params,
    const rtengine::ProcEvent& ev,
    const Glib::ustring& descr,
    const ParamsEdited* paramsEdited)
{
}


void Smoothing::updateGeometry(int fw, int fh)
{
    labMasks->updateGeometry(fw, fh);
}


void Smoothing::regionGet(int idx)
{
    if (idx < 0 || size_t(idx) >= data.size()) {
        return;
    }
    
    auto &r = data[idx];
    r.mode = SmoothingParams::Region::Mode(mode->get_active_row_number());
    r.channel = SmoothingParams::Region::Channel(channel->get_active_row_number());
    r.radius = radius->getValue();
    r.epsilon = epsilon->getValue();
    r.iterations = iterations->getValue();
    r.sigma = sigma->getValue();
    r.falloff = falloff->getValue();
    r.nlstrength = nlstrength->getValue();
    r.nldetail = nldetail->getValue();
}


void Smoothing::regionShow(int idx)
{
    const bool disable = listener;
    if (disable) {
        disableListener();
    }

    auto &r = data[idx];
    mode->set_active(int(r.mode));
    channel->set_active(int(r.channel));
    radius->setValue(r.radius);
    epsilon->setValue(r.epsilon);
    iterations->setValue(r.iterations);
    sigma->setValue(r.sigma);
    falloff->setValue(r.falloff);
    nlstrength->setValue(r.nlstrength);
    nldetail->setValue(r.nldetail);
    
    if (disable) {
        enableListener();
    }
}


void Smoothing::channelChanged()
{
    if (listener && getEnabled()) {
        listener->panelChanged(EvChannel, channel->get_active_text());
    }
}


void Smoothing::modeChanged()
{
    int r = mode->get_active_row_number();
    nl_box->hide();
    gaussian_box->hide();
    guided_box->hide();
    if (r == 0) {
        guided_box->show();
    } else if (r == 1 || r == 2) {
        gaussian_box->show();
    } else {
        nl_box->show();
    }
    falloff->set_visible(mode->get_active_row_number() == 2);
    if (listener && getEnabled()) {
        listener->panelChanged(EvMode, mode->get_active_text());
    }
}


void Smoothing::setAreaDrawListener(AreaDrawListener *l)
{
    labMasks->setAreaDrawListener(l);
}


void Smoothing::setDeltaEColorProvider(DeltaEColorProvider *p)
{
    labMasks->setDeltaEColorProvider(p);
}


void Smoothing::toolReset(bool to_initial)
{
    ProcParams pp;
    if (to_initial) {
        pp.smoothing = initial_params;
    }
    pp.smoothing.enabled = getEnabled();
    read(&pp);
}

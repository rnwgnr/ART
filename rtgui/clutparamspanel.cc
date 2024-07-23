/* -*- C++ -*-
 *
 *  This file is part of ART.
 *
 *  Copyright 2023 Alberto Griggio <alberto.griggio@gmail.com>
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

#include "clutparamspanel.h"
#include "guiutils.h"
#include "multilangmgr.h"
#include "curveeditor.h"
#include "curveeditorgroup.h"

namespace {

class CLUTParamsCurveEditorGroup: public CurveEditorGroup {
public:
    CLUTParamsCurveEditorGroup(Glib::ustring &curveDir, Glib::ustring groupLabel="", float curvesRatio=1.f):
        CurveEditorGroup(curveDir, groupLabel, curvesRatio) {}

    const std::vector<CurveEditor *> getCurveEditors() const { return curveEditors; }
};

} // namespace


CLUTParamsPanel::CLUTParamsPanel():
    sig_blocked_(false)
{
    set_orientation(Gtk::ORIENTATION_VERTICAL);
}


Gtk::SizeRequestMode CLUTParamsPanel::get_request_mode_vfunc() const
{
    return Gtk::SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}


void CLUTParamsPanel::setParams(const std::vector<rtengine::CLUTParamDescriptor> &params)
{
    widgets_.clear();
    for (auto c : get_children()) {
        remove(*c);
    }

    params_ = params;

    if (params.empty()) {
        return;
    }

    const auto lbl =
        [](const Glib::ustring &l) -> Glib::ustring
        {
            if (!l.empty() && l[0] == '$') {
                auto pos = l.find(';');
                if (pos != Glib::ustring::npos) {
                    auto key = l.substr(1, pos-1);
                    auto dflt = l.substr(pos+1);
                    auto res = M(key);
                    return (res == key) ? dflt : res;
                } else {
                    return M(l.c_str()+1);
                }
            } else {
                return l;
            }
        };

    Gtk::VBox *vb = this;

    std::vector<std::pair<Glib::ustring, Gtk::Box *>> groups;

    const auto reset =
        [this]() -> void
        {
            setValue({});
            emit_signal();
        };
    Gtk::Button *r = Gtk::manage(new Gtk::Button());
    r->set_tooltip_markup(M("ADJUSTER_RESET_TO_DEFAULT"));
    r->add(*Gtk::manage(new RTImage("undo-small.png", "redo-small.png")));
    r->signal_clicked().connect(sigc::slot<void>(reset));
    setExpandAlignProperties(r, false, false, Gtk::ALIGN_END, Gtk::ALIGN_CENTER);
    r->set_relief(Gtk::RELIEF_NONE);
    r->get_style_context()->add_class(GTK_STYLE_CLASS_FLAT);
    r->set_can_focus(false);
    r->set_size_request(-1, 20);
    Gtk::HBox *hb = Gtk::manage(new Gtk::HBox());
    auto sep = Gtk::manage(new Gtk::HSeparator());
    sep->set_vexpand(false);
    sep->set_valign(Gtk::ALIGN_CENTER);
    hb->pack_start(*sep);
    hb->pack_start(*r, Gtk::PACK_SHRINK, 2);
    vb->pack_start(*hb);
    
    for (auto &d : params) {
        Gtk::Widget *w = nullptr;
        Gtk::Box *box = nullptr;
        if (!d.gui_group.empty()) {
            for (auto &p : groups) {
                if (p.first == d.gui_group) {
                    box = p.second;
                    break;
                }
            }
            if (!box) {
                Gtk::Frame *e = Gtk::manage(new Gtk::Frame(lbl(d.gui_group)));
                Gtk::VBox *tb = Gtk::manage(new Gtk::VBox());
                e->set_name("ExpanderBox2");
                e->add(*tb);
                vb->pack_start(*e);
                box = tb;
                groups.emplace_back(d.gui_group, tb);
            }
        }
        if (!box) {
            box = vb;
        }
        switch (d.type) {
        case rtengine::CLUTParamType::PT_BOOL: {
            Gtk::CheckButton *b = Gtk::manage(new Gtk::CheckButton(lbl(d.gui_name)));
            b->signal_toggled().connect(sigc::mem_fun(*this, &CLUTParamsPanel::emit_signal));
            w = b;
            box->pack_start(*b);
        }   break;
        case rtengine::CLUTParamType::PT_CHOICE: {
            MyComboBoxText *c = Gtk::manage(new MyComboBoxText());
            for (auto l : d.choices) {
                c->append(lbl(l));
            }
            Gtk::HBox *hb = Gtk::manage(new Gtk::HBox());
            hb->pack_start(*Gtk::manage(new Gtk::Label(lbl(d.gui_name) + ": ")), Gtk::PACK_SHRINK);
            hb->pack_start(*c);
            c->signal_changed().connect(sigc::mem_fun(*this, &CLUTParamsPanel::emit_signal));
            w = c;
            box->pack_start(*hb);
        }   break;
        case rtengine::CLUTParamType::PT_CURVE:
        case rtengine::CLUTParamType::PT_FLATCURVE:
        case rtengine::CLUTParamType::PT_FLATCURVE_PERIODIC: {
            auto grp = Gtk::manage(new CLUTParamsCurveEditorGroup(options.lastColorToningCurvesDir, lbl(d.gui_name)));
            grp->setCurveListener(this);
            CurveEditor *ce = nullptr;
            if (d.type == rtengine::CLUTParamType::PT_CURVE) {
                ce = grp->addCurve(CT_Diagonal, "");
                static_cast<DiagonalCurveEditor *>(ce)->setResetCurve(DiagonalCurveType(d.value_default[0]), d.value_default);
            } else {
                ce = grp->addCurve(CT_Flat, "", nullptr, false, d.type == rtengine::CLUTParamType::PT_FLATCURVE_PERIODIC);
                static_cast<FlatCurveEditor *>(ce)->setResetCurve(FlatCurveType(d.value_default[0]), d.value_default);
            }
            grp->curveListComplete();
            box->pack_start(*grp);
            w = grp;
        }   break;
        case rtengine::CLUTParamType::PT_INT:
        case rtengine::CLUTParamType::PT_FLOAT:
        default: {
            Adjuster *a = Gtk::manage(new Adjuster(lbl(d.gui_name), d.value_min, d.value_max, d.gui_step, d.value_default.empty() ? 0.0 : d.value_default[0]));
            a->setAdjusterListener(this);
            box->pack_start(*a);
            w = a;
        }   break;
        }
        if (!d.gui_tooltip.empty()) {
            w->set_tooltip_markup(d.gui_tooltip);
        }
        widgets_.push_back(w);
    }
    show_all_children();
}


rtengine::CLUTParamValueMap CLUTParamsPanel::getValue() const
{
    rtengine::CLUTParamValueMap values;
    
    for (size_t i = 0; i < params_.size(); ++i) {
        auto w = widgets_[i];
        auto &d = params_[i];

        std::vector<double> v = { 0 };
        
        switch (d.type) {
        case rtengine::CLUTParamType::PT_BOOL: 
            v[0] = static_cast<Gtk::CheckButton *>(w)->get_active();
            break;
        case rtengine::CLUTParamType::PT_CHOICE:
            v[0] = static_cast<MyComboBoxText *>(w)->get_active_row_number();
            break;
        case rtengine::CLUTParamType::PT_CURVE:
        case rtengine::CLUTParamType::PT_FLATCURVE:
        case rtengine::CLUTParamType::PT_FLATCURVE_PERIODIC:
            v = static_cast<CLUTParamsCurveEditorGroup *>(w)->getCurveEditors()[0]->getCurve();
            break;
        case rtengine::CLUTParamType::PT_INT:
        case rtengine::CLUTParamType::PT_FLOAT:
        default:
            v[0] = static_cast<Adjuster *>(w)->getValue();
            break;
        }

        values[d.name] = v;
    }

    return values;
}


void CLUTParamsPanel::setValue(const rtengine::CLUTParamValueMap &val)
{
    bool prev = sig_blocked_;
    sig_blocked_ = true;
    
    for (size_t i = 0; i < params_.size(); ++i) {
        auto w = widgets_[i];
        auto &d = params_[i];

        auto it = val.find(d.name);
        auto vv = it != val.end() ? it->second : d.value_default;
        auto v = vv.empty() ? 0.0 : vv[0];
        
        switch (d.type) {
        case rtengine::CLUTParamType::PT_BOOL: 
            static_cast<Gtk::CheckButton *>(w)->set_active(bool(v));
            break;
        case rtengine::CLUTParamType::PT_CHOICE:
            static_cast<MyComboBoxText *>(w)->set_active(int(v));
            break;
        case rtengine::CLUTParamType::PT_CURVE:
        case rtengine::CLUTParamType::PT_FLATCURVE:
        case rtengine::CLUTParamType::PT_FLATCURVE_PERIODIC: {
            CurveEditor *ce = static_cast<CLUTParamsCurveEditorGroup *>(w)->getCurveEditors()[0];
            ce->setCurve(vv);
            ce->openIfNonlinear();
        }   break;
        case rtengine::CLUTParamType::PT_INT:
        case rtengine::CLUTParamType::PT_FLOAT:
        default:
            static_cast<Adjuster *>(w)->setValue(v);
            break;
        }
    }

    sig_blocked_ = prev;
}


void CLUTParamsPanel::emit_signal()
{
    if (!sig_blocked_) {
        sig_changed_.emit();
    }
}

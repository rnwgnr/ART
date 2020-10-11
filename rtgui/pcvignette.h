/* -*- C++ -*-
 *  
 *  This file is part of RawTherapee.
 */
#pragma once

#include <gtkmm.h>
#include "adjuster.h"
#include "toolpanel.h"
#include "edit.h"

class PCVignette: public ToolParamBlock, public AdjusterListener, public FoldableToolPanel, public EditSubscriber {

protected:
    Adjuster *strength;
    Adjuster *feather;
    Adjuster *roundness;
    Adjuster *centerX;
    Adjuster *centerY;
    rtengine::ProcEvent EvCenter;

    Gtk::ToggleButton* edit;
    rtengine::Coord draggedCenter;
    sigc::connection editConn;
    int lastObject;
    
    rtengine::procparams::PCVignetteParams initial_params;

    void editToggled();
    void updateGeometry(const int centerX, const int centerY);
    
public:
    PCVignette();
    ~PCVignette();

    void read(const rtengine::procparams::ProcParams *pp) override;
    void write(rtengine::procparams::ProcParams *pp) override;
    void setDefaults(const rtengine::procparams::ProcParams *defParams) override;
    void adjusterChanged(Adjuster *a, double newval) override;
    void adjusterAutoToggled(Adjuster *a, bool newval) override;
    void enabledChanged() override;
    void trimValues(rtengine::procparams::ProcParams *pp) override;
    void toolReset(bool to_initial) override;

    void setEditProvider (EditDataProvider* provider) override;

    // EditSubscriber interface
    CursorShape getCursor(const int objectID) override;
    bool mouseOver(const int modifierKey) override;
    bool button1Pressed(const int modifierKey) override;
    bool button1Released() override;
    bool drag1(const int modifierKey) override;
    void switchOffEditMode () override;
};

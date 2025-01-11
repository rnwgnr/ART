// -*- C++ -*-

#pragma once

#include <gtkmm.h>
#include "../rtengine/settings.h"

namespace art {

void gdk_set_monitor_profile(GdkWindow *window, rtengine::Settings::StdMonitorProfile prof);

} // namespace art

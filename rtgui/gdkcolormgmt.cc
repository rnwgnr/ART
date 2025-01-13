#include "gdkcolormgmt.h"
#include "options.h"
#ifdef __APPLE__
#  include <ApplicationServices/ApplicationServices.h>
#endif 
#include <stdio.h>

#include <gdk/gdkconfig.h>
#ifdef GDK_WINDOWING_QUARTZ
# include <gdk/gdkquartz.h>
#endif

namespace art {

void gdk_set_monitor_profile(GdkWindow *window, rtengine::Settings::StdMonitorProfile prof)
{
#if defined ART_OS_COLOR_MGMT && defined __APPLE__ && defined GDK_QUARTZ_WINDOW_SUPPORTS_COLORSPACE
    auto colorspace = kCGColorSpaceSRGB;
    switch (prof) {
    case rtengine::Settings::StdMonitorProfile::DISPLAY_P3:
        colorspace = kCGColorSpaceDisplayP3;
        break;
    case rtengine::Settings::StdMonitorProfile::ADOBE_RGB:
        colorspace = kCGColorSpaceAdobeRGB1998;
        break;
    default:
        colorspace = kCGColorSpaceSRGB;
    }
    if (options.rtSettings.verbose > 1) {
        fprintf(stderr, "gdk_set_monitor_profile: %s\n", CFStringGetCStringPtr(colorspace, kCFStringEncodingUTF8));
    }
    g_object_set_data(G_OBJECT(window), "gdk-quartz-colorspace", (gpointer)colorspace);
#endif
}

} // namespace art

#include <gdk/gdk.h>
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>

void macos_tag_colorspace(GdkWindow *w)
{
#ifdef ART_MACOS_DISPLAYP3_PROFILE
    auto colorspace = kCGColorSpaceDisplayP3; //kCGColorSpaceITUR_2020;
    fprintf(stderr, "macos_tag_colorspace: %s\n", CFStringGetCStringPtr(colorspace, kCFStringEncodingUTF8));
    g_object_set_data(G_OBJECT(w), "colorspace", (gpointer)colorspace);
#endif
}

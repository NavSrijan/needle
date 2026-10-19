#include <gtk/gtk.h>
#include <gtk-layer-shell.h>
#include <gdk/gdkwayland.h>
#include <gio/gio.h>
#include <libsoup/soup.h>
#include <cairo.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>

#include "needle.h"

static void activate(GApplication *application, gpointer data) {
    (void)application; (void)data;
    start_wallpaper();
}

static void app_startup(GApplication *application, gpointer data) {
    (void)data;
    wallpapers = g_ptr_array_new_with_free_func(wallpaper_free);
    config_init();
    player_init();
    g_application_hold(application);
}

static void app_shutdown(GApplication *application, gpointer data) {
    (void)application; (void)data;
    player_shutdown();
}

int main(int argc, char **argv) {
    app=gtk_application_new("com.needle.desktop",G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app,"startup",G_CALLBACK(app_startup),NULL);
    g_signal_connect(app,"activate",G_CALLBACK(activate),NULL);
    g_signal_connect(app,"shutdown",G_CALLBACK(app_shutdown),NULL);
    int status=g_application_run(G_APPLICATION(app),argc,argv);
    g_object_unref(app);
    return status;
}

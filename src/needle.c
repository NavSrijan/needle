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

GdkPixbuf *integrated_scene;

static void activate(GApplication *application, gpointer data) {
    (void)application; (void)data;
    start_wallpaper();
}

static gchar *find_asset_path(const char *name) {
    const char *env = g_getenv("NEEDLE_ASSET_DIR");
    const char *dirs[3];
    guint n_dirs = 0;
    if (env && *env) dirs[n_dirs++] = env;
#ifdef NEEDLE_INSTALL_ASSET_DIR
    dirs[n_dirs++] = NEEDLE_INSTALL_ASSET_DIR;
#endif
    dirs[n_dirs++] = "assets";
    for (guint i=0; i<n_dirs; i++) {
        gchar *path = g_build_filename(dirs[i], name, NULL);
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) return path;
        g_free(path);
    }
    return NULL;
}

void load_visual_assets(void) {
    gchar *scene = find_asset_path("listening-room.png");
    if (scene) { integrated_scene = gdk_pixbuf_new_from_file(scene, NULL); g_free(scene); }
    if (!integrated_scene)
        g_warning("Needle listening-room scene is missing; set NEEDLE_ASSET_DIR to the assets folder.");
}

static void app_startup(GApplication *application, gpointer data) {
    (void)data;
    load_visual_assets();
    wallpapers = g_ptr_array_new_with_free_func(wallpaper_free);
    config_init();
    player_init();
    g_application_hold(application);
}

int main(int argc, char **argv) {
    app=gtk_application_new("com.needle.desktop",G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app,"startup",G_CALLBACK(app_startup),NULL);
    g_signal_connect(app,"activate",G_CALLBACK(activate),NULL);
    int status=g_application_run(G_APPLICATION(app),argc,argv);
    g_object_unref(app);
    return status;
}

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

GKeyFile *prefs;
gchar *prefs_path;
GFileMonitor *prefs_monitor;
static gchar *pref_cache = NULL;

const char *get_pref(const char *key, const char *fallback) {
    GError *err = NULL;
    g_free(pref_cache);
    pref_cache = g_key_file_get_string(prefs, "Needle", key, &err);
    if (err) { g_error_free(err); return fallback; }
    return pref_cache;
}

gboolean get_bool_pref(const char *key, gboolean fallback) {
    GError *err = NULL;
    gboolean value = g_key_file_get_boolean(prefs, "Needle", key, &err);
    if (err) { g_error_free(err); return fallback; }
    return value;
}

static void save_prefs(void) {
    gchar *contents = g_key_file_to_data(prefs, NULL, NULL);
    gchar *dir = g_path_get_dirname(prefs_path);
    g_mkdir_with_parents(dir, 0700);
    g_file_set_contents(prefs_path, contents, -1, NULL);
    g_free(contents); g_free(dir);
}


void ensure_autostart(void) {
    gchar *path = g_build_filename(g_get_user_config_dir(), "autostart", "needle.desktop", NULL);
    if (get_bool_pref("launch-login", FALSE)) {
        gchar *dir = g_path_get_dirname(path);
        g_mkdir_with_parents(dir, 0700);
        g_free(dir);
        g_file_set_contents(path,
            "[Desktop Entry]\nType=Application\nName=Needle Desktop\nExec=needle\nNoDisplay=true\n", -1, NULL);
    } else {
        g_remove(path);
    }
    g_free(path);
}

static void config_changed(GFileMonitor *monitor, GFile *file, GFile *other,
                           GFileMonitorEvent event, gpointer unused) {
    (void)monitor; (void)file; (void)other; (void)unused;
    if (event != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT &&
        event != G_FILE_MONITOR_EVENT_CREATED && event != G_FILE_MONITOR_EVENT_MOVED_IN) return;
    GKeyFile *updated = g_key_file_new();
    if (!g_key_file_load_from_file(updated, prefs_path, G_KEY_FILE_KEEP_COMMENTS, NULL)) {
        g_key_file_unref(updated);
        return;
    }
    g_key_file_unref(prefs);
    prefs = updated;
    ensure_autostart();
    for (guint i = 0; i < wallpapers->len; i++) refresh_wallpaper(g_ptr_array_index(wallpapers, i));
    update_visibility();
}


void config_init(void) {
    prefs = g_key_file_new();
    prefs_path = g_build_filename(g_get_user_config_dir(), "needle", "config.ini", NULL);
    if (!g_key_file_load_from_file(prefs, prefs_path, G_KEY_FILE_KEEP_COMMENTS, NULL)) {
        g_key_file_set_string(prefs, "Needle", "framing", "mid");
        g_key_file_set_boolean(prefs, "Needle", "transparent-background", FALSE);
        g_key_file_set_boolean(prefs, "Needle", "show-art", TRUE);
        g_key_file_set_boolean(prefs, "Needle", "show-caption", TRUE);
        g_key_file_set_boolean(prefs, "Needle", "hide-paused", FALSE);
        g_key_file_set_boolean(prefs, "Needle", "launch-login", FALSE);
        save_prefs();
    }
    gchar *dir = g_path_get_dirname(prefs_path);
    GFile *directory = g_file_new_for_path(dir);
    prefs_monitor = g_file_monitor_directory(directory, G_FILE_MONITOR_WATCH_MOVES, NULL, NULL);
    if (prefs_monitor) g_signal_connect(prefs_monitor, "changed", G_CALLBACK(config_changed), NULL);
    g_object_unref(directory); g_free(dir);
    ensure_autostart();
}

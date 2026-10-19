#pragma once
#include <gtk/gtk.h>
#include <gtk-layer-shell.h>
#include <gdk/gdkwayland.h>
#include <gio/gio.h>
#include <libsoup/soup.h>
#include <cairo.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>

typedef struct {
    GtkWidget *window;
    GtkWidget *canvas;
    GdkMonitor *monitor;
    cairo_surface_t *static_scene;
    int width, height;
} Wallpaper;

typedef struct {
    gchar *title, *artist, *album, *art_url, *status, *player;
    GdkPixbuf *cover;
} NowPlaying;

typedef struct {
    gchar *key;
    GdkPixbuf *cover;
} RecentCover;

extern GtkApplication *app;
extern GPtrArray *wallpapers;
extern GKeyFile *prefs;
extern gchar *prefs_path;
extern GFileMonitor *prefs_monitor;
extern GDBusConnection *bus;
extern SoupSession *http_session;
extern NowPlaying now;
extern gboolean wallpaper_started;
extern double spin_angle;

const char *get_pref(const char *key, const char *fallback);
gboolean get_bool_pref(const char *key, gboolean fallback);
void config_init(void);
void ensure_autostart(void);
void wallpaper_free(gpointer data);
void draw_programmatic_scene(cairo_t *cr, double w, double h);
void draw_scene(cairo_t *cr, int w, int h, double angle);
gboolean wallpaper_draw(GtkWidget *widget, cairo_t *cr, gpointer data);
void start_animation(void);
void refresh_wallpaper(Wallpaper *wp);
void update_visibility(void);
void start_wallpaper(void);
void player_init(void);
void player_shutdown(void);
const GPtrArray *player_recent_covers(void);

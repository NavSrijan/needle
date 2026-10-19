#define G_APPLICATION_ENABLE_DIAGNOSTIC
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

static GtkApplication *app;
static GPtrArray *wallpapers;
static GtkWidget *settings_window;
static GKeyFile *prefs;
static gchar *prefs_path;
static GDBusConnection *bus;
static SoupSession *http_session;
static NowPlaying now = {0};
static gboolean settings_requested = FALSE;
static gboolean wallpaper_started = FALSE;
static guint tick_id = 0;
static double spin_angle = 0;
static gint64 last_spin_tick = 0;
static gchar *pref_cache = NULL;
static GtkWidget *settings_track_label, *settings_artist_label, *settings_cover;
static GdkPixbuf *room_after_hours, *room_day, *room_studio, *turntable_asset;

#define MAX_ARTWORK_BYTES (12 * 1024 * 1024)
typedef struct {
    gchar *url;
    SoupMessage *message;
    GCancellable *cancellable;
} CoverRequest;

static const char *get_pref(const char *key, const char *fallback) {
    GError *err = NULL;
    g_free(pref_cache);
    pref_cache = g_key_file_get_string(prefs, "Needle", key, &err);
    if (err) { g_error_free(err); return fallback; }
    return pref_cache;
}

static gboolean get_bool_pref(const char *key, gboolean fallback) {
    GError *err = NULL;
    gboolean value = g_key_file_get_boolean(prefs, "Needle", key, &err);
    if (err) { g_error_free(err); return fallback; }
    return value;
}

static double get_scale(void) {
    GError *err = NULL;
    double value = g_key_file_get_double(prefs, "Needle", "scale", &err);
    if (err) { g_error_free(err); return 1.0; }
    return CLAMP(value, 0.72, 1.24);
}

static void save_prefs(void) {
    gchar *contents = g_key_file_to_data(prefs, NULL, NULL);
    gchar *dir = g_path_get_dirname(prefs_path);
    g_mkdir_with_parents(dir, 0700);
    g_file_set_contents(prefs_path, contents, -1, NULL);
    g_free(contents); g_free(dir);
}

static void set_pref_string(const char *key, const char *value) {
    g_key_file_set_string(prefs, "Needle", key, value);
    save_prefs();
}

static void set_pref_bool(const char *key, gboolean value) {
    g_key_file_set_boolean(prefs, "Needle", key, value);
    save_prefs();
}

static void set_pref_double(const char *key, double value) {
    g_key_file_set_double(prefs, "Needle", key, value);
    save_prefs();
}

static void invalidate_static_scenes(void);

static void set_source(cairo_t *cr, double r, double g, double b, double a) {
    cairo_set_source_rgba(cr, r, g, b, a);
}

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double radius) {
    double r = MIN(radius, MIN(w, h) / 2.0);
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -G_PI_2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI_2);
    cairo_arc(cr, x + r, y + h - r, r, G_PI_2, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, 3 * G_PI_2);
    cairo_close_path(cr);
}

static void text(cairo_t *cr, const char *value, double x, double y, double size,
                 const char *face, cairo_font_weight_t weight, double r, double g, double b, double a) {
    cairo_select_font_face(cr, face, CAIRO_FONT_SLANT_NORMAL, weight);
    cairo_set_font_size(cr, size);
    cairo_move_to(cr, x, y);
    set_source(cr, r, g, b, a);
    cairo_show_text(cr, value ? value : "");
}

static void centered_text(cairo_t *cr, const char *value, double cx, double y, double size,
                          const char *face, double r, double g, double b, double a) {
    cairo_text_extents_t ext;
    cairo_select_font_face(cr, face, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
    cairo_text_extents(cr, value ? value : "", &ext);
    text(cr, value, cx - ext.width / 2.0, y, size, face, CAIRO_FONT_WEIGHT_NORMAL, r, g, b, a);
}

static gboolean is_scene(const char *scene) { return g_strcmp0(get_pref("scene", "after-hours"), scene) == 0; }

static void paint_cover_image(cairo_t *cr, GdkPixbuf *image, double x, double y, double w, double h) {
    double iw = gdk_pixbuf_get_width(image), ih = gdk_pixbuf_get_height(image);
    double scale = MAX(w / iw, h / ih);
    double dx = x + (w - iw * scale) / 2.0;
    double dy = y + (h - ih * scale) / 2.0;
    cairo_save(cr);
    cairo_rectangle(cr, x, y, w, h); cairo_clip(cr);
    cairo_translate(cr, dx, dy); cairo_scale(cr, scale, scale);
    gdk_cairo_set_source_pixbuf(cr, image, 0, 0); cairo_paint(cr);
    cairo_restore(cr);
}

static void draw_background(cairo_t *cr, int w, int h) {
    GdkPixbuf *background = room_after_hours;
    if (is_scene("listening-room")) background = room_day;
    else if (is_scene("studio")) background = room_studio;
    if (background) {
        paint_cover_image(cr, background, 0, 0, w, h);
        /* Soft vignette keeps the desktop legible while preserving the photo. */
        cairo_pattern_t *shade = cairo_pattern_create_radial(w * .5, h * .47, MIN(w, h) * .16,
            w * .5, h * .47, MAX(w, h) * .82);
        cairo_pattern_add_color_stop_rgba(shade, 0, 0, 0, 0, .02);
        cairo_pattern_add_color_stop_rgba(shade, 1, 0, 0, 0, .36);
        cairo_rectangle(cr, 0, 0, w, h); cairo_set_source(cr, shade); cairo_fill(cr);
        cairo_pattern_destroy(shade);
        return;
    }
    cairo_pattern_t *wall = cairo_pattern_create_linear(0, 0, 0, h * .61);
    cairo_pattern_add_color_stop_rgb(wall, 0, .10, .09, .08);
    cairo_pattern_add_color_stop_rgb(wall, 1, .25, .18, .13);
    cairo_rectangle(cr, 0, 0, w, h * .61); cairo_set_source(cr, wall); cairo_fill(cr); cairo_pattern_destroy(wall);
    cairo_pattern_t *desk = cairo_pattern_create_linear(0, h * .60, 0, h);
    cairo_pattern_add_color_stop_rgb(desk, 0, .44, .29, .17);
    cairo_pattern_add_color_stop_rgb(desk, 1, .18, .10, .06);
    cairo_rectangle(cr, 0, h * .60, w, h * .40); cairo_set_source(cr, desk); cairo_fill(cr); cairo_pattern_destroy(desk);
}

static void draw_sleeve(cairo_t *cr, double x, double y, double s, gboolean shadow) {
    double size = 190 * s;
    if (!get_bool_pref("show-art", TRUE)) return;
    if (shadow) {
        set_source(cr, 0, 0, 0, .35); rounded_rect(cr, x + 8*s, y + 12*s, size, size, 3*s); cairo_fill(cr);
    }
    cairo_save(cr);
    cairo_translate(cr, x + size/2, y + size/2); cairo_rotate(cr, -.035); cairo_translate(cr, -size/2, -size/2);
    if (now.cover) {
        cairo_save(cr); rounded_rect(cr, 0, 0, size, size, 2*s); cairo_clip(cr);
        double pw = gdk_pixbuf_get_width(now.cover), ph = gdk_pixbuf_get_height(now.cover);
        double z = MAX(size/pw, size/ph); cairo_translate(cr, (size-pw*z)/2, (size-ph*z)/2); cairo_scale(cr, z, z);
        gdk_cairo_set_source_pixbuf(cr, now.cover, 0, 0); cairo_paint(cr); cairo_restore(cr);
    } else {
        /* A quiet typographic sleeve is preferable to invented geometric cover art. */
        cairo_pattern_t *paper = cairo_pattern_create_linear(0, 0, size, size);
        cairo_pattern_add_color_stop_rgb(paper, 0, .79, .72, .60);
        cairo_pattern_add_color_stop_rgb(paper, 1, .42, .32, .23);
        rounded_rect(cr, 0, 0, size, size, 2*s); cairo_set_source(cr, paper); cairo_fill(cr); cairo_pattern_destroy(paper);
        set_source(cr, .18, .13, .09, .36); cairo_rectangle(cr, 12*s, 12*s, 2*s, size-24*s); cairo_fill(cr);
        char *album = g_utf8_strup(now.album && *now.album ? now.album : "NEEDLE EDITIONS", -1);
        if (strlen(album) > 21) album[21] = 0;
        text(cr, album, 20*s, size-34*s, 12*s, "Serif", CAIRO_FONT_WEIGHT_BOLD, .18, .13, .09, .96);
        g_free(album);
        char *artist = g_utf8_strup(now.artist && *now.artist ? now.artist : "SIDE A  ·  33⅓ RPM", -1);
        if (strlen(artist) > 25) artist[25] = 0;
        text(cr, artist, 20*s, size-20*s, 6.6*s, "Sans", CAIRO_FONT_WEIGHT_BOLD, .20, .15, .10, .82);
        g_free(artist);
    }
    /* Paper edge and a soft specular strip sell the sleeve as a physical jacket. */
    rounded_rect(cr, 0, 0, size, size, 2*s); cairo_set_line_width(cr, 1*s); set_source(cr, 1, .93, .78, .35); cairo_stroke(cr);
    cairo_pattern_t *gloss = cairo_pattern_create_linear(0, 0, size, size);
    cairo_pattern_add_color_stop_rgba(gloss, 0, 1, 1, 1, .14); cairo_pattern_add_color_stop_rgba(gloss, .42, 1, 1, 1, 0); cairo_pattern_add_color_stop_rgba(gloss, 1, 0, 0, 0, .08);
    rounded_rect(cr, 0, 0, size, size, 2*s); cairo_set_source(cr, gloss); cairo_fill(cr); cairo_pattern_destroy(gloss);
    cairo_restore(cr);
}

static void draw_turntable(cairo_t *cr, double cx, double cy, double s, double angle) {
    const double image_w = 820*s, image_h = image_w * 2.0/3.0;
    const double image_x = cx - image_w/2.0, image_y = cy - 110*s;
    if (turntable_asset) paint_cover_image(cr, turntable_asset, image_x, image_y, image_w, image_h);

    /* The platter in the photographic cutout is empty; this record is the live, spinning layer. */
    const double disc_x = image_x + image_w * .43;
    const double disc_y = image_y + image_h * .36;
    const double rx = image_w * .302, ry = image_h * .226;
    cairo_save(cr);
    cairo_translate(cr, disc_x, disc_y); cairo_scale(cr, 1, ry/rx);
    cairo_pattern_t *vinyl = cairo_pattern_create_radial(-.18*rx, -.22*rx, .015*rx, 0, 0, rx);
    cairo_pattern_add_color_stop_rgb(vinyl, 0, .25, .25, .23);
    cairo_pattern_add_color_stop_rgb(vinyl, .16, .045, .048, .046);
    cairo_pattern_add_color_stop_rgb(vinyl, .74, .018, .020, .019);
    cairo_pattern_add_color_stop_rgb(vinyl, 1, .13, .13, .12);
    cairo_arc(cr, 0, 0, rx*.96, 0, 2*G_PI); cairo_set_source(cr, vinyl); cairo_fill(cr); cairo_pattern_destroy(vinyl);
    cairo_save(cr); cairo_rotate(cr, angle);
    for (int i=0; i<72; i++) {
        double r = rx * (.26 + i*.0094);
        if (r > rx*.92) break;
        set_source(cr, .78, .76, .70, (i % 4 == 0) ? .13 : .045);
        cairo_set_line_width(cr, MAX(.42, s*.4));
        cairo_arc(cr, 0, 0, r, 0, 2*G_PI); cairo_stroke(cr);
    }
    /* A restrained moving glint makes the slow groove pattern visibly rotate. */
    cairo_arc(cr, 0, 0, rx*.79, angle, angle + .24);
    set_source(cr, .95, .90, .77, .23); cairo_set_line_width(cr, rx*.025); cairo_stroke(cr);
    cairo_pattern_t *label = cairo_pattern_create_linear(-1, -rx*.22, 1, rx*.22);
    cairo_pattern_add_color_stop_rgb(label, 0, .75, .56, .35); cairo_pattern_add_color_stop_rgb(label, .5, .88, .76, .55); cairo_pattern_add_color_stop_rgb(label, 1, .36, .29, .20);
    cairo_arc(cr, 0, 0, rx*.225, 0, 2*G_PI); cairo_set_source(cr, label); cairo_fill(cr); cairo_pattern_destroy(label);
    cairo_arc(cr, 0, 0, rx*.225, 0, 2*G_PI); cairo_set_line_width(cr, MAX(.8, s)); set_source(cr, .17, .14, .10, .55); cairo_stroke(cr);
    centered_text(cr, "33⅓", 0, rx*.035, rx*.052, "Sans", .19, .15, .11, .94);
    cairo_restore(cr);
    cairo_arc(cr, 0, 0, rx*.96, 0, 2*G_PI); cairo_set_line_width(cr, MAX(1.0, 1.3*s)); set_source(cr, .72, .68, .59, .55); cairo_stroke(cr);
    cairo_arc(cr, 0, 0, rx*.018, 0, 2*G_PI); set_source(cr, .82, .80, .74, 1); cairo_fill(cr);
    cairo_restore(cr);

    /* Tonearm sits above the record and follows the platter perspective. */
    double pivot_x = image_x + image_w*.812, pivot_y = image_y + image_h*.31;
    double tip_x = disc_x + rx*.79, tip_y = disc_y + ry*.57;
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, pivot_x, pivot_y); cairo_line_to(cr, tip_x, tip_y);
    set_source(cr, .85, .81, .72, .98); cairo_set_line_width(cr, 7*s); cairo_stroke(cr);
    cairo_move_to(cr, pivot_x, pivot_y); cairo_line_to(cr, tip_x, tip_y);
    set_source(cr, 1, .96, .86, .9); cairo_set_line_width(cr, 2.2*s); cairo_stroke(cr);
    cairo_save(cr); cairo_translate(cr, tip_x, tip_y); cairo_rotate(cr, -.25);
    rounded_rect(cr, -12*s, -4*s, 25*s, 11*s, 3*s); set_source(cr, .23, .20, .16, 1); cairo_fill(cr);
    cairo_move_to(cr, 0, 5*s); cairo_line_to(cr, 1*s, 15*s); set_source(cr, .91, .84, .70, .95); cairo_set_line_width(cr, 1.2*s); cairo_stroke(cr);
    cairo_restore(cr);
}

static void draw_now_playing(cairo_t *cr, int w, int h, double s) {
    if (!get_bool_pref("show-caption", TRUE)) return;
    double x = w * .56, y = h * .075, pw = 350*s, ph = 76*s;
    rounded_rect(cr, x, y, pw, ph, 10*s); set_source(cr, .055, .052, .047, .78); cairo_fill(cr);
    rounded_rect(cr, x, y, pw, ph, 10*s); cairo_set_line_width(cr, 1*s); set_source(cr, 1, .91, .76, .19); cairo_stroke(cr);
    double art = 56*s, ax = x + 11*s, ay = y + 10*s;
    rounded_rect(cr, ax, ay, art, art, 4*s); cairo_save(cr); cairo_clip(cr);
    if (now.cover) paint_cover_image(cr, now.cover, ax, ay, art, art);
    else {
        cairo_pattern_t *p = cairo_pattern_create_linear(ax, ay, ax + art, ay + art);
        cairo_pattern_add_color_stop_rgb(p, 0, .55, .35, .23); cairo_pattern_add_color_stop_rgb(p, 1, .17, .22, .20);
        cairo_rectangle(cr, ax, ay, art, art); cairo_set_source(cr, p); cairo_fill(cr); cairo_pattern_destroy(p);
        centered_text(cr, "33⅓", ax + art/2, ay + art*.56, 11*s, "Serif", .96, .84, .62, .95);
    }
    cairo_restore(cr);
    double tx = x + 79*s;
    cairo_arc(cr, tx + 3*s, y + 18*s, 3*s, 0, 2*G_PI); set_source(cr, .73, .86, .63, .96); cairo_fill(cr);
    text(cr, "NOW SPINNING", tx + 12*s, y + 21*s, 7*s, "Sans", CAIRO_FONT_WEIGHT_BOLD, .75, .71, .64, .96);
    char *title = g_strdup(now.title && *now.title ? now.title : "Needle is ready");
    if (strlen(title) > 31) title[31] = 0;
    text(cr, title, tx, y + 43*s, 14*s, "Serif", CAIRO_FONT_WEIGHT_BOLD, .98, .93, .84, .98); g_free(title);
    char *byline = g_strdup_printf("%s%s%s", now.artist && *now.artist ? now.artist : "Start a player", (now.album && *now.album) ? "  ·  " : "", now.album && *now.album ? now.album : "");
    if (strlen(byline) > 46) byline[46] = 0;
    text(cr, byline, tx, y + 59*s, 8*s, "Sans", CAIRO_FONT_WEIGHT_NORMAL, .77, .73, .67, .97); g_free(byline);
}

static void draw_scene(cairo_t *cr, int w, int h, double angle) {
    cairo_save(cr);
    if (get_bool_pref("backdrop", TRUE)) draw_background(cr, w, h);
    double s = MIN(w / 1280.0, h / 800.0) * get_scale();
    s = MAX(.48, MIN(s, 1.48));
    double cx = w * .50, cy = h * .56;
    draw_turntable(cr, cx, cy, s, angle);
    draw_sleeve(cr, cx - 500*s, cy, s, TRUE);
    draw_now_playing(cr, w, h, s);
    cairo_restore(cr);
}

static gboolean wallpaper_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)data;
    GtkAllocation a; gtk_widget_get_allocation(widget, &a);
    if (a.width < 2 || a.height < 2) return FALSE;
    draw_scene(cr, a.width, a.height, spin_angle);
    return FALSE;
}

static gboolean tick_wallpapers(gpointer unused) {
    (void)unused;
    gint64 now_us = g_get_monotonic_time();
    if (last_spin_tick == 0) last_spin_tick = now_us;
    if (g_strcmp0(now.status, "Playing") != 0) { last_spin_tick = now_us; return G_SOURCE_CONTINUE; }
    double elapsed = (now_us - last_spin_tick) / 1000000.0;
    last_spin_tick = now_us;
    spin_angle = fmod(spin_angle + elapsed * (2.0 * G_PI * 33.333333 / 60.0), 2.0 * G_PI);
    for (guint i = 0; i < wallpapers->len; i++) {
        Wallpaper *wp = g_ptr_array_index(wallpapers, i);
        if (wp->canvas) gtk_widget_queue_draw(wp->canvas);
    }
    return G_SOURCE_CONTINUE;
}

static void refresh_wallpaper(Wallpaper *wp) {
    if (wp->canvas) gtk_widget_queue_draw(wp->canvas);
}

static void update_visibility(void) {
    gboolean should_hide = get_bool_pref("hide-paused", FALSE) && g_strcmp0(now.status, "Playing") != 0;
    for (guint i = 0; i < wallpapers->len; i++) {
        Wallpaper *wp = g_ptr_array_index(wallpapers, i);
        if (should_hide) gtk_widget_hide(wp->window);
        else gtk_widget_show_all(wp->window);
    }
}

static void wallpaper_free(gpointer data) {
    Wallpaper *wp = data;
    if (wp->static_scene) cairo_surface_destroy(wp->static_scene);
    g_free(wp);
}

static void build_wallpaper(GdkMonitor *monitor) {
    Wallpaper *wp = g_new0(Wallpaper, 1);
    wp->monitor = monitor;
    wp->window = gtk_application_window_new(GTK_APPLICATION(app));
    gtk_window_set_title(GTK_WINDOW(wp->window), "Needle desktop scene");
    gtk_window_set_decorated(GTK_WINDOW(wp->window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(wp->window), FALSE);
    gtk_widget_set_app_paintable(wp->window, TRUE);
    gtk_widget_set_name(wp->window, "needle-wallpaper");
    GdkScreen *screen = gtk_widget_get_screen(wp->window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) gtk_widget_set_visual(wp->window, visual);
    wp->canvas = gtk_drawing_area_new();
    gtk_widget_set_hexpand(wp->canvas, TRUE); gtk_widget_set_vexpand(wp->canvas, TRUE);
    gtk_container_add(GTK_CONTAINER(wp->window), wp->canvas);
    g_signal_connect(wp->canvas, "draw", G_CALLBACK(wallpaper_draw), wp);
    if (GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default()) && gtk_layer_is_supported()) {
        gtk_layer_init_for_window(GTK_WINDOW(wp->window));
        gtk_layer_set_namespace(GTK_WINDOW(wp->window), "needle-wallpaper");
        gtk_layer_set_layer(GTK_WINDOW(wp->window), GTK_LAYER_SHELL_LAYER_BACKGROUND);
        gtk_layer_set_anchor(GTK_WINDOW(wp->window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(wp->window), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(wp->window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(wp->window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
        gtk_layer_set_exclusive_zone(GTK_WINDOW(wp->window), -1);
        gtk_layer_set_keyboard_mode(GTK_WINDOW(wp->window), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
        gtk_layer_set_monitor(GTK_WINDOW(wp->window), monitor);
    } else {
        if (GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default()))
            g_warning("This Wayland compositor does not expose layer-shell; using the normal desktop-window fallback.");
        gtk_window_set_type_hint(GTK_WINDOW(wp->window), GDK_WINDOW_TYPE_HINT_DESKTOP);
        gtk_window_set_keep_below(GTK_WINDOW(wp->window), TRUE);
        gtk_window_set_skip_taskbar_hint(GTK_WINDOW(wp->window), TRUE);
        gtk_window_set_skip_pager_hint(GTK_WINDOW(wp->window), TRUE);
        GdkRectangle geo; gdk_monitor_get_geometry(monitor, &geo);
        gtk_window_move(GTK_WINDOW(wp->window), geo.x, geo.y);
        gtk_window_set_default_size(GTK_WINDOW(wp->window), geo.width, geo.height);
        gtk_window_fullscreen(GTK_WINDOW(wp->window));
    }
    gtk_widget_show_all(wp->window);
    /* The wallpaper is decorative; let desktop icons and pointer actions pass through it. */
    cairo_region_t *input_region = cairo_region_create();
    gdk_window_input_shape_combine_region(gtk_widget_get_window(wp->window), input_region, 0, 0);
    cairo_region_destroy(input_region);
    if (get_bool_pref("hide-paused", FALSE) && g_strcmp0(now.status, "Playing") != 0) gtk_widget_hide(wp->window);
    gtk_widget_queue_draw(wp->canvas);
    g_ptr_array_add(wallpapers, wp);
}

static void open_settings(void);

static void monitors_changed(GdkDisplay *display, gpointer unused) {
    (void)unused;
    if (!wallpaper_started) return;
    while (wallpapers->len) {
        Wallpaper *wp = g_ptr_array_index(wallpapers, wallpapers->len - 1);
        gtk_widget_destroy(wp->window);
        g_ptr_array_remove_index(wallpapers, wallpapers->len - 1);
    }
    int n = gdk_display_get_n_monitors(display);
    for (int i = 0; i < n; i++) build_wallpaper(gdk_display_get_monitor(display, i));
}

static void start_wallpaper(void) {
    if (wallpaper_started) return;
    wallpaper_started = TRUE;
    GdkDisplay *display = gdk_display_get_default();
    g_signal_connect(display, "monitor-added", G_CALLBACK(monitors_changed), NULL);
    g_signal_connect(display, "monitor-removed", G_CALLBACK(monitors_changed), NULL);
    int n = gdk_display_get_n_monitors(display);
    for (int i = 0; i < n; i++) build_wallpaper(gdk_display_get_monitor(display, i));
    if (tick_id == 0) tick_id = g_timeout_add(40, tick_wallpapers, NULL);
}

static char *variant_string(GVariant *dict, const char *key) {
    const char *value = NULL;
    if (!g_variant_lookup(dict, key, "&s", &value) || !value) return NULL;
    return g_strdup(value);
}

static char *variant_artist(GVariant *dict) {
    GVariant *v = g_variant_lookup_value(dict, "xesam:artist", G_VARIANT_TYPE_STRING_ARRAY);
    if (!v) return NULL;
    char *artist = NULL;
    if (g_variant_n_children(v) > 0) {
        GVariant *first = g_variant_get_child_value(v, 0);
        artist = g_variant_dup_string(first, NULL); g_variant_unref(first);
    }
    g_variant_unref(v);
    return artist;
}

static gboolean query_player(const char *name, NowPlaying *out) {
    GError *err = NULL;
    GVariant *reply = g_dbus_connection_call_sync(bus, name, "/org/mpris/MediaPlayer2",
        "org.freedesktop.DBus.Properties", "GetAll", g_variant_new("(s)", "org.mpris.MediaPlayer2.Player"),
        G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NONE, 600, NULL, &err);
    if (!reply) { g_clear_error(&err); return FALSE; }
    GVariant *dict = g_variant_get_child_value(reply, 0);
    char *status = variant_string(dict, "PlaybackStatus");
    GVariant *metadata = g_variant_lookup_value(dict, "Metadata", G_VARIANT_TYPE("a{sv}"));
    if (!metadata) { g_free(status); g_variant_unref(dict); g_variant_unref(reply); return FALSE; }
    char *title = variant_string(metadata, "xesam:title");
    char *artist = variant_artist(metadata);
    char *album = variant_string(metadata, "xesam:album");
    char *art = variant_string(metadata, "mpris:artUrl");
    out->title = title; out->artist = artist; out->album = album; out->art_url = art;
    out->status = status; out->player = g_strdup(name);
    g_variant_unref(metadata); g_variant_unref(dict); g_variant_unref(reply);
    return TRUE;
}

static void now_clear(NowPlaying *item) {
    g_free(item->title); g_free(item->artist); g_free(item->album); g_free(item->art_url); g_free(item->status); g_free(item->player);
    g_clear_object(&item->cover);
    memset(item, 0, sizeof(*item));
}

static void cover_request_free(CoverRequest *request) {
    if (!request) return;
    g_free(request->url);
    g_clear_object(&request->message);
    g_clear_object(&request->cancellable);
    g_free(request);
}

static void cover_headers_received(SoupMessage *message, gpointer data) {
    GCancellable *cancellable = G_CANCELLABLE(data);
    goffset length = soup_message_headers_get_content_length(soup_message_get_response_headers(message));
    if (length > MAX_ARTWORK_BYTES) g_cancellable_cancel(cancellable);
}

static void cover_downloaded(GObject *source, GAsyncResult *result, gpointer data) {
    CoverRequest *request = data;
    GError *error = NULL;
    GBytes *bytes = soup_session_send_and_read_finish(SOUP_SESSION(source), result, &error);
    GdkPixbuf *pixbuf = NULL;
    if (bytes && soup_message_get_status(request->message) == SOUP_STATUS_OK && g_bytes_get_size(bytes) <= MAX_ARTWORK_BYTES) {
        GInputStream *stream = g_memory_input_stream_new_from_bytes(bytes);
        pixbuf = gdk_pixbuf_new_from_stream_at_scale(stream, 320, 320, TRUE, NULL, &error);
        g_object_unref(stream);
    }
    if (g_strcmp0(now.art_url, request->url) == 0 && pixbuf) {
        g_clear_object(&now.cover);
        now.cover = pixbuf;
        for (guint i = 0; i < wallpapers->len; i++) refresh_wallpaper(g_ptr_array_index(wallpapers, i));
        if (settings_cover) gtk_widget_queue_draw(settings_cover);
    } else g_clear_object(&pixbuf);
    if (error) g_error_free(error);
    if (bytes) g_bytes_unref(bytes);
    cover_request_free(request);
}

static void refresh_art(void) {
    g_clear_object(&now.cover);
    if (!now.art_url) return;
    if (g_str_has_prefix(now.art_url, "file://")) {
        char *path = g_filename_from_uri(now.art_url, NULL, NULL);
        if (path) {
            GError *err = NULL; now.cover = gdk_pixbuf_new_from_file_at_scale(path, 320, 320, TRUE, &err);
            if (err) g_error_free(err);
            g_free(path);
        }
        return;
    }
    if (!g_str_has_prefix(now.art_url, "https://") && !g_str_has_prefix(now.art_url, "http://")) return;
    CoverRequest *request = g_new0(CoverRequest, 1);
    request->url = g_strdup(now.art_url);
    request->message = soup_message_new("GET", request->url);
    request->cancellable = g_cancellable_new();
    if (!request->message) { cover_request_free(request); return; }
    g_signal_connect(request->message, "got-headers", G_CALLBACK(cover_headers_received), request->cancellable);
    soup_session_send_and_read_async(http_session, request->message, G_PRIORITY_DEFAULT, request->cancellable, cover_downloaded, request);
}

static gboolean poll_mpris(gpointer unused) {
    (void)unused;
    if (!bus) return G_SOURCE_CONTINUE;
    GError *err = NULL;
    GVariant *reply = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "ListNames", NULL, G_VARIANT_TYPE("(as)"), G_DBUS_CALL_FLAGS_NONE, 500, NULL, &err);
    if (!reply) { g_clear_error(&err); return G_SOURCE_CONTINUE; }
    GVariant *names = g_variant_get_child_value(reply, 0);
    NowPlaying candidate = {0};
    char *fallback_player = NULL;
    for (gsize i = 0; i < g_variant_n_children(names); i++) {
        GVariant *entry = g_variant_get_child_value(names, i);
        const char *name = g_variant_get_string(entry, NULL);
        if (g_str_has_prefix(name, "org.mpris.MediaPlayer2.")) {
            NowPlaying tmp = {0};
            if (query_player(name, &tmp)) {
                if (!fallback_player) fallback_player = g_strdup(name);
                if (g_strcmp0(tmp.status, "Playing") == 0) { now_clear(&candidate); candidate = tmp; g_variant_unref(entry); break; }
                now_clear(&tmp);
            }
        }
        g_variant_unref(entry);
    }
    if (!candidate.player && fallback_player) query_player(fallback_player, &candidate);
    g_free(fallback_player); g_variant_unref(names); g_variant_unref(reply);
    gboolean changed = g_strcmp0(candidate.title, now.title) || g_strcmp0(candidate.artist, now.artist) ||
        g_strcmp0(candidate.album, now.album) || g_strcmp0(candidate.art_url, now.art_url) ||
        g_strcmp0(candidate.status, now.status) || g_strcmp0(candidate.player, now.player);
    if (changed) {
        now_clear(&now); now = candidate; refresh_art();
        for (guint i = 0; i < wallpapers->len; i++) refresh_wallpaper(g_ptr_array_index(wallpapers, i));
        update_visibility();
        if (settings_track_label) gtk_label_set_text(GTK_LABEL(settings_track_label), now.title && *now.title ? now.title : "No player active");
        if (settings_artist_label) gtk_label_set_text(GTK_LABEL(settings_artist_label), now.artist && *now.artist ? now.artist : "Start a player with MPRIS support");
        if (settings_cover) gtk_widget_queue_draw(settings_cover);
    } else now_clear(&candidate);
    return G_SOURCE_CONTINUE;
}

static void mpris_control(const char *method) {
    if (!bus || !now.player) return;
    g_dbus_connection_call(bus, now.player, "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player", method,
        NULL, NULL, G_DBUS_CALL_FLAGS_NONE, 900, NULL, NULL, NULL);
}

static void style_settings(GtkWidget *window) {
    GtkCssProvider *css = gtk_css_provider_new();
    const char *style = "window#needle-settings { background: #171614; color: #eee5d9; }"
        "#brand { font-family: serif; font-style: italic; font-size: 25px; color: #f1e6d6; }"
        ".eyebrow { color: #a99b87; font-size: 9px; letter-spacing: 1px; }"
        ".section { color: #c4a982; font-size: 9px; letter-spacing: 1.2px; font-weight: bold; }"
        ".row { border-top: 1px solid #39352f; padding: 10px 0; }"
        ".row-title { color: #f0e8dc; font-size: 12px; } .row-note { color: #999184; font-size: 10px; }"
        "button { background: #302c27; color: #eee5d9; border: 1px solid #4a433a; border-radius: 6px; padding: 7px 10px; }"
        "button:checked, button.suggested-action { background: #a97c4d; color: #fff6e9; }"
        "combobox, scale { color: #eee5d9; }";
    gtk_css_provider_load_from_data(css, style, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    gtk_widget_set_name(window, "needle-settings");
}

static GtkWidget *row_box(const char *title, const char *note) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_style_context_add_class(gtk_widget_get_style_context(row), "row");
    GtkWidget *copy = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    GtkWidget *a = gtk_label_new(title), *b = gtk_label_new(note);
    gtk_widget_set_halign(a, GTK_ALIGN_START); gtk_widget_set_halign(b, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(a), "row-title"); gtk_style_context_add_class(gtk_widget_get_style_context(b), "row-note");
    gtk_box_pack_start(GTK_BOX(copy), a, FALSE, FALSE, 0); gtk_box_pack_start(GTK_BOX(copy), b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), copy, TRUE, TRUE, 0);
    return row;
}

static void scale_changed(GtkRange *range, gpointer unused) {
    (void)unused; set_pref_double("scale", gtk_range_get_value(range));
    for (guint i = 0; i < wallpapers->len; i++) refresh_wallpaper(g_ptr_array_index(wallpapers, i));
}

static void scene_changed(GtkComboBox *combo, gpointer unused) {
    (void)unused; GtkTreeIter iter; GtkTreeModel *model;
    if (!gtk_combo_box_get_active_iter(combo, &iter)) return;
    model = gtk_combo_box_get_model(combo); char *value = NULL;
    gtk_tree_model_get(model, &iter, 1, &value, -1);
    if (value) { set_pref_string("scene", value); g_free(value); }
    invalidate_static_scenes();
    for (guint i = 0; i < wallpapers->len; i++) refresh_wallpaper(g_ptr_array_index(wallpapers, i));
}

static void button_method(GtkButton *button, gpointer method) { (void)button; mpris_control(method); }

static gboolean mini_cover_draw(GtkWidget *widget, cairo_t *cr, gpointer unused) {
    (void)unused;
    GtkAllocation a; gtk_widget_get_allocation(widget, &a);
    if (now.cover) {
        double z = MAX((double)a.width / gdk_pixbuf_get_width(now.cover), (double)a.height / gdk_pixbuf_get_height(now.cover));
        cairo_save(cr); cairo_scale(cr, z, z); gdk_cairo_set_source_pixbuf(cr, now.cover, 0, 0); cairo_paint(cr); cairo_restore(cr);
    } else {
        cairo_pattern_t *p = cairo_pattern_create_linear(0, 0, a.width, a.height);
        cairo_pattern_add_color_stop_rgb(p, 0, .14, .31, .31); cairo_pattern_add_color_stop_rgb(p, 1, .65, .34, .20);
        cairo_rectangle(cr, 0, 0, a.width, a.height); cairo_set_source(cr, p); cairo_fill(cr); cairo_pattern_destroy(p);
    }
    return FALSE;
}

static gboolean switch_changed(GtkSwitch *sw, gboolean state, gpointer key) {
    (void)sw;
    const char *name = key;
    set_pref_bool(name, state);
    for (guint j = 0; j < wallpapers->len; j++) refresh_wallpaper(g_ptr_array_index(wallpapers, j));
    if (g_strcmp0(name, "hide-paused") == 0) update_visibility();
    if (g_strcmp0(name, "launch-login") == 0) {
        gchar *autostart = g_build_filename(g_get_user_config_dir(), "autostart", "needle.desktop", NULL);
        if (state) {
            gchar *dir = g_path_get_dirname(autostart); g_mkdir_with_parents(dir, 0700); g_free(dir);
            g_file_set_contents(autostart, "[Desktop Entry]\nType=Application\nName=Needle Desktop\nExec=needle\nX-GNOME-Autostart-enabled=true\nNoDisplay=true\n", -1, NULL);
        } else g_remove(autostart);
        g_free(autostart);
    }
    return FALSE;
}

static gboolean settings_delete(GtkWidget *widget, GdkEvent *event, gpointer data) {
    (void)event; (void)data; gtk_widget_hide(widget); return TRUE;
}

static void open_settings(void) {
    if (settings_window) { gtk_window_present(GTK_WINDOW(settings_window)); return; }
    settings_window = gtk_application_window_new(GTK_APPLICATION(app));
    gtk_window_set_title(GTK_WINDOW(settings_window), "Needle Settings");
    gtk_window_set_default_size(GTK_WINDOW(settings_window), 360, 610);
    gtk_window_set_resizable(GTK_WINDOW(settings_window), FALSE);
    g_signal_connect(settings_window, "delete-event", G_CALLBACK(settings_delete), NULL);
    style_settings(settings_window);
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 21);
    gtk_container_add(GTK_CONTAINER(settings_window), outer);
    GtkWidget *brand = gtk_label_new("needle"); gtk_widget_set_halign(brand, GTK_ALIGN_START); gtk_widget_set_name(brand, "brand");
    gtk_box_pack_start(GTK_BOX(outer), brand, FALSE, FALSE, 0);
    GtkWidget *sub = gtk_label_new("YOUR DESKTOP, IN ROTATION"); gtk_widget_set_halign(sub, GTK_ALIGN_START); gtk_style_context_add_class(gtk_widget_get_style_context(sub), "eyebrow");
    gtk_box_pack_start(GTK_BOX(outer), sub, FALSE, FALSE, 3);
    GtkWidget *playerrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12); gtk_widget_set_margin_top(playerrow, 16); gtk_widget_set_margin_bottom(playerrow, 14);
    GtkWidget *cover = gtk_drawing_area_new(); gtk_widget_set_size_request(cover, 42, 42); gtk_widget_set_name(cover, "mini-cover"); settings_cover=cover;
    g_signal_connect(cover, "draw", G_CALLBACK(mini_cover_draw), NULL);
    GtkWidget *trackcopy = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    GtkWidget *connected = gtk_label_new("CONNECTED PLAYER"); gtk_widget_set_halign(connected, GTK_ALIGN_START); gtk_style_context_add_class(gtk_widget_get_style_context(connected), "eyebrow");
    GtkWidget *track = gtk_label_new(now.title && *now.title ? now.title : "No player active"); gtk_widget_set_halign(track, GTK_ALIGN_START); gtk_label_set_ellipsize(GTK_LABEL(track), PANGO_ELLIPSIZE_END); settings_track_label=track;
    GtkWidget *artist = gtk_label_new(now.artist && *now.artist ? now.artist : "Start a player with MPRIS support"); gtk_widget_set_halign(artist, GTK_ALIGN_START); gtk_style_context_add_class(gtk_widget_get_style_context(artist), "row-note"); settings_artist_label=artist;
    gtk_box_pack_start(GTK_BOX(trackcopy), connected, FALSE, FALSE, 0); gtk_box_pack_start(GTK_BOX(trackcopy), track, FALSE, FALSE, 0); gtk_box_pack_start(GTK_BOX(trackcopy), artist, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(playerrow), cover, FALSE, FALSE, 0); gtk_box_pack_start(GTK_BOX(playerrow), trackcopy, TRUE, TRUE, 0);
    const char *playerbuttons[] = {"|◂", "Ⅱ", "▸|"}; const char *methods[] = {"Previous", "PlayPause", "Next"};
    for (int i=0;i<3;i++) { GtkWidget *b=gtk_button_new_with_label(playerbuttons[i]); g_signal_connect(b,"clicked",G_CALLBACK(button_method),(gpointer)methods[i]); gtk_box_pack_start(GTK_BOX(playerrow),b,FALSE,FALSE,0); }
    gtk_box_pack_start(GTK_BOX(outer), playerrow, FALSE, FALSE, 0);
    GtkWidget *section = gtk_label_new("THE LISTENING ROOM"); gtk_widget_set_halign(section, GTK_ALIGN_START); gtk_style_context_add_class(gtk_widget_get_style_context(section), "section");
    gtk_box_pack_start(GTK_BOX(outer), section, FALSE, FALSE, 7);
    GtkWidget *scene = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(scene), "after-hours", "After hours");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(scene), "listening-room", "Listening room");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(scene), "studio", "Studio");
    const char *sname=get_pref("scene","after-hours"); gtk_combo_box_set_active_id(GTK_COMBO_BOX(scene),sname);
    g_signal_connect(scene,"changed",G_CALLBACK(scene_changed),NULL);
    GtkWidget *scenerow = row_box("Room scene", "Choose the atmosphere"); gtk_box_pack_start(GTK_BOX(scenerow),scene,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(outer),scenerow,FALSE,FALSE,0);
    GtkWidget *slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, .72, 1.24, .01); gtk_range_set_value(GTK_RANGE(slider), get_scale()); gtk_scale_set_draw_value(GTK_SCALE(slider), FALSE); gtk_widget_set_size_request(slider, 115, -1); g_signal_connect(slider,"value-changed",G_CALLBACK(scale_changed),NULL);
    GtkWidget *sizerow = row_box("Turntable size", "Scale it to your desktop"); gtk_box_pack_start(GTK_BOX(sizerow),slider,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(outer),sizerow,FALSE,FALSE,0);
    struct {const char *title,*note,*key; gboolean fallback;} toggles[] = {
        {"Room background", "Wall and warm wood desk", "backdrop", TRUE},
        {"Show album sleeve", "Display artwork beside the deck", "show-art", TRUE},
        {"Now playing details", "Track and artist under the deck", "show-caption", TRUE},
        {"Hide while paused", "Clear the desktop when playback stops", "hide-paused", FALSE},
        {"Launch on login", "Start the desktop scene with your session", "launch-login", FALSE}
    };
    for (guint i=0;i<G_N_ELEMENTS(toggles);i++) {
        GtkWidget *row=row_box(toggles[i].title,toggles[i].note), *toggle=gtk_switch_new();
        gtk_switch_set_active(GTK_SWITCH(toggle),get_bool_pref(toggles[i].key,toggles[i].fallback));
        g_signal_connect(toggle,"state-set",G_CALLBACK(switch_changed),(gpointer)toggles[i].key);
        gtk_widget_set_valign(toggle,GTK_ALIGN_CENTER); gtk_box_pack_start(GTK_BOX(row),toggle,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(outer),row,FALSE,FALSE,0);
    }
    GtkWidget *foot = gtk_label_new("WAYLAND  ·  LAYER SHELL  ·  MPRIS"); gtk_widget_set_halign(foot, GTK_ALIGN_START); gtk_widget_set_margin_top(foot, 13); gtk_style_context_add_class(gtk_widget_get_style_context(foot), "eyebrow"); gtk_box_pack_start(GTK_BOX(outer), foot, FALSE, FALSE, 0);
    gtk_widget_show_all(settings_window);
}

static void invalidate_static_scenes(void) { /* reserved for the cached scene renderer */ }

static int command_line(GApplication *application, GApplicationCommandLine *cmdline, gpointer data) {
    (void)data;
    int argc=0; char **argv=g_application_command_line_get_arguments(cmdline,&argc);
    settings_requested=FALSE;
    for(int i=1;i<argc;i++) if(g_strcmp0(argv[i],"--settings")==0) settings_requested=TRUE;
    g_strfreev(argv);
    g_application_activate(application);
    return 0;
}

static void activate(GApplication *application, gpointer data) {
    (void)application; (void)data;
    start_wallpaper();
    if(settings_requested) { open_settings(); settings_requested=FALSE; }
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

static void load_visual_assets(void) {
    gchar *room = find_asset_path("after-hours.jpg");
    gchar *day = find_asset_path("listening-room.jpg");
    gchar *studio = find_asset_path("studio.jpg");
    gchar *deck = find_asset_path("turntable.png");
    if (room) { room_after_hours = gdk_pixbuf_new_from_file(room, NULL); g_free(room); }
    if (day) { room_day = gdk_pixbuf_new_from_file(day, NULL); g_free(day); }
    if (studio) { room_studio = gdk_pixbuf_new_from_file(studio, NULL); g_free(studio); }
    if (deck) { turntable_asset = gdk_pixbuf_new_from_file(deck, NULL); g_free(deck); }
    if (!room_after_hours || !room_day || !room_studio || !turntable_asset)
        g_warning("Needle visual assets are missing; set NEEDLE_ASSET_DIR to the assets folder.");
}

static void app_startup(GApplication *application, gpointer data) {
    (void)data;
    load_visual_assets();
    prefs=g_key_file_new();
    const char *config=g_get_user_config_dir();
    prefs_path=g_build_filename(config,"needle","config.ini",NULL);
    g_key_file_load_from_file(prefs,prefs_path,G_KEY_FILE_KEEP_COMMENTS,NULL);
    wallpapers=g_ptr_array_new_with_free_func(wallpaper_free);
    bus=g_bus_get_sync(G_BUS_TYPE_SESSION,NULL,NULL);
    http_session=soup_session_new();
    g_object_set(http_session, "timeout", 8u, "user-agent", "Needle/0.1", NULL);
    g_timeout_add(1400,poll_mpris,NULL);
    g_application_hold(application);
}

int main(int argc, char **argv) {
    app=gtk_application_new("com.needle.desktop",G_APPLICATION_HANDLES_COMMAND_LINE);
    g_signal_connect(app,"startup",G_CALLBACK(app_startup),NULL);
    g_signal_connect(app,"command-line",G_CALLBACK(command_line),NULL);
    g_signal_connect(app,"activate",G_CALLBACK(activate),NULL);
    int status=g_application_run(G_APPLICATION(app),argc,argv);
    g_object_unref(app);
    return status;
}

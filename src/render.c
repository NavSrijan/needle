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

static guint tick_id = 0;
double spin_angle = 0;
static gint64 last_spin_tick = 0;

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

static void draw_sleeve(cairo_t *cr, double x, double y, double size) {
    if (!get_bool_pref("show-art", TRUE)) return;
    set_source(cr, .035, .025, .018, .42); rounded_rect(cr, x + size*.035, y + size*.055, size, size, size*.012); cairo_fill(cr);
    cairo_save(cr);
    cairo_translate(cr, x + size/2, y + size/2); cairo_rotate(cr, -.035); cairo_translate(cr, -size/2, -size/2);
    if (now.cover) {
        cairo_save(cr); cairo_rectangle(cr, 0, 0, size, size); cairo_clip(cr);
        double pw = gdk_pixbuf_get_width(now.cover), ph = gdk_pixbuf_get_height(now.cover);
        double z = MAX(size/pw, size/ph); cairo_translate(cr, (size-pw*z)/2, (size-ph*z)/2); cairo_scale(cr, z, z);
        gdk_cairo_set_source_pixbuf(cr, now.cover, 0, 0); cairo_paint(cr); cairo_restore(cr);
    } else {
        /* No abstract placeholder art: show a plain kraft inner sleeve until cover art arrives. */
        cairo_pattern_t *paper = cairo_pattern_create_linear(0, 0, size, size);
        cairo_pattern_add_color_stop_rgb(paper, 0, .62, .48, .32);
        cairo_pattern_add_color_stop_rgb(paper, 1, .38, .26, .17);
        cairo_rectangle(cr, 0, 0, size, size); cairo_set_source(cr, paper); cairo_fill(cr); cairo_pattern_destroy(paper);
    }
    cairo_rectangle(cr, 0, 0, size, size); cairo_set_line_width(cr, MAX(.6, size*.004)); set_source(cr, .14, .09, .05, .55); cairo_stroke(cr);
    cairo_restore(cr);
}

static void draw_live_record(cairo_t *cr, int w, int h, double angle) {
    /* Coordinates match the hand-drawn platter in scene.c. */
    const double cx = w*.643, cy = h*.566, rx = w*.171, ry = h*.060;
    cairo_save(cr);
    cairo_translate(cr, cx, cy); cairo_scale(cr, 1, ry/rx);
    cairo_pattern_t *vinyl = cairo_pattern_create_radial(-rx*.18, -rx*.2, rx*.025, 0, 0, rx);
    cairo_pattern_add_color_stop_rgb(vinyl, 0, .11, .11, .105);
    cairo_pattern_add_color_stop_rgb(vinyl, .42, .035, .036, .034);
    cairo_pattern_add_color_stop_rgb(vinyl, .92, .018, .019, .018);
    cairo_pattern_add_color_stop_rgb(vinyl, 1, .08, .075, .067);
    cairo_arc(cr, 0, 0, rx*.985, 0, 2*G_PI); cairo_set_source(cr, vinyl); cairo_fill(cr); cairo_pattern_destroy(vinyl);
    /* Fine, evenly spaced grooves keep the disc smooth at desktop viewing distance. */
    for (int i = 0; i < 53; i++) {
        const double r = rx*(.29 + i*.0128);
        set_source(cr, .66, .65, .60, (i % 5 == 0) ? .105 : .034);
        cairo_set_line_width(cr, MAX(.45, w/3000.0));
        cairo_arc(cr, 0, 0, r, 0, 2*G_PI); cairo_stroke(cr);
    }
    cairo_save(cr); cairo_rotate(cr, angle);
    cairo_arc(cr, 0, 0, rx*.77, -.22, .22); set_source(cr, .92, .87, .77, .20);
    cairo_set_line_width(cr, rx*.018); cairo_stroke(cr);
    cairo_restore(cr);
    cairo_pattern_t *label = cairo_pattern_create_radial(-rx*.03, -rx*.04, rx*.01, 0, 0, rx*.23);
    cairo_pattern_add_color_stop_rgb(label, 0, .70, .51, .31);
    cairo_pattern_add_color_stop_rgb(label, .72, .48, .32, .19);
    cairo_pattern_add_color_stop_rgb(label, 1, .24, .17, .11);
    cairo_arc(cr, 0, 0, rx*.23, 0, 2*G_PI); cairo_set_source(cr, label); cairo_fill(cr); cairo_pattern_destroy(label);
    cairo_arc(cr, 0, 0, rx*.23, 0, 2*G_PI); cairo_set_line_width(cr, MAX(1, w/1400.0)); set_source(cr, .13, .095, .06, .68); cairo_stroke(cr);
    centered_text(cr, "33⅓", 0, rx*.035, rx*.052, "Sans", .16, .12, .075, .96);
    cairo_arc(cr, 0, 0, rx*.985, 0, 2*G_PI); cairo_set_line_width(cr, MAX(1, w/1700.0)); set_source(cr, .76, .64, .46, .60); cairo_stroke(cr);
    cairo_restore(cr);
}

static void draw_stylus_contact(cairo_t *cr, double scene_w, double scene_h) {
    /* Continue the hand-drawn tonearm over the record so the stylus visibly lands on it. */
    const double start_x = scene_w*.784, start_y = scene_h*.535;
    const double head_x = scene_w*.738, head_y = scene_h*.554;
    const double tip_x = scene_w*.732, tip_y = scene_h*.588;
    double angle = atan2(head_y - start_y, head_x - start_x);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, start_x, start_y); cairo_line_to(cr, head_x, head_y);
    set_source(cr, .72, .67, .58, .96); cairo_set_line_width(cr, scene_w*.004); cairo_stroke(cr);
    cairo_move_to(cr, start_x, start_y); cairo_line_to(cr, head_x, head_y);
    set_source(cr, 1, .94, .81, .86); cairo_set_line_width(cr, scene_w*.0014); cairo_stroke(cr);
    cairo_save(cr); cairo_translate(cr, head_x, head_y); cairo_rotate(cr, angle);
    rounded_rect(cr, -scene_w*.010, -scene_h*.006, scene_w*.021, scene_h*.013, scene_w*.0025);
    set_source(cr, .12, .105, .088, 1); cairo_fill(cr);
    cairo_restore(cr);
    cairo_move_to(cr, head_x, head_y + scene_h*.005);
    cairo_line_to(cr, tip_x, tip_y);
    set_source(cr, .91, .83, .67, .98); cairo_set_line_width(cr, MAX(.8, scene_w*.00065)); cairo_stroke(cr);
    cairo_arc(cr, tip_x, tip_y, MAX(1.1, scene_w*.0008), 0, 2*G_PI);
    set_source(cr, .97, .89, .72, 1); cairo_fill(cr);
}

static void draw_recent_covers(cairo_t *cr, double scene_w, double scene_h) {
    const GPtrArray *recent = player_recent_covers();
    if (!get_bool_pref("show-art", TRUE) || !recent) return;
    const double centers[] = {scene_w*.452, scene_w*.656, scene_w*.860};
    const double side = scene_h*.153;
    for (guint i = 0; i < G_N_ELEMENTS(centers) && i < recent->len; i++) {
        RecentCover *item = g_ptr_array_index((GPtrArray *)recent, i);
        if (!item || !item->cover) continue;
        double x = centers[i] - side*.5, y = scene_h*.047;
        cairo_save(cr);
        cairo_rectangle(cr, x, y, side, side); cairo_clip(cr);
        paint_cover_image(cr, item->cover, x, y, side, side);
        cairo_restore(cr);
    }
}

static void draw_cover_stand(cairo_t *cr, double x, double y, double size) {
    /* Walnut easel and a shallow shelf sit behind the jacket. */
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    set_source(cr, .16, .075, .032, .95); cairo_set_line_width(cr, size*.032);
    cairo_move_to(cr, x + size*.12, y + size*.82); cairo_line_to(cr, x + size*.035, y + size*1.08); cairo_stroke(cr);
    cairo_move_to(cr, x + size*.88, y + size*.82); cairo_line_to(cr, x + size*.965, y + size*1.08); cairo_stroke(cr);
    set_source(cr, .30, .15, .065, .98);
    rounded_rect(cr, x - size*.045, y + size*.985, size*1.09, size*.075, size*.018); cairo_fill(cr);
    set_source(cr, .67, .41, .18, .72); cairo_set_line_width(cr, MAX(.7, size*.006));
    cairo_move_to(cr, x - size*.025, y + size*1.005); cairo_line_to(cr, x + size*1.025, y + size*1.005); cairo_stroke(cr);
}

static void draw_current_sleeve(cairo_t *cr, double x, double y, double size) {
    if (!get_bool_pref("show-art", TRUE)) return;
    draw_cover_stand(cr, x, y, size);
    draw_sleeve(cr, x, y, size);
    if (get_bool_pref("show-caption", TRUE) && now.title && *now.title) {
        char *title = g_strdup(now.title);
        if (strlen(title) > 25) title[25] = 0;
        centered_text(cr, title, x + size*.5, y + size*1.039, size*.040,
            "Sans", .91, .78, .56, .96);
        g_free(title);
    }
}

void draw_scene(cairo_t *cr, int w, int h, double angle) {
    cairo_save(cr);
    const double scene_w = 1920;
    const double scene_h = 1080;
    const char *framing = get_pref("framing", "mid");
    double zoom = g_strcmp0(framing, "wide") == 0 ? .94 : g_strcmp0(framing, "close") == 0 ? 1.07 : 1.0;
    const double fit = MAX(w/scene_w, h/scene_h);
    cairo_translate(cr, w*.5, h*.5); cairo_scale(cr, fit*zoom, fit*zoom);
    cairo_translate(cr, -scene_w*.5, -scene_h*.5);
    if (!get_bool_pref("transparent-background", FALSE)) draw_programmatic_scene(cr, scene_w, scene_h);
    draw_recent_covers(cr, scene_w, scene_h);
    draw_live_record(cr, scene_w, scene_h, angle);
    draw_stylus_contact(cr, scene_w, scene_h);
    double sleeve_size = scene_w*.105;
    draw_current_sleeve(cr, scene_w*.265, scene_h*.61, sleeve_size);
    cairo_restore(cr);
}

gboolean wallpaper_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
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

void start_animation(void) {
    if (tick_id == 0) tick_id = g_timeout_add(40, tick_wallpapers, NULL);
}

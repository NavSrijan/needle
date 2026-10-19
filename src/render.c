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

static void fitted_centered_text(cairo_t *cr, const char *value, double cx, double y,
                                double max_width, double size, gboolean bold,
                                double r, double g, double b, double a) {
    if (!value || !*value) return;
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
        bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, value, &ext);
    double scale_x = ext.width > max_width ? max_width / ext.width : 1.0;
    cairo_save(cr);
    cairo_translate(cr, cx, y);
    cairo_scale(cr, scale_x, 1.0);
    set_source(cr, r, g, b, a);
    cairo_move_to(cr, -ext.width / 2.0, 0);
    cairo_show_text(cr, value);
    cairo_restore(cr);
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
    cairo_arc(cr, 0, 0, rx*.985, 0, 2*G_PI); cairo_set_line_width(cr, MAX(1, w/1700.0)); set_source(cr, .76, .64, .46, .60); cairo_stroke(cr);
    cairo_restore(cr);
}

static void draw_stylus_contact(cairo_t *cr, double scene_w, double scene_h) {
    /* Continue the scene's tonearm precisely from its drawn stylus end. */
    const double start_x = scene_w*.785, start_y = scene_h*.55125;
    const double head_x = scene_w*.757, head_y = scene_h*.5625;
    const double tip_x = scene_w*.754, tip_y = scene_h*.5875;
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

static void draw_recent_covers(cairo_t *cr, double scene_w, double scene_h,
                               const GPtrArray *recent, const NowPlaying *current, double opacity) {
    if (!get_bool_pref("show-art", TRUE) || !recent) return;
    /* These are the three square picture windows in scene.c's 1200 x 800 design. */
    const double centers[] = {459, 660, 861};
    const double side = 150;
    gchar *current_key = g_strdup_printf("%s\n%s\n%s", current->title ? current->title : "",
        current->artist ? current->artist : "", current->album ? current->album : "");
    cairo_push_group(cr);
    guint shown = 0;
    for (guint i = 0; i < recent->len && shown < G_N_ELEMENTS(centers); i++) {
        RecentCover *item = g_ptr_array_index((GPtrArray *)recent, i);
        if (!item) continue;
        /* The current track is already on the table sleeve; the wall shows history. */
        if ((current->title || current->artist || current->album) && g_strcmp0(item->key, current_key) == 0) continue;
        const guint slot = shown++;
        double scale_x = scene_w / 1200.0, scale_y = scene_h / 800.0;
        double x = (centers[slot] - side*.5) * scale_x, y = 56 * scale_y;
        double cover_w = side * scale_x, cover_h = side * scale_y;
        if (item->cover) {
            cairo_save(cr);
            cairo_rectangle(cr, x, y, cover_w, cover_h); cairo_clip(cr);
            paint_cover_image(cr, item->cover, x, y, cover_w, cover_h);
            cairo_restore(cr);
        }

        gchar **metadata = g_strsplit(item->key ? item->key : "", "\n", 3);
        fitted_centered_text(cr, metadata[0], centers[slot] * scale_x, 231 * scale_y,
            164 * scale_x, 9.2 * scale_y, TRUE, .94, .86, .73, .98);
        fitted_centered_text(cr, metadata[1], centers[slot] * scale_x, 244 * scale_y,
            164 * scale_x, 7.0 * scale_y, FALSE, .78, .69, .57, .88);
        g_strfreev(metadata);
    }
    cairo_pop_group_to_source(cr); cairo_paint_with_alpha(cr, CLAMP(opacity, 0.0, 1.0));
    g_free(current_key);
}

static void draw_current_cover(cairo_t *cr, double scene_w, double scene_h,
                               const NowPlaying *track, double opacity) {
    if (!get_bool_pref("show-art", TRUE)) return;
    gboolean has_metadata = (track->title && *track->title) || (track->artist && *track->artist) || (track->album && *track->album);
    if ((!track->cover && !has_metadata) || opacity <= 0.0) return;
    const double scale_x = scene_w / 1200.0, scale_y = scene_h / 800.0;
    cairo_save(cr);
    cairo_matrix_t jacket;
    cairo_matrix_init(&jacket, .80, -.225, .435, .66, 186, 608);
    cairo_scale(cr, scale_x, scale_y);
    cairo_transform(cr, &jacket);
    cairo_push_group(cr);
    cairo_rectangle(cr, 6, 6, 188, 188); cairo_clip(cr);
    if (track->cover) {
        paint_cover_image(cr, track->cover, 6, 6, 188, 188);
    } else {
        cairo_pattern_t *paper = cairo_pattern_create_linear(6, 6, 194, 194);
        cairo_pattern_add_color_stop_rgb(paper, 0, .075, .16, .19);
        cairo_pattern_add_color_stop_rgb(paper, 1, .035, .075, .09);
        cairo_rectangle(cr, 6, 6, 188, 188); cairo_set_source(cr, paper); cairo_fill(cr);
        cairo_pattern_destroy(paper);
        fitted_centered_text(cr, track->title, 100, 91, 166, 18, TRUE, .97, .91, .81, 1);
        fitted_centered_text(cr, track->artist, 100, 116, 166, 11, FALSE, .83, .75, .63, .96);
        fitted_centered_text(cr, track->album, 100, 145, 166, 8, FALSE, .73, .66, .56, .86);
    }
    cairo_pop_group_to_source(cr); cairo_paint_with_alpha(cr, CLAMP(opacity, 0.0, 1.0));
    cairo_restore(cr);
}

static char *player_display_name(const char *bus_name) {
    if (!bus_name || !*bus_name) return NULL;
    const char *short_name = strrchr(bus_name, '.');
    short_name = short_name ? short_name + 1 : bus_name;
    char *name = g_strdup(short_name);
    if (*name) name[0] = g_ascii_toupper(name[0]);
    return name;
}

static void draw_player_metadata(cairo_t *cr, double scene_w, double scene_h,
                                 const NowPlaying *track, double opacity) {
    const double sx = scene_w / 1200.0, sy = scene_h / 800.0;
    const gboolean has_track = (track->title && *track->title) || (track->artist && *track->artist) ||
                               (track->album && *track->album);
    if (!has_track || opacity <= 0.0) return;
    const char *fields[5];
    gboolean bold[5];
    guint count = 0;
    char *player_name = NULL;

    if (get_bool_pref("show-title", TRUE) && track->title && *track->title) {
        fields[count] = track->title; bold[count++] = TRUE;
    }
    if (get_bool_pref("show-artist", TRUE) && track->artist && *track->artist) {
        fields[count] = track->artist; bold[count++] = FALSE;
    }
    if (get_bool_pref("show-album", TRUE) && track->album && *track->album) {
        fields[count] = track->album; bold[count++] = FALSE;
    }
    if (get_bool_pref("show-status", FALSE) && track->status && *track->status) {
        fields[count] = track->status; bold[count++] = FALSE;
    }
    if (get_bool_pref("show-player", FALSE) && track->player) {
        player_name = player_display_name(track->player);
        if (player_name && *player_name) { fields[count] = player_name; bold[count++] = FALSE; }
    }
    if (count == 0) { g_free(player_name); return; }

    /* Small inset display integrated into the turntable's walnut front panel. */
    cairo_save(cr);
    cairo_scale(cr, sx, sy);
    cairo_push_group(cr);
    rounded_rect(cr, 470, 577, 423, 57, 5);
    set_source(cr, .035, .055, .062, .96); cairo_fill_preserve(cr);
    set_source(cr, .57, .40, .24, .84); cairo_set_line_width(cr, 1.0); cairo_stroke(cr);
    set_source(cr, .91, .72, .48, .24); cairo_set_line_width(cr, .8);
    cairo_move_to(cr, 478, 580); cairo_line_to(cr, 885, 580); cairo_stroke(cr);

    double first_y, last_y;
    switch (count) {
        case 1: first_y = last_y = 610; break;
        case 2: first_y = 600; last_y = 620; break;
        case 3: first_y = 596; last_y = 620; break;
        case 4: first_y = 594; last_y = 622; break;
        default: first_y = 592; last_y = 624; break;
    }
    for (guint i = 0; i < count; i++) {
        double y = count == 1 ? first_y : first_y + (last_y-first_y)*i/(count-1);
        double size = i == 0 && bold[i] ? 11.2 : (count > 3 ? 7.2 : 8.5);
        double r = (i == 0 && bold[i]) ? .97 : .77;
        double g = (i == 0 && bold[i]) ? .91 : .78;
        double b = (i == 0 && bold[i]) ? .80 : .66;
        fitted_centered_text(cr, fields[i], 682, y, 382, size, bold[i], r, g, b, .96);
    }
    cairo_pop_group_to_source(cr); cairo_paint_with_alpha(cr, CLAMP(opacity, 0.0, 1.0));
    cairo_restore(cr);
    g_free(player_name);
}

void draw_scene(cairo_t *cr, int w, int h, double angle) {
    cairo_save(cr);
    const double scene_w = 1200;
    /* Match the virtual canvas to the monitor aspect before applying framing.
       This preserves the complete top and bottom edges on widescreen displays. */
    const double scene_h = scene_w * ((double)h / MAX(w, 1));
    const char *framing = get_pref("framing", "mid");
    double zoom = g_strcmp0(framing, "wide") == 0 ? .94 : g_strcmp0(framing, "close") == 0 ? 1.07 : 1.0;
    const double fit = MAX(w/scene_w, h/scene_h);
    cairo_translate(cr, w*.5, h*.5); cairo_scale(cr, fit*zoom, fit*zoom);
    cairo_translate(cr, -scene_w*.5, -scene_h*.5);
    double transition = player_transition_progress();
    gboolean transitioning = player_transition_active();
    if (!get_bool_pref("transparent-background", FALSE)) draw_programmatic_scene(cr, scene_w, scene_h);
    if (transitioning) {
        draw_recent_covers(cr, scene_w, scene_h, player_transition_recent_covers(), &transition_from, 1.0-transition);
        draw_current_cover(cr, scene_w, scene_h, &transition_from, 1.0-transition);
        draw_player_metadata(cr, scene_w, scene_h, &transition_from, 1.0-transition);
    }
    draw_recent_covers(cr, scene_w, scene_h, player_recent_covers(), &now, transitioning ? transition : 1.0);
    draw_live_record(cr, scene_w, scene_h, angle);
    draw_stylus_contact(cr, scene_w, scene_h);
    draw_current_cover(cr, scene_w, scene_h, &now, transitioning ? transition : 1.0);
    draw_player_metadata(cr, scene_w, scene_h, &now, transitioning ? transition : 1.0);
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
    gboolean playing = g_strcmp0(now.status, "Playing") == 0;
    double elapsed = playing ? (now_us - last_spin_tick) / 1000000.0 : 0.0;
    last_spin_tick = now_us;
    if (playing) spin_angle = fmod(spin_angle + elapsed * (2.0 * G_PI * 33.333333 / 60.0), 2.0 * G_PI);
    if (!playing && !player_transition_active()) return G_SOURCE_CONTINUE;
    for (guint i = 0; i < wallpapers->len; i++) {
        Wallpaper *wp = g_ptr_array_index(wallpapers, i);
        if (wp->canvas) gtk_widget_queue_draw(wp->canvas);
    }
    return G_SOURCE_CONTINUE;
}

void start_animation(void) {
    if (tick_id == 0) tick_id = g_timeout_add(40, tick_wallpapers, NULL);
}

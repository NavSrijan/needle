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

double spin_angle = 0;
static gint64 last_spin_tick_us = 0;

static void queue_scene_damage(GtkWidget *widget, double x, double y, double width, double height) {
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    if (allocation.width < 1 || allocation.height < 1) return;

    const double scene_w = 1200.0;
    const double scene_h = scene_w * ((double)allocation.height / allocation.width);
    const char *framing = get_pref("framing", "mid");
    const double zoom = g_strcmp0(framing, "wide") == 0 ? .94 :
                        g_strcmp0(framing, "close") == 0 ? 1.07 : 1.0;
    const double scale = MAX(allocation.width / scene_w, allocation.height / scene_h) * zoom;
    const double offset_x = (allocation.width - scene_w * scale) * .5;
    const double offset_y = (allocation.height - scene_h * scale) * .5;
    const int left = MAX(0, (int)floor(offset_x + x * scale - 4));
    const int top = MAX(0, (int)floor(offset_y + y * scale - 4));
    const int right = MIN(allocation.width, (int)ceil(offset_x + (x + width) * scale + 4));
    const int bottom = MIN(allocation.height, (int)ceil(offset_y + (y + height) * scale + 4));
    if (right > left && bottom > top)
        gtk_widget_queue_draw_area(widget, left, top, right - left, bottom - top);
}

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

static void paint_cover_image(cairo_t *cr, GdkPixbuf *image, double x, double y, double w, double h,
                              double opacity) {
    double iw = gdk_pixbuf_get_width(image), ih = gdk_pixbuf_get_height(image);
    double scale = MAX(w / iw, h / ih);
    double dx = x + (w - iw * scale) / 2.0;
    double dy = y + (h - ih * scale) / 2.0;
    cairo_save(cr);
    cairo_rectangle(cr, x, y, w, h); cairo_clip(cr);
    cairo_translate(cr, dx, dy); cairo_scale(cr, scale, scale);
    gdk_cairo_set_source_pixbuf(cr, image, 0, 0); cairo_paint_with_alpha(cr, CLAMP(opacity, 0.0, 1.0));
    cairo_restore(cr);
}

static void draw_record_base(cairo_t *cr, double scene_w, double rx) {
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
        cairo_set_line_width(cr, MAX(.45, scene_w/3000.0));
        cairo_arc(cr, 0, 0, r, 0, 2*G_PI); cairo_stroke(cr);
    }
    cairo_pattern_t *label = cairo_pattern_create_radial(-rx*.03, -rx*.04, rx*.01, 0, 0, rx*.23);
    cairo_pattern_add_color_stop_rgb(label, 0, .70, .51, .31);
    cairo_pattern_add_color_stop_rgb(label, .72, .48, .32, .19);
    cairo_pattern_add_color_stop_rgb(label, 1, .24, .17, .11);
    cairo_arc(cr, 0, 0, rx*.23, 0, 2*G_PI); cairo_set_source(cr, label); cairo_fill(cr); cairo_pattern_destroy(label);
    cairo_arc(cr, 0, 0, rx*.23, 0, 2*G_PI); cairo_set_line_width(cr, MAX(1, scene_w/1400.0)); set_source(cr, .13, .095, .06, .68); cairo_stroke(cr);
    cairo_arc(cr, 0, 0, rx*.985, 0, 2*G_PI); cairo_set_line_width(cr, MAX(1, scene_w/1700.0)); set_source(cr, .76, .64, .46, .60); cairo_stroke(cr);
}

static void cache_static_platter(Wallpaper *wallpaper, double scene_w, double scene_h, double scale) {
    if (!wallpaper || (wallpaper->static_platter && wallpaper->platter_cache_scale == scale &&
        wallpaper->platter_cache_height == scene_h)) return;
    if (wallpaper->static_platter) cairo_surface_destroy(wallpaper->static_platter);
    const double rx = scene_w*.171, ry = scene_h*.060, margin = 2.0;
    int pixel_w = MAX(1, (int)ceil((2*rx + 2*margin)*scale));
    int pixel_h = MAX(1, (int)ceil((2*ry + 2*margin)*scale));
    wallpaper->static_platter = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pixel_w, pixel_h);
    cairo_t *base = cairo_create(wallpaper->static_platter);
    cairo_set_operator(base, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(base, 0, 0, 0, 0); cairo_paint(base);
    cairo_set_operator(base, CAIRO_OPERATOR_OVER);
    cairo_scale(base, scale, scale);
    cairo_translate(base, rx + margin, ry + margin);
    cairo_scale(base, 1, ry/rx);
    draw_record_base(base, scene_w, rx);
    cairo_destroy(base);
    cairo_surface_flush(wallpaper->static_platter);
    wallpaper->platter_cache_scale = scale;
    wallpaper->platter_cache_height = scene_h;
}

static void draw_live_record(cairo_t *cr, double w, double h, double angle,
                             Wallpaper *wallpaper, double scale) {
    /* The record body and grooves do not move; only its soft highlight rotates. */
    const double cx = w*.643, cy = h*.566, rx = w*.171, ry = h*.060, margin = 2.0;
    if (wallpaper) {
        cache_static_platter(wallpaper, w, h, scale);
        cairo_save(cr);
        cairo_translate(cr, cx-rx-margin, cy-ry-margin);
        cairo_scale(cr, 1.0/scale, 1.0/scale);
        cairo_set_source_surface(cr, wallpaper->static_platter, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
    } else {
        cairo_save(cr); cairo_translate(cr, cx, cy); cairo_scale(cr, 1, ry/rx);
        draw_record_base(cr, w, rx); cairo_restore(cr);
    }
    cairo_save(cr); cairo_translate(cr, cx, cy); cairo_scale(cr, 1, ry/rx); cairo_rotate(cr, angle);
    cairo_arc(cr, 0, 0, rx*.77, -.22, .22); set_source(cr, .92, .87, .77, .20);
    cairo_set_line_width(cr, rx*.018); cairo_stroke(cr);
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
    if (!get_bool_pref("show-art", TRUE) || !recent || opacity <= 0.0) return;
    /* These are the three square picture windows in scene.c's 1200 x 800 design. */
    const double centers[] = {459, 660, 861};
    const double side = 150;
    guint shown = 0;
    for (guint i = 0; i < recent->len && shown < G_N_ELEMENTS(centers); i++) {
        RecentCover *item = g_ptr_array_index((GPtrArray *)recent, i);
        if (!item) continue;
        /* The current track is already on the table sleeve; the wall shows history. */
        if ((current->title || current->artist) &&
            g_strcmp0(item->title ? item->title : "", current->title ? current->title : "") == 0 &&
            g_strcmp0(item->artist ? item->artist : "", current->artist ? current->artist : "") == 0) continue;
        /* Empty gallery frames are less useful than showing only tracks with real artwork. */
        if (!item->cover) continue;
        const guint slot = shown++;
        double scale_x = scene_w / 1200.0, scale_y = scene_h / 800.0;
        double x = (centers[slot] - side*.5) * scale_x, y = 56 * scale_y;
        double cover_w = side * scale_x, cover_h = side * scale_y;
        if (item->cover) {
            cairo_save(cr);
            cairo_rectangle(cr, x, y, cover_w, cover_h); cairo_clip(cr);
            paint_cover_image(cr, item->cover, x, y, cover_w, cover_h, opacity);
            cairo_restore(cr);
        }

        fitted_centered_text(cr, item->title, centers[slot] * scale_x, 231 * scale_y,
            164 * scale_x, 9.2 * scale_y, TRUE, .94, .86, .73, .98 * opacity);
        fitted_centered_text(cr, item->artist, centers[slot] * scale_x, 244 * scale_y,
            164 * scale_x, 7.0 * scale_y, FALSE, .78, .69, .57, .88 * opacity);
    }
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
    cairo_rectangle(cr, 6, 6, 188, 188); cairo_clip(cr);
    if (track->cover) {
        paint_cover_image(cr, track->cover, 6, 6, 188, 188, opacity);
    } else {
        cairo_pattern_t *paper = cairo_pattern_create_linear(6, 6, 194, 194);
        cairo_pattern_add_color_stop_rgba(paper, 0, .075, .16, .19, opacity);
        cairo_pattern_add_color_stop_rgba(paper, 1, .035, .075, .09, opacity);
        cairo_rectangle(cr, 6, 6, 188, 188); cairo_set_source(cr, paper); cairo_fill(cr);
        cairo_pattern_destroy(paper);
        fitted_centered_text(cr, track->title, 100, 91, 166, 18, TRUE, .97, .91, .81, opacity);
        fitted_centered_text(cr, track->artist, 100, 116, 166, 11, FALSE, .83, .75, .63, .96 * opacity);
        fitted_centered_text(cr, track->album, 100, 145, 166, 8, FALSE, .73, .66, .56, .86 * opacity);
    }
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
    if (get_bool_pref("show-album", TRUE) && track->album && *track->album &&
        g_strcmp0(track->album, track->title) != 0 && g_strcmp0(track->album, track->artist) != 0) {
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
    rounded_rect(cr, 470, 577, 423, 57, 5);
    set_source(cr, .035, .055, .062, .96 * opacity); cairo_fill_preserve(cr);
    set_source(cr, .57, .40, .24, .84 * opacity); cairo_set_line_width(cr, 1.0); cairo_stroke(cr);
    set_source(cr, .91, .72, .48, .24 * opacity); cairo_set_line_width(cr, .8);
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
        fitted_centered_text(cr, fields[i], 682, y, 382, size, bold[i], r, g, b, .96 * opacity);
    }
    cairo_restore(cr);
    g_free(player_name);
}

static void set_scene_transform(cairo_t *cr, int w, int h, double scene_w, double scene_h, double zoom) {
    const double fit = MAX(w/scene_w, h/scene_h);
    cairo_translate(cr, w*.5, h*.5); cairo_scale(cr, fit*zoom, fit*zoom);
    cairo_translate(cr, -scene_w*.5, -scene_h*.5);
}

static void cache_static_scene(Wallpaper *wallpaper, int w, int h,
                               double scene_w, double scene_h, double zoom) {
    if (!wallpaper || (wallpaper->static_scene && wallpaper->width == w &&
        wallpaper->height == h && wallpaper->static_scene_zoom == zoom)) return;
    if (wallpaper->static_scene) cairo_surface_destroy(wallpaper->static_scene);

    /* Rasterize once per monitor; replaying a Cairo recording still reruns every scene path per frame. */
    const double resolution_scale = MIN(1.0, 2560.0 / MAX(w, h));
    int cache_w = MAX(1, (int)lround(w * resolution_scale));
    int cache_h = MAX(1, (int)lround(h * resolution_scale));
    wallpaper->static_scene = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cache_w, cache_h);
    cairo_t *static_cr = cairo_create(wallpaper->static_scene);
    cairo_set_operator(static_cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(static_cr, 0, 0, 0, 0); cairo_paint(static_cr);
    cairo_set_operator(static_cr, CAIRO_OPERATOR_OVER);
    cairo_scale(static_cr, cache_w / (double)w, cache_h / (double)h);
    set_scene_transform(static_cr, w, h, scene_w, scene_h, zoom);
    draw_programmatic_scene(static_cr, scene_w, scene_h);
    cairo_destroy(static_cr);
    wallpaper->width = w;
    wallpaper->height = h;
    wallpaper->static_scene_zoom = zoom;
    cairo_surface_flush(wallpaper->static_scene);
}

void draw_scene(cairo_t *cr, int w, int h, double angle, Wallpaper *wallpaper) {
    cairo_save(cr);
    const double scene_w = 1200;
    /* Match the virtual canvas to the monitor aspect before applying framing.
       This preserves the complete top and bottom edges on widescreen displays. */
    const double scene_h = scene_w * ((double)h / MAX(w, 1));
    const char *framing = get_pref("framing", "mid");
    double zoom = g_strcmp0(framing, "wide") == 0 ? .94 : g_strcmp0(framing, "close") == 0 ? 1.07 : 1.0;
    double transition = player_transition_progress();
    gboolean transitioning = player_transition_active();
    if (!get_bool_pref("transparent-background", FALSE)) {
        if (wallpaper) {
            cache_static_scene(wallpaper, w, h, scene_w, scene_h, zoom);
            int cache_w = cairo_image_surface_get_width(wallpaper->static_scene);
            int cache_h = cairo_image_surface_get_height(wallpaper->static_scene);
            cairo_save(cr); cairo_identity_matrix(cr);
            cairo_scale(cr, w / (double)cache_w, h / (double)cache_h);
            cairo_set_source_surface(cr, wallpaper->static_scene, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);
        } else {
            cairo_save(cr); set_scene_transform(cr, w, h, scene_w, scene_h, zoom);
            draw_programmatic_scene(cr, scene_w, scene_h); cairo_restore(cr);
        }
    }
    cairo_save(cr); set_scene_transform(cr, w, h, scene_w, scene_h, zoom);
    if (transitioning) {
        draw_recent_covers(cr, scene_w, scene_h, player_transition_recent_covers(), &transition_from, 1.0-transition);
        draw_current_cover(cr, scene_w, scene_h, &transition_from, 1.0-transition);
        draw_player_metadata(cr, scene_w, scene_h, &transition_from, 1.0-transition);
    }
    draw_recent_covers(cr, scene_w, scene_h, player_recent_covers(), &now, transitioning ? transition : 1.0);
    const double scene_scale = MAX(w/scene_w, h/scene_h) * zoom;
    draw_live_record(cr, scene_w, scene_h, angle, wallpaper, scene_scale);
    draw_stylus_contact(cr, scene_w, scene_h);
    draw_current_cover(cr, scene_w, scene_h, &now, transitioning ? transition : 1.0);
    draw_player_metadata(cr, scene_w, scene_h, &now, transitioning ? transition : 1.0);
    cairo_restore(cr);
    cairo_restore(cr);
}

gboolean wallpaper_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    GtkAllocation a; gtk_widget_get_allocation(widget, &a);
    if (a.width < 2 || a.height < 2) return FALSE;
    draw_scene(cr, a.width, a.height, spin_angle, data);
    return FALSE;
}

static gboolean tick_wallpaper_frame(GtkWidget *widget, GdkFrameClock *clock, gpointer data) {
    Wallpaper *wallpaper = data;
    const gint64 frame_time = gdk_frame_clock_get_frame_time(clock);
    gboolean playing = g_strcmp0(now.status, "Playing") == 0;
    const gint64 now_us = g_get_monotonic_time();
    if (playing) {
        if (last_spin_tick_us > 0) {
            const double elapsed = (now_us - last_spin_tick_us) / 1000000.0;
            spin_angle = fmod(spin_angle + elapsed * (2.0 * G_PI * 33.333333 / 60.0), 2.0 * G_PI);
        }
        last_spin_tick_us = now_us;
    } else {
        last_spin_tick_us = 0;
    }
    gint64 refresh_interval = 0;
    gdk_frame_clock_get_refresh_info(clock, frame_time, &refresh_interval, NULL);
    if (refresh_interval <= 0) refresh_interval = 16667;
    const gint64 target_interval = MAX(refresh_interval, 13889); /* At most 72 redraws per second. */
    if (wallpaper && wallpaper->last_queued_frame_time > 0 &&
        frame_time - wallpaper->last_queued_frame_time < target_interval)
        return G_SOURCE_CONTINUE;
    if (wallpaper) wallpaper->last_queued_frame_time = frame_time;
    if (playing) {
        GtkAllocation allocation;
        gtk_widget_get_allocation(widget, &allocation);
        const double scene_w = 1200.0;
        const double scene_h = scene_w * ((double)allocation.height / MAX(allocation.width, 1));
        const double cx = scene_w * .643, cy = scene_h * .566;
        const double rx = scene_w * .171, ry = scene_h * .060;
        queue_scene_damage(widget, cx-rx-3, cy-ry-3, 2*rx+6, 2*ry+6);
    }
    if (player_transition_active()) {
        queue_scene_damage(widget, 370, 48, 575, 208);
        queue_scene_damage(widget, 180, 548, 730, 205);
    }
    return G_SOURCE_CONTINUE;
}

void start_animation(void) {
    for (guint i = 0; i < wallpapers->len; i++) {
        Wallpaper *wp = g_ptr_array_index(wallpapers, i);
        if (wp->canvas && wp->frame_callback_id == 0)
            wp->frame_callback_id = gtk_widget_add_tick_callback(wp->canvas, tick_wallpaper_frame, wp, NULL);
    }
}

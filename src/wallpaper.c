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

GtkApplication *app;
GPtrArray *wallpapers;
gboolean wallpaper_started = FALSE;

void refresh_wallpaper(Wallpaper *wp) {
    if (wp->canvas) gtk_widget_queue_draw(wp->canvas);
}

void update_visibility(void) {
    gboolean should_hide = get_bool_pref("hide-paused", FALSE) && g_strcmp0(now.status, "Playing") != 0;
    for (guint i = 0; i < wallpapers->len; i++) {
        Wallpaper *wp = g_ptr_array_index(wallpapers, i);
        if (should_hide) gtk_widget_hide(wp->window);
        else gtk_widget_show_all(wp->window);
    }
}

void wallpaper_free(gpointer data) {
    Wallpaper *wp = data;
    if (wp->static_scene) cairo_surface_destroy(wp->static_scene);
    if (wp->static_platter) cairo_surface_destroy(wp->static_platter);
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
    start_animation();
}

void start_wallpaper(void) {
    if (wallpaper_started) return;
    wallpaper_started = TRUE;
    GdkDisplay *display = gdk_display_get_default();
    g_signal_connect(display, "monitor-added", G_CALLBACK(monitors_changed), NULL);
    g_signal_connect(display, "monitor-removed", G_CALLBACK(monitors_changed), NULL);
    int n = gdk_display_get_n_monitors(display);
    for (int i = 0; i < n; i++) build_wallpaper(gdk_display_get_monitor(display, i));
    start_animation();
}

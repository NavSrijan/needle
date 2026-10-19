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

#define MAX_ARTWORK_BYTES (12 * 1024 * 1024)
typedef struct {
    gchar *url;
    SoupMessage *message;
    GCancellable *cancellable;
} CoverRequest;
GDBusConnection *bus;
SoupSession *http_session;
NowPlaying now = {0};

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
    } else now_clear(&candidate);
    return G_SOURCE_CONTINUE;
}

void player_init(void) {
    bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    http_session = soup_session_new();
    g_object_set(http_session, "timeout", 8u, "user-agent", "Needle/0.1", NULL);
    g_timeout_add(1400, poll_mpris, NULL);
}

void player_shutdown(void) {
    now_clear(&now);
    g_clear_object(&bus);
    g_clear_object(&http_session);
}

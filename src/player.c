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
static GPtrArray *recent_covers;
static gint64 transition_started_us;
static gboolean transition_active;
static GCancellable *active_artwork_cancellable;

static void now_clear(NowPlaying *item);

static void recent_cover_free(gpointer data) {
    RecentCover *item = data;
    g_free(item->title);
    g_free(item->artist);
    g_free(item->album);
    g_clear_object(&item->cover);
    g_free(item);
}

static gboolean recent_cover_matches(const RecentCover *item, const NowPlaying *track) {
    const char *item_title = item->title ? item->title : "";
    const char *track_title = track->title ? track->title : "";
    const char *item_artist = item->artist ? item->artist : "";
    const char *track_artist = track->artist ? track->artist : "";
    if (*item_title && *track_title && g_strcmp0(item_title, track_title) == 0)
        return !*item_artist || !*track_artist || g_strcmp0(item_artist, track_artist) == 0;
    if ((!*item_title || !*track_title) && *item_artist && *track_artist &&
        g_strcmp0(item_artist, track_artist) == 0) {
        const char *item_album = item->album ? item->album : "";
        const char *track_album = track->album ? track->album : "";
        return !*item_album || !*track_album || g_strcmp0(item_album, track_album) == 0;
    }
    return FALSE;
}

static void remember_current_cover(void) {
    if (!recent_covers || (!now.title && !now.artist && !now.album)) return;
    GdkPixbuf *cover = now.cover ? g_object_ref(now.cover) : NULL;
    for (guint i = 0; i < recent_covers->len; i++) {
        RecentCover *old = g_ptr_array_index(recent_covers, i);
        if (recent_cover_matches(old, &now)) {
            if (!cover && old->cover) cover = g_object_ref(old->cover);
            g_ptr_array_remove_index(recent_covers, i);
            break;
        }
    }
    RecentCover *item = g_new0(RecentCover, 1);
    item->title = g_strdup(now.title);
    item->artist = g_strdup(now.artist);
    item->album = g_strdup(now.album);
    item->cover = cover;
    g_ptr_array_insert(recent_covers, 0, item);
    /* Retain the current item plus three previous tracks for the wall gallery. */
    while (recent_covers->len > 4) g_ptr_array_remove_index(recent_covers, recent_covers->len - 1);
}

const GPtrArray *player_recent_covers(void) { return recent_covers; }
gboolean player_transition_active(void) { return transition_active; }

static void start_track_transition(void) {
    transition_started_us = g_get_monotonic_time();
    transition_active = TRUE;
}

double player_transition_progress(void) {
    if (!transition_active) return 1.0;
    double t = (g_get_monotonic_time() - transition_started_us) / 650000.0;
    if (t >= 1.0) {
        transition_active = FALSE;
        return 1.0;
    }
    t = CLAMP(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
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
    /* Unknown-length (including chunked) bodies cannot be safely buffered by send_and_read. */
    if (length < 0 || length > MAX_ARTWORK_BYTES) g_cancellable_cancel(cancellable);
}

static void cover_downloaded(GObject *source, GAsyncResult *result, gpointer data) {
    CoverRequest *request = data;
    if (active_artwork_cancellable == request->cancellable)
        g_clear_object(&active_artwork_cancellable);
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
        remember_current_cover();
        for (guint i = 0; i < wallpapers->len; i++) refresh_wallpaper(g_ptr_array_index(wallpapers, i));
    } else g_clear_object(&pixbuf);
    if (error) g_error_free(error);
    if (bytes) g_bytes_unref(bytes);
    cover_request_free(request);
}

static void refresh_art(void) {
    if (active_artwork_cancellable) g_cancellable_cancel(active_artwork_cancellable);
    g_clear_object(&active_artwork_cancellable);
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
    active_artwork_cancellable = g_object_ref(request->cancellable);
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
    for (gsize i = 0; i < g_variant_n_children(names); i++) {
        GVariant *entry = g_variant_get_child_value(names, i);
        const char *name = g_variant_get_string(entry, NULL);
        if (g_str_has_prefix(name, "org.mpris.MediaPlayer2.")) {
            NowPlaying tmp = {0};
            if (query_player(name, &tmp)) {
                if (g_strcmp0(tmp.status, "Playing") == 0) {
                    now_clear(&candidate); candidate = tmp; g_variant_unref(entry); break;
                }
                if (!candidate.player) candidate = tmp;
                else now_clear(&tmp);
            }
        }
        g_variant_unref(entry);
    }
    g_variant_unref(names); g_variant_unref(reply);
    gboolean changed = g_strcmp0(candidate.title, now.title) || g_strcmp0(candidate.artist, now.artist) ||
        g_strcmp0(candidate.album, now.album) || g_strcmp0(candidate.art_url, now.art_url) ||
        g_strcmp0(candidate.status, now.status) || g_strcmp0(candidate.player, now.player);
    if (changed) {
        gboolean track_changed = g_strcmp0(candidate.title, now.title) ||
            g_strcmp0(candidate.artist, now.artist) || g_strcmp0(candidate.player, now.player) ||
            ((!candidate.title || !*candidate.title) && (!now.title || !*now.title) &&
             (!candidate.artist || !*candidate.artist) && (!now.artist || !*now.artist) &&
             g_strcmp0(candidate.album, now.album));
        if (track_changed) start_track_transition();
        const gboolean same_art = g_strcmp0(candidate.art_url, now.art_url) == 0;
        GdkPixbuf *previous_cover = same_art && now.cover ? g_object_ref(now.cover) : NULL;
        now_clear(&now); now = candidate;
        if (previous_cover) now.cover = previous_cover;
        else refresh_art();
        remember_current_cover();
        for (guint i = 0; i < wallpapers->len; i++) refresh_wallpaper(g_ptr_array_index(wallpapers, i));
        update_visibility();
    } else now_clear(&candidate);
    return G_SOURCE_CONTINUE;
}

void player_init(void) {
    recent_covers = g_ptr_array_new_with_free_func(recent_cover_free);
    bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    http_session = soup_session_new();
    g_object_set(http_session, "timeout", 8u, "user-agent", "Needle/0.1", NULL);
    g_timeout_add(1400, poll_mpris, NULL);
}

void player_shutdown(void) {
    if (active_artwork_cancellable) g_cancellable_cancel(active_artwork_cancellable);
    g_clear_object(&active_artwork_cancellable);
    now_clear(&now);
    g_clear_pointer(&recent_covers, g_ptr_array_unref);
    g_clear_object(&bus);
    g_clear_object(&http_session);
}

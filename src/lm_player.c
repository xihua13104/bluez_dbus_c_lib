#include "lm_player.h"
#include "lm_player_priv.h"
#include "bluez_dbus.h"
#include "lm_device.h"
#include "lm_device_priv.h"
#include "lm_log.h"
#include "lm_utils.h"

#define TAG "lm_player"

struct lm_player {
    GDBusConnection *dbus_conn; // Borrowed
    lm_device_t *device; // Borrowed
    gchar *device_path;
    gchar *path; // Owned
    gchar *name; // Owned
    gchar *type; // Owned
    gchar *status; // Owned
    guint32 position; // Owned
    lm_player_track_t *track;	/* Player current track */
    lm_player_profile_t profile; // Owned
};

static const gchar *player_status_str[] = {
    "playing",
    "stopped",
    "paused",
    "forward-seek",
    "reverse-seek",
    "error"
};

static lm_player_status_t lm_player_string_to_status(const gchar *status_str)
{
    for (guint i = 0; i < G_N_ELEMENTS(player_status_str); i++) {
        if (g_strcmp0(status_str, player_status_str[i]) == 0) {
            return (lm_player_status_t)i;
        }
    }
    return LM_PLAYER_ERROR;
}

static lm_player_profile_t lm_player_path_to_profile(const gchar *path)
{
    if (path == NULL)
        return LM_PLAYER_PROFILE_NULL;

    if (g_strstr_len(path, -1, "/avrcp/"))
        return LM_PLAYER_PROFILE_AVRCP;
    else if (g_strstr_len(path, -1, "/mcp/"))
        return LM_PLAYER_PROFILE_MCP;

    return LM_PLAYER_PROFILE_NULL;
}

static lm_player_track_t *lm_player_track_create(void)
{
    lm_player_track_t *track = g_new0(lm_player_track_t, 1);
    track->title = NULL;
    track->artist = NULL;
    track->album = NULL;
    track->gerneral_name = NULL;
    track->number_of_tracks = 0;
    track->track_number = 0;
    track->duration = 0;
    track->image_handle = NULL;

    return track;
}

static void lm_player_track_destroy(lm_player_track_t *track)
{
    g_assert(track);

    if (track->title)
        g_free((gpointer)track->title);
    if (track->artist)
        g_free((gpointer)track->artist);
    if (track->album)
        g_free((gpointer)track->album);
    if (track->gerneral_name)
        g_free((gpointer)track->gerneral_name);
    if (track->image_handle)
        g_free((gpointer)track->image_handle);

    g_free(track);
}

lm_player_t *lm_player_create(lm_device_t *device, const gchar *path)
{
    lm_player_t *player = g_new0(lm_player_t, 1);
    player->dbus_conn = lm_device_get_dbus_conn(device);
    player->device = device;
    player->path = g_strdup(path);
    player->status = g_strdup("stopped");
    player->position = 0;
    player->profile = lm_player_path_to_profile(path);
    player->track = lm_player_track_create();

    lm_log_debug(TAG, "create player '%s' success", path);
    return player;
}

void lm_player_destroy(lm_player_t *player)
{
    g_assert(player);

    lm_log_debug(TAG, "destroy player '%s' success", player->path);

    if (player->path)
        g_free((gpointer)player->path);
    if (player->name)
        g_free((gpointer)player->name);
    if (player->type)
        g_free((gpointer)player->type);
    if (player->status)
        g_free((gpointer)player->status);
    if (player->track)
        lm_player_track_destroy(player->track);

    g_free(player);
}

void lm_player_update_property(lm_player_t *player,
        const char *property_name, GVariant *property_value)
{
    lm_log_debug(TAG, "%s property_name:%s",  __func__, property_name);
    if (g_str_equal(property_name, MEDIA_PLAYER_PROPERTY_DEVICE)) {
        if (player->device_path)
            g_free((gpointer)player->device_path);
        player->device_path = g_strdup(g_variant_get_string(property_value, NULL));
        g_assert(g_variant_is_object_path(player->device_path));
        lm_log_info(TAG, "device path '%s'", player->device_path);
    } else if (g_str_equal(property_name, MEDIA_PLAYER_PROPERTY_NAME)) {
        if (player->name)
            g_free((gpointer)player->name);
        player->name = g_strdup(g_variant_get_string(property_value, NULL));
        lm_log_info(TAG, "name '%s'", player->name);
    } else if (g_str_equal(property_name, MEDIA_PLAYER_PROPERTY_TYPE)) {
        if (player->type)
            g_free((gpointer)player->type);
        player->type = g_strdup(g_variant_get_string(property_value, NULL));
        lm_log_info(TAG, "type '%s'", player->type);
    } else if (g_str_equal(property_name, MEDIA_PLAYER_PROPERTY_STATUS)) {
        if (player->status)
            g_free((gpointer)player->status);
        player->status = g_strdup(g_variant_get_string(property_value, NULL));
        lm_log_info(TAG, "status '%s'", player->status);
        if (lm_device_get_active_player(player->device) == player) {
            lm_player_status_change_ind_t ind = {
                .player = player
            };
            lm_app_event_callback(LM_PLAYER_STATUS_CHANGE_IND, LM_STATUS_SUCCESS, &ind);
        }
    } else if (g_str_equal(property_name, MEDIA_PLAYER_PROPERTY_POSITION)) {
        player->position = g_variant_get_uint32(property_value);
        lm_log_debug(TAG, "position %d", player->position);
    } else if (g_str_equal(property_name, MEDIA_PLAYER_PROPERTY_TRACK)) {
        GVariantIter track_iter;
        g_variant_iter_init(&track_iter, property_value);
        char *track_key = NULL;
        GVariant *track_value = NULL;
        while (g_variant_iter_loop(&track_iter, "{sv}", &track_key, &track_value)) {
            if (g_str_equal(track_key, "Title")) {
                if (player->track->title)
                    g_free((gpointer)player->track->title);
                player->track->title = g_strdup(g_variant_get_string(track_value, NULL));
                lm_log_info(TAG, "title name '%s'", player->track->title);
            } else if (g_str_equal(track_key, "Artist")) {
                if (player->track->artist)
                    g_free((gpointer)player->track->artist);
                player->track->artist = g_strdup(g_variant_get_string(track_value, NULL));
                lm_log_debug(TAG, "artist name '%s'", player->track->artist);
            } else if (g_str_equal(track_key, "Album")) {
                if (player->track->album)
                    g_free((gpointer)player->track->album);
                player->track->album = g_strdup(g_variant_get_string(track_value, NULL));
                lm_log_debug(TAG, "album name '%s'", player->track->album);
            } else if (g_str_equal(track_key, "Genre")) {
                if (player->track->gerneral_name)
                    g_free((gpointer)player->track->gerneral_name);
                player->track->gerneral_name = g_strdup(g_variant_get_string(track_value, NULL));
                lm_log_debug(TAG, "gerneral name '%s'", player->track->gerneral_name);
            } else if (g_str_equal(track_key, "NumberOfTracks")) {
                player->track->number_of_tracks = g_variant_get_uint32(track_value);
                lm_log_debug(TAG, "number of tracks 0x%x", player->track->number_of_tracks);
            } else if (g_str_equal(track_key, "TrackNumber")) {
                player->track->track_number = g_variant_get_uint32(track_value);
                lm_log_debug(TAG, "track number 0x%x", player->track->track_number);
            } else if (g_str_equal(track_key, "Duration")) {
                player->track->duration = g_variant_get_uint32(track_value);
                lm_log_debug(TAG, "duration 0x%x", player->track->duration);
            } else if (g_str_equal(track_key, "ImgHandle")) {
                if (player->track->image_handle)
                    g_free((gpointer)player->track->image_handle);
                player->track->image_handle = g_strdup(g_variant_get_string(track_value, NULL));
                lm_log_debug(TAG, "image handle '%s'", player->track->image_handle);
            }
        }
        if (lm_device_get_active_player(player->device) == player) {
            lm_player_track_update_ind_t ind = {
                .player = player
            };
            lm_app_event_callback(LM_PLAYER_TRACK_UPDATE_IND, LM_STATUS_SUCCESS, &ind);
        }
    }
}

lm_player_status_t lm_player_get_status(lm_player_t *player)
{
    g_assert(player);

    return lm_player_string_to_status(player->status);
}

guint32 lm_player_get_position(lm_player_t *player)
{
    g_assert(player);

    return player->position;
}

gchar *lm_player_get_name(lm_player_t *player)
{
    g_assert(player);

    return player->name;
}

gchar *lm_player_get_type(lm_player_t *player)
{
    g_assert(player);

    return player->type;
}

gchar *lm_player_get_path(lm_player_t *player)
{
    g_assert(player);

    return player->path;
}

lm_player_track_t *lm_player_get_track(lm_player_t *player)
{
    g_assert(player);

    return player->track;
}

static void lm_player_call_method_cb(__attribute__((unused)) GObject *source_object,
                                        GAsyncResult *res,
                                        gpointer user_data)
{
    lm_player_t *player = (lm_player_t *) user_data;
    g_assert(player != NULL);

    GError *error = NULL;
    GVariant *value = g_dbus_connection_call_finish(player->dbus_conn, res, &error);
    if (value != NULL) {
        g_variant_unref(value);
    }

    if (error != NULL) {
        lm_log_error(TAG, "failed to call player method (error %d: %s)", error->code, error->message);
        g_clear_error(&error);
    }
}

static void lm_player_call_method(lm_player_t *player, const gchar *method, GVariant *parameters)
{
    g_assert(player != NULL);
    g_assert(method != NULL);

    g_dbus_connection_call(player->dbus_conn,
                           BLUEZ_DBUS,
                           player->path,
                           INTERFACE_MEDIA_PLAYER,
                           method,
                           parameters,
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                           NULL,
                           (GAsyncReadyCallback) lm_player_call_method_cb,
                           player);
}

lm_status_t lm_player_play(lm_player_t *player)
{
    g_assert(player);

    if (lm_player_get_status(player) == LM_PLAYER_PLAYING) {
        lm_log_warn(TAG, "player '%s' is already playing", player->path);
        return LM_STATUS_SUCCESS;
    }

    lm_player_call_method(player, MEDIA_PLAYER_METHOD_PLAY, NULL);
    return LM_STATUS_SUCCESS;
}

lm_status_t lm_player_pause(lm_player_t *player)
{
    g_assert(player);

    if (lm_player_get_status(player) == LM_PLAYER_PAUSED) {
        lm_log_warn(TAG, "player '%s' is already paused", player->path);
        return LM_STATUS_SUCCESS;
    }

    lm_player_call_method(player, MEDIA_PLAYER_METHOD_PAUSE, NULL);
    return LM_STATUS_SUCCESS;
}

lm_status_t lm_player_stop(lm_player_t *player)
{
    g_assert(player);

    if (lm_player_get_status(player) == LM_PLAYER_STOPPED) {
        lm_log_warn(TAG, "player '%s' is already stopped", player->path);
        return LM_STATUS_SUCCESS;
    }

    lm_player_call_method(player, MEDIA_PLAYER_METHOD_STOP, NULL);
    return LM_STATUS_SUCCESS;
}

lm_status_t lm_player_next(lm_player_t *player)
{
    g_assert(player);

    if (lm_player_get_status(player) == LM_PLAYER_PLAYING ||
        lm_player_get_status(player) == LM_PLAYER_PAUSED) {
        lm_player_call_method(player, MEDIA_PLAYER_METHOD_NEXT, NULL);
        return LM_STATUS_SUCCESS;
    } else {
        lm_log_error(TAG, "player '%s' cannot go to next track in current status", player->path);
        return LM_STATUS_FAIL;
    }
}

lm_status_t lm_player_previous(lm_player_t *player)
{
    g_assert(player);

    if (lm_player_get_status(player) == LM_PLAYER_PLAYING ||
        lm_player_get_status(player) == LM_PLAYER_PAUSED) {
        lm_player_call_method(player, MEDIA_PLAYER_METHOD_PREVIOUS, NULL);
        return LM_STATUS_SUCCESS;
    } else {
        lm_log_error(TAG, "player '%s' cannot go to previous track in current status", player->path);
        return LM_STATUS_FAIL;
    }
}

lm_player_profile_t lm_player_get_profile(lm_player_t *player)
{
    g_assert(player);

    return player->profile;
}

lm_device_t *lm_player_get_device(lm_player_t *player)
{
    g_assert(player);

    return player->device;
}
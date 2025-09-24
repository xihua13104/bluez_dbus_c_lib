#ifndef __LM_PLAYER_H__
#define __LM_PLAYER_H__

#include <glib.h>
#include <gio/gio.h>
#include "lm_forward_decl.h"
#include "lm_type.h"

typedef enum {
    LM_PLAYER_PLAYING,
    LM_PLAYER_STOPPED,
    LM_PLAYER_PAUSED,
    LM_PLAYER_FORWARD_SEEK,
    LM_PLAYER_REVERSE_SEEK,
    LM_PLAYER_ERROR
} lm_player_status_t;

typedef enum {
    LM_PLAYER_PROFILE_NULL = 0,
    LM_PLAYER_PROFILE_AVRCP,
    LM_PLAYER_PROFILE_MCP
} lm_player_profile_t;

typedef struct {
    gchar *title;
    gchar *artist;
    gchar *album;
    gchar *gerneral_name;
    guint32 number_of_tracks;
    guint32 track_number;
    guint32 duration;
    gchar *image_handle;
} lm_player_track_t;

typedef struct {
    lm_player_t *player;
} lm_player_added_ind_t;
#define LM_PLAYER_ADDED_IND                (LM_MODULE_PLAYER | 0x0001)

#define LM_PLAYER_REMOVED_IND              (LM_MODULE_PLAYER | 0x0002)

typedef struct {
    lm_player_t *player;
} lm_player_update_ind_t;
#define LM_PLAYER_UPDATE_IND               (LM_MODULE_PLAYER | 0x0003)

typedef struct {
    lm_player_t *player;
} lm_player_status_change_ind_t;
#define LM_PLAYER_STATUS_CHANGE_IND        (LM_MODULE_PLAYER | 0x0004)

typedef struct {
    lm_player_t *player;
} lm_player_track_update_ind_t;
#define LM_PLAYER_TRACK_UPDATE_IND         (LM_MODULE_PLAYER | 0x0005)

lm_player_status_t lm_player_get_status(lm_player_t *player);

guint32 lm_player_get_position(lm_player_t *player);

gchar *lm_player_get_name(lm_player_t *player);

gchar *lm_player_get_type(lm_player_t *player);

gchar *lm_player_get_path(lm_player_t *player);

lm_player_track_t *lm_player_get_track(lm_player_t *player);

lm_status_t lm_player_play(lm_player_t *player);

lm_status_t lm_player_pause(lm_player_t *player);

lm_status_t lm_player_stop(lm_player_t *player);

lm_status_t lm_player_next(lm_player_t *player);

lm_status_t lm_player_previous(lm_player_t *player);

lm_player_profile_t lm_player_get_profile(lm_player_t *player);

lm_device_t *lm_player_get_device(lm_player_t *player);

#endif //__LM_PLAYER_H__
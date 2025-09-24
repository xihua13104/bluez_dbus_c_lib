#ifndef __LM_PLAYER_PRIV_H__
#define __LM_PLAYER_PRIV_H__
#include "lm_type.h"
#include <glib.h>
#include <gio/gio.h>
#include "lm_forward_decl.h"
#include "lm_player.h"
#include "lm_device.h"

lm_player_t *lm_player_create(lm_device_t *device, const gchar *path);

void lm_player_destroy(lm_player_t *player);

void lm_player_update_property(lm_player_t *player,
        const char *property_name, GVariant *property_value);

#endif //__LM_PLAYER_PRIV_H__
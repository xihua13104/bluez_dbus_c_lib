#ifndef __LM_ADV_H__
#define __LM_ADV_H__

#include <glib.h>
#include <gio/gio.h>
#include <bluetooth/bluetooth.h>
#include "lm_adapter.h"
#include "lm_forward_decl.h"

typedef enum {
    LM_ADV_SC_1M = 0,
    LM_ADV_SC_2M,
    LM_ADV_SC_CODED
} lm_adv_secondary_channel_t;

typedef enum {
    LM_ADV_PERIPHERAL,
    LM_ADV_BROADCAST
} lm_adv_type_t;

lm_adv_t *lm_adv_create(void);

void lm_adv_destroy(lm_adv_t *adv);

void lm_adv_set_type(lm_adv_t *adv, lm_adv_type_t type);

lm_adv_type_t lm_adv_get_type(lm_adv_t *adv);

void lm_adv_set_local_name(lm_adv_t *adv, const gchar *name);

const gchar *lm_adv_get_local_name(lm_adv_t *adv);

void lm_adv_set_services(lm_adv_t *adv, const GPtrArray * service_uuids);

void lm_adv_set_manufacturer_data(lm_adv_t *adv, guint16 manufacturer_id, const GByteArray *byteArray);

void lm_adv_set_service_data(lm_adv_t *adv, const gchar* service_uuid, const GByteArray *byteArray);

void lm_adv_set_interval(lm_adv_t *adv, guint32 min, guint32 max);

const gchar *lm_adv_get_path(const lm_adv_t *adv);

void lm_adv_set_appearance(lm_adv_t *adv, guint16 appearance);

guint16 lm_adv_get_appearance(lm_adv_t *adv);

void lm_adv_set_discoverable(lm_adv_t *adv, gboolean discoverable);

gboolean lm_adv_is_discoverable(lm_adv_t *adv);

void lm_adv_set_discoverable_timeout(lm_adv_t *adv, guint16 timeout);

guint16 lm_adv_get_discoverable_timeout(lm_adv_t *adv);

void lm_adv_set_tx_power(lm_adv_t *adv, gint16 tx_power);

gint16 lm_adv_get_tx_power(lm_adv_t *adv);

void lm_adv_set_secondary_channel(lm_adv_t *adv, lm_adv_secondary_channel_t secondary_channel);

lm_adv_secondary_channel_t lm_adv_get_secondary_channel(lm_adv_t *adv);

void lm_adv_set_rsi(lm_adv_t *adv);

lm_status_t lm_adv_register(lm_adv_t *adv);

lm_status_t lm_adv_unregister(lm_adv_t *adv);

#endif //__LM_ADV_H__
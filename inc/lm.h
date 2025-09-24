#ifndef __LM_H__
#define __LM_H__
#include "lm_type.h"
#include <glib.h>
#include <gio/gio.h>
#include "lm_transport.h"

typedef enum {
    LM_CALLBACK_TYPE_APP_EVENT = 0,
    LM_CALLBACK_TYPE_GET_AUDIO_LOCATION_CFG = 1,
    LM_CALLBACK_TYPE_MAX = 10
} lm_callback_type_t;

#define LM_MODULE_MASK(module)          (1 << ((module) >> LM_MODULE_OFFSET))
#define MODULE_MASK_GENERAL             LM_MODULE_MASK(LM_MODULE_GENERAL)
#define MODULE_MASK_ADAPTER             LM_MODULE_MASK(LM_MODULE_ADAPTER)
#define MODULE_MASK_ADV                 LM_MODULE_MASK(LM_MODULE_ADV)
#define MODULE_MASK_AGENT               LM_MODULE_MASK(LM_MODULE_AGENT)
#define MODULE_MASK_DEVICE              LM_MODULE_MASK(LM_MODULE_DEVICE)
#define MODULE_MASK_PLAYER              LM_MODULE_MASK(LM_MODULE_PLAYER)
#define MODULE_MASK_TRANSPORT           LM_MODULE_MASK(LM_MODULE_TRANSPORT)
typedef guint32 lm_callback_module_mask_t;

typedef lm_status_t (*lm_app_callback_func_t)(lm_msg_type_t msg, lm_status_t status, void *buf);

typedef lm_status_t (*lm_get_audio_location_cfg_t)(lm_transport_profile_t profile,
                        lm_transport_audio_location_t *location);

lm_status_t lm_register_callback(lm_callback_type_t type, lm_callback_module_mask_t module_mask, void *cb);

lm_status_t lm_unregister_callback(lm_callback_type_t type, lm_callback_module_mask_t module_mask, void *cb);

void lm_app_event_callback(lm_msg_type_t msg, lm_status_t status, void *buf);

lm_status_t lm_init(void);

lm_status_t lm_deinit(void);

GDBusConnection *lm_get_gdbus_connection(void);

lm_status_t lm_get_audio_location_config(lm_transport_profile_t profile,
                        lm_transport_audio_location_t *location);
#endif //__LM_H__
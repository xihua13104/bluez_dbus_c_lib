#ifndef __LM_DEVICE_PRIV_H__
#define __LM_DEVICE_PRIV_H__
#include "lm_type.h"
#include <glib.h>
#include <gio/gio.h>
#include "lm_forward_decl.h"
#include "lm_device.h"
#include "lm_adapter.h"

lm_device_t *lm_device_create_with_bdaddr(lm_adapter_t *adapter, const bdaddr_t *addr);

lm_device_t *lm_device_create_with_path(lm_adapter_t *adapter, const gchar *path);

void lm_device_destroy(lm_device_t *device);

void lm_device_set_bonding_state(lm_device_t *device, lm_device_bonding_state_t bonding_state);

void lm_device_update_property(lm_device_t *device, const char *property_name, GVariant *property_value);

void lm_device_load_properties(lm_device_t *device);

GDBusConnection *lm_device_get_dbus_conn(const lm_device_t *device);

gboolean lm_device_is_special_device(lm_device_t *device);

void lm_device_set_conn_bearer(lm_device_t *device, lm_device_conn_bearer_t bearer);

void lm_device_reset_conn_bearer(lm_device_t *device, lm_device_conn_bearer_t bearer);

gboolean lm_device_has_bearer(lm_device_t *device, lm_device_conn_bearer_t bearer);

#endif //__LM_DEVICE_PRIV_H__
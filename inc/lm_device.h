#ifndef __LM_DEVICE_H__
#define __LM_DEVICE_H__

#include <glib.h>
#include <gio/gio.h>
#include <bluetooth/bluetooth.h>
#include "lm_type.h"
#include "lm_transport.h"
#include "lm_forward_decl.h"

#define LM_DEVICE_ADDR_STR_LEN                sizeof("XX:XX:XX:XX:XX:XX")

#define LM_DEVICE_BLUEZ_DBUS_ADDR_STR_LEN     sizeof("dev_XX_XX_XX_XX_XX_XX")

typedef enum {
    LM_DEVICE_CONN_NONE = 0,
    LM_DEVICE_CONN_LE = (1 << 0),
    LM_DEVICE_CONN_BREDR = (1 << 1),
    LM_DEVICE_CONN_DUAL = (LM_DEVICE_CONN_LE | LM_DEVICE_CONN_BREDR)
} lm_device_conn_bearer_t;

typedef enum {
    LM_DEVICE_DISCONNECTED = 0,
    LM_DEVICE_CONNECTED = 1,
    LM_DEVICE_CONNECTING = 2,
    LM_DEVICE_DISCONNECTING = 3
} lm_device_connection_state_t;

typedef enum {
    LM_DEVICE_BOND_NONE = 0,
    LM_DEVICE_BONDING = 1,
    LM_DEVICE_BONDED = 2
} lm_device_bonding_state_t;

typedef struct {
    lm_adapter_t *adapter;
    lm_device_t *device;
    const gchar *bearer;
} lm_device_connected_ind_t;
#define LM_DEVICE_CONNECTED_IND        (LM_MODULE_DEVICE | 0x0001)

typedef struct {
    lm_adapter_t *adapter;
    lm_device_t *device;
    const gchar *bearer;
    const gchar *reason;
} lm_device_disconnected_ind_t;
#define LM_DEVICE_DISCONNECTED_IND     (LM_MODULE_DEVICE | 0x0002)

typedef struct {
    lm_adapter_t *adapter;
    lm_device_t *device;
} lm_device_removed_ind_t;
#define LM_DEVICE_REMOVED_IND          (LM_MODULE_DEVICE | 0x0003)

typedef struct {
    lm_device_t *device;
} lm_device_bcast_sync_up_ind_t;
#define LM_DEVICE_BCAST_SYNC_UP_IND    (LM_MODULE_DEVICE | 0x0004)

typedef struct {
    lm_device_t *device;
} lm_device_bcast_sync_lost_ind_t;
#define LM_DEVICE_BCAST_SYNC_LOST_IND   (LM_MODULE_DEVICE | 0x0005)

typedef struct {
    lm_adapter_t *adapter;
    lm_device_t *device;
} lm_device_conn_state_change_ind_t;
#define LM_DEVICE_CONN_STATE_CHANGE_IND  (LM_MODULE_DEVICE | 0x0007)

lm_device_t *lm_device_lookup_by_bdaddr(lm_adapter_t *adapter, const bdaddr_t *addr);

lm_device_t *lm_device_lookup_by_path(lm_adapter_t *adapter, const gchar *path);

const gchar *lm_device_get_name(lm_device_t *device);

const gchar *lm_device_get_address(const lm_device_t *device);

lm_device_connection_state_t lm_device_get_connection_state(const lm_device_t *device);

lm_device_bonding_state_t lm_device_get_bonding_state(const lm_device_t *device);

gint16 lm_device_get_rssi(const lm_device_t *device);

gboolean lm_device_has_service(const lm_device_t *device, const gchar *service_uuid);

GHashTable *lm_device_get_service_data(const lm_device_t *device);

const gchar *lm_device_get_path(lm_device_t *device);

GList *lm_device_get_uuids(lm_device_t *device);

bdaddr_t lm_device_get_bdaddr(lm_device_t *device);

lm_adapter_t *lm_device_get_adapter(const lm_device_t *device);

gchar *lm_device_to_string(const lm_device_t *device);

lm_player_t *lm_device_get_active_player(lm_device_t *device);

lm_transport_t *lm_device_get_active_transport(lm_device_t *device);

lm_device_conn_bearer_t lm_device_get_conn_bearer(const lm_device_t *device);

void lm_device_disconnect(lm_device_t *device);

lm_status_t lm_device_disconnect_sync(lm_device_t *device);

lm_status_t lm_device_connect_sync(lm_device_t *device);

lm_status_t lm_device_start_sync_broadcast(lm_device_t *device,
    lm_transport_audio_location_t location);

lm_status_t lm_device_stop_sync_broadcast(lm_device_t *device);

GPtrArray *lm_device_get_transports(lm_device_t *device, lm_transport_profile_t profile);

#endif //__LM_DEVICE_H__
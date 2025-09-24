#ifndef __LM_ADAPTER_H__
#define __LM_ADAPTER_H__
#include "lm_type.h"
#include <glib.h>
#include <gio/gio.h>
#include "lm_forward_decl.h"
#include "lm_device.h"

typedef enum {
    LM_ADAPTER_POWER_ON = 0,
    LM_ADAPTER_POWER_OFF,
    LM_ADAPTER_POWER_TURNING_ON,
    LM_ADAPTER_POWER_TURNING_OFF,
    LM_ADAPTER_POWER_OFF_BLOCKED
} lm_adapter_power_state_t;

typedef enum {
    LM_ADAPTER_DISCOVERY_STOPPED = 0,
    LM_ADAPTER_DISCOVERY_STARTING,
    LM_ADAPTER_DISCOVERY_STARTED,
    LM_ADAPTER_DISCOVERY_STOPPING,
} lm_adapter_discovery_state_t;

/* how remote broadcast devices are discovered */
typedef enum {
    LM_ADAPTER_BCAST_DISCOVERED_BY_ASSISTANT = 0,
    LM_ADAPTER_BCAST_DISCOVERED_BY_SINK_SCAN = 1,
} lm_adapter_bcast_discovery_method_t;

typedef struct {
    lm_adapter_t *adapter;
} lm_adapter_power_on_cnf_t;
#define LM_ADAPTER_POWER_ON_CNF         (LM_MODULE_ADAPTER | 0x0001)

typedef struct {
    lm_adapter_t *adapter;
} lm_adapter_power_off_cnf_t;
#define LM_ADAPTER_POWER_OFF_CNF        (LM_MODULE_ADAPTER | 0x0002)

typedef struct {
    lm_adapter_t *adapter;
} lm_adapter_discovery_state_change_ind_t;
#define LM_ADAPTER_DISCOVERY_STATE_CHANGE_IND  (LM_MODULE_ADAPTER | 0x0003)

typedef struct {
    lm_adapter_t *adapter;
    lm_device_t *device;
} lm_adapter_discovery_result_ind_t;
#define LM_ADAPTER_DISCOVERY_RESULT_IND        (LM_MODULE_ADAPTER | 0x0004)

typedef struct {
    lm_device_t *device;
    lm_adapter_bcast_discovery_method_t method;
    GPtrArray *bcast_transports;
} lm_adapter_bcast_discovered_ind_t;
#define LM_ADAPTER_BCAST_DISCOVERED_IND  (LM_MODULE_ADAPTER | 0x0005)

typedef struct {
    lm_adapter_t *adapter;
} lm_adapter_discovery_complete_ind_t;
#define LM_ADAPTER_DISCOVERY_COMPLETE_IND      (LM_MODULE_ADAPTER | 0x0006)

typedef struct {
    lm_adapter_t *adapter;
    lm_transport_t *transport;
} lm_adapter_local_bcast_transport_state_change_ind_t;
#define LM_ADAPTER_LOCAL_BCAST_TRANSPORT_STATE_CHANGE_IND  (LM_MODULE_ADAPTER | 0x0007)

lm_adapter_t *lm_adapter_get_default(void);

void lm_adapter_destroy(lm_adapter_t *adapter);

gboolean lm_adapter_is_power_on(lm_adapter_t *adapter);

lm_adapter_power_state_t lm_adapter_get_power_state(lm_adapter_t *adapter);

lm_status_t lm_adapter_power_on(lm_adapter_t *adapter);

lm_status_t lm_adapter_power_off(lm_adapter_t *adapter);

lm_adapter_discovery_state_t lm_adapter_get_discovery_state(lm_adapter_t *adapter);

lm_status_t lm_adapter_start_discovery(lm_adapter_t *adapter);

lm_status_t lm_adapter_stop_discovery(lm_adapter_t *adapter);

void lm_adapter_set_discovery_filter(lm_adapter_t *adapter,
                                            gint16 rssi_threshold,
                                            const GPtrArray *service_uuids,
                                            const gchar *pattern,
                                            guint max_devices,
                                            guint timeout);

void lm_adapter_clear_discovery_filter(lm_adapter_t *adapter);

lm_status_t lm_adapter_discoverable_on(lm_adapter_t *adapter);

lm_status_t lm_adapter_discoverable_off(lm_adapter_t *adapter);

gboolean lm_adapter_is_discoverable(lm_adapter_t *adapter);

lm_status_t lm_adapter_connectable_on(lm_adapter_t *adapter);

lm_status_t lm_adapter_connectable_off(lm_adapter_t *adapter);

gboolean lm_adapter_is_connectable(lm_adapter_t *adapter);

const gchar *lm_adapter_get_path(lm_adapter_t *adapter);

lm_status_t lm_adapter_set_alias(lm_adapter_t *adapter, const gchar *alias);

const gchar *lm_adapter_get_alias(lm_adapter_t *adapter);

const gchar *lm_adapter_get_address(lm_adapter_t *adapter);

GList *lm_adapter_get_connected_devices(lm_adapter_t *adapter);

GDBusConnection *lm_adapter_get_dbus_conn(lm_adapter_t *adapter);

gboolean lm_adapter_is_advertising(lm_adapter_t *adapter);

lm_status_t lm_adapter_start_adv(lm_adapter_t *adapter, lm_adv_t *adv);

lm_status_t lm_adapter_stop_adv(lm_adapter_t *adapter, lm_adv_t *adv);

lm_status_t lm_adapter_remove_device(lm_adapter_t *adapter, lm_device_t *device);

GHashTable *lm_adapter_get_device_cache(lm_adapter_t *adapter);

#endif //__LM_ADAPTER_H__
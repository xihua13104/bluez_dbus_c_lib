#include "lm_device.h"
#include "lm_device_priv.h"
#include "bluez_dbus.h"
#include "lm_adapter.h"
#include "lm_player.h"
#include "lm_player_priv.h"
#include "lm_transport.h"
#include "lm_transport_priv.h"
#include "lm_log.h"
#include "lm_utils.h"
#include "lm_uuids.h"
#include "lm.h"
#include <glib.h>
#include <gio/gio.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#define TAG "lm_device"

#define BCAST_TRANSPORT_TIMER_LENGTH (100) /*unit: milliseconds*/

struct lm_device {
    GDBusConnection *dbus_conn; // Borrowed
    bdaddr_t addr;  // Owned
    lm_adapter_t *adapter; // Borrowed
    const gchar *path; // Owned
    const gchar *name; // Owned
    const gchar *address; // Owned
    const gchar *address_type; // Owned
    const gchar *alias; // Owned
    gboolean services_resolved;
    gboolean service_discovery_started;
    gboolean paired;
    gint16 rssi;
    gboolean trusted;
    gint16 txpower;
    GHashTable *manufacturer_data; // Owned
    GHashTable *service_data; // Owned
    GList *uuids; // Owned
    guint mtu;
    GList *services_list; // Owned

    lm_device_connection_state_t connection_state;
    lm_device_bonding_state_t bonding_state;
    lm_device_conn_bearer_t conn_bearer; // Owned, indicates the connection bearer of the device

    guint player_prop_changed;
    guint transport_prop_changed;
    guint iface_added;
    guint iface_removed;

    GHashTable *services; // Borrowed
    GHashTable *characteristics; // Borrowed
    GHashTable *descriptors; // Borrowed

    lm_player_t *active_player; // Owned, the player that is currently active on this device
    GHashTable *players;

    lm_transport_t *active_transport; // Owned, the transport that is currently active on this device
    GHashTable *transports;

    guint bcast_transport_timer_id;
    lm_transport_audio_location_t bcast_audio_location;
    gboolean bcast_sync_notified;

    /* memory self-management */
    gint ref_count;
};

static const gchar *connection_state_names[] = {
    "DISCONNECTED",
    "CONNECTED",
    "CONNECTING",
    "DISCONNECTING"
};

static void lm_device_free_uuids(lm_device_t *device);
static void lm_device_free_manufacturer_data(lm_device_t *device);
static void lm_device_free_service_data(lm_device_t *device);
static void lm_device_set_conn_state(lm_device_t *device,
                                              lm_device_connection_state_t state);

static lm_player_t *lm_device_find_player(lm_device_t *device, lm_player_profile_t profile)
{
    g_assert (device);

    lm_player_t *player = NULL;

    GList *all_players = g_hash_table_get_values(device->players);
    if (g_list_length(all_players) <= 0) {
        g_list_free(all_players);
        return NULL;
    }

    for (GList *iterator = all_players; iterator; iterator = iterator->next) {
        if (lm_player_get_profile((lm_player_t *) iterator->data) == profile) {
            player = (lm_player_t *) iterator->data;
            break;
        }
    }

    g_list_free(all_players);

    return player;
}

static void lm_device_update_active_player(lm_device_t *device)
{
    g_assert(device);

    lm_player_t *old_active = device->active_player;

    /* avrcp player gets the higher priority */
    lm_player_t *new_active = lm_device_find_player(device, LM_PLAYER_PROFILE_AVRCP);
    if (!new_active)
        new_active = lm_device_find_player(device, LM_PLAYER_PROFILE_MCP);

    if (old_active == new_active)
        return;

    device->active_player = new_active;

    if (device->active_player)
        lm_log_info(TAG, "active player updated to '%s'", lm_player_get_path(device->active_player));

    if (!old_active && device->active_player) {
        lm_player_added_ind_t ind = {
             .player = device->active_player
        };
        lm_app_event_callback(LM_PLAYER_ADDED_IND, LM_STATUS_SUCCESS, &ind);
    } else if (old_active && !device->active_player) {
        lm_app_event_callback(LM_PLAYER_REMOVED_IND, LM_STATUS_SUCCESS, NULL);
    } else if (old_active != device->active_player) {
        lm_player_update_ind_t ind = {
             .player = device->active_player
        };
        lm_app_event_callback(LM_PLAYER_UPDATE_IND, LM_STATUS_SUCCESS, &ind);
    }
}

static lm_transport_t *lm_device_find_transport(lm_device_t *device, lm_transport_profile_t profile)
{
    g_assert (device);

    lm_transport_t *transport = NULL;

    GList *all_trans = g_hash_table_get_values(device->transports);
    if (g_list_length(all_trans) <= 0) {
        g_list_free(all_trans);
        return NULL;
    }

    for (GList *iterator = all_trans; iterator; iterator = iterator->next) {
        if (lm_transport_get_profile((lm_transport_t *) iterator->data) == profile) {
            transport = (lm_transport_t *) iterator->data;
            break;
        }
    }

    g_list_free(all_trans);

    return transport;
}

static void lm_device_update_active_transport(lm_device_t *device)
{
    g_assert(device);

    lm_transport_t *old_active = device->active_transport;

    /* a2dp sink transport gets the higher priority */
    lm_transport_t *new_active = lm_device_find_transport(device, LM_TRANSPORT_PROFILE_A2DP_SINK);
    if (!new_active)
        new_active = lm_device_find_transport(device, LM_TRANSPORT_PROFILE_BAP_SINK);

    if (old_active == new_active)
        return;

    device->active_transport = new_active;

    if (device->active_transport)
        lm_log_info(TAG, "active transport updated to '%s' '%s'",
                    lm_transport_get_profile_name(device->active_transport),
                    lm_transport_get_path(device->active_transport));

    if (!old_active && device->active_transport) {
        lm_transport_added_ind_t ind = {
             .transport = device->active_transport
        };
        lm_app_event_callback(LM_TRANSPORT_ADDED_IND, LM_STATUS_SUCCESS, &ind);
    } else if (old_active && !device->active_transport) {
        lm_app_event_callback(LM_TRANSPORT_REMOVED_IND, LM_STATUS_SUCCESS, NULL);
    } else if (old_active != device->active_transport) {
        lm_transport_update_ind_t ind = {
             .transport = device->active_transport
        };
        lm_app_event_callback(LM_TRANSPORT_UPDATE_IND, LM_STATUS_SUCCESS, &ind);
    }
}

static gboolean bcast_sink_transport_timer_cb(gpointer user_data)
{
    lm_status_t status = LM_STATUS_FAIL;
    lm_device_t *device = (lm_device_t *)user_data;
    lm_adapter_t *adapter = NULL;
    GList *connected_devices = NULL;
    g_assert(device);

    GPtrArray *bcast_transports = lm_device_get_transports(device, LM_TRANSPORT_PROFILE_BAP_BCAST_SINK);

    lm_log_info(TAG, "bcast sink transport num %d", bcast_transports->len);

    if (bcast_transports->len == 0)
        goto exit;

    lm_adapter_bcast_discovered_ind_t ind = {
        .device = device,
        .method = LM_ADAPTER_BCAST_DISCOVERED_BY_ASSISTANT,
        .bcast_transports = bcast_transports
    };

    adapter = lm_device_get_adapter(device);
    connected_devices = lm_adapter_get_connected_devices(adapter);
    if (LM_ADAPTER_DISCOVERY_STARTED == lm_adapter_get_discovery_state(adapter) ||
        g_list_length(connected_devices) == 0)
        ind.method = LM_ADAPTER_BCAST_DISCOVERED_BY_SINK_SCAN;

    lm_app_event_callback(LM_ADAPTER_BCAST_DISCOVERED_IND, LM_STATUS_SUCCESS, &ind);

    status = LM_STATUS_SUCCESS;

exit:
    if (bcast_transports)
        g_ptr_array_free(bcast_transports, TRUE);
    if (connected_devices)
        g_list_free(connected_devices);
    if (status == LM_STATUS_FAIL)
        return TRUE; /* FALSE:stop period timer; TRUE:keep period timer. */

    device->bcast_transport_timer_id = 0;
    return FALSE;
}

static void on_interface_appeared(__attribute__((unused)) GDBusConnection *conn,
                                          __attribute__((unused)) const gchar *sender_name,
                                          __attribute__((unused)) const gchar *object_path,
                                          __attribute__((unused)) const gchar *interface,
                                          __attribute__((unused)) const gchar *signal_name,
                                          GVariant *parameters,
                                          gpointer user_data) {
    GVariantIter *interfaces = NULL;
    const gchar *object = NULL;
    const gchar *interface_name = NULL;
    GVariant *properties = NULL;
    char *property_name = NULL;
    GVariantIter iter;
    GVariant *property_value = NULL;
    lm_device_t *device = (lm_device_t *) user_data;
    g_assert(device);

    g_assert(g_str_equal(g_variant_get_type_string(parameters), "(oa{sa{sv}})"));
    g_variant_get(parameters, "(&oa{sa{sv}})", &object, &interfaces);

     lm_log_debug(TAG, "on_interface_appeared, sender:%s, path:%s, interface:%s, signal:%s",
                sender_name, object, interface, signal_name);

    while (g_variant_iter_loop(interfaces, "{&s@a{sv}}", &interface_name, &properties)) {
        if (g_str_equal(interface_name, INTERFACE_MEDIA_PLAYER)) {
            // Skip this player if it is not for this device
            if (!g_str_has_prefix(object, device->path))
                continue;

            lm_log_debug(TAG, "media player '%s' added", object);

            lm_player_t *player = (lm_player_t *)g_hash_table_lookup(device->players, object);
            if (!player) {
                player = lm_player_create(device, object);
                g_hash_table_insert(device->players, g_strdup(object), player);
            }
            g_variant_iter_init(&iter, properties);
            while (g_variant_iter_loop(&iter, "{&sv}", &property_name, &property_value)) {
                lm_player_update_property(player, property_name, property_value);
            }
            lm_device_update_active_player(device);
        } else if (g_str_equal(interface_name, INTERFACE_MEDIA_TRANSPORT)) {
            // Skip this transport if it is not for this device
            if (!g_str_has_prefix(object, device->path))
                continue;

            lm_log_debug(TAG, "media transport '%s' added", object);

            lm_transport_t *transport = NULL;
            transport = (lm_transport_t *)g_hash_table_lookup(device->transports, object);
            if (!transport) {
                transport = lm_transport_create(device, object);
                g_hash_table_insert(device->transports, g_strdup(object), transport);
            }
            g_variant_iter_init(&iter, properties);
            while (g_variant_iter_loop(&iter, "{&sv}", &property_name, &property_value)) {
                lm_transport_update_property(transport, property_name, property_value);
            }
            lm_device_update_active_transport(device);

            if (lm_transport_get_profile(transport) == LM_TRANSPORT_PROFILE_BAP_BCAST_SINK) {
                /* PA sync complete */
                lm_log_debug(TAG, "bcast transport '%s' appeared", object);

                if (!device->bcast_transport_timer_id) {
                    /* delay to wait transport setup done */
                    device->bcast_transport_timer_id = g_timeout_add(BCAST_TRANSPORT_TIMER_LENGTH,
                                                                    bcast_sink_transport_timer_cb,
                                                                    device);
                }
            }
        }
    }

    if (interfaces)
        g_variant_iter_free(interfaces);
}

static void on_interface_disappeared(__attribute__((unused)) GDBusConnection *conn,
                                             __attribute__((unused)) const gchar *sender_name,
                                             __attribute__((unused)) const gchar *object_path,
                                             __attribute__((unused)) const gchar *interface,
                                             __attribute__((unused)) const gchar *signal_name,
                                             GVariant *parameters,
                                             gpointer user_data) {
    GVariantIter *interfaces = NULL;
    const gchar *object = NULL;
    const gchar *interface_name = NULL;

    lm_device_t *device = (lm_device_t *) user_data;
    g_assert(device);

    g_assert(g_str_equal(g_variant_get_type_string(parameters), "(oas)"));
    g_variant_get(parameters, "(&oas)", &object, &interfaces);

    lm_log_debug(TAG, "on_interface_disappeared, sender:%s, path:%s, interface:%s, signal:%s",
               sender_name, object, interface, signal_name);

    while (g_variant_iter_loop(interfaces, "s", &interface_name)) {
        lm_log_debug(TAG, "interface %s removed from object %s", interface_name, object);
        if (g_str_equal(interface_name, INTERFACE_MEDIA_PLAYER)) {
            // Skip this player if it is not for this device
            if (!g_str_has_prefix(object, device->path))
                continue;

            lm_log_debug(TAG, "media player '%s' removed", object);

            lm_player_t *player = (lm_player_t *)g_hash_table_lookup(device->players, object);
            if (!player)
                continue;

            g_hash_table_remove(device->players, object);
            lm_device_update_active_player(device);
        } else if (g_str_equal(interface_name, INTERFACE_MEDIA_TRANSPORT)) {
            // Skip this player if it is not for this device
            if (!g_str_has_prefix(object, device->path))
                continue;

            lm_log_debug(TAG, "media transport '%s' removed", object);

            lm_transport_t *transport = (lm_transport_t *)g_hash_table_lookup(device->transports, object);
            if (!transport)
                continue;

            lm_transport_profile_t profile = lm_transport_get_profile(transport);
            g_hash_table_remove(device->transports, object);
            lm_device_update_active_transport(device);
            if (profile == LM_TRANSPORT_PROFILE_BAP_BCAST_SINK) {
                lm_log_debug(TAG, "bcast transport '%s' disappeared", object);
                /* all BAP BCAST SINK transports disappeared */
                if (NULL == lm_device_find_transport(device, LM_TRANSPORT_PROFILE_BAP_BCAST_SINK)) {
                    lm_device_bcast_sync_lost_ind_t ind = {
                        .device = device,
                    };
                    lm_app_event_callback(LM_DEVICE_BCAST_SYNC_LOST_IND, LM_STATUS_SUCCESS, &ind);
                }
            }
        }
    }

    if (interfaces)
        g_variant_iter_free(interfaces);
}

static void on_player_prop_changed(__attribute__((unused)) GDBusConnection *conn,
                                          __attribute__((unused)) const gchar *sender,
                                          __attribute__((unused)) const gchar *path,
                                          __attribute__((unused)) const gchar *interface,
                                          __attribute__((unused)) const gchar *signal,
                                          GVariant *parameters,
                                          void *user_data)
{

    GVariantIter *properties_changed = NULL;
    GVariantIter *properties_invalidated = NULL;
    const gchar *iface = NULL;
    const gchar *property_name = NULL;
    GVariant *property_value = NULL;

    lm_device_t *device = (lm_device_t *) user_data;
    g_assert(device);

    lm_log_debug(TAG, "on_player_prop_changed, sender:%s, path:%s, interface:%s, signal:%s",
            sender, path, interface, signal);

    // Skip this player if it is not for this device
    if (!g_str_has_prefix(path, device->path))
        return;

    lm_player_t *player = (lm_player_t *)g_hash_table_lookup(device->players, path);
    if (!player) {
        lm_log_error(TAG, "player not found for path: '%s' on device '%s'", path, device->path);
        return;
    }

    g_assert(g_str_equal(g_variant_get_type_string(parameters), "(sa{sv}as)"));
    g_variant_get(parameters, "(&sa{sv}as)", &iface, &properties_changed, &properties_invalidated);
    while (g_variant_iter_loop(properties_changed, "{&sv}", &property_name, &property_value)) {
        lm_player_update_property(player, property_name, property_value);
    }

    if (properties_changed)
        g_variant_iter_free(properties_changed);

    if (properties_invalidated)
        g_variant_iter_free(properties_invalidated);
}

static void on_transport_prop_changed(__attribute__((unused)) GDBusConnection *conn,
                                          __attribute__((unused)) const gchar *sender,
                                          __attribute__((unused)) const gchar *path,
                                          __attribute__((unused)) const gchar *interface,
                                          __attribute__((unused)) const gchar *signal,
                                          GVariant *parameters,
                                          void *user_data) {

    GVariantIter *properties_changed = NULL;
    GVariantIter *properties_invalidated = NULL;
    const gchar *iface = NULL;
    const gchar *property_name = NULL;
    GVariant *property_value = NULL;

    lm_device_t *device = (lm_device_t *) user_data;
    g_assert(device);

    lm_log_debug(TAG, "on_transport_prop_changed, sender:%s, path:%s, interface:%s, signal:%s",
            sender, path, interface, signal);

    // Skip this player if it is not for this device
    if (!g_str_has_prefix(path, device->path))
        return;

    lm_transport_t *transport = (lm_transport_t *)g_hash_table_lookup(device->transports, path);
    if (!transport) {
        lm_log_error(TAG, "transport not found for path: %s on device '%s'", path, device->path);
        return;
    }

    lm_transport_state_t old_state = lm_transport_get_state(transport);

    g_assert(g_str_equal(g_variant_get_type_string(parameters), "(sa{sv}as)"));
    g_variant_get(parameters, "(&sa{sv}as)", &iface, &properties_changed, &properties_invalidated);
    while (g_variant_iter_loop(properties_changed, "{&sv}", &property_name, &property_value)) {
        lm_transport_update_property(transport, property_name, property_value);
    }

    if (LM_TRANSPORT_ACTIVE == lm_transport_get_state(transport) &&
        old_state != lm_transport_get_state(transport) &&
        LM_TRANSPORT_PROFILE_BAP_BCAST_SINK == lm_transport_get_profile(transport) &&
        !device->bcast_sync_notified) {
        device->bcast_sync_notified = TRUE;
        lm_device_bcast_sync_up_ind_t ind = {
                .device = device
        };
        lm_app_event_callback(LM_DEVICE_BCAST_SYNC_UP_IND, LM_STATUS_SUCCESS, &ind);
    }

    if (properties_changed)
        g_variant_iter_free(properties_changed);

    if (properties_invalidated)
        g_variant_iter_free(properties_invalidated);
}

static void lm_device_subscribe_signal(lm_device_t *device)
{
    g_assert(device);

    device->iface_added = g_dbus_connection_signal_subscribe(device->dbus_conn,
                                                            BLUEZ_DBUS,
                                                            INTERFACE_OBJECT_MANAGER,
                                                            OBJECT_MANAGER_SIGNAL_INTERFACE_ADDED,
                                                            NULL,
                                                            NULL,
                                                            G_DBUS_SIGNAL_FLAGS_NONE,
                                                            on_interface_appeared,
                                                            device,
                                                            NULL);

    device->iface_removed = g_dbus_connection_signal_subscribe(device->dbus_conn,
                                                            BLUEZ_DBUS,
                                                            INTERFACE_OBJECT_MANAGER,
                                                            OBJECT_MANAGER_SIGNAL_INTERFACE_REMOVED,
                                                            NULL,
                                                            NULL,
                                                            G_DBUS_SIGNAL_FLAGS_NONE,
                                                            on_interface_disappeared,
                                                            device,
                                                            NULL);

    device->transport_prop_changed = g_dbus_connection_signal_subscribe(device->dbus_conn,
                                                            BLUEZ_DBUS,
                                                            INTERFACE_PROPERTIES,
                                                            PROPERTIES_SIGNAL_CHANGED,
                                                            NULL,
                                                            INTERFACE_MEDIA_TRANSPORT,
                                                            G_DBUS_SIGNAL_FLAGS_NONE,
                                                            on_transport_prop_changed,
                                                            device,
                                                            NULL);

    device->player_prop_changed = g_dbus_connection_signal_subscribe(device->dbus_conn,
                                                            BLUEZ_DBUS,
                                                            INTERFACE_PROPERTIES,
                                                            PROPERTIES_SIGNAL_CHANGED,
                                                            NULL,
                                                            INTERFACE_MEDIA_PLAYER,
                                                            G_DBUS_SIGNAL_FLAGS_NONE,
                                                            on_player_prop_changed,
                                                            device,
                                                            NULL);
}

static void lm_device_unsubscribe_signal(lm_device_t *device)
{
    g_assert(device);

    g_dbus_connection_signal_unsubscribe(device->dbus_conn, device->iface_added);
    device->iface_added = 0;
    g_dbus_connection_signal_unsubscribe(device->dbus_conn, device->iface_removed);
    device->iface_removed = 0;
    g_dbus_connection_signal_unsubscribe(device->dbus_conn, device->transport_prop_changed);
    device->transport_prop_changed = 0;
    g_dbus_connection_signal_unsubscribe(device->dbus_conn, device->player_prop_changed);
    device->player_prop_changed = 0;
}

lm_device_t *lm_device_create_with_bdaddr(lm_adapter_t *adapter, const bdaddr_t *addr)
{
    lm_device_t *device;
    g_assert(adapter && addr);

    if ((device = g_new0(lm_device_t, 1)) == NULL)
        return NULL;

    device->adapter = adapter;
    device->dbus_conn = lm_adapter_get_dbus_conn(adapter);
    device->rssi = -255;
    device->txpower = -255;
    device->mtu = 23;
    bacpy(&device->addr, addr);
    device->address = g_strdup_printf("%.2X:%.2X:%.2X:%.2X:%.2X:%.2X",
        addr->b[5], addr->b[4], addr->b[3], addr->b[2], addr->b[1], addr->b[0]);

    device->path = g_strdup_printf("%s/dev_%.2X_%.2X_%.2X_%.2X_%.2X_%.2X",
        lm_adapter_get_path(adapter),
        addr->b[5], addr->b[4], addr->b[3], addr->b[2], addr->b[1], addr->b[0]);
    device->players = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                                (GDestroyNotify) lm_player_destroy);
    device->transports = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                                (GDestroyNotify) lm_transport_destroy);
    lm_device_subscribe_signal(device);

    lm_log_debug(TAG, "create device '%s' success", device->path);
    return device;
}

lm_device_t *lm_device_create_with_path(lm_adapter_t *adapter, const gchar *path)
{
    lm_device_t *device;
    bdaddr_t addr;

    g_assert(adapter && path);

    if ((device = g_new0(lm_device_t, 1)) == NULL)
        return NULL;

    device->adapter = adapter;
    device->dbus_conn = lm_adapter_get_dbus_conn(adapter);
    device->adapter = adapter;
    device->rssi = -255;
    device->txpower = -255;
    device->mtu = 23;
    device->path = g_strdup(path);
    lm_utils_dbus_bluez_object_path_to_bdaddr(path, &addr);
    bacpy(&device->addr, &addr);

    device->address = g_strdup_printf("%.2X:%.2X:%.2X:%.2X:%.2X:%.2X",
        addr.b[5], addr.b[4], addr.b[3], addr.b[2], addr.b[1], addr.b[0]);
    device->players = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                                (GDestroyNotify) lm_player_destroy);
    device->transports = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                                (GDestroyNotify) lm_transport_destroy);
    lm_device_subscribe_signal(device);

    lm_log_debug(TAG, "create device '%s'", path);
    return device;
}

void lm_device_destroy(lm_device_t *device)
{
    g_assert(device);

    lm_log_debug(TAG, "destroy device '%s'", device->path);

    lm_device_unsubscribe_signal(device);

    if (device->path)
        g_free((gpointer)device->path);

    if (device->name)
        g_free((gpointer)device->name);

    if (device->address)
        g_free((gpointer)device->address);

    if (device->address_type)
        g_free((gpointer)device->address_type);

    if (device->alias)
        g_free((gpointer)device->alias);

    if (device->transports)
        g_hash_table_destroy(device->transports);

    if (device->players)
        g_hash_table_destroy(device->players);

    lm_device_free_manufacturer_data(device);

    lm_device_free_uuids(device);

    lm_device_free_service_data(device);

    g_free((gpointer)device);
}

lm_device_t *lm_device_lookup_by_bdaddr(lm_adapter_t *adapter, const bdaddr_t *addr)
{
    lm_device_t *device = NULL;
    const gchar *path;

    g_assert(adapter && addr);

    path = g_strdup_printf("%s/dev_%.2X_%.2X_%.2X_%.2X_%.2X_%.2X",
        lm_adapter_get_path(adapter),
        addr->b[5], addr->b[4], addr->b[3], addr->b[2], addr->b[1], addr->b[0]);
    device = (lm_device_t *)g_hash_table_lookup(lm_adapter_get_device_cache(adapter), path);
    g_free((void *)path);

    return device;
}

lm_device_t *lm_device_lookup_by_path(lm_adapter_t *adapter, const gchar *path)
{
    g_assert(adapter && path);
    return (lm_device_t *)g_hash_table_lookup(lm_adapter_get_device_cache(adapter), path);
}

static void lm_device_load_properties_cb(__attribute__((unused)) GObject *source_object,
                                                      GAsyncResult *res,
                                                      gpointer user_data) {

    lm_device_t *device = (lm_device_t *) user_data;
    g_assert(device != NULL);

    GError *error = NULL;
    GVariant *result = g_dbus_connection_call_finish(device->dbus_conn, res, &error);

    if (error != NULL) {
        lm_log_error(TAG, "failed to call '%s' (error %d: %s)", "GetAll", error->code, error->message);
        g_clear_error(&error);
    }

    if (result != NULL) {
        GVariantIter *iter = NULL;
        const gchar *property_name = NULL;
        GVariant *property_value = NULL;

        g_assert(g_str_equal(g_variant_get_type_string(result), "(a{sv})"));
        g_variant_get(result, "(a{sv})", &iter);
        while (g_variant_iter_loop(iter, "{&sv}", &property_name, &property_value)) {
            lm_device_update_property(device, property_name, property_value);
        }

        if (iter != NULL) {
            g_variant_iter_free(iter);
        }
        g_variant_unref(result);
    }
}

void lm_device_load_properties(lm_device_t *device) {
    g_dbus_connection_call(device->dbus_conn,
                           BLUEZ_DBUS,
                           device->path,
                           INTERFACE_PROPERTIES,
                           PROPERTIES_METHOD_GET_ALL,
                           g_variant_new("(s)", INTERFACE_DEVICE),
                           G_VARIANT_TYPE("(a{sv})"),
                           G_DBUS_CALL_FLAGS_NONE,
                           BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                           NULL,
                           (GAsyncReadyCallback) lm_device_load_properties_cb,
                           device);
}

static void lm_device_free_uuids(lm_device_t *device)
{
    if (device->uuids != NULL) {
        g_list_free_full(device->uuids, g_free);
        device->uuids = NULL;
    }
}

static void lm_device_free_manufacturer_data(lm_device_t *device)
{
    g_assert(device != NULL);

    if (device->manufacturer_data != NULL) {
        g_hash_table_destroy(device->manufacturer_data);
        device->manufacturer_data = NULL;
    }
}

static void lm_device_free_service_data(lm_device_t *device)
{
    g_assert(device != NULL);

    if (device->service_data != NULL) {
        g_hash_table_destroy(device->service_data);
        device->service_data = NULL;
    }
}

static void lm_device_set_conn_state(lm_device_t *device,
                                              lm_device_connection_state_t state)
{
    lm_device_connection_state_t old_state = device->connection_state;
    device->connection_state = state;

    if (device->connection_state != old_state) {
        lm_device_conn_state_change_ind_t ind = {
            .adapter = device->adapter,
            .device = device
        };
        lm_app_event_callback(LM_DEVICE_CONN_STATE_CHANGE_IND, LM_STATUS_SUCCESS, &ind);
    }
}

void lm_device_set_bonding_state(lm_device_t *device, lm_device_bonding_state_t bonding_state) {
    g_assert(device != NULL);

    device->bonding_state = bonding_state;
}

gchar *lm_device_to_string(const lm_device_t *device) {
    g_assert(device != NULL);

    // First build up uuids string
    GString *uuids = g_string_new("[");
    if (g_list_length(device->uuids) > 0) {
        for (GList *iterator = device->uuids; iterator; iterator = iterator->next) {
            g_string_append_printf(uuids, "%s, ", (gchar *) iterator->data);
        }
        g_string_truncate(uuids, uuids->len - 2);
    }
    g_string_append(uuids, "]");

    // Build up manufacturer data string
    GString *manufacturer_data = g_string_new("[");
    if (device->manufacturer_data != NULL && g_hash_table_size(device->manufacturer_data) > 0) {
        GHashTableIter iter;
        int *key;
        gpointer value;
        g_hash_table_iter_init(&iter, device->manufacturer_data);
        while (g_hash_table_iter_next(&iter, (gpointer) &key, &value)) {
            GByteArray *byte_array = (GByteArray *) value;
            GString *byteArrayString = lm_utils_g_byte_array_as_hex(byte_array);
            gint keyInt = *key;
            g_string_append_printf(manufacturer_data, "%04X -> %s, ", keyInt, byteArrayString->str);
            g_string_free(byteArrayString, TRUE);
        }
        g_string_truncate(manufacturer_data, manufacturer_data->len - 2);
    }
    g_string_append(manufacturer_data, "]");

    // Build up service data string
    GString *service_data = g_string_new("[");
    if (device->service_data != NULL && g_hash_table_size(device->service_data) > 0) {
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, device->service_data);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            GByteArray *byte_array = (GByteArray *) value;
            GString *byteArrayString = lm_utils_g_byte_array_as_hex(byte_array);
            g_string_append_printf(service_data, "%s -> %s, ", (gchar *) key, byteArrayString->str);
            g_string_free(byteArrayString, TRUE);
        }
        g_string_truncate(service_data, service_data->len - 2);
    }
    g_string_append(service_data, "]");

    gchar *result = g_strdup_printf(
            "device{name='%s', address='%s', address_type=%s, rssi=%d, uuids=%s, manufacturer_data=%s, service_data=%s, paired=%s, txpower=%d path='%s' }",
            device->name,
            device->address,
            device->address_type,
            device->rssi,
            uuids->str,
            manufacturer_data->str,
            service_data->str,
            device->paired ? "true" : "false",
            device->txpower,
            device->path
    );

    g_string_free(uuids, TRUE);
    g_string_free(manufacturer_data, TRUE);
    g_string_free(service_data, TRUE);
    return result;
}

bdaddr_t lm_device_get_bdaddr(lm_device_t *device)
{
    g_assert(device);
    return device->addr;
}

lm_device_connection_state_t lm_device_get_connection_state(const lm_device_t *device) {
    g_assert(device != NULL);
    return device->connection_state;
}

const gchar *lm_device_get_connection_state_name(const lm_device_t *device) {
    g_assert(device != NULL);
    return connection_state_names[device->connection_state];
}

const gchar *lm_device_get_address(const lm_device_t *device) {
    g_assert(device != NULL);
    return device->address;
}

void lm_device_set_address(lm_device_t *device, const gchar *address) {
    g_assert(device != NULL);
    g_assert(address != NULL);

    g_free((gchar *) device->address);
    device->address = g_strdup(address);
}

const gchar *lm_device_get_address_type(const lm_device_t *device) {
    g_assert(device != NULL);
    return device->address_type;
}

void lm_device_set_address_type(lm_device_t *device, const gchar *address_type) {
    g_assert(device != NULL);
    g_assert(address_type != NULL);

    g_free((gchar *) device->address_type);
    device->address_type = g_strdup(address_type);
}

const gchar *lm_device_get_alias(lm_device_t *device) {
    g_assert(device != NULL);
    return device->alias;
}

void lm_device_set_alias(lm_device_t *device, const gchar *alias) {
    g_assert(device != NULL);
    g_assert(alias != NULL);

    g_free((gchar *) device->alias);
    device->alias = g_strdup(alias);
}

const gchar *lm_device_get_name(lm_device_t *device) {
    g_assert(device != NULL);
    return device->name;
}

void lm_device_set_name(lm_device_t *device, const gchar *name) {
    g_assert(device != NULL);
    g_assert(name != NULL);
    g_assert(strlen(name) > 0);

    g_free((gchar *) device->name);
    device->name = g_strdup(name);
}

const gchar *lm_device_get_path(lm_device_t *device) {
    g_assert(device != NULL);
    return device->path;
}

void lm_device_set_path(lm_device_t *device, const gchar *path) {
    g_assert(device != NULL);
    g_assert(path != NULL);

    g_free((gchar *) device->path);
    device->path = g_strdup(path);
}

gboolean lm_device_get_paired(lm_device_t *device) {
    g_assert(device != NULL);
    return device->paired;
}

void lm_device_set_paired(lm_device_t *device, gboolean paired) {
    g_assert(device != NULL);
    device->paired = paired;
    lm_device_set_bonding_state(device, paired ? LM_DEVICE_BONDED : LM_DEVICE_BOND_NONE);
}

gint16 lm_device_get_rssi(const lm_device_t *device) {
    g_assert(device != NULL);
    return device->rssi;
}

void lm_device_set_rssi(lm_device_t *device, gint16 rssi) {
    g_assert(device != NULL);
    device->rssi = rssi;
}

gboolean lm_device_get_trusted(lm_device_t *device) {
    g_assert(device != NULL);
    return device->trusted;
}

void lm_device_set_trusted(lm_device_t *device, gboolean trusted) {
    g_assert(device != NULL);
    device->trusted = trusted;
}

gint16 lm_device_get_txpower(lm_device_t *device) {
    g_assert(device != NULL);
    return device->txpower;
}

void lm_device_set_txpower(lm_device_t *device, gint16 txpower) {
    g_assert(device != NULL);
    device->txpower = txpower;
}

GList *lm_device_get_uuids(lm_device_t *device) {
    g_assert(device != NULL);
    return device->uuids;
}

void lm_device_set_uuids(lm_device_t *device, GList *uuids) {
    g_assert(device != NULL);

    lm_device_free_uuids(device);
    device->uuids = uuids;
}

GHashTable *lm_device_get_manufacturer_data(const lm_device_t *device) {
    g_assert(device != NULL);
    return device->manufacturer_data;
}

void lm_device_set_manufacturer_data(lm_device_t *device, GHashTable *manufacturer_data) {
    g_assert(device != NULL);

    lm_device_free_manufacturer_data(device);
    device->manufacturer_data = manufacturer_data;
}

GHashTable *lm_device_get_service_data(const lm_device_t *device)
{
    g_assert(device != NULL);
    return device->service_data;
}

void lm_device_set_service_data(lm_device_t *device, GHashTable *service_data) {
    g_assert(device != NULL);

    lm_device_free_service_data(device);
    device->service_data = service_data;
}

GDBusConnection *lm_device_get_dbus_conn(const lm_device_t *device)
{
    g_assert(device != NULL);
    return device->dbus_conn;
}

lm_device_bonding_state_t lm_device_get_bonding_state(const lm_device_t *device) {
    g_assert(device != NULL);
    return device->bonding_state;
}

lm_adapter_t *lm_device_get_adapter(const lm_device_t *device) {
    g_assert(device != NULL);
    return device->adapter;
}

guint lm_device_get_mtu(const lm_device_t *device) {
    g_assert(device != NULL);
    return device->mtu;
}

gboolean lm_device_has_service(const lm_device_t *device, const gchar *service_uuid) {
    g_assert(device != NULL);
    g_assert(g_uuid_string_is_valid(service_uuid));

    if (device->uuids != NULL && g_list_length(device->uuids) > 0) {
        for (GList *iterator = device->uuids; iterator; iterator = iterator->next) {
            if (g_str_equal(service_uuid, (gchar *) iterator->data)) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

void lm_device_update_property(lm_device_t *device, const gchar *property_name, GVariant *property_value) {
    lm_log_debug(TAG, "%s property_name:%s",  __func__, property_name);
    if (g_str_equal(property_name, DEVICE_PROPERTY_ADDRESS)) {
        lm_device_set_address(device, g_variant_get_string(property_value, NULL));
    } else if (g_str_equal(property_name, DEVICE_PROPERTY_ADDRESS_TYPE)) {
        lm_device_set_address_type(device, g_variant_get_string(property_value, NULL));
    } else if (g_str_equal(property_name, DEVICE_PROPERTY_ALIAS)) {
        lm_device_set_alias(device, g_variant_get_string(property_value, NULL));
    } else if (g_str_equal(property_name, DEVICE_PROPERTY_CONNECTED)) {
        lm_device_set_conn_state(device, g_variant_get_boolean(property_value) ? LM_DEVICE_CONNECTED : LM_DEVICE_DISCONNECTED);
    } else if (g_str_equal(property_name, DEVICE_PROPERTY_NAME)) {
        lm_device_set_name(device, g_variant_get_string(property_value, NULL));
    } else if (g_str_equal(property_name, DEVICE_PROPERTY_PAIRED)) {
        lm_device_set_paired(device, g_variant_get_boolean(property_value));
    } else if (g_str_equal(property_name, DEVICE_PROPERTY_RSSI)) {
        lm_device_set_rssi(device, g_variant_get_int16(property_value));
    } else if (g_str_equal(property_name, DEVICE_PROPERTY_TRUSTED)) {
        lm_device_set_trusted(device, g_variant_get_boolean(property_value));
    } else if (g_str_equal(property_name, DEVICE_PROPERTY_TXPOWER)) {
        lm_device_set_txpower(device, g_variant_get_int16(property_value));
    } else if (g_str_equal(property_name, DEVICE_PROPERTY_UUIDS)) {
        lm_device_set_uuids(device, lm_utils_g_variant_string_array_to_list(property_value));
    } else if (g_str_equal(property_name, DEVICE_PROPERTY_MANUFACTURER_DATA)) {
        GVariantIter *iter;
        g_variant_get(property_value, "a{qv}", &iter);

        GVariant *array;
        guint16 key;
        GHashTable *manufacturer_data = g_hash_table_new_full(g_int_hash, g_int_equal,
                                                              g_free, (GDestroyNotify) lm_utils_byte_array_free);
        while (g_variant_iter_loop(iter, "{qv}", &key, &array)) {
            gsize data_length = 0;
            guint8 *data = (guint8 *) g_variant_get_fixed_array(array, &data_length, sizeof(guint8));
            GByteArray *byte_array = g_byte_array_sized_new((guint) data_length);
            g_byte_array_append(byte_array, data, (guint) data_length);

            int *key_copy = g_new0 (gint, 1);
            *key_copy = key;

            g_hash_table_insert(manufacturer_data, key_copy, byte_array);
        }
        lm_device_set_manufacturer_data(device, manufacturer_data);
        g_variant_iter_free(iter);
    } else if (g_str_equal(property_name, DEVICE_PROPERTY_SERVICE_DATA)) {
        GVariantIter *iter;
        g_variant_get(property_value, "a{sv}", &iter);

        GVariant *array;
        gchar *key;

        GHashTable *service_data = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                         g_free, (GDestroyNotify) lm_utils_byte_array_free);
        while (g_variant_iter_loop(iter, "{sv}", &key, &array)) {
            gsize data_length = 0;
            guint8 *data = (guint8 *) g_variant_get_fixed_array(array, &data_length, sizeof(guint8));
            GByteArray *byte_array = g_byte_array_sized_new((guint) data_length);
            g_byte_array_append(byte_array, data, (guint) data_length);

            gchar *key_copy = g_strdup(key);

            g_hash_table_insert(service_data, key_copy, byte_array);
        }
        lm_device_set_service_data(device, service_data);
        g_variant_iter_free(iter);
    }
}

lm_player_t *lm_device_get_active_player(lm_device_t *device)
{
    g_assert(device != NULL);

    return device->active_player;
}

lm_transport_t *lm_device_get_active_transport(lm_device_t *device)
{
    g_assert(device != NULL);

    return device->active_transport;
}

gboolean lm_device_is_special_device(lm_device_t *device)
{
    g_assert(device);

    // Check if the device is a special device (e.g., LEA Manager)
    if (device->uuids && g_list_length(device->uuids) > 0) {
        for (GList *iterator = device->uuids; iterator; iterator = iterator->next) {
            if (g_str_equal((gchar *) iterator->data, BCAST_AUDIO_AUNOUNCEMENT_SERVICE_UUID)) {
                return TRUE;
            }
        }
    }

    return FALSE;
}

lm_device_conn_bearer_t lm_device_get_conn_bearer(const lm_device_t *device)
{
    g_assert(device);

    return device->conn_bearer;
}

void lm_device_set_conn_bearer(lm_device_t *device, lm_device_conn_bearer_t bearer)
{
    g_assert(device);

    if (!(device->conn_bearer & bearer)) {
        device->conn_bearer |= bearer;
        lm_log_debug(TAG, "device '%s' new bearer set: 0x%x", device->path, bearer);
    }
}

void lm_device_reset_conn_bearer(lm_device_t *device, lm_device_conn_bearer_t bearer)
{
    g_assert(device);

    if (device->conn_bearer & bearer) {
        device->conn_bearer &= ~bearer;
        lm_log_debug(TAG, "device '%s' bearer reset: 0x%x", device->path, bearer);
    }
}

gboolean lm_device_has_bearer(lm_device_t *device, lm_device_conn_bearer_t bearer)
{
    g_assert(device);
    return (device->conn_bearer & bearer) != 0;
}

static void lm_device_disconnect_cb(__attribute__((unused)) GObject *source_object,
                                               GAsyncResult *res,
                                               gpointer user_data) {

    lm_device_t *device = (lm_device_t *) user_data;
    g_assert(device != NULL);

    GError *error = NULL;
    GVariant *value = g_dbus_connection_call_finish(device->dbus_conn, res, &error);
    if (value != NULL) {
        g_variant_unref(value);
    }

    if (error != NULL) {
        lm_log_error(TAG, "failed to call '%s' (error %d: %s)", DEVICE_METHOD_DISCONNECT, error->code, error->message);
        lm_device_set_conn_state(device, LM_DEVICE_CONNECTED);
        g_clear_error(&error);
    }
}

void lm_device_disconnect(lm_device_t *device)
{
    g_assert(device != NULL);
    g_assert(device->path != NULL);

    if (device->connection_state != LM_DEVICE_CONNECTED)
        return;

    lm_log_debug(TAG, "Disconnecting '%s' (%s)", device->name, device->address);

    lm_device_set_conn_state(device, LM_DEVICE_DISCONNECTING);
    g_dbus_connection_call(device->dbus_conn,
                           BLUEZ_DBUS,
                           device->path,
                           INTERFACE_DEVICE,
                           DEVICE_METHOD_DISCONNECT,
                           NULL,
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                           NULL,
                           (GAsyncReadyCallback) lm_device_disconnect_cb,
                           device);
}

lm_status_t lm_device_disconnect_sync(lm_device_t *device)
{
    GError *error = NULL;
    g_assert(device != NULL);
    g_assert(device->path != NULL);

    if (device->connection_state != LM_DEVICE_CONNECTED)
        return LM_STATUS_FAIL;

    lm_log_debug(TAG, "Disconnecting '%s' (%s)", device->name, device->address);

    g_dbus_connection_call_sync(device->dbus_conn,
                           BLUEZ_DBUS,
                           device->path,
                           INTERFACE_DEVICE,
                           DEVICE_METHOD_DISCONNECT,
                           NULL,
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                           NULL,
                           &error);

    if (error) {
        lm_log_error(TAG, "Failed to disconnect device '%s': %s", device->path, error->message);
        g_error_free(error);
        return LM_STATUS_FAIL;
    }

    lm_device_set_conn_state(device, LM_DEVICE_DISCONNECTED);

    return LM_STATUS_SUCCESS;
}

lm_status_t lm_device_connect_sync(lm_device_t *device)
{
    GError *error = NULL;
    g_assert(device != NULL);
    g_assert(device->path != NULL);

    if (device->connection_state != LM_DEVICE_DISCONNECTED)
        return LM_STATUS_FAIL;

    lm_log_debug(TAG, "Connecting '%s' (%s)", device->name, device->address);

    g_dbus_connection_call_sync(device->dbus_conn,
                           BLUEZ_DBUS,
                           device->path,
                           INTERFACE_DEVICE,
                           DEVICE_METHOD_CONNECT,
                           NULL,
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                           NULL,
                           &error);

    if (error) {
        lm_log_error(TAG, "Failed to connect device '%s': %s", device->path);
        g_error_free(error);
        return LM_STATUS_FAIL;
    }

    lm_device_set_conn_state(device, LM_DEVICE_CONNECTED);

    return LM_STATUS_SUCCESS;
}

lm_status_t lm_device_start_sync_broadcast(lm_device_t *device, lm_transport_audio_location_t location)
{
    g_assert(device);

    lm_log_info(TAG, "Start syncing broadcast with device '%s', location %d", device->path, location);

    GPtrArray *bcast_transports = lm_device_get_transports(device, LM_TRANSPORT_PROFILE_BAP_BCAST_SINK);
    if (bcast_transports->len == 0) {
        g_ptr_array_free(bcast_transports, TRUE);
        lm_log_error(TAG, "No broadcast transports available");
        return LM_STATUS_FAIL;
    }

    device->bcast_audio_location = location;

    switch (location) {
        case LM_TRANSPORT_AUDIO_LOCATION_NONE:
            break;
        case LM_TRANSPORT_AUDIO_LOCATION_MONO_LEFT:
        case LM_TRANSPORT_AUDIO_LOCATION_MONO_RIGHT: {
            lm_transport_t *transport = (lm_transport_t *)g_ptr_array_index(bcast_transports, location);
            if (transport)
                lm_transport_select(transport);
            break;
        }
        case LM_TRANSPORT_AUDIO_LOCATION_STEREO: {
            lm_transport_set_links(bcast_transports);
            for (guint i = 0; i < bcast_transports->len; i++)
                lm_transport_select((lm_transport_t *)g_ptr_array_index(bcast_transports, i));
            break;
        }
        default:
            break;
    }

    g_ptr_array_free(bcast_transports, TRUE);
    return LM_STATUS_SUCCESS;
}

lm_status_t lm_device_stop_sync_broadcast(lm_device_t *device)
{
    g_assert(device);

    lm_log_info(TAG, "Stop syncing broadcast with device '%s'", device->path);

    GPtrArray *bcast_transports = lm_device_get_transports(device, LM_TRANSPORT_PROFILE_BAP_BCAST_SINK);
    if (bcast_transports->len == 0) {
        g_ptr_array_free(bcast_transports, TRUE);
        lm_log_error(TAG, "No broadcast transports available");
        return LM_STATUS_FAIL;
    }
    return lm_adapter_remove_device(device->adapter, device);
}

GPtrArray *lm_device_get_transports(lm_device_t *device, lm_transport_profile_t profile)
{
    g_assert(device != NULL);
    GHashTableIter hash_iter;
    gpointer key, value;
    GPtrArray *result = g_ptr_array_new();

    g_hash_table_iter_init(&hash_iter, device->transports);
    while (g_hash_table_iter_next(&hash_iter, &key, &value)) {
        lm_transport_t *transport = (lm_transport_t *)value;
        if (lm_transport_get_profile(transport) == profile)
            g_ptr_array_add(result, transport);
    }

    return result;
}
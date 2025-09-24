#include "bluez_dbus.h"
#include "lm_adv.h"
#include "lm_log.h"
#include "lm_utils.h"
#include "lm.h"
#include "bluez_iface.h"
#include <glib.h>
#include <gio/gio.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#define TAG "lm_adv"

struct lm_adv {
    GDBusConnection *dbus_conn;  // Borrowed
    gchar *path; // Owned
    gchar *local_name; // Owned
    GPtrArray *services; // Owned
    GHashTable *manufacturer_data; // Owned
    GHashTable *service_data; // Owned
    guint registration_id;
    guint32 min_interval;
    guint32 max_interval;
    guint16 appearance;
    gboolean discoverable;
    guint16 discoverable_timeout;
    gint16 tx_power;
    lm_adv_type_t type;
    GPtrArray *includes; // owned
    lm_adv_secondary_channel_t secondary_channel;
};

typedef struct {
    guint8 active_instances;
    guint8 supported_instances;
    GPtrArray *supported_secondary_channels;
    GPtrArray *supported_includes;
} lm_adv_manager_info_t;

static gchar *secondary_channel_str[] = {
    "1M",
    "2M",
    "Coded"
};

static gchar *adv_type_str[] = {
    "peripheral",
    "broadcast"
};

static void add_manufacturer_data(gpointer key, gpointer value, gpointer userdata) {
    GByteArray *byteArray = (GByteArray *) value;
    GVariant *byteArrayVariant = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, byteArray->data,
                                                           byteArray->len, sizeof(guint8));
    guint16 manufacturer_id = (guint16) *(int *) key;
    g_variant_builder_add((GVariantBuilder *) userdata, "{qv}", manufacturer_id, byteArrayVariant);
}

static void add_service_data(gpointer key, gpointer value, gpointer userdata) {
    GByteArray *byteArray = (GByteArray *) value;
    GVariant *byteArrayVariant = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, byteArray->data,
                                                           byteArray->len, sizeof(guint8));
    g_variant_builder_add((GVariantBuilder *) userdata, "{sv}", (char *) key, byteArrayVariant);
}

static lm_adv_manager_info_t * lm_adv_manager_info_create(void)
{
    lm_adv_manager_info_t *info = g_new0(lm_adv_manager_info_t, 1);
    info->supported_secondary_channels = g_ptr_array_new_with_free_func(g_free);
    info->supported_includes = g_ptr_array_new_with_free_func(g_free);
    return info;
}

static void lm_adv_manager_info_destroy(lm_adv_manager_info_t *info)
{
    if (!info)
        return;

    if (info->supported_secondary_channels)
        g_ptr_array_free(info->supported_secondary_channels, TRUE);

    if (info->supported_includes)
        g_ptr_array_free(info->supported_includes, TRUE);

    g_free((void *)info);
}

static lm_status_t lm_adv_manager_get_info(GDBusConnection *dbus_conn, lm_adv_manager_info_t *info)
{
    g_assert(dbus_conn && info);
    lm_log_debug(TAG, "Getting advertising manager information");

    GError *error = NULL;
    GVariant *result = g_dbus_connection_call_sync(
                                            dbus_conn,
                                            BLUEZ_DBUS,
                                            "/",
                                            INTERFACE_OBJECT_MANAGER,
                                            OBJECT_MANAGER_METHOD_GET_MANAGED_OBJECTS,
                                            NULL,
                                            G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
                                            G_DBUS_CALL_FLAGS_NONE,
                                            BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                                            NULL,
                                            &error
    );

    if (!result) {
        lm_log_error(TAG, "Error GetManagedObjects: %s", error->message);
        g_clear_error(&error);
        return LM_STATUS_FAIL;
    }

    GVariantIter *iter = NULL;
    g_variant_get(result, "(a{oa{sa{sv}}})", &iter);

    const gchar *object_path;
    GVariant *ifaces_and_properties;

    while (g_variant_iter_loop(iter, "{&o@a{sa{sv}}}", &object_path, &ifaces_and_properties)) {
        GVariantIter iter2;
        const gchar *interface_name;
        GVariant *properties;

        g_variant_iter_init(&iter2, ifaces_and_properties);
        while (g_variant_iter_loop(&iter2, "{&s@a{sv}}", &interface_name, &properties)) {
            if (!g_str_equal(interface_name, INTERFACE_ADV_MANAGER)) {
                //g_variant_unref(properties);
                continue;
            }

            GVariantIter iter3;
            gchar *property_name;
            GVariant *property_value;

            g_variant_iter_init(&iter3, properties);
            while (g_variant_iter_loop(&iter3, "{&sv}", &property_name, &property_value)) {
                if (g_str_equal(property_name, ADV_MANAGER_PROPERTY_ACTIVE_INSTANCES)) {
                    info->active_instances = g_variant_get_byte(property_value);
                } else if (g_str_equal(property_name, ADV_MANAGER_PROPERTY_SUPPORTED_INSTANCES)) {
                    info->supported_instances = g_variant_get_byte(property_value);
                } else if (g_str_equal(property_name, ADV_MANAGER_PROPERTY_SUPPORTED_SECONDARY_CHANNELS)) {
                    GVariantIter *array_iter;
                    gchar *value;
                    g_variant_get(property_value, "as", &array_iter);
                    while (g_variant_iter_loop(array_iter, "s", &value)) {
                        g_ptr_array_add(info->supported_secondary_channels, g_strdup(value));
                    }
                    g_variant_iter_free(array_iter);
                } else if (g_str_equal(property_name, ADV_MANAGER_PROPERTY_SUPPORTED_INCLUDES)) {
                    GVariantIter *array_iter;
                    gchar *value;
                    g_variant_get(property_value, "as", &array_iter);
                    while (g_variant_iter_loop(array_iter, "s", &value)) {
                        g_ptr_array_add(info->supported_includes, g_strdup(value));
                    }
                    g_variant_iter_free(array_iter);
                }
            }
            //g_variant_unref(properties);
        }
        //g_variant_unref(ifaces_and_properties);
    }

    g_variant_iter_free(iter);
    g_variant_unref(result);

    return LM_STATUS_SUCCESS;
}

lm_adv_t *lm_adv_create(void)
{
    lm_adv_t *adv = NULL;
    lm_adv_manager_info_t *info = NULL;
    GDBusConnection *dbus_conn = lm_get_gdbus_connection();
    if (!dbus_conn) {
        lm_log_error(TAG, "no dbus connection, please call lm_init() first!");
        goto EXIT;
    }

    info = lm_adv_manager_info_create();

    if (LM_STATUS_SUCCESS != lm_adv_manager_get_info(dbus_conn, info)) {
        lm_log_error(TAG, "can not get adv manager info!");
        goto EXIT;
    }

    lm_log_debug(TAG, "active_instances:%d, supported_instances:%d",
                    info->active_instances,
                    info->supported_instances);
    if (!info->supported_instances) {
        lm_log_error(TAG, "no available adv instance, active:%d, supported:%d",
                    info->active_instances, info->supported_instances);
        goto EXIT;
    }

    adv = g_new0(lm_adv_t, 1);
    adv->dbus_conn = dbus_conn;

    adv->path = g_strdup_printf("/org/bluez/lmadv_instance%d", info->active_instances);
    adv->manufacturer_data = g_hash_table_new_full(g_int_hash, g_int_equal, g_free,
                                                             (GDestroyNotify) lm_utils_byte_array_free);
    adv->service_data = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                                        (GDestroyNotify) lm_utils_byte_array_free);
    adv->min_interval = 200;
    adv->max_interval = 500;
    adv->tx_power = 4;
    adv->type = LM_ADV_PERIPHERAL;
    adv->includes = g_ptr_array_new();
    adv->secondary_channel = LM_ADV_SC_1M;

EXIT:
    if (info)
        lm_adv_manager_info_destroy(info);
    return adv;
}

void lm_adv_set_type(lm_adv_t *adv, lm_adv_type_t type)
{
    g_assert(adv);
    adv->type = type;
}

lm_adv_type_t lm_adv_get_type(lm_adv_t *adv)
{
    g_assert(adv);
    return adv->type;
}

void lm_adv_destroy(lm_adv_t *adv)
{
    g_assert(adv);

    g_free(adv->path);
    adv->path = NULL;

    g_free(adv->local_name);
    adv->local_name = NULL;

    if (adv->services != NULL) {
        g_ptr_array_free(adv->services, TRUE);
        adv->services = NULL;
    }
    if (adv->manufacturer_data != NULL) {
        g_hash_table_destroy(adv->manufacturer_data);
        adv->manufacturer_data = NULL;
    }
    if (adv->service_data != NULL) {
        g_hash_table_destroy(adv->service_data);
        adv->service_data = NULL;
    }

    if (adv->includes != NULL) {
        g_ptr_array_free(adv->includes, TRUE);
        adv->includes = NULL;
    }

    g_free(adv);
}

void lm_adv_set_local_name(lm_adv_t *adv, const gchar *name)
{
    g_assert(adv && name);

    if (adv->local_name)
        g_free((void *)adv->local_name);
    adv->local_name = g_strdup(name);
}

const gchar *lm_adv_get_local_name(lm_adv_t *adv)
{
    return adv->local_name;
}

void lm_adv_set_services(lm_adv_t *adv, const GPtrArray * service_uuids)
{
    g_assert(adv);
    g_assert(service_uuids != NULL);
    if (adv->services != NULL) {
        g_ptr_array_free(adv->services, TRUE);
    }
    adv->services = g_ptr_array_new_with_free_func(g_free);

    for (guint i = 0; i < service_uuids->len; i++) {
        g_ptr_array_add(adv->services, g_strdup(g_ptr_array_index(service_uuids, i)));
    }
}

void lm_adv_set_manufacturer_data(lm_adv_t *adv, guint16 manufacturer_id, const GByteArray *byteArray)
{
    g_assert(adv);
    g_assert(adv->manufacturer_data != NULL);
    g_assert(byteArray != NULL);

    int man_id = manufacturer_id;
    g_hash_table_remove(adv->manufacturer_data, &man_id);

    int *key = g_new0 (int, 1);
    *key = manufacturer_id;

    GByteArray *value = g_byte_array_sized_new(byteArray->len);
    g_byte_array_append(value, byteArray->data, byteArray->len);

    g_hash_table_insert(adv->manufacturer_data, key, value);
}

void lm_adv_set_service_data(lm_adv_t *adv, const gchar* service_uuid, const GByteArray *byteArray)
{
    g_assert(adv);
    g_assert(adv->service_data != NULL);
    g_assert(service_uuid != NULL);
    g_assert(lm_utils_is_valid_uuid(service_uuid));
    g_assert(byteArray != NULL);

    g_hash_table_remove(adv->service_data, service_uuid);

    GByteArray *value = g_byte_array_sized_new(byteArray->len);
    g_byte_array_append(value, byteArray->data, byteArray->len);

    g_hash_table_insert(adv->service_data, g_strdup(service_uuid), value);
}

void lm_adv_set_interval(lm_adv_t *adv, guint32 min, guint32 max)
{
    g_assert(adv);
    g_assert(min <= max);

    adv->min_interval = min;
    adv->max_interval = max;
}

const gchar *lm_adv_get_path(const lm_adv_t *adv)
{
    g_assert(adv);
    return adv->path;
}

void lm_adv_set_appearance(lm_adv_t *adv, guint16 appearance)
{
    g_assert(adv);
    adv->appearance = appearance;
}

guint16 lm_adv_get_appearance(lm_adv_t *adv)
{
    g_assert(adv);
    return adv->appearance;
}

void lm_adv_set_discoverable(lm_adv_t *adv, gboolean discoverable)
{
    g_assert(adv);
    adv->discoverable = discoverable;
}

gboolean lm_adv_is_discoverable(lm_adv_t *adv)
{
    g_assert(adv);
    return adv->discoverable;
}

void lm_adv_set_discoverable_timeout(lm_adv_t *adv, guint16 timeout)
{
    g_assert(adv);
    adv->discoverable_timeout = timeout;
}

guint16 lm_adv_get_discoverable_timeout(lm_adv_t *adv)
{
    g_assert(adv);
    return adv->discoverable_timeout;
}

void lm_adv_set_tx_power(lm_adv_t *adv, gint16 tx_power)
{
    g_assert(adv);
    g_assert(tx_power >= -127);
    g_assert(tx_power <= 20);

    adv->tx_power = tx_power;

    // Add 'tx-power' to the list of includes if needed
    if (adv->includes == NULL) {
        adv->includes = g_ptr_array_new();
    }
    // Try to remove to avoid adding duplicated value
    g_ptr_array_remove(adv->includes, "tx-power");
    g_ptr_array_add(adv->includes, "tx-power");
}

gint16 lm_adv_get_tx_power(lm_adv_t *adv)
{
    g_assert(adv);
    return adv->tx_power;
}

void lm_adv_set_secondary_channel(lm_adv_t *adv, lm_adv_secondary_channel_t secondary_channel)
{
    g_assert(adv);
    g_assert(secondary_channel <= LM_ADV_SC_CODED);

    adv->secondary_channel = secondary_channel;
}

lm_adv_secondary_channel_t lm_adv_get_secondary_channel(lm_adv_t *adv)
{
    g_assert(adv);
    return adv->secondary_channel;
}

void lm_adv_set_rsi(lm_adv_t *adv)
{
    g_assert(adv);

    if (adv->includes == NULL) {
        adv->includes = g_ptr_array_new();
    }
    // Try to remove to avoid adding duplicated value
    g_ptr_array_remove(adv->includes, "rsi");
    g_ptr_array_add(adv->includes, "rsi");
}

static GVariant *lm_adv_get_property(__attribute__((unused)) GDBusConnection *connection,
                                     __attribute__((unused)) const gchar *sender,
                                     __attribute__((unused)) const gchar *object_path,
                                     __attribute__((unused)) const gchar *interface_name,
                                     const gchar *property_name,
                                     __attribute__((unused)) GError **error,
                                     __attribute__((unused)) gpointer user_data) {

    GVariant *ret = NULL;
    lm_adv_t *adv = user_data;
    g_assert(adv);

    if (g_str_equal(property_name, ADV_PROPERTY_TYPE)) {
        ret = g_variant_new_string(adv_type_str[adv->type]);
    } else if (g_str_equal(property_name, ADV_PROPERTY_LOCAL_NAME)) {
        ret = adv->local_name ? g_variant_new_string(adv->local_name) : NULL;
    } else if (g_str_equal(property_name, ADV_PROPERTY_SERVICE_UUIDS)) {
        GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("as"));
        if (adv->services != NULL) {
            for (guint i = 0; i < adv->services->len; i++) {
                char *service_uuid = g_ptr_array_index(adv->services, i);
                g_variant_builder_add(builder, "s", service_uuid);
            }
        }
        ret = g_variant_builder_end(builder);
        g_variant_builder_unref(builder);
    } else if (g_str_equal(property_name, ADV_PROPERTY_MANUFACTURE_DATA)) {
        GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("a{qv}"));
        if (adv->manufacturer_data != NULL && g_hash_table_size(adv->manufacturer_data) > 0) {
            g_hash_table_foreach(adv->manufacturer_data, add_manufacturer_data, builder);
        }
        ret = g_variant_builder_end(builder);
        g_variant_builder_unref(builder);
    } else if (g_str_equal(property_name, ADV_PROPERTY_SERVICE_DATA)) {
        GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
        if (adv->service_data != NULL && g_hash_table_size(adv->service_data) > 0) {
            g_hash_table_foreach(adv->service_data, add_service_data, builder);
        }
        ret = g_variant_builder_end(builder);
        g_variant_builder_unref(builder);
    } else if (g_str_equal(property_name, ADV_PROPERTY_MIN_INTERVAL)) {
        lm_log_debug(TAG, "setting advertising MinInterval to %dms (requires experimental if version < v5.77)", adv->min_interval);
        ret = g_variant_new_uint32(adv->min_interval);
    } else if (g_str_equal(property_name, ADV_PROPERTY_MAX_INTERVAL)) {
        lm_log_debug(TAG, "setting advertising MaxInterval to %dms (requires experimental if version < v5.77)", adv->max_interval);
        ret = g_variant_new_uint32(adv->max_interval);
    } else if (g_str_equal(property_name, ADV_PROPERTY_APPEARANCE)) {
        ret = g_variant_new_uint16(adv->appearance);
    } else if (g_str_equal(property_name, ADV_PROPERTY_DISCOVERABLE)) {
        ret = g_variant_new_boolean(adv->discoverable);
    } else if (g_str_equal(property_name, ADV_PROPERTY_TX_POWER)) {
        ret = g_variant_new_int16(adv->tx_power);
    } else if (g_str_equal(property_name, ADV_PROPERTY_INCLUDES)) {
        GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("as"));
        if (adv->includes != NULL) {
            for (guint i = 0; i < adv->includes->len; i++) {
                char *element = g_ptr_array_index(adv->includes, i);
                g_variant_builder_add(builder, "s", element);
            }
        }
        ret = g_variant_builder_end(builder);
        g_variant_builder_unref(builder);
    } else if (g_str_equal(property_name, ADV_PROPERTY_SECONDARY_CHANNEL)) {
        ret = g_variant_new_string(secondary_channel_str[adv->secondary_channel]);
    }
    return ret;
}

static void lm_adv_method_call(__attribute__((unused)) GDBusConnection *conn,
                                      __attribute__((unused)) const gchar *sender,
                                      __attribute__((unused)) const gchar *path,
                                      __attribute__((unused)) const gchar *interface,
                                      __attribute__((unused)) const gchar *method,
                                      __attribute__((unused)) GVariant *params,
                                      __attribute__((unused)) GDBusMethodInvocation *invocation,
                                      __attribute__((unused)) void *userdata) {
    lm_log_debug(TAG, "adv method called");
}

static const GDBusInterfaceVTable lm_adv_method_table = {
        .method_call = lm_adv_method_call,
        .get_property = lm_adv_get_property
};

lm_status_t lm_adv_register(lm_adv_t *adv)
{
    g_assert(adv);

    GDBusConnection *dbus_conn = lm_get_gdbus_connection();
    if (!dbus_conn) {
        lm_log_error(TAG, "no dbus connection, please call lm_init() first!");
        return LM_STATUS_FAIL;
    }

    GError *error = NULL;
    adv->registration_id = g_dbus_connection_register_object(
                                        dbus_conn,
                                        adv->path,
                                        (GDBusInterfaceInfo *)&bluez_leadvertisement1_interface,
                                        &lm_adv_method_table,
                                        adv,
                                        NULL,
                                        &error);

    if (error != NULL) {
        lm_log_error(TAG, "registering adv failed: %s", error->message);
        g_clear_error(&error);
        return LM_STATUS_FAIL;
    }

    return LM_STATUS_SUCCESS;
}

lm_status_t lm_adv_unregister(lm_adv_t *adv)
{
    g_assert(adv);
    GDBusConnection *dbus_conn = lm_get_gdbus_connection();
    if (!dbus_conn) {
        lm_log_error(TAG, "no dbus connection, please call lm_init() first!");
        return LM_STATUS_FAIL;
    }

    gboolean result = g_dbus_connection_unregister_object(dbus_conn,
                                                          adv->registration_id);
    if (!result) {
        lm_log_error(TAG, "failed to unregister adv");
        return LM_STATUS_FAIL;
    }

    return LM_STATUS_SUCCESS;
}

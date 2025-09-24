#include "bluez_dbus.h"
#include "lm_device.h"
#include "lm_device_priv.h"
#include "lm_adapter.h"
#include "lm_log.h"
#include "lm_utils.h"
#include "lm_uuids.h"
#include "lm_transport.h"
#include "lm_transport_priv.h"
#include <math.h>

#define TAG "lm_transport"

#define AVRCP_VOLUME_MIN   0
#define AVRCP_VOLUME_MAX   0x7F

#define MCP_VOLUME_MIN     0
#define MCP_VOLUME_MAX     0xFF

#define VOLUME_PERCENTAGE_MAX 100.0f

struct lm_transport {
    GDBusConnection *dbus_conn;
    lm_device_t *device;
    const gchar *path;       /* object path */
    const gchar *device_path;/* Device object which the transport is connected to. */
    const gchar *uuid;       /* UUID of the profile which the transport is for. */
    guint8 codec;            /* Assigned number of codec that the transport support. */
    guint8 *config;
    gsize  config_size;
    const gchar *state;     /* Indicates the state of the transport. */
    guint16 delay;          /* Transport delay in 1/10 of millisecond. */
    guint16 volume;         /* Indicates volume level of the transport. */
    const gchar *endpoint;  /* Endpoint object which the transport is associated with. */
    guint32 location;       /* Indicates transport Audio Location. */
    guint8 *meta;
    gsize meta_size;
    GPtrArray *links;       /* Linked transport objects which the transport is associated with. */
    lm_transport_qos_t qos;
    lm_transport_profile_t profile; /* Indicates the profile of the transport. */
};

typedef struct {
    lm_transport_profile_t profile;
    const gchar *uuid;
} lm_transport_profile_map_t;

/*
"idle": not streaming
"pending": streaming but not acquired
"broadcasting": streaming but not acquired, applicable only for transports created by a broadcast sink
"active": streaming and acquired
*/
static const gchar *transport_state_str[] = {
    "error",
    "idle",
    "pending",
    "broadcasting",
    "active"
};

static const gchar *transport_profile_str[] = {
    "null",
    "a2dp_sink",
    "bap_sink",
    "bap_bcast_sink",
    "bap_bcast_src"
};

static const lm_transport_profile_map_t transport_profile_map[] = {
    {LM_TRANSPORT_PROFILE_NULL,           NULL_SERVICE_UUID},
    {LM_TRANSPORT_PROFILE_A2DP_SINK,      AUDIO_SINK_SERVICE_UUID},
    {LM_TRANSPORT_PROFILE_BAP_SINK,       SINK_PAC_SERVICE_UUID},
    {LM_TRANSPORT_PROFILE_BAP_BCAST_SINK, BASIC_AUDIO_AUNOUNCEMENT_SERVICE_UUID},
    {LM_TRANSPORT_PROFILE_BAP_BCAST_SRC,  BCAST_AUDIO_AUNOUNCEMENT_SERVICE_UUID},
};

static lm_transport_state_t lm_transport_string_to_state(const gchar *state_str)
{
   if (state_str == NULL)
        return LM_TRANSPORT_ERROR;

    for (guint i = 0; i < G_N_ELEMENTS(transport_state_str); i++) {
        if (g_strcmp0(state_str, transport_state_str[i]) == 0) {
            return (lm_transport_state_t)i;
        }
    }
    return LM_TRANSPORT_ERROR;
}

static lm_transport_profile_t lm_transport_uuid_to_profile(const gchar *uuid)
{
   if (uuid == NULL)
        return LM_TRANSPORT_PROFILE_NULL;

    for (guint i = 0; i < G_N_ELEMENTS(transport_profile_map); i++) {
        if (g_strcmp0(uuid, transport_profile_map[i].uuid) == 0) {
            return transport_profile_map[i].profile;
        }
    }
    return LM_TRANSPORT_PROFILE_NULL;
}

lm_transport_t *lm_transport_create(lm_device_t *device, const gchar *path)
{
    g_assert(path);
    g_assert(strlen(path) > 0);

    lm_transport_t *transport = g_new0(lm_transport_t, 1);
    transport->dbus_conn = device ? lm_device_get_dbus_conn(device) : lm_get_gdbus_connection();
    transport->device = device;
    transport->path = g_strdup(path);

    lm_log_debug(TAG, "create transport '%s'", path);
    return transport;
}

void lm_transport_destroy(lm_transport_t *transport)
{
    g_assert(transport);

    lm_log_debug(TAG, "destroy transport '%s'", transport->path);

    if (transport->path)
        g_free((gpointer)transport->path);
    if (transport->device_path)
        g_free((gpointer)transport->device_path);
    if (transport->uuid)
        g_free((gpointer)transport->uuid);
    if (transport->config)
        g_free((gpointer)transport->config);
    if (transport->state)
        g_free((gpointer)transport->state);
    if (transport->endpoint)
        g_free((gpointer)transport->endpoint);
    if (transport->meta)
        g_free((gpointer)transport->meta);
   //  if (transport->links)
   //      g_strfreev((gchar **)transport->links);
    if (transport->qos.bcode)
        g_free((gpointer)transport->qos.bcode);

    g_free(transport);
}

const gchar *lm_transport_get_path(lm_transport_t *transport)
{
    g_assert(transport);
    return transport->path;
}

lm_device_t *lm_transport_get_device(lm_transport_t *transport)
{
    g_assert(transport);
    return transport->device;
}

const gchar *lm_transport_get_device_path(lm_transport_t *transport)
{
    g_assert(transport);
    return transport->device_path;
}

const gchar *lm_transport_get_uuid(lm_transport_t *transport)
{
    g_assert(transport);
    return transport->uuid;
}

guint8 lm_transport_get_codec(lm_transport_t *transport)
{
    g_assert(transport);
    return transport->codec;
}

lm_transport_state_t lm_transport_get_state(lm_transport_t *transport)
{
    g_assert(transport);
    return lm_transport_string_to_state(transport->state);
}

guint32 lm_transport_get_location(lm_transport_t *transport)
{
    g_assert(transport);
    return transport->location;
}

lm_transport_qos_t *lm_transport_get_qos(lm_transport_t *transport)
{
    g_assert(transport);
    return &transport->qos;
}

lm_transport_profile_t lm_transport_get_profile(lm_transport_t *transport)
{
    g_assert(transport);
    return transport->profile;
}

const gchar *lm_transport_get_profile_name(lm_transport_t *transport)
{
    g_assert(transport);
    return transport_profile_str[transport->profile];
}

static void lm_transport_call_method_cb(__attribute__((unused)) GObject *source_object,
                                        GAsyncResult *res,
                                        gpointer user_data)
{
    lm_transport_t *transport = (lm_transport_t *) user_data;
    g_assert(transport != NULL);

    GError *error = NULL;
    GVariant *value = g_dbus_connection_call_finish(transport->dbus_conn, res, &error);
    if (value != NULL) {
        g_variant_unref(value);
    }

    if (error != NULL) {
        lm_log_error(TAG, "failed to call player method (error %d '%s')", error->code, error->message);
        g_clear_error(&error);
    }
}

static void lm_transport_call_method(lm_transport_t *transport, const gchar *method, GVariant *parameters)
{
    g_assert(transport != NULL);
    g_assert(method != NULL);

    g_dbus_connection_call(transport->dbus_conn,
                           BLUEZ_DBUS,
                           transport->path,
                           INTERFACE_MEDIA_TRANSPORT,
                           method,
                           parameters,
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                           NULL,
                           (GAsyncReadyCallback) lm_transport_call_method_cb,
                           transport);
}

lm_status_t lm_transport_select(lm_transport_t *transport)
{
    g_assert(transport);

    if (lm_transport_get_state(transport) != LM_TRANSPORT_IDLE) {
        lm_log_error(TAG, "transport '%s' is not ready to select", transport->path);
        return LM_STATUS_FAIL;
    }

    lm_transport_call_method(transport, MEDIA_TRANSPORT_METHOD_SELECT, NULL);

    return LM_STATUS_SUCCESS;
}

lm_status_t lm_transport_unselect(lm_transport_t *transport)
{
    g_assert(transport);

    if (lm_transport_get_state(transport) != LM_TRANSPORT_ACTIVE) {
        lm_log_error(TAG, "transport '%s' is not ready to unselect", transport->path);
        return LM_STATUS_FAIL;
    }

    lm_transport_call_method(transport, MEDIA_TRANSPORT_METHOD_UNSELECT, NULL);

    return LM_STATUS_SUCCESS;
}

static void __attribute__((unused)) lm_transport_set_property_cb(__attribute__((unused)) GObject *source_object,
                                        GAsyncResult *res,
                                        gpointer user_data)
{
    lm_transport_t *transport = (lm_transport_t *) user_data;
    g_assert(transport != NULL);

    GError *error = NULL;
    GVariant *value = g_dbus_connection_call_finish(transport->dbus_conn, res, &error);
    if (value != NULL) {
        g_variant_unref(value);
    }

    if (error != NULL) {
        lm_log_error(TAG, "failed to set property (error %d '%s') on transport '%s'",
            error->code, error->message, transport->path);
        g_clear_error(&error);
    }
}

static lm_status_t __attribute__((unused)) lm_transport_set_property_async(lm_transport_t *transport, const gchar *property, GVariant *value)
{
    g_assert(transport != NULL);
    g_assert(property != NULL);
    g_assert(value != NULL);

    g_dbus_connection_call(transport->dbus_conn,
                           BLUEZ_DBUS,
                           transport->path,
                           INTERFACE_PROPERTIES,
                           PROPERTIES_METHOD_SET,
                           g_variant_new("(ssv)", INTERFACE_MEDIA_TRANSPORT, property, value),
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                           NULL,
                           (GAsyncReadyCallback) lm_transport_set_property_cb,
                           transport);

    return LM_STATUS_SUCCESS;
}

static lm_status_t lm_transport_set_property_sync(lm_transport_t *transport, const gchar *property, GVariant *value)
{
    g_assert(transport != NULL);
    g_assert(property != NULL);
    g_assert(value != NULL);

    GError *error = NULL;
    GVariant *result = g_dbus_connection_call_sync(transport->dbus_conn,
                           BLUEZ_DBUS,
                           transport->path,
                           INTERFACE_PROPERTIES,
                           PROPERTIES_METHOD_SET,
                           g_variant_new("(ssv)", INTERFACE_MEDIA_TRANSPORT, property, value),
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                           NULL,
                           &error);
    if (error != NULL) {
        lm_log_error(TAG, "Failed to set transport property: %s", error->message);
        g_error_free(error);
        return LM_STATUS_FAIL;
    }
    lm_log_debug(TAG, "set transport property success");

    g_variant_unref(result);

    return LM_STATUS_SUCCESS;
}

lm_status_t lm_transport_set_links(GPtrArray *transports)
{
    g_assert(transports != NULL);

    if (transports->len < 2) {
       lm_log_error(TAG, "No need to link transports");
       return LM_STATUS_FAIL;
    }

    GVariantBuilder *array_builder = g_variant_builder_new(G_VARIANT_TYPE("ao"));

    for (guint i = 1; i < transports->len; i++) {
        lm_transport_t *transport = (lm_transport_t *)g_ptr_array_index(transports, i);
        if (!transport)
            continue;
        g_variant_builder_add(array_builder, "o", transport->path);
    }

    lm_transport_set_property_sync((lm_transport_t *)g_ptr_array_index(transports, 0),
                              MEDIA_TRANSPORT_PROPERTY_LINKS,
                              g_variant_new("ao", array_builder));
    g_variant_builder_unref(array_builder);

    return LM_STATUS_SUCCESS;
}

float lm_transport_get_volume_percentage(lm_transport_t *transport)
{
    g_assert(transport);

    if (transport->profile == LM_TRANSPORT_PROFILE_A2DP_SINK) {
        return roundf(((float)transport->volume * VOLUME_PERCENTAGE_MAX) / (float)AVRCP_VOLUME_MAX);
    } else if (transport->profile == LM_TRANSPORT_PROFILE_BAP_SINK) {
        return roundf(((float)transport->volume * VOLUME_PERCENTAGE_MAX) / (float)MCP_VOLUME_MAX);
    }

    return 0.0f;
}

lm_status_t lm_transport_set_volume_percentage(lm_transport_t *transport, float volume_per)
{
    g_assert(transport);
    guint16 volume = 0;

    if (volume_per > VOLUME_PERCENTAGE_MAX)
        volume_per = VOLUME_PERCENTAGE_MAX;
    else if (volume_per < 0.0f)
        volume_per = 0.0f;

    if (transport->profile == LM_TRANSPORT_PROFILE_A2DP_SINK) {
        volume = (guint16)lroundf((volume_per * (float)AVRCP_VOLUME_MAX) / VOLUME_PERCENTAGE_MAX);
    } else if (transport->profile == LM_TRANSPORT_PROFILE_BAP_SINK) {
        volume = (guint16)lroundf((volume_per * (float)MCP_VOLUME_MAX) / VOLUME_PERCENTAGE_MAX);
    } else {
        return LM_STATUS_INVALID_ARGS;
    }

    lm_status_t status = lm_transport_set_property_sync(transport, MEDIA_TRANSPORT_PROPERTY_VOLUME,
                                          g_variant_new("q", volume));
   if (status == LM_STATUS_SUCCESS) {
      transport->volume = volume;
      lm_log_info(TAG, "set volume to %.1f%% %d\n", transport->path, volume_per, volume);
   }

   return status;
}

void lm_transport_update_property(lm_transport_t *transport,
                  const char *property_name, GVariant *property_value)
{
    lm_log_debug(TAG, "transport '%s %s' property update", transport->path, lm_transport_get_profile_name(transport));

    if (g_str_equal(property_name, MEDIA_TRANSPORT_PROPERTY_DEVICE)) {
        if (transport->device_path)
            g_free((gpointer)transport->device_path);
        transport->device_path = g_strdup(g_variant_get_string(property_value, NULL));
        g_assert(g_variant_is_object_path(transport->device_path));
        lm_log_debug(TAG, "device path:'%s'", transport->device_path);
    } else if (g_str_equal(property_name, MEDIA_TRANSPORT_PROPERTY_UUID)) {
        if (transport->uuid)
            g_free((gpointer)transport->uuid);
        transport->uuid = g_strdup(g_variant_get_string(property_value, NULL));
        lm_log_info(TAG, "uuid:'%s'", transport->uuid);
        transport->profile = lm_transport_uuid_to_profile(transport->uuid);
    } else if (g_str_equal(property_name, MEDIA_TRANSPORT_PROPERTY_CODEC)) {
        transport->codec = g_variant_get_byte(property_value);
        lm_log_info(TAG, "codec:0x%x", transport->codec);
    } else if (g_str_equal(property_name, MEDIA_TRANSPORT_PROPERTY_CONFIG)) {
        if (transport->config)
            g_free(transport->config);
        gsize size;
        const guint8 *config = g_variant_get_fixed_array(property_value, &size, sizeof(guint8));
        transport->config = g_memdup2(config, size);
        transport->config_size = size;
        for (guint16 i = 0;i < transport->config_size;i++) {
           lm_log_debug(TAG, "config[%d]:0x%x", i, transport->config[i]);
        }
    } else if (g_str_equal(property_name, MEDIA_TRANSPORT_PROPERTY_STATE)) {
        if (transport->state)
            g_free((gpointer)transport->state);
        transport->state = g_strdup(g_variant_get_string(property_value, NULL));
        lm_log_info(TAG, "state:'%s'", transport->state);

        if (!transport->device)
            return;

        if (lm_device_get_active_transport(transport->device) == transport) {
            lm_transport_state_change_ind_t ind = {
                .transport = transport
            };
            lm_app_event_callback(LM_TRANSPORT_STATE_CHANGE_IND, LM_STATUS_SUCCESS, &ind);
        }
    } else if (g_str_equal(property_name, MEDIA_TRANSPORT_PROPERTY_DELAY)) {
        transport->delay = g_variant_get_uint16(property_value);
        lm_log_debug(TAG, "delay 0x%x", transport->delay);
    } else if (g_str_equal(property_name, MEDIA_TRANSPORT_PROPERTY_VOLUME)) {
        transport->volume = g_variant_get_uint16(property_value);
        lm_log_info(TAG, "volume 0x%x(%d) %.1f%%", transport->volume,
            transport->volume,lm_transport_get_volume_percentage(transport));

        if (!transport->device)
            return;

        if (lm_device_get_active_transport(transport->device) == transport) {
            lm_transport_volume_change_ind_t ind = {
                .transport = transport
            };
            lm_app_event_callback(LM_TRANSPORT_VOLUME_CHANGE_IND, LM_STATUS_SUCCESS, &ind);
        }
    } else if (g_str_equal(property_name, MEDIA_TRANSPORT_PROPERTY_ENDPOINT)) {
        if (transport->endpoint)
            g_free((gpointer)transport->endpoint);
        transport->endpoint = g_strdup(g_variant_get_string(property_value, NULL));
        g_assert(g_variant_is_object_path(transport->endpoint));
        lm_log_info(TAG, "endpoint path '%s'", transport->endpoint);
    } else if (g_str_equal(property_name, MEDIA_TRANSPORT_PROPERTY_LOCATION)) {
        transport->location = g_variant_get_uint32(property_value);
        lm_log_info(TAG, "location 0x%x", transport->location);
    } else if (g_str_equal(property_name, MEDIA_TRANSPORT_PROPERTY_METADATA)) {
        if (transport->meta)
            g_free((gpointer)transport->meta);
        gsize size;
        const guint8 *metadata = g_variant_get_fixed_array(property_value, &size, sizeof(guint8));
        transport->meta = g_memdup2(metadata, size);
        transport->meta_size = size;
        for (guint16 i = 0;i < transport->meta_size;i++) {
           lm_log_debug(TAG, "meta[%d]:0x%x", i, transport->meta[i]);
        }
    } else if (g_str_equal(property_name, MEDIA_TRANSPORT_PROPERTY_QOS)) {
        GVariantIter qos_iter;
        g_variant_iter_init(&qos_iter, property_value);
        char *qos_key = NULL;
        GVariant *qos_value = NULL;
        while (g_variant_iter_loop(&qos_iter, "{sv}", &qos_key, &qos_value)) {
            if (g_str_equal(qos_key, "BIG")) {
                transport->qos.big = g_variant_get_byte(qos_value);
                lm_log_debug(TAG, "BIG 0x%x", transport->qos.big);
            } else if (g_str_equal(qos_key, "BIS")) {
                transport->qos.bis = g_variant_get_byte(qos_value);
                lm_log_debug(TAG, "BIS 0x%x", transport->qos.bis);
            } else if (g_str_equal(qos_key, "BCode")) {
                if (transport->qos.bcode)
                   g_free((gpointer)transport->qos.bcode);
                gsize bcode_size;
                const guint8 *bcode = g_variant_get_fixed_array(qos_value, &bcode_size, sizeof(guint8));
                transport->qos.bcode = g_memdup2(bcode, bcode_size);
                transport->qos.bcode_size = bcode_size;
               lm_log_debug(TAG, "bcode '%s'", (gchar *)transport->qos.bcode);
            } else if (g_str_equal(qos_key, "Framing")) {
                transport->qos.framing = g_variant_get_byte(qos_value);
                lm_log_debug(TAG, "framing 0x%x", transport->qos.framing);
            } else if (g_str_equal(qos_key, "PresentationDelay")) {
                transport->qos.presentation_delay = g_variant_get_uint32(qos_value);
                lm_log_debug(TAG, "presentation delay 0x%x", transport->qos.presentation_delay);
            } else if (g_str_equal(qos_key, "Interval")) {
                transport->qos.interval = g_variant_get_uint32(qos_value);
                lm_log_debug(TAG, "interval 0x%x", transport->qos.interval);
            } else if (g_str_equal(qos_key, "PHY")) {
                transport->qos.phy = g_variant_get_byte(qos_value);
                lm_log_debug(TAG, "phy 0x%x", transport->qos.phy);
            } else if (g_str_equal(qos_key, "SDU")) {
                transport->qos.sdu = g_variant_get_uint16(qos_value);
                lm_log_debug(TAG, "sdu 0x%x", transport->qos.sdu);
            } else if (g_str_equal(qos_key, "Retransmissions")) {
                transport->qos.rtn = g_variant_get_byte(qos_value);
                lm_log_debug(TAG, "rtn 0x%x", transport->qos.rtn);
            } else if (g_str_equal(qos_key, "Latency")) {
                transport->qos.latency = g_variant_get_uint16(qos_value);
                lm_log_debug(TAG, "latency 0x%x", transport->qos.latency);
            }
        }

        if (!transport->device)
            return;

        if (lm_device_get_active_transport(transport->device) == transport) {
            lm_transport_qos_update_ind_t ind = {
                .transport = transport
            };
            lm_app_event_callback(LM_TRANSPORT_QOS_UPDATE_IND, LM_STATUS_SUCCESS, &ind);
        }
    }
}
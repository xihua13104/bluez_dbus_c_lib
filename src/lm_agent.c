#include "bluez_dbus.h"
#include "lm_agent.h"
#include "lm_device.h"
#include "lm_device_priv.h"
#include "lm_adapter.h"
#include "lm_log.h"
#include "lm_utils.h"
#include "lm.h"
#include "bluez_iface.h"
#include <glib.h>
#include <gio/gio.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#define TAG "lm_agent"

struct lm_agent {
    GDBusConnection *dbus_conn;  // Borrowed
    lm_adapter_t *adapter; // Borrowed
    gchar *path; // Owned
    lm_agent_io_capability_t io_capability;
    guint registration_id;
};

static gchar *agent_io_capa_name[] = {
    "DisplayOnly",
    "DisplayYesNo",
    "KeyboardOnly",
    "NoInputNoOutput",
    "KeyboardDisplay"
};

static lm_status_t lm_register_agent(lm_agent_t *agent);
static lm_status_t lm_agentmanager_register_agent(lm_agent_t *agent) ;

lm_agent_t *lm_agent_create(lm_adapter_t *adapter, lm_agent_io_capability_t io_capability) {
    lm_agent_t *agent = g_new0(lm_agent_t, 1);
    agent->path = g_strdup("/org/bluez/lm_agent");
    agent->dbus_conn = lm_adapter_get_dbus_conn(adapter);
    agent->adapter = adapter;
    agent->io_capability = io_capability;
    lm_register_agent(agent);
    lm_agentmanager_register_agent(agent);
    return agent;
}

void lm_agent_destroy(lm_agent_t *agent) {
    g_assert (agent != NULL);
    gboolean result = g_dbus_connection_unregister_object(agent->dbus_conn, agent->registration_id);
    if (!result) {
        lm_log_error(TAG, "could not unregister agent");
    }

    g_free((gchar *) agent->path);
    agent->path = NULL;

    agent->dbus_conn = NULL;
    agent->adapter = NULL;

    g_free(agent);
}

static void lm_agent_method_call(__attribute__((unused)) GDBusConnection *conn,
                                __attribute__((unused)) const gchar *sender,
                                __attribute__((unused)) const gchar *path,
                                __attribute__((unused)) const gchar *interface,
                                __attribute__((unused)) const gchar *method,
                                __attribute__((unused)) GVariant *params,
                                GDBusMethodInvocation *invocation,
                                void *userdata)
{
    guint32 pass;
    guint16 entered;
    gchar *object_path = NULL;
    gchar *pin = NULL;
    gchar *uuid = NULL;

    lm_agent_t *agent = (lm_agent_t *) userdata;
    g_assert(agent != NULL);

    lm_adapter_t *adapter = agent->adapter;
    g_assert(adapter != NULL);

    lm_log_debug(TAG, "lm_agent_method_call '%s'", method);

    if (g_str_equal(method, AGENT_METHOD_REQUEST_PIN_CODE)) {
        g_variant_get(params, "(o)", &object_path);
        lm_log_debug(TAG, "request pincode for %s", object_path);
        g_free(object_path);

        // add code to request pin
        pin = "123";
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", pin));
    } else if (g_str_equal(method, AGENT_METHOD_DISPLAY_PIN_CODE)) {
        g_variant_get(params, "(os)", &object_path, &pin);
        lm_log_debug(TAG, "displaying pincode %s", pin);
        g_free(object_path);
        g_free(pin);
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_str_equal(method, AGENT_METHOD_REQUEST_PASSKEY)) {
        g_variant_get(params, "(o)", &object_path);
        lm_device_t *device = lm_device_lookup_by_path(adapter, object_path);
        g_free(object_path);

        if (device != NULL) {
            lm_device_set_bonding_state(device, LM_DEVICE_BONDING);
        }
        lm_agent_req_passkey_ind_t ind = {
            .device = device,
            .passkey = 0
        };
        lm_app_event_callback(LM_AGENT_REQ_PASSKEY_IND, LM_STATUS_SUCCESS, &ind);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", ind.passkey));
        //  g_dbus_method_invocation_return_dbus_error(invocation, "org.bluez.Error.Rejected", "No passkey inputted");
    } else if (g_str_equal(method, AGENT_METHOD_DISPLAY_PASSKEY)) {
        g_variant_get(params, "(ouq)", &object_path, &pass, &entered);
        lm_log_info(TAG, "passkey: %u, entered: %u", pass, entered);
        g_free(object_path);
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_str_equal(method, AGENT_METHOD_REQUEST_CONFIRMATION)) {
        g_variant_get(params, "(ou)", &object_path, &pass);
        g_free(object_path);
        lm_log_debug(TAG, "request confirmation for %u", pass);
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_str_equal(method, AGENT_METHOD_REQUEST_AUTHORIZATION)) {
        g_variant_get(params, "(o)", &object_path);
        lm_log_debug(TAG, "request for authorization %s", object_path);
        lm_device_t *device = lm_device_lookup_by_path(adapter, object_path);
        g_free(object_path);
        if (device != NULL) {
            lm_device_set_bonding_state(device, LM_DEVICE_BONDING);
        }
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_str_equal(method, AGENT_METHOD_AUTHORIZESERVICE)) {
        g_variant_get(params, "(os)", &object_path, &uuid);
        lm_log_debug(TAG, "authorize service");
        g_free(object_path);
        g_free(uuid);
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_str_equal(method, AGENT_METHOD_CANCEL)) {
        lm_log_debug(TAG, "cancelling pairing");
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_str_equal(method, AGENT_METHOD_RELEASE)) {
        lm_log_debug(TAG, "agent released");
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else
        lm_log_error(TAG, "We should not come here, unknown method");
}

static const GDBusInterfaceVTable agent_method_table = {
        .method_call = lm_agent_method_call,
};

static lm_status_t lm_register_agent(lm_agent_t *agent) {
    GError *error = NULL;

    agent->registration_id = g_dbus_connection_register_object(agent->dbus_conn,
                                                               agent->path,
                                                               (GDBusInterfaceInfo *)&bluez_agent1_interface,
                                                               &agent_method_table,
                                                               agent, NULL, &error);

    if (error != NULL) {
        lm_log_error(TAG, "Register agent object failed %s\n", error->message);
        g_clear_error(&error);
        return LM_STATUS_FAIL;
    }
    return LM_STATUS_SUCCESS;
}

static lm_status_t lm_agentmanager_call_method(GDBusConnection *dbus_conn, const gchar *method, GVariant *param) {
    g_assert(dbus_conn != NULL);
    g_assert(method != NULL);

    GVariant *result;
    GError *error = NULL;

    result = g_dbus_connection_call_sync(dbus_conn,
                                         BLUEZ_DBUS,
                                         "/org/bluez",
                                         INTERFACE_AGENT_MANAGER,
                                         method,
                                         param,
                                         NULL,
                                         G_DBUS_CALL_FLAGS_NONE,
                                         BLUEZ_DBUS_CONNECTION_CALL_TIMEOUT,
                                         NULL,
                                         &error);
    if (result)
        g_variant_unref(result);

    if (error != NULL) {
        lm_log_error(TAG, "AgentManager call failed '%s': %s\n", method, error->message);
        g_clear_error(&error);
        return LM_STATUS_FAIL;
    }

    return LM_STATUS_SUCCESS;
}

static lm_status_t lm_agentmanager_register_agent(lm_agent_t *agent) {
    g_assert(agent != NULL);

    gchar *capability = agent_io_capa_name[agent->io_capability];

    lm_status_t result = lm_agentmanager_call_method(agent->dbus_conn, AGENT_MANAGER_METHOD_REGISTER,
                                               g_variant_new("(os)", agent->path, capability));
    if (result == EXIT_FAILURE) {
        lm_log_error(TAG, "failed to register agent");
    }

    result = lm_agentmanager_call_method(agent->dbus_conn, AGENT_MANAGER_METHOD_REQUEST_DEFAULT, g_variant_new("(o)", agent->path));
    if (result == EXIT_FAILURE) {
        lm_log_error(TAG, "failed to register agent as default agent");
    }
    return result;
}

const gchar *lm_agent_get_path(const lm_agent_t *agent) {
    g_assert(agent != NULL);
    return agent->path;
}

lm_adapter_t *lm_agent_get_adapter(const lm_agent_t *agent){
	g_assert(agent != NULL);
	return agent->adapter;
}

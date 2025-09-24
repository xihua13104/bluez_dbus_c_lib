#include "lm.h"
#include "lm_log.h"
#include "lm_transport.h"
#include <glib.h>

#define LM_APP_CALLBACK_MAX     20
#define TAG                     "lm"

typedef struct {
    gboolean in_use;
    guint32 mask;
    lm_app_callback_func_t cb;
} lm_app_callback_block_t;

typedef struct {
    gboolean in_use;
    void *cb;
} lm_usr_callback_block_t;

typedef enum {
    LM_IDLE = 0,
    LM_INITIALIZING,
    LM_READY,
    LM_DEINITIALIZING,
} lm_state_t;

typedef struct {
    GDBusConnection *gdbus_conn;
    GMainLoop *main_loop;
    pthread_t thread_id;
    lm_state_t state;
} lm_context_t;

static lm_app_callback_block_t app_callback_table[LM_APP_CALLBACK_MAX] = {0};
static lm_usr_callback_block_t usr_callback_table[LM_CALLBACK_TYPE_MAX - 1];
static lm_context_t lm_context = {0};

static void *lm_dbus_thread(void *user_data)
{
    GMainLoop *main_loop = (GMainLoop *)user_data;

    lm_log_info(TAG, "enter lea manager dbus thread");
    lm_context.state = LM_READY;
    g_main_loop_run(main_loop);
    lm_log_info(TAG, "exit lea manager dbus thread");
    pthread_exit(NULL);

    return NULL;
}

lm_status_t lm_init(void)
{
    if (LM_IDLE != lm_context.state) {
        lm_log_error(TAG, "wrong state:%d", lm_context.state);
        return LM_STATUS_FAIL;
    }

    lm_context.state = LM_INITIALIZING;

    // Get a DBus connection
    lm_context.gdbus_conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
    if (!lm_context.gdbus_conn) {
        lm_log_error(TAG, "get dbus connection fail");
        goto FAIL;
    }

    if (!lm_context.main_loop)
        lm_context.main_loop = g_main_loop_new(NULL, FALSE);

    if (pthread_create(&lm_context.thread_id, NULL, lm_dbus_thread, (void *)lm_context.main_loop)) {
        lm_log_error(TAG, "thread create failed");
        goto FAIL;
    }

    lm_log_info(TAG, "lea manager initialized successfully");

    return LM_STATUS_SUCCESS;

FAIL:
    if (lm_context.main_loop) {
        g_main_loop_quit(lm_context.main_loop);
        // Clean up mainloop
        g_main_loop_unref(lm_context.main_loop);
        lm_context.main_loop = NULL;
    }

    if (lm_context.thread_id) {
        pthread_join(lm_context.thread_id, NULL);
        lm_context.thread_id = 0;
    }

    if (lm_context.gdbus_conn) {
        g_dbus_connection_close_sync(lm_context.gdbus_conn, NULL, NULL);
        g_object_unref(lm_context.gdbus_conn);
        lm_context.gdbus_conn = NULL;
    }
    lm_context.state = LM_IDLE;

    return LM_STATUS_FAIL;
}

lm_status_t lm_deinit(void)
{
    if (lm_context.state != LM_READY) {
        lm_log_error(TAG, "wrong state:%d", lm_context.state);
        return LM_STATUS_FAIL;
    }

    if (lm_context.main_loop) {
        g_main_loop_quit(lm_context.main_loop);
    }

    if (lm_context.thread_id) {
        pthread_join(lm_context.thread_id, NULL);
        lm_context.thread_id = 0;
    }

    if (lm_context.main_loop) {
        g_main_loop_unref(lm_context.main_loop);
        lm_context.main_loop = NULL;
    }

    if (lm_context.gdbus_conn) {
        g_dbus_connection_close_sync(lm_context.gdbus_conn, NULL, NULL);
        g_object_unref(lm_context.gdbus_conn);
        lm_context.gdbus_conn = NULL;
    }

    lm_context.state = LM_IDLE;
    lm_log_info(TAG, "lea manager deinitialized successfully");

    return LM_STATUS_SUCCESS;
}

GDBusConnection *lm_get_gdbus_connection(void)
{
    return lm_context.gdbus_conn;
}

lm_status_t lm_register_callback(lm_callback_type_t type,
        lm_callback_module_mask_t module_mask,
        void *cb)
{
    lm_status_t status = LM_STATUS_FAIL;
    guint32 i = 0;

    switch (type) {
        case LM_CALLBACK_TYPE_APP_EVENT:
            for (i = 0; i < LM_APP_CALLBACK_MAX; i++) {
                if (!app_callback_table[i].in_use && app_callback_table[i].cb == NULL && app_callback_table[i].mask == 0 && cb) {
                    app_callback_table[i].in_use = TRUE;
                    app_callback_table[i].mask = module_mask;
                    app_callback_table[i].cb = (lm_app_callback_func_t)cb;
                    status = LM_STATUS_SUCCESS;
                    lm_log_debug(TAG, "register callback, module mask 0x%08x", module_mask);
                    break;
                }
            }
            break;
        default:
            if (type < LM_CALLBACK_TYPE_MAX && !usr_callback_table[type - 1].in_use) {
                usr_callback_table[type - 1].cb = cb;
                usr_callback_table[type - 1].in_use = TRUE;
            } else {
               lm_log_error(TAG, "fail to register callback type %d", type);
                status = LM_STATUS_FAIL;
            }
            break;
    }

    return status;
}

lm_status_t lm_unregister_callback(lm_callback_type_t type,
                __attribute__((unused)) lm_callback_module_mask_t module_mask,
                void *cb)
{
    lm_status_t status = LM_STATUS_FAIL;
    guint32 i = 0;

    switch (type) {
        case LM_CALLBACK_TYPE_APP_EVENT:
            for (i = 0; i < LM_APP_CALLBACK_MAX; i++) {
                if (app_callback_table[i].in_use && app_callback_table[i].cb == (lm_app_callback_func_t)cb) {
                    app_callback_table[i].in_use = FALSE;
                    app_callback_table[i].mask = 0;
                    app_callback_table[i].cb = NULL;
                    status = LM_STATUS_SUCCESS;
                    break;
                }
            }
            break;
        default:
            if (type < LM_CALLBACK_TYPE_MAX && usr_callback_table[type - 1].in_use) {
                usr_callback_table[type - 1].cb = NULL;
                usr_callback_table[type - 1].in_use = FALSE;
            }
            break;
    }

    return status;

}

void lm_app_event_callback(lm_msg_type_t msg, lm_status_t status, void *buf)
{
    guint32 i = 0;
    guint32 module_mask = LM_MODULE_MASK(msg);

    lm_log_debug(TAG, "app event callback, msg:0x%08x, module mask:0x%08x", msg, module_mask);
    for (i = 0; i < LM_APP_CALLBACK_MAX; i++) {
        if (app_callback_table[i].in_use && app_callback_table[i].cb && app_callback_table[i].mask & module_mask) {
            app_callback_table[i].cb(msg, status, buf);
        }
    }
}

lm_status_t lm_get_audio_location_config(lm_transport_profile_t profile,
                        lm_transport_audio_location_t *location)
{
    lm_callback_type_t cb_type = LM_CALLBACK_TYPE_GET_AUDIO_LOCATION_CFG;
    lm_get_audio_location_cfg_t cb = (lm_get_audio_location_cfg_t)usr_callback_table[cb_type - 1].cb;
    if (usr_callback_table[cb_type - 1].in_use && cb)
        return cb(profile, location);

    return LM_STATUS_FAIL;
}
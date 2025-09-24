#ifndef __LM_AGENT_H__
#define __LM_AGENT_H__

#include <glib.h>
#include <gio/gio.h>
#include "lm_forward_decl.h"

typedef enum {
    LM_AGENT_IO_CAPA_DISPLAY_ONLY,
    LM_AGENT_IO_CAPA_DISPLAY_YES_NO,
    LM_AGENT_IO_CAPA_KEYBOARD_ONLY,
    LM_AGENT_IO_CAPA_NO_INPUT_NO_OUTPUT,
    LM_AGENT_IO_CAPA_KEYBOARD_DISPLAY
} lm_agent_io_capability_t;

typedef struct {
    lm_device_t *device;
    guint32 passkey;
} lm_agent_req_passkey_ind_t;
#define LM_AGENT_REQ_PASSKEY_IND        (LM_MODULE_AGENT | 0x0001)

lm_agent_t *lm_agent_create(lm_adapter_t *adapter, lm_agent_io_capability_t io_capability);

void lm_agent_destroy(lm_agent_t *agent);

const gchar *lm_agent_get_path(const lm_agent_t *agent);

lm_adapter_t *lm_agent_get_adapter(const lm_agent_t *agent);

#endif //__LM_AGENT_H__
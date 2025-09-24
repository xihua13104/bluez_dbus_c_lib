#ifndef __LM_TYPE_H__
#define __LM_TYPE_H__

#include <glib.h>
#include <gio/gio.h>

#define LM_MODULE_OFFSET                        27

#define LM_MODULE_GENERAL                       (0x00 << LM_MODULE_OFFSET)
#define LM_MODULE_ADAPTER                       (0x01 << LM_MODULE_OFFSET)
#define LM_MODULE_ADV                           (0x02 << LM_MODULE_OFFSET)
#define LM_MODULE_AGENT                         (0x03 << LM_MODULE_OFFSET)
#define LM_MODULE_DEVICE                        (0x04 << LM_MODULE_OFFSET)
#define LM_MODULE_PLAYER                        (0x05 << LM_MODULE_OFFSET)
#define LM_MODULE_TRANSPORT                     (0x06 << LM_MODULE_OFFSET)
#define LM_MODULE_ENDPOINT                      (0x07 << LM_MODULE_OFFSET)
#define LM_MODULE_GATT                          (0x08 << LM_MODULE_OFFSET)
#define LM_MODULE_MAX                           (0x1F << LM_MODULE_OFFSET)

#define LM_STATUS_SUCCESS                       (LM_MODULE_GENERAL | (0))
#define LM_STATUS_FAIL                          (LM_MODULE_GENERAL | (1 << 0))
#define LM_STATUS_INVALID_ARGS                  (LM_MODULE_GENERAL | (1 << 1))
#define LM_STATUS_PENDING                       (LM_MODULE_GENERAL | (1 << 2))
#define LM_STATUS_BUSY                          (LM_MODULE_GENERAL | (1 << 3))
#define LM_STATUS_TIMEOUT                       (LM_MODULE_GENERAL | (1 << 4))
#define LM_STATUS_NOT_READY                     (LM_MODULE_GENERAL | (1 << 5))

typedef guint32 lm_status_t;

typedef guint32 lm_msg_type_t;

#endif
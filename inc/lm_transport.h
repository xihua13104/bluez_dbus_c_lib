#ifndef __LM_TRANSPORT_H__
#define __LM_TRANSPORT_H__

#include <glib.h>
#include <gio/gio.h>
#include "lm_forward_decl.h"
#include "lm_type.h"

typedef struct {
    guint8  big;
    guint8  bis;
    guint8  sync_factor;
    guint8  packing;
    guint8  framing;
    guint8  encryption;
    guint8 *bcode;
    guint16 bcode_size;
    guint8  options;
    guint16 skip;
    guint16 sync_timeout;
    guint8  sync_cte_type;
    guint8  mse;
    guint16 timeout;
    guint8  pa_sync;
    guint32 interval; /* Frame interval */
    guint16 latency; /* Transport Latency */
    guint16 sdu; /* Maximum SDU Size */
    guint8  phy; /* PHY */
    guint8  rtn; /* Retransmission Effort */
    guint32 presentation_delay;
} lm_transport_qos_t;

typedef enum {
    LM_TRANSPORT_ERROR = 0,
    LM_TRANSPORT_IDLE,
    LM_TRANSPORT_PENDING,
    LM_TRANSPORT_BROADCASTING,
    LM_TRANSPORT_ACTIVE
} lm_transport_state_t;

typedef enum {
    LM_TRANSPORT_PROFILE_NULL = 0,
    LM_TRANSPORT_PROFILE_A2DP_SINK,
    LM_TRANSPORT_PROFILE_BAP_SINK,
    LM_TRANSPORT_PROFILE_BAP_BCAST_SINK,
    LM_TRANSPORT_PROFILE_BAP_BCAST_SRC
} lm_transport_profile_t;

typedef enum {
    LM_TRANSPORT_AUDIO_LOCATION_NONE = -1,
    LM_TRANSPORT_AUDIO_LOCATION_MONO_LEFT = 0,
    LM_TRANSPORT_AUDIO_LOCATION_MONO_RIGHT,
    LM_TRANSPORT_AUDIO_LOCATION_STEREO
} lm_transport_audio_location_t;

typedef struct {
    lm_transport_t *transport;
} lm_transport_added_ind_t;
#define LM_TRANSPORT_ADDED_IND              (LM_MODULE_TRANSPORT | 0x0001)

#define LM_TRANSPORT_REMOVED_IND            (LM_MODULE_TRANSPORT | 0x0002)

typedef struct {
    lm_transport_t *transport;
} lm_transport_update_ind_t;
#define LM_TRANSPORT_UPDATE_IND             (LM_MODULE_TRANSPORT | 0x0003)

typedef struct {
    lm_transport_t *transport;
} lm_transport_state_change_ind_t;
#define LM_TRANSPORT_STATE_CHANGE_IND       (LM_MODULE_TRANSPORT | 0x0004)

typedef struct {
    lm_transport_t *transport;
} lm_transport_qos_update_ind_t;
#define LM_TRANSPORT_QOS_UPDATE_IND         (LM_MODULE_TRANSPORT | 0x0005)

typedef struct {
    lm_transport_t *transport;
} lm_transport_volume_change_ind_t;
#define LM_TRANSPORT_VOLUME_CHANGE_IND      (LM_MODULE_TRANSPORT | 0x0006)

const gchar *lm_transport_get_path(lm_transport_t *transport);

const gchar *lm_transport_get_uuid(lm_transport_t *transport);

lm_device_t *lm_transport_get_device(lm_transport_t *transport);

const gchar *lm_transport_get_device_path(lm_transport_t *transport);

lm_transport_state_t lm_transport_get_state(lm_transport_t *transport);

float lm_transport_get_volume_percentage(lm_transport_t *transport);

lm_status_t lm_transport_set_volume_percentage(lm_transport_t *transport, float volume_per);

lm_transport_profile_t lm_transport_get_profile(lm_transport_t *transport);

const gchar *lm_transport_get_profile_name(lm_transport_t *transport);

lm_transport_qos_t *lm_transport_get_qos(lm_transport_t *transport);
#endif //__LM_TRANSPORT_H__
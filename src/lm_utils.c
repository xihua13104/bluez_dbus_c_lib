#include "lm_utils.h"
#include "lm_log.h"
#include <glib.h>
#include <gio/gio.h>
#include <sys/time.h>
#include "math.h"
#include <ctype.h>

#define TAG "lm_utils"

#include <bluetooth/bluetooth.h>

gint lm_utils_dbus_bluez_object_path_to_hci_dev_id(const gchar *path) {
    if ((path = strstr(path, "/hci")) == NULL || path[4] == '\0')
        return -1;
    return atoi(&path[4]);
}

bdaddr_t *lm_utils_dbus_bluez_object_path_to_bdaddr(const gchar *path, bdaddr_t *addr) {

    gchar tmp[sizeof("00:00:00:00:00:00")] = { 0 };

    if ((path = strstr(path, "/dev_")) != NULL)
        strncpy(tmp, path + 5, sizeof(tmp) - 1);

    for (size_t i = 0; i < sizeof(tmp); i++)
        if (tmp[i] == '_')
            tmp[i] = ':';

    if (str2ba(tmp, addr) == -1)
        return NULL;

    return addr;
}

gchar *lm_utils_variant_sanitize_object_path(gchar *path) {

    gchar *tmp = path - 1;

    while (*(++tmp) != '\0')
        if (!(*tmp == '/' || isalnum(*tmp)))
            *tmp = '_';

    return path;
}

gboolean lm_utils_variant_validate_value(GVariant *value, const GVariantType *type,
        const gchar *name) {
    if (g_variant_is_of_type(value, type))
        return TRUE;
    lm_log_error(TAG, "Invalid variant type: %s: %s != %s", name,
            g_variant_get_type_string(value), (const gchar *)type);
    return FALSE;
}

GSource *lm_utils_io_create_watch_full(GIOChannel *channel, gint priority,
        GIOCondition cond, GIOFunc func, void *userdata, GDestroyNotify notify) {
    GSource *watch = g_io_create_watch(channel, cond);
    g_source_set_callback(watch, G_SOURCE_FUNC(func), userdata, notify);
    g_source_set_priority(watch, priority);
    g_source_attach(watch, NULL);
    return watch;
}

guint lm_utils_bdaddr_hash(const void *v) {
    const uint16_t *b = (uint16_t *)((const bdaddr_t *)v)->b;
    return (b[0] | ((uint32_t)b[1]) << 16) * b[2];
}

gboolean lm_utils_bdaddr_equal(const void *v1, const void *v2) {
    return bacmp(v1, v2) == 0;
}

void lm_utils_bytes_to_hex(gchar *dest, const guint8 *src, guint length) {
    const gchar xx[] = "0123456789abcdef";
    int n = (int) length;
    while (--n >= 0) dest[n] = xx[(src[n >> 1] >> ((1 - (n & 1)) << 2)) & 0xF];
}

GString *lm_utils_g_byte_array_as_hex(const GByteArray *byteArray) {
    guint hexLength = byteArray->len * 2;
    GString *result = g_string_sized_new( hexLength + 1);
    lm_utils_bytes_to_hex(result->str, byteArray->data, hexLength);
    result->str[hexLength] = 0;
    result->len = (gsize) hexLength;
    return result;
}

GList *lm_utils_g_variant_string_array_to_list(GVariant *value) {
    g_assert(value != NULL);
    g_assert(g_str_equal(g_variant_get_type_string(value), "as"));

    GList *list = NULL;
    gchar *data;
    GVariantIter iter;

    g_variant_iter_init(&iter, value);
    while (g_variant_iter_loop(&iter, "s", &data)) {
        list = g_list_append(list, g_strdup(data));
    }
    return list;
}

gchar *lm_utils_date_time_format_iso8601(GDateTime *datetime) {
    GString *outstr = NULL;
    gchar *main_date = NULL;
    gint64 offset;
    gchar *format = "%Y-%m-%dT%H:%M:%S";

    /* Main date and time. */
    main_date = g_date_time_format(datetime, format);
    outstr = g_string_new(main_date);
    g_free(main_date);

    /* Timezone. Format it as `%:::z` unless the offset is zero, in which case
     * we can simply use `Z`. */
    offset = g_date_time_get_utc_offset(datetime);

    if (offset == 0) {
        g_string_append_c (outstr, 'Z');
    } else {
        gchar *time_zone = g_date_time_format(datetime, "%:z");
        g_string_append(outstr, time_zone);
        g_free(time_zone);
    }

    return g_string_free(outstr, FALSE);
}

gboolean lm_utils_is_lowercase(const gchar *str) {
    for (int i = 0; str[i] != '\0'; i++) {
        if (g_ascii_isalpha(str[i]) && g_ascii_isupper(str[i])) return FALSE;
    }

    return TRUE;
}

gboolean lm_utils_is_valid_uuid(const gchar *uuid) {
    if (uuid == NULL) {
        g_critical("uuid is NULL");
        return FALSE;
    }

    if (!g_uuid_string_is_valid(uuid)) {
        g_critical("%s is not a valid UUID", uuid);
        return FALSE;
    }

    if (!lm_utils_is_lowercase(uuid)) {
        g_critical("%s is not entirely lowercase", uuid);
        return FALSE;
    }

    return TRUE;
}

gchar* lm_utils_replace_char(gchar* str, gchar find, gchar replace){
    gchar *current_pos = strchr(str,find);
    while (current_pos) {
        *current_pos = replace;
        current_pos = strchr(current_pos,find);
    }
    return str;
}

gchar *lm_utils_path_to_address(const gchar *path) {
    gchar *address = g_strdup_printf("%s", path+(strlen(path)-17));
    return lm_utils_replace_char(address, '_', ':');
}

GByteArray *lm_utils_g_variant_get_byte_array(GVariant *variant) {
    g_assert(variant != NULL);
    g_assert(g_str_equal(g_variant_get_type_string(variant), "ay"));

    size_t data_length = 0;
    guint8 *data = (guint8 *) g_variant_get_fixed_array(variant, &data_length, sizeof(guint8));
    return g_byte_array_new_take(data, data_length);
}

gchar* lm_utils_random_string(gsize length) {
    GString *string = g_string_sized_new(length);

    // Seed rand with current time in nanoseconds
    struct timespec nanos;
    clock_gettime(CLOCK_MONOTONIC, &nanos);
    srand((unsigned int) nanos.tv_nsec);

    const gchar charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (size_t n = 0; n < length; n++) {
        int key = rand() % (int)(sizeof(charset) - 1);
        g_string_append_c(string, charset[key]);
    }

    return g_string_free(string, FALSE);
}

void lm_utils_byte_array_free(GByteArray *byteArray)
{
    g_byte_array_free(byteArray, TRUE);
}
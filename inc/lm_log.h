#ifndef __LM_LOG_H__
#define __LM_LOG_H__

#include <glib.h>

typedef enum {
    LM_LOG_DEBUG = 0,
    LM_LOG_INFO = 1,
    LM_LOG_WARN = 2,
    LM_LOG_ERROR = 3
} lm_log_level_t;


#define lm_log_debug(tag, format, ...) lm_log_at_level(LM_LOG_DEBUG, tag, format, ##__VA_ARGS__)
#define lm_log_info(tag, format, ...)  lm_log_at_level(LM_LOG_INFO, tag, format, ##__VA_ARGS__)
#define lm_log_warn(tag, format, ...)  lm_log_at_level(LM_LOG_WARN, tag, format,  ##__VA_ARGS__)
#define lm_log_error(tag, format, ...) lm_log_at_level(LM_LOG_ERROR, tag, format, ##__VA_ARGS__)

void lm_log_at_level(lm_log_level_t level, const char* tag, const char *format, ...);

void lm_log_set_level(lm_log_level_t level);

void lm_log_set_filename(const char* filename, unsigned long max_size, unsigned int max_files);

typedef void (*lm_log_event_callback_t)(lm_log_level_t level, const char *tag, const char *message);

void lm_log_set_handler(lm_log_event_callback_t callback);

void lm_log_enabled(gboolean enabled);

#endif //__LM_LOG_H__

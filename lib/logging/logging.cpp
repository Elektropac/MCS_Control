#include "logging.h"
#include <stdarg.h>
#include <stdio.h>

// ------------------------------------------
// Config
// ------------------------------------------
#define LOG_BUFFER_SIZE  2048   // ring buffer for recent messages
#define LOG_LINE_MAX     128    // max length per message

// ------------------------------------------
// State
// ------------------------------------------
static LogLevel s_level = LOG_NORMAL;
static char s_buffer[LOG_BUFFER_SIZE];
static size_t s_write_pos = 0;
static bool s_wrapped = false;

// ------------------------------------------
// Internal
// ------------------------------------------
static const char* level_prefix(LogLevel level) {
    switch (level) {
        case LOG_ERROR:   return "[ERR] ";
        case LOG_NORMAL:  return "[INF] ";
        case LOG_VERBOSE: return "[VRB] ";
        case LOG_DEBUG:   return "[DBG] ";
        default:          return "";
    }
}

static void log_write(LogLevel level, const char* fmt, va_list args) {
    if (level > s_level) return;
    if (s_level == LOG_SILENT) return;

    char line[LOG_LINE_MAX];
    const char* prefix = level_prefix(level);
    int prefix_len = strlen(prefix);
    memcpy(line, prefix, prefix_len);

    int msg_len = vsnprintf(line + prefix_len, LOG_LINE_MAX - prefix_len - 1, fmt, args);
    int total_len = prefix_len + msg_len;
    if (total_len >= LOG_LINE_MAX - 2) total_len = LOG_LINE_MAX - 3;
    line[total_len] = '\r';
    line[total_len + 1] = '\n';
    line[total_len + 2] = '\0';
    total_len += 2;

    // Output to Serial
    Serial.print(line);

    // Store in ring buffer
    for (int i = 0; i < total_len; i++) {
        s_buffer[s_write_pos] = line[i];
        s_write_pos = (s_write_pos + 1) % LOG_BUFFER_SIZE;
        if (s_write_pos == 0) s_wrapped = true;
    }
}

// ------------------------------------------
// Public API
// ------------------------------------------

void log_init() {
    s_level = LOG_NORMAL;
    s_write_pos = 0;
    s_wrapped = false;
    memset(s_buffer, 0, LOG_BUFFER_SIZE);
}

void log_set_level(LogLevel level) {
    s_level = level;
}

LogLevel log_get_level() {
    return s_level;
}

void log_error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write(LOG_ERROR, fmt, args);
    va_end(args);
}

void log_info(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write(LOG_NORMAL, fmt, args);
    va_end(args);
}

void log_verbose(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write(LOG_VERBOSE, fmt, args);
    va_end(args);
}

void log_debug(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write(LOG_DEBUG, fmt, args);
    va_end(args);
}

String log_recent() {
    String result;
    if (s_wrapped) {
        for (size_t i = s_write_pos; i < LOG_BUFFER_SIZE; i++) {
            if (s_buffer[i]) result += s_buffer[i];
        }
    }
    for (size_t i = 0; i < s_write_pos; i++) {
        if (s_buffer[i]) result += s_buffer[i];
    }
    return result;
}

void log_clear() {
    s_write_pos = 0;
    s_wrapped = false;
    memset(s_buffer, 0, LOG_BUFFER_SIZE);
}

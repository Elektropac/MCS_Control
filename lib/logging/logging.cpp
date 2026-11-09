#include "logging.h"
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <LittleFS.h>

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
    // Emit only printable ASCII + CR/LF so the result is always valid UTF-8
    // (and safe inside JSON). The ring buffer can wrap mid-line or hold stray
    // bytes; those are skipped rather than corrupting the reply.
    auto emit = [&](char ch) {
        if (ch == '\r' || ch == '\n' || (ch >= 0x20 && ch < 0x7F)) {
            result += ch;
        }
    };
    if (s_wrapped) {
        for (size_t i = s_write_pos; i < LOG_BUFFER_SIZE; i++) {
            if (s_buffer[i]) emit(s_buffer[i]);
        }
    }
    for (size_t i = 0; i < s_write_pos; i++) {
        if (s_buffer[i]) emit(s_buffer[i]);
    }
    return result;
}

void log_clear() {
    s_write_pos = 0;
    s_wrapped = false;
    memset(s_buffer, 0, LOG_BUFFER_SIZE);
}

// ------------------------------------------
// FLASH EVENT LOG (persistent, low-rate)
// ------------------------------------------
#define EVENT_LOG_PATH      "/events.log"
#define EVENT_LOG_MAX_BYTES  (256 * 1024)   // cap; rolls over keeping newer half
#define EVENT_LINE_MAX       160

void event_log_init() {
    // Nothing to pre-create; file is created on first append. We just make sure
    // the filesystem is available (mounted elsewhere in file_system::init()).
}

// ---- Clock (no RTC): client-supplied UTC, tracked relative to millis() ----
static uint32_t s_utc_at_boot = 0;   // UTC epoch that corresponds to millis()=0
static bool     s_clock_set   = false;
static int32_t  s_tz_offset   = 0;   // local = UTC + offset (seconds)
static bool     s_tz_set      = false;

void clock_set_utc(uint32_t epoch_seconds) {
    if (epoch_seconds < 1700000000UL) return;   // sanity: after ~2023-11
    s_utc_at_boot = epoch_seconds - (millis() / 1000);
    s_clock_set = true;
}

void clock_set_tz_offset(int32_t seconds) {
    // plausible range -14h..+14h
    if (seconds < -50400 || seconds > 50400) return;
    s_tz_offset = seconds;
    s_tz_set = true;
}

bool clock_has_time() { return s_clock_set; }
bool clock_has_tz() { return s_tz_set; }

uint32_t clock_now_utc() {
    if (!s_clock_set) return 0;
    return s_utc_at_boot + (millis() / 1000);
}

static String fmt_time(uint32_t epoch, bool with_z) {
    time_t tt = (time_t)epoch;
    struct tm g;
    gmtime_r(&tt, &g);   // we already applied any offset into epoch
    char buf[24];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d%s",
             g.tm_year + 1900, g.tm_mon + 1, g.tm_mday,
             g.tm_hour, g.tm_min, g.tm_sec, with_z ? "Z" : "");
    return String(buf);
}

String clock_now_string() {
    if (!s_clock_set) return String("");
    return fmt_time(clock_now_utc(), true);
}

String clock_local_string() {
    if (!s_clock_set) return String("");
    if (!s_tz_set) return fmt_time(clock_now_utc(), true);   // fall back to UTC
    return fmt_time((uint32_t)((int32_t)clock_now_utc() + s_tz_offset), false);
}

// Format a timestamp prefix: real UTC if known, else "+seconds since boot".
static void timestamp_prefix(char* buf, size_t n) {
    if (s_clock_set) {
        uint32_t t = clock_now_utc();
        time_t tt = (time_t)t;
        struct tm g;
        gmtime_r(&tt, &g);
        snprintf(buf, n, "%04d-%02d-%02d %02d:%02d:%02dZ",
                 g.tm_year + 1900, g.tm_mon + 1, g.tm_mday,
                 g.tm_hour, g.tm_min, g.tm_sec);
    } else {
        uint32_t ms = millis();
        snprintf(buf, n, "+%lu.%03lus (no time)",
                 (unsigned long)(ms / 1000), (unsigned long)(ms % 1000));
    }
}

static void event_log_rollover_if_needed() {
    File f = LittleFS.open(EVENT_LOG_PATH, "r");
    if (!f) return;
    size_t sz = f.size();
    if (sz < EVENT_LOG_MAX_BYTES) { f.close(); return; }
    // Keep the newer half: read from the midpoint, rewrite the file.
    size_t keepFrom = sz / 2;
    f.seek(keepFrom);
    String tail = f.readString();
    f.close();
    // Drop a partial first line so we start clean.
    int nl = tail.indexOf('\n');
    if (nl >= 0) tail = tail.substring(nl + 1);
    File w = LittleFS.open(EVENT_LOG_PATH, "w");
    if (w) { w.print(tail); w.close(); }
}

void event_log_add(const char* fmt, ...) {
    char msg[EVENT_LINE_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, EVENT_LINE_MAX, fmt, args);
    va_end(args);

    // Timestamp: real UTC if a client has set the clock, else "+since boot".
    char ts[48];
    timestamp_prefix(ts, sizeof(ts));
    char line[EVENT_LINE_MAX + 64];
    snprintf(line, sizeof(line), "[%s] %s\r\n", ts, msg);

    event_log_rollover_if_needed();
    File f = LittleFS.open(EVENT_LOG_PATH, "a");
    if (f) { f.print(line); f.close(); }
    // Also mirror to serial so it shows live too.
    Serial.print(line);
}

String event_log_read() {
    File f = LittleFS.open(EVENT_LOG_PATH, "r");
    if (!f) return String("");
    String s = f.readString();
    f.close();
    return s;
}

void event_log_clear() {
    LittleFS.remove(EVENT_LOG_PATH);
}

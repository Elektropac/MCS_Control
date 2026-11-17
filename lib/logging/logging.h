#pragma once
// =======================================================
// LOGGING — simple leveled log system
// =======================================================
//
// Log levels:
//   LOG_SILENT  — no output
//   LOG_ERROR   — errors only
//   LOG_NORMAL  — important events
//   LOG_VERBOSE — detailed info
//   LOG_DEBUG   — everything (noisy)
//
// Usage:
//   log_init();
//   log_info("System started");
//   log_debug("ADC read: %d mV", value);
//   log_error("Chip 0x21 not found");
//
// Output goes to Serial. A ring buffer keeps recent
// messages for web/display access.
//
// =======================================================
#include <Arduino.h>

enum LogLevel : uint8_t {
    LOG_SILENT  = 0,
    LOG_ERROR   = 1,
    LOG_NORMAL  = 2,
    LOG_VERBOSE = 3,
    LOG_DEBUG   = 4,
};

// Initialize logging (call early in setup)
void log_init();

// Set/get current log level
void log_set_level(LogLevel level);
LogLevel log_get_level();

// Log functions. Marked printf-style so the compiler checks the arguments
// (e.g. passing an Arduino String to %s is a warning, not silent garbage).
#define LOG_PRINTF_FMT __attribute__((format(printf, 1, 2)))
void log_error(const char* fmt, ...) LOG_PRINTF_FMT;
void log_info(const char* fmt, ...) LOG_PRINTF_FMT;
void log_verbose(const char* fmt, ...) LOG_PRINTF_FMT;
void log_debug(const char* fmt, ...) LOG_PRINTF_FMT;

// Access recent log history (ring buffer)
String log_recent();
void log_clear();

// -------------------------------------------------------
// FLASH EVENT LOG (persistent) — only important, low-rate
// events (alarms, boot, errors, transactions). Survives
// reboot. Stored in LittleFS with a size cap + rollover so
// flash wear stays low (events are rare, unlike the RAM log).
// -------------------------------------------------------
void event_log_init();
// Append one event line (a timestamp is prepended).
void event_log_add(const char* fmt, ...);
// Return the whole flash event log (newest handling done app-side).
String event_log_read();
void event_log_clear();

// -------------------------------------------------------
// CLOCK (no RTC) — a connecting client sends the current
// UTC time; we remember it relative to millis() so we can
// stamp events with real UTC until reboot. If never set,
// events fall back to a "+seconds since boot" stamp.
// -------------------------------------------------------
void clock_set_utc(uint32_t epoch_seconds);   // called on connect
void clock_set_tz_offset(int32_t seconds);    // local = UTC + offset (0 if unknown)
bool clock_has_time();
bool clock_has_tz();
uint32_t clock_now_utc();                      // 0 if unknown
// Formatted "YYYY-MM-DD HH:MM:SSZ" (UTC) if the clock is set, else "".
String clock_now_string();
// Local time "YYYY-MM-DD HH:MM:SS" if tz known, else falls back to UTC string.
String clock_local_string();

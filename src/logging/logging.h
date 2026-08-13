#pragma once
// =======================================================
// LOGGING — simple leveled log system
// =======================================================
//
// Log levels:
//   LOG_SILENT  — no output
//   LOG_NORMAL  — important events only
//   LOG_VERBOSE — detailed info
//   LOG_DEBUG   — everything (noisy)
//
// Usage:
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

// Log functions
void log_error(const char* fmt, ...);
void log_info(const char* fmt, ...);
void log_verbose(const char* fmt, ...);
void log_debug(const char* fmt, ...);

// Access recent log history (ring buffer)
String log_recent();
void log_clear();

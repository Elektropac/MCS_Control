#pragma once
// =======================================================
// Test API — interactive hardware tests for the test page
// =======================================================
// Maps test_* subjects from web/test.html to the current firmware's
// hardware drivers. Kept small and safe: buzzer sounds, relay toggle,
// channel supply voltage. Returns a small JSON result per call.
// =======================================================
#include <ArduinoJson.h>

// Handle a test_* subject. Returns "" if the subject is not a test command.
String test_api_run(const String& subject, JsonObject data);

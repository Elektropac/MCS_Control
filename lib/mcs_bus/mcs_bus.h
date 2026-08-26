#pragma once
// =======================================================
// MCS Message Bus — internal pub/sub for apps
// =======================================================
// JSON messages between tasks via FreeRTOS queues.
// Each subscriber gets its own inbox (queue).
// Thread-safe: any task can publish at any time.
// =======================================================

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#define MCS_BUS_MAX_SUBSCRIBERS  16
#define MCS_BUS_INBOX_SIZE       8
#define MCS_BUS_MAX_TOPIC_LEN    48
#define MCS_BUS_MAX_PAYLOAD_LEN  512

struct McsMessage {
    char topic[MCS_BUS_MAX_TOPIC_LEN];
    char payload[MCS_BUS_MAX_PAYLOAD_LEN];  // serialized JSON
};

namespace mcs_bus {

    // Initialize the message bus. Call once in setup().
    void init();

    // Subscribe to a topic. Callback is called in YOUR task context
    // when you call process_inbox(). Use '#' as wildcard.
    // Returns a subscriber handle (for unsubscribe).
    int subscribe(const char* topic, void(*callback)(const char* topic, const JsonObject& data));

    // Unsubscribe
    void unsubscribe(int handle);

    // Publish a message. All matching subscribers get a copy in their inbox.
    // Safe to call from any task.
    void publish(const char* topic, const JsonDocument& data);

    // Process your inbox — calls your callbacks for queued messages.
    // Call this once per loop iteration in your task.
    void process_inbox();
}

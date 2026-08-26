#include "mcs_bus.h"
#include <freertos/semphr.h>

namespace mcs_bus {

    struct Subscriber {
        bool active;
        char topic[MCS_BUS_MAX_TOPIC_LEN];
        void(*callback)(const char* topic, const JsonObject& data);
        QueueHandle_t inbox;
        TaskHandle_t owner_task;
    };

    static Subscriber s_subs[MCS_BUS_MAX_SUBSCRIBERS];
    static SemaphoreHandle_t s_mutex = nullptr;

    // Simple wildcard matching: "pump/#" matches "pump/1/done"
    static bool topic_matches(const char* pattern, const char* topic) {
        while (*pattern && *topic) {
            if (*pattern == '#') return true;  // wildcard matches rest
            if (*pattern != *topic) return false;
            pattern++;
            topic++;
        }
        return (*pattern == '\0' && *topic == '\0');
    }

    void init() {
        s_mutex = xSemaphoreCreateMutex();
        for (int i = 0; i < MCS_BUS_MAX_SUBSCRIBERS; i++) {
            s_subs[i].active = false;
            s_subs[i].inbox = nullptr;
        }
    }

    int subscribe(const char* topic, void(*callback)(const char* topic, const JsonObject& data)) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);

        int slot = -1;
        for (int i = 0; i < MCS_BUS_MAX_SUBSCRIBERS; i++) {
            if (!s_subs[i].active) {
                slot = i;
                break;
            }
        }

        if (slot >= 0) {
            s_subs[slot].active = true;
            strncpy(s_subs[slot].topic, topic, MCS_BUS_MAX_TOPIC_LEN - 1);
            s_subs[slot].topic[MCS_BUS_MAX_TOPIC_LEN - 1] = '\0';
            s_subs[slot].callback = callback;
            s_subs[slot].owner_task = xTaskGetCurrentTaskHandle();

            if (s_subs[slot].inbox == nullptr) {
                s_subs[slot].inbox = xQueueCreate(MCS_BUS_INBOX_SIZE, sizeof(McsMessage));
            } else {
                xQueueReset(s_subs[slot].inbox);
            }
        }

        xSemaphoreGive(s_mutex);
        return slot;
    }

    void unsubscribe(int handle) {
        if (handle < 0 || handle >= MCS_BUS_MAX_SUBSCRIBERS) return;
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_subs[handle].active = false;
        xSemaphoreGive(s_mutex);
    }

    void publish(const char* topic, const JsonDocument& data) {
        McsMessage msg;
        strncpy(msg.topic, topic, MCS_BUS_MAX_TOPIC_LEN - 1);
        msg.topic[MCS_BUS_MAX_TOPIC_LEN - 1] = '\0';
        serializeJson(data, msg.payload, MCS_BUS_MAX_PAYLOAD_LEN);

        // Deliver to all matching subscribers
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        for (int i = 0; i < MCS_BUS_MAX_SUBSCRIBERS; i++) {
            if (s_subs[i].active && topic_matches(s_subs[i].topic, topic)) {
                // Non-blocking enqueue — if inbox is full, message is dropped
                xQueueSend(s_subs[i].inbox, &msg, 0);
            }
        }
        xSemaphoreGive(s_mutex);
    }

    void process_inbox() {
        TaskHandle_t me = xTaskGetCurrentTaskHandle();
        McsMessage msg;

        for (int i = 0; i < MCS_BUS_MAX_SUBSCRIBERS; i++) {
            if (s_subs[i].active && s_subs[i].owner_task == me) {
                while (xQueueReceive(s_subs[i].inbox, &msg, 0) == pdTRUE) {
                    // Parse JSON and call the callback
                    JsonDocument doc;
                    DeserializationError err = deserializeJson(doc, msg.payload);
                    if (!err) {
                        s_subs[i].callback(msg.topic, doc.as<JsonObject>());
                    }
                }
            }
        }
    }
}

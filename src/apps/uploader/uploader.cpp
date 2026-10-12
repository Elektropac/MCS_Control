#include "uploader.h"

#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "logging.h"
#include "web_socket.h"

namespace uploader
{
    // ---- Tunables --------------------------------------------------------
    static const uint8_t  PROBE_QUEUE_LEN = 16;
    static const uint8_t  PUMP_QUEUE_LEN  = 16;
    static const uint32_t UPLOAD_INTERVAL_MS = 1000;  // drain + send once/sec

    // ---- Queue handles ---------------------------------------------------
    static QueueHandle_t s_probe_queue = nullptr;
    static QueueHandle_t s_pump_queue  = nullptr;
    static TaskHandle_t  s_task_handle = nullptr;

    void init()
    {
        s_probe_queue = xQueueCreate(PROBE_QUEUE_LEN, sizeof(ProbeSample));
        s_pump_queue  = xQueueCreate(PUMP_QUEUE_LEN,  sizeof(PumpSample));

        if (!s_probe_queue || !s_pump_queue) {
            log_error("[uploader] failed to create queues");
        }
    }

    bool queue_probe(const ProbeSample& sample)
    {
        if (!s_probe_queue) return false;
        // timeout 0: never block a producer; drop if the queue is full.
        bool ok = xQueueSend(s_probe_queue, &sample, 0) == pdPASS;
        if (ok) {
            log_info("[uploader] queued probe '%s': mm=%d liters=%d state=%s",
                     sample.id, (int)sample.mm, (int)sample.liters, sample.state);
        } else {
            log_error("[uploader] probe queue FULL, dropped '%s'", sample.id);
        }
        return ok;
    }

    bool queue_pump(const PumpSample& sample)
    {
        if (!s_pump_queue) return false;
        bool ok = xQueueSend(s_pump_queue, &sample, 0) == pdPASS;
        if (ok) {
            log_info("[uploader] queued pump '%s': litres=%.2f total=%.2f state=%s",
                     sample.id, sample.litres, sample.total_litres, sample.state);
        } else {
            log_error("[uploader] pump queue FULL, dropped '%s'", sample.id);
        }
        return ok;
    }

    // ---- Task ------------------------------------------------------------
    static void uploader_task(void* param)
    {
        (void)param;

        ProbeSample probe;
        PumpSample  pump;

        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(UPLOAD_INTERVAL_MS));

            // Only build/send if the transport is up — otherwise just drain
            // so stale items don't pile up and we always send fresh data.
            bool connected = web_socket::is_connected;

            JsonDocument doc;
            doc["subject"] = "device_data";
            JsonObject data = doc["data"].to<JsonObject>();

            bool has_data = false;

            // --- Drain probe queue ---
            JsonArray probes = data["probes"].to<JsonArray>();
            while (xQueueReceive(s_probe_queue, &probe, 0) == pdPASS) {
                has_data = true;
                JsonObject o = probes.add<JsonObject>();
                o["id"]     = probe.id;
                o["media"]  = probe.media;
                o["ma"]     = serialized(String(probe.ma, 2));
                o["mm"]     = probe.mm;
                o["liters"] = probe.liters;
                o["pct"]    = serialized(String(probe.pct, 1));
                o["state"]  = probe.state;
                o["ts"]     = probe.timestamp;
            }

            // --- Drain pump queue ---
            JsonArray pumps = data["pumps"].to<JsonArray>();
            while (xQueueReceive(s_pump_queue, &pump, 0) == pdPASS) {
                has_data = true;
                JsonObject o = pumps.add<JsonObject>();
                o["id"]           = pump.id;
                o["litres"]       = serialized(String(pump.litres, 2));
                o["total_litres"] = serialized(String(pump.total_litres, 2));
                o["pulses"]       = pump.pulses;
                o["state"]        = pump.state;
                o["ts"]           = pump.timestamp;
            }

            if (!has_data) continue;        // nothing to send this round

            if (!connected) {
                log_info("[uploader] transport down, dropped %u probes + %u pumps",
                         probes.size(), pumps.size());
                continue;
            }

            String payload;
            serializeJson(doc, payload);
            web_socket::sendMessage(payload);
            log_info("[uploader] sent %u probes + %u pumps (%u bytes)",
                     probes.size(), pumps.size(), payload.length());
        }
    }

    void start_task()
    {
        xTaskCreate(uploader_task, "uploader", 6144, nullptr, 1, &s_task_handle);
    }
}

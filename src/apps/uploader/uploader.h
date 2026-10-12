#pragma once
// =======================================================
// uploader — queue-based data upload task
// =======================================================
// Follows the pattern in docs/esp32_freertos_uploader_example.md:
// producers (cloudgauge probes, pump/fueling) push fixed-size RAW structs
// into FreeRTOS queues. A single uploaderTask wakes periodically, drains all
// queues, batches everything into ONE JSON payload, and hands it to the
// transport via web_socket::sendMessage().
//
// Why raw structs (not finished JSON strings) in the queue?
//   - A queue item is a fixed-size POD copied by value — no heap, no String
//     ownership crossing task boundaries. RAM use is bounded and predictable.
//   - The uploader owns JSON building in one place, so the wire format can
//     evolve without touching the producers.
// =======================================================
#include <Arduino.h>

namespace uploader
{
    // ---- Producer-facing data structures (raw values only) --------------

    // One probe measurement snapshot.
    struct ProbeSample
    {
        char     id[16];      // probe id from config (e.g. "probe_1")
        char     media[16];   // liquid name ("Diesel", ...)
        float    ma;          // smoothed loop current
        int32_t  mm;          // level in mm
        int32_t  liters;      // computed liters (0 if no LUT)
        float    pct;         // fill percent
        char     state[16];   // "normal" | "low_level" | "overfill" | "fault"
        uint32_t timestamp;   // millis() when sampled
    };

    // One fueling/pump snapshot (sent e.g. at transaction end or periodically).
    struct PumpSample
    {
        char     id[16];      // pump id from config (e.g. "fueling_1")
        float    litres;      // litres for the current/last transaction
        float    total_litres;// lifetime totaliser
        uint32_t pulses;      // raw pulse count
        char     state[16];   // engine state ("idle", "fueling", ...)
        uint32_t timestamp;   // millis()
    };

    // ---- Lifecycle -------------------------------------------------------

    // Create the queues. Call once in setup() BEFORE any producer runs.
    void init();

    // Start the uploader FreeRTOS task.
    void start_task();

    // ---- Producer API (non-blocking; drops if the queue is full) ---------

    // Returns true if the item was queued.
    bool queue_probe(const ProbeSample& sample);
    bool queue_pump(const PumpSample& sample);
}

#pragma once
#include "Arduino.h"
#include "freertos/queue.h"

namespace queues
{
    extern QueueSetHandle_t queues_set;

    extern int probe_queue_length;
    extern QueueHandle_t probe_queue;
    struct ProbeSample
    {
        String id;
        String type;        // "mcs_level" or "mcs_level_temp"
        // mcs_level: raw current signal
        float ma;           // raw signal in milliamperes (mcs_level)
        // mcs_level_temp: raw voltage signals
        float level_mv;     // raw level signal in millivolts (mcs_level_temp)
        float temp_mv;      // raw temp signal in millivolts (mcs_level_temp)
        // Converted values (both types)
        float mm;           // level in millimeters
        float liters;       // level in liters (via LUT, 0 if no LUT)
        float temp_c;       // temperature in °C (mcs_level_temp only, 0 otherwise)
        float temp_f;       // temperature in °F (mcs_level_temp only, 0 otherwise)
    };


    extern int pump_queue_length;
    extern QueueHandle_t pump_queue;
    struct PumpSample
    {
        String id;
    };

    void init();
    void send_probe_sample(const ProbeSample &data);
    void send_pump_sample(const PumpSample &data);

} // namespace queues
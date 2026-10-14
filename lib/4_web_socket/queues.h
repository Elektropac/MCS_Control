#pragma once
#include "Arduino.h"
#include "freertos/queue.h"


namespace queue_types {
    struct Probe_level
    {
        String id;
        float ma;           // raw signal in milliamperes (mcs_level)
        float mm;           // level in millimeters
        float liters;       // level in liters (via LUT, 0 if no LUT)
    };

    struct Probe_level_temp
    {
        String id;
        float mm;           // level in millimeters
        float liters;       // level in liters (via LUT, 0 if no LUT)
        float level_mv;      // raw level signal in millivolts (mcs_level_temp)
        float temp_mv;      // raw temp signal in millivolts (mcs_level_temp)
        float temp_c;       // temperature in °C (mcs_level_temp only, 0 otherwise)
        float temp_f;       // temperature in °F (mcs_level_temp only, 0 otherwise)
    };

    struct PumpSample
    {
        String id;
    };
}

namespace queue_sizes {
    extern int probe_level;
    extern int probe_level_temp;
    extern int pump;
}

namespace queues
{
    extern QueueSetHandle_t queues_set;
    extern QueueHandle_t probe_level;
    extern QueueHandle_t probe_level_temp;
    extern QueueHandle_t pump;
    

    void init();
    void send_probe_level(const queue_types::Probe_level &data);
    void send_probe_level_temp(const queue_types::Probe_level_temp &data);
    void send_pump_sample(const queue_types::PumpSample &data);

} // namespace queues
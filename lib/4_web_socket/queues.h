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
        float ma; // measurement in milliamperes
        float mm; // measurement in millimeters
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
#include "queues.h"

namespace queues
{
    QueueSetHandle_t queues_set = nullptr;

    int probe_queue_length = 10;
    QueueHandle_t probe_queue = nullptr;

    int pump_queue_length = 10;
    QueueHandle_t pump_queue = nullptr;

    void init()
    {
        probe_queue = xQueueCreate(probe_queue_length, sizeof(ProbeSample));
        pump_queue = xQueueCreate(pump_queue_length, sizeof(PumpSample));

        int queues_set_length = probe_queue_length + pump_queue_length;
        queues_set = xQueueCreateSet(queues_set_length);

        xQueueAddToSet(probe_queue, queues_set);
        xQueueAddToSet(pump_queue, queues_set);
    }

    void send_probe_sample(const ProbeSample &data)
    {
        xQueueSend(probe_queue, &data, portMAX_DELAY);
    }

    void send_pump_sample(const PumpSample &data)
    {
        xQueueSend(pump_queue, &data, portMAX_DELAY);
    }
} // namespace queues
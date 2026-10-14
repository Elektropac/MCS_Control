#include "queues.h"

namespace queue_sizes {
    int probe_level = 8;
    int probe_level_temp = 4;
    int pump = 2;
}

namespace queues
{
    QueueSetHandle_t queues_set = nullptr;

    QueueHandle_t probe_level = nullptr;

    QueueHandle_t probe_level_temp = nullptr;

    QueueHandle_t pump = nullptr;

    void init()
    {
        probe_level = xQueueCreate(queue_sizes::probe_level, sizeof(queue_types::Probe_level));
        probe_level_temp = xQueueCreate(queue_sizes::probe_level_temp, sizeof(queue_types::Probe_level_temp));
        pump = xQueueCreate(queue_sizes::pump, sizeof(queue_types::PumpSample));

        int queues_set_length = queue_sizes::probe_level + queue_sizes::probe_level_temp + queue_sizes::pump;
        queues_set = xQueueCreateSet(queues_set_length);

        xQueueAddToSet(probe_level, queues_set);
        xQueueAddToSet(probe_level_temp, queues_set);
        xQueueAddToSet(pump, queues_set);
    }

    void send_probe_level(const queue_types::Probe_level &data)
    {
        xQueueSend(probe_level, &data, portMAX_DELAY);
    }

    void send_probe_level_temp(const queue_types::Probe_level_temp &data)
    {
        xQueueSend(probe_level_temp, &data, portMAX_DELAY);
    }

    void send_pump_sample(const queue_types::PumpSample &data)
    {
        xQueueSend(pump, &data, portMAX_DELAY);
    }
} // namespace queues
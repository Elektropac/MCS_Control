# ESP32 FreeRTOS Uploader Task Example

This example shows an ESP32 design where multiple FreeRTOS queues are used for different data types.

The uploader task wakes up periodically, drains all available data from each queue, builds an upload payload, and sends it to an API.

## Architecture

```text
sensorQueue ──┐
eventQueue ───┼──> uploaderTask
statusQueue ──┘        │
                       ▼
                 Build payload
                       │
                       ▼
                    HTTP API
```

## Data Structures

```cpp
struct SensorReading {
    uint8_t sensorId;
    float value;
    uint32_t timestamp;
};

struct EventData {
    uint16_t eventId;
    uint32_t timestamp;
};

struct StatusData {
    bool online;
    uint8_t batteryPercent;
    uint32_t timestamp;
};
```

## Queue Handles

```cpp
QueueHandle_t sensorQueue;
QueueHandle_t eventQueue;
QueueHandle_t statusQueue;
```

## Create the Queues

```cpp
void createQueues()
{
    sensorQueue = xQueueCreate(32, sizeof(SensorReading));
    eventQueue  = xQueueCreate(16, sizeof(EventData));
    statusQueue = xQueueCreate(8, sizeof(StatusData));
}
```

The first parameter is how many items the queue can hold.

For example:

```cpp
xQueueCreate(32, sizeof(SensorReading));
```

means the queue can store up to 32 `SensorReading` items.

---

## Sensor Task

A sensor task can read multiple sensors and push each reading into the same sensor queue.

```cpp
void sensorTask(void *parameter)
{
    while (true) {

        SensorReading reading;

        // Temperature sensor
        reading.sensorId = 1;
        reading.value = readTemperature();
        reading.timestamp = millis();

        xQueueSend(sensorQueue, &reading, portMAX_DELAY);

        // Humidity sensor
        reading.sensorId = 2;
        reading.value = readHumidity();
        reading.timestamp = millis();

        xQueueSend(sensorQueue, &reading, portMAX_DELAY);

        // Pressure sensor
        reading.sensorId = 3;
        reading.value = readPressure();
        reading.timestamp = millis();

        xQueueSend(sensorQueue, &reading, portMAX_DELAY);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

The sensor ID tells the uploader which sensor produced the value.

---

## Event Producer Example

```cpp
void sendEvent(uint16_t eventId)
{
    EventData event;

    event.eventId = eventId;
    event.timestamp = millis();

    xQueueSend(eventQueue, &event, 0);
}
```

Using `0` means:

> Try to put the item in the queue immediately. Do not wait if the queue is full.

---

## Status Producer Example

```cpp
void sendStatus()
{
    StatusData status;

    status.online = true;
    status.batteryPercent = 85;
    status.timestamp = millis();

    xQueueSend(statusQueue, &status, 0);
}
```

---

# Uploader Task

The uploader checks each queue and removes all currently available items.

```cpp
void uploaderTask(void *parameter)
{
    SensorReading sensor;
    EventData event;
    StatusData status;

    while (true) {

        bool hasData = false;

        // Start building JSON or another payload format here.

        while (xQueueReceive(sensorQueue, &sensor, 0) == pdPASS) {

            hasData = true;

            // Example:
            //
            // addSensorToPayload(
            //     sensor.sensorId,
            //     sensor.value,
            //     sensor.timestamp
            // );
        }

        while (xQueueReceive(eventQueue, &event, 0) == pdPASS) {

            hasData = true;

            // addEventToPayload(
            //     event.eventId,
            //     event.timestamp
            // );
        }

        while (xQueueReceive(statusQueue, &status, 0) == pdPASS) {

            hasData = true;

            // addStatusToPayload(
            //     status.online,
            //     status.batteryPercent,
            //     status.timestamp
            // );
        }

        if (hasData) {

            // uploadPayload();
        }

        // Upload/check queues once per second.
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

## Why `xQueueReceive(..., 0)`?

```cpp
xQueueReceive(sensorQueue, &sensor, 0);
```

The third parameter is the amount of time the task should wait for data.

Using `0` means:

```text
Is something in the queue?
    │
    ├── yes -> receive it
    │
    └── no  -> continue immediately
```

This is useful here because the uploader wants to check several queues.

If `portMAX_DELAY` were used:

```cpp
xQueueReceive(sensorQueue, &sensor, portMAX_DELAY);
```

the uploader could wait forever on `sensorQueue` and would not get a chance to check `eventQueue` or `statusQueue`.

---

# Draining a Queue

This pattern:

```cpp
while (xQueueReceive(sensorQueue, &sensor, 0) == pdPASS) {
    // Process sensor
}
```

means:

```text
receive item
    ↓
another item?
    ↓
receive item
    ↓
another item?
    ↓
...
    ↓
queue empty
    ↓
continue
```

It drains everything currently waiting in the queue.

---

# Complete Setup Example

```cpp
#include <Arduino.h>

QueueHandle_t sensorQueue;
QueueHandle_t eventQueue;
QueueHandle_t statusQueue;

struct SensorReading {
    uint8_t sensorId;
    float value;
    uint32_t timestamp;
};

struct EventData {
    uint16_t eventId;
    uint32_t timestamp;
};

struct StatusData {
    bool online;
    uint8_t batteryPercent;
    uint32_t timestamp;
};

void uploaderTask(void *parameter)
{
    SensorReading sensor;
    EventData event;
    StatusData status;

    while (true) {

        bool hasData = false;

        while (xQueueReceive(sensorQueue, &sensor, 0) == pdPASS) {

            hasData = true;

            Serial.printf(
                "Sensor %u: %.2f\n",
                sensor.sensorId,
                sensor.value
            );
        }

        while (xQueueReceive(eventQueue, &event, 0) == pdPASS) {

            hasData = true;

            Serial.printf(
                "Event: %u\n",
                event.eventId
            );
        }

        while (xQueueReceive(statusQueue, &status, 0) == pdPASS) {

            hasData = true;

            Serial.printf(
                "Status: online=%d battery=%u%%\n",
                status.online,
                status.batteryPercent
            );
        }

        if (hasData) {
            Serial.println("Build payload and upload here");
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void setup()
{
    Serial.begin(115200);

    sensorQueue = xQueueCreate(
        32,
        sizeof(SensorReading)
    );

    eventQueue = xQueueCreate(
        16,
        sizeof(EventData)
    );

    statusQueue = xQueueCreate(
        8,
        sizeof(StatusData)
    );

    xTaskCreate(
        uploaderTask,
        "Uploader",
        4096,
        NULL,
        1,
        NULL
    );
}

void loop()
{
}
```

# Design Notes

This design works well when:

- Different parts of the firmware produce different types of data.
- You want each data type to have its own queue.
- Uploading can happen periodically instead of immediately.
- You want to batch multiple readings into one API request.

A typical next step would be to build one JSON object containing all drained queue items and send that JSON using HTTP.

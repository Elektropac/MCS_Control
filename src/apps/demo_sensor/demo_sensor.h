#pragma once
#include <ArduinoJson.h>

void demo_sensor_init(const JsonObject& config);
void demo_sensor_task(void* param);

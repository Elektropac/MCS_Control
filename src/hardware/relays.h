#pragma once
#include <Arduino.h>

enum Relay : uint8_t { RELAY_A, RELAY_B };

void relays_init();
void relay_set(Relay relay, bool on);
bool relay_get(Relay relay);

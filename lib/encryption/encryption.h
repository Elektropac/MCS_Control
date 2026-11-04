#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

String Encrypt(String data_in, String key);
String Decrypt(String data_in, String key);

String Encrypt_Json(JsonDocument &doc, String key);
JsonDocument Decrypt_Json(String data_in, String key);
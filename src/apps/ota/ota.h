#pragma once
// =======================================================
// OTA — over-the-air firmware update (ported from legacy control fw)
// =======================================================
// Ported for the RTOS firmware:
//   - network status read from w5500::/wifi:: (never touches Niklas' net code)
//   - logging via log_info/log_error
//   - plain HTTP only (Tailscale Funnel / HTTPS override logic removed;
//     active server is http://52.56.195.98/ota/control)
//   - runs as its own FreeRTOS task (ota_start_task) instead of loop() polling
//   - FUELING-SAFE: never starts a download while a pump is active; waits.
//
// Server contract (unchanged): manifest.json + releases.json describe
// available builds; firmware compares versions and streams the .bin to the
// ESP32 OTA partition (app0/app1), then reboots.
// =======================================================

#include <Arduino.h>

enum OtaState : uint8_t {
    OTA_IDLE = 0,
    OTA_CHECKING = 1,
    OTA_READY = 2,
    OTA_UPDATING = 3,
    OTA_SUCCESS = 4,
    OTA_ERROR = 5,
    OTA_WAITING_FUELING = 6   // update requested but blocked by active fueling
};

// Lifecycle
void otaInit();              // reset state + load saved config (call once at boot)
void ota_start_task();       // start the FreeRTOS worker/poll task

// Config (manifest + releases URLs, persisted in NVS)
const char* otaCurrentVersion();
const char* otaManifestUrl();
const char* otaReleasesUrl();
void otaLoadConfig(String& manifestUrl, String& releasesUrl);
bool otaSaveConfig(const String& manifestUrl, const String& releasesUrl);
bool otaConfigValid(const String& manifestUrl, const String& releasesUrl);

// Actions (safe to call from web/UI task — they just set a pending flag)
bool otaRequestCheck();
bool otaRequestUpdate();
bool otaSelectVersion(const String& version);
bool otaBusy();

// Status (for web UI / JSON)
OtaState otaState();
String otaStateText();
String otaStatusMessage();
String otaAvailableVersion();
String otaSelectedVersion();
String otaSelectedTransport();
String otaSelectedUrl();
uint32_t otaStatusCounter();
unsigned long otaLastUpdateMs();
unsigned long otaLastCheckMs();
bool otaUpdateAvailable();
size_t otaAvailableSize();
String otaStatusJson();

// Release list access (for OLED menu / non-web UIs). Count is populated by a
// successful otaRequestCheck(). Index must be < otaReleaseCount().
size_t otaReleaseCount();
String otaReleaseVersionAt(size_t index);

// Download progress (bytes). Valid during OTA_UPDATING.
size_t otaDownloadedBytes();
size_t otaDownloadTotalBytes();

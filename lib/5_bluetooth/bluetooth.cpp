#include "bluetooth.h"

// BLE stack: NimBLE (h2zero/NimBLE-Arduino), peripheral role only.
// Replaced Bluedroid on 2026-11-17 to free RAM (Bluedroid took ~86 KB of heap
// and left too little contiguous memory for TLS). The GATT layout, names,
// UUIDs, properties and the take/done + encrypted chunked protocol are
// UNCHANGED, so the app needs no change.
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <deque>
#include <string>

#include "logging.h"
#include <esp_heap_caps.h>
#include "my_wifi.h"
#include "encryption.h"
#include "function_silo.h"

#define MAIN_SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define MAIN_INPUT_CHARACTERISTIC_UUID "2c3b4d5e-6f7a-8b9c-d0e1-f2a3b4c5d6e7"
#define MAIN_OUTPUT_CHARACTERISTIC_UUID "1b9a473a-4493-4536-8b2b-9d4133488256"

#define SECONDARY_SERVICE_UUID "2d145a50-eadc-4505-bf0b-17a6c89d3758"
#define SECONDARY_INPUT_CHARACTERISTIC_UUID "6f70c48b-72f0-42e5-9a07-a9147e6c9dba"
#define SECONDARY_OUTPUT_CHARACTERISTIC_UUID "a2ce29fb-bdea-436e-a5e2-497a764ce4ce"

#define SELECTER_SERVICE_UUID "1b7819c2-1390-4ac1-b497-6e87b8541ea4"
#define AVAILABLE_CHARACTERISTIC_UUID "6e01896c-0aa0-4418-9cbf-ba4c276f3009"

#define STATE_SERVICE_UUID "05ffec94-33b4-4065-83c5-381e1a021071"
#define STATE_CHARACTERISTIC_UUID "eb9fad1c-011d-4fb7-b21a-82ca6f8208c9"

struct bluetooth_serial
{
    NimBLECharacteristic *input;
    NimBLECharacteristic *output;
    bool is_available;
    String encryption_key;
    unsigned long last_message_millis;
    String input_buffer;   // accumulates chunked command writes until '\n'
};

// Writes arrive in NimBLE's host task (onWrite callback). They are queued here
// and processed in order by the bluetooth task (bluetooth::poll), so a fast
// sequence of chunked writes can never overwrite each other. (With Bluedroid
// the task polled the characteristic value every 10 ms — a write that landed
// between two polls was silently lost.) Capped so a misbehaving client can't
// eat the heap.
static const size_t   RX_QUEUE_MAX_BYTES = 8192;
static SemaphoreHandle_t s_rx_lock = nullptr;
static TaskHandle_t      s_ble_task = nullptr;   // the task running bluetooth::poll()

struct RxQueue {
    std::deque<std::string> writes;
    size_t bytes = 0;
};
static RxQueue s_rx_main, s_rx_secondary;

static void rx_push(RxQueue &q, const std::string &v)
{
    if (v.empty()) return;
    xSemaphoreTake(s_rx_lock, portMAX_DELAY);
    if (q.bytes + v.size() <= RX_QUEUE_MAX_BYTES) {
        q.writes.push_back(v);
        q.bytes += v.size();
    }
    xSemaphoreGive(s_rx_lock);
    if (s_ble_task) xTaskNotifyGive(s_ble_task);
}

static bool rx_pop(RxQueue &q, std::string &out)
{
    bool ok = false;
    xSemaphoreTake(s_rx_lock, portMAX_DELAY);
    if (!q.writes.empty()) {
        out = std::move(q.writes.front());
        q.writes.pop_front();
        q.bytes -= out.size();
        ok = true;
    }
    xSemaphoreGive(s_rx_lock);
    return ok;
}

class InputCallbacks : public NimBLECharacteristicCallbacks
{
public:
    explicit InputCallbacks(RxQueue &q) : m_q(q) {}
    void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &) override
    {
        rx_push(m_q, c->getValue());
    }
private:
    RxQueue &m_q;
};

// notify() can fail when NimBLE is out of buffers (many chunks in a row).
// Bluedroid queued internally; here we retry briefly so a reply is never
// silently truncated.
static void notify_reliable(NimBLECharacteristic *c, const uint8_t *data, size_t len)
{
    for (int attempt = 0; attempt < 20; attempt++) {
        if (c->notify(data, len)) return;
        delay(10);
    }
    log_error("[Bluetooth] notify failed after retries (%u bytes)", (unsigned)len);
}

namespace bluetooth_services
{
    bluetooth_serial main_serial = {
        input : NULL,
        output : NULL,
        is_available : true,
        encryption_key : "",
        last_message_millis : 0,
        input_buffer : ""
    };
    bluetooth_serial secondary_serial = {
        input : NULL,
        output : NULL,
        is_available : true,
        encryption_key : "",
        last_message_millis : 0,
        input_buffer : ""
    };

    // selecter service characteristics
    NimBLECharacteristic *availableCharacteristic = NULL;
    // state service characteristics
    NimBLECharacteristic *stateCharacteristic = NULL;

    void init(NimBLEServer *server)
    {
        NimBLEService *mainService = server->createService(MAIN_SERVICE_UUID);
        NimBLEService *secondaryService = server->createService(SECONDARY_SERVICE_UUID);
        NimBLEService *selecterService = server->createService(SELECTER_SERVICE_UUID);
        NimBLEService *stateService = server->createService(STATE_SERVICE_UUID);

        // Same properties as before. NimBLE adds the 0x2902 (CCCD) descriptor
        // to NOTIFY characteristics by itself — no BLE2902 needed.
        main_serial.input = mainService->createCharacteristic(
            MAIN_INPUT_CHARACTERISTIC_UUID, NIMBLE_PROPERTY::WRITE);
        main_serial.input->setCallbacks(new InputCallbacks(s_rx_main));
        main_serial.output = mainService->createCharacteristic(
            MAIN_OUTPUT_CHARACTERISTIC_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

        secondary_serial.input = secondaryService->createCharacteristic(
            SECONDARY_INPUT_CHARACTERISTIC_UUID, NIMBLE_PROPERTY::WRITE);
        secondary_serial.input->setCallbacks(new InputCallbacks(s_rx_secondary));
        secondary_serial.output = secondaryService->createCharacteristic(
            SECONDARY_OUTPUT_CHARACTERISTIC_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

        availableCharacteristic = selecterService->createCharacteristic(
            AVAILABLE_CHARACTERISTIC_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

        stateCharacteristic = stateService->createCharacteristic(
            STATE_CHARACTERISTIC_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

        mainService->start();
        secondaryService->start();
        selecterService->start();
        stateService->start();
    }

    // Handle ONE write (as received) on a channel. Logic unchanged from the
    // Bluedroid version; only the source of the value changed (queue instead
    // of polling the characteristic).
    void run_main_and_secondary(bluetooth_serial &serial, const std::string &_value)
    {
        auto inputValue = String(_value.c_str());

        if (inputValue.length() > 0)
        {
            if (serial.is_available == true)
            {
                // then the input need to be "take:<encryption_key:16>"
                if (inputValue.startsWith("take:"))
                {
                    log_info("[Bluetooth] Received take command with encryption key.");
                    String encryption_key = inputValue.substring(5);
                    // Handle the encryption key as needed
                    if (encryption_key.length() == 16)
                    {
                        // Valid encryption key, handle it as needed
                        serial.is_available = false;            // Mark the serial as no longer available
                        serial.encryption_key = encryption_key; // Store the encryption key
                        serial.last_message_millis = millis();  // start the inactivity timeout from NOW
                        serial.input_buffer = "";               // fresh command buffer for this session
                        log_info("[Bluetooth] Serial marked as unavailable and encryption key stored.");
                        
                        JsonDocument doc;
                        doc["subject"] = "take";
                        doc["data"]["encryption_key"] = encryption_key;
                        String response = Encrypt_Json(doc, encryption_key);
                        serial.output->setValue(response.c_str());
                        notify_reliable(serial.output, (const uint8_t *)response.c_str(), response.length());
                    }
                    else
                    {
                        log_error("[Bluetooth] Invalid encryption key length: %s", encryption_key.c_str());
                    }
                }

                return;
            }
            else if (serial.is_available == false && inputValue.startsWith("done:"))
            {   // The serial is currently in use, handle accordingly
                // the input input can be "done:<encryption_key:16:same_as_take>"
                String encryption_key = inputValue.substring(5);
                // Handle the encryption key as needed
                if (encryption_key.length() == 16 && encryption_key == serial.encryption_key)
                {
                    // Valid encryption key, mark the serial as available again
                    serial.is_available = true;
                    serial.encryption_key = ""; // Clear the stored encryption key
                    serial.input_buffer = "";   // drop any partial command
                    log_info("[Bluetooth] Serial marked as available again.");
                }

                return;
            }
            else if (serial.is_available == false)
            {
                // Commands may exceed one BLE write (the encrypted+hex blob for
                // e.g. fuel_authorize can be > 512 B). The app now sends every
                // command in <=CHUNK-byte writes terminated by '\n' (hex never
                // contains newline), mirroring the output path. Accumulate here
                // until we see the '\n', then decrypt the complete payload.
                serial.input_buffer += inputValue;
                int nl = serial.input_buffer.indexOf('\n');
                if (nl < 0)
                {
                    // Guard against a runaway buffer (missing terminator).
                    if (serial.input_buffer.length() > 8192)
                    {
                        log_error("[Bluetooth] Input buffer overflow, discarding.");
                        serial.input_buffer = "";
                    }
                    serial.last_message_millis = millis();
                    return;   // wait for more chunks
                }

                String payload = serial.input_buffer.substring(0, nl);
                serial.input_buffer = serial.input_buffer.substring(nl + 1);

                JsonDocument doc = Decrypt_Json(payload, serial.encryption_key);
                if (doc.isNull())
                {
                    log_error("[Bluetooth] Failed to decrypt JSON input: %s", payload.c_str());
                    serial.last_message_millis = millis();
                    return;
                }

                String result = function_silo::run_function_silo(doc);
                // Encrypt the function RESULT (not the request doc). Previously
                // this encrypted `doc`, so the device echoed the request back
                // instead of returning the data. result is already-serialized JSON.
                String encrypted_result = Encrypt(result, serial.encryption_key);
                if (encrypted_result.length() > 0)
                {
                    // The encrypted payload (hex) is often > one BLE notify can
                    // carry, so send it in chunks and terminate with '\n' (hex
                    // never contains newline). The app concatenates until '\n'.
                    // 120 B is safe even at the iOS default MTU (~185) with ATT
                    // overhead; larger replies (e.g. the 2 KB RAM log) were
                    // corrupting at 180. A slightly longer inter-chunk delay
                    // lets the BLE stack flush each notify reliably.
                    const size_t CHUNK = 120;
                    size_t total = encrypted_result.length();
                    for (size_t off = 0; off < total; off += CHUNK)
                    {
                        size_t n = (total - off < CHUNK) ? (total - off) : CHUNK;
                        serial.output->setValue(
                            (uint8_t*)encrypted_result.c_str() + off, n);
                        notify_reliable(serial.output,
                            (const uint8_t*)encrypted_result.c_str() + off, n);
                        delay(20);   // let the stack flush each notify
                    }
                    // end-of-message marker
                    uint8_t nl = '\n';
                    serial.output->setValue(&nl, 1);
                    notify_reliable(serial.output, &nl, 1);
                }
            }


            if (serial.is_available == false && (millis() - serial.last_message_millis) > 10000) /// Timeout after 10 seconds of inactivity
            {
                log_error("[Bluetooth] Serial timeout, marking as available.");
                serial.is_available = true;
                serial.encryption_key = "";
                serial.input_buffer = "";
            }
            
            serial.last_message_millis = millis();
        }
    }

    static const char *s_last_available = nullptr;

    void poll()
    {
        std::string v;
        while (rx_pop(s_rx_main, v))      run_main_and_secondary(main_serial, v);
        while (rx_pop(s_rx_secondary, v)) run_main_and_secondary(secondary_serial, v);

        const char *avail = main_serial.is_available      ? "main"
                          : secondary_serial.is_available ? "secondary"
                                                          : "none";
        // Value is always current for reads. Notify only when it CHANGES —
        // the old code notified every 10 ms (100 notifications/s) even when
        // nothing changed. (The app does not subscribe to this characteristic.)
        if (avail != s_last_available)
        {
            availableCharacteristic->setValue(avail);
            availableCharacteristic->notify();
            s_last_available = avail;
        }
    }

    // Release both channels. Call when a BLE client disconnects so the next
    // client's `take:` is handled fresh (otherwise a stale "unavailable" state
    // makes the next take get mis-routed into the decrypt branch).
    void reset_channels()
    {
        main_serial.is_available = true;
        main_serial.encryption_key = "";
        secondary_serial.is_available = true;
        secondary_serial.encryption_key = "";
    }
}

namespace bluetooth
{
    NimBLEServer *pServer = NULL;

    // Two app sessions can be connected at once (main + secondary channel).
    static const uint8_t MAX_CLIENTS = 2;

    class MyServerCallbacks : public NimBLEServerCallbacks
    {
        void onConnect(NimBLEServer *server, NimBLEConnInfo &) override
        {
            log_info("[Bluetooth] Device Connected");
            // Keep advertising so a second client can connect (as before).
            if (server->getConnectedCount() < MAX_CLIENTS) NimBLEDevice::startAdvertising();
        }

        void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int reason) override
        {
            log_info("[Bluetooth] Device Disconnected (reason %d)", reason);
            bluetooth_services::reset_channels();   // free channels for next client
            NimBLEDevice::startAdvertising();
        }
    };

    static bool s_ble_ready = false;

    void init()
    {
        // Keep the old guard: never let a failed allocation abort the device.
        // NimBLE needs far less than Bluedroid, so this practically never trips.
        const size_t kNeededContiguous = 40 * 1024;
        size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        if (largest < kNeededContiguous) {
            log_error("[Bluetooth] skipping init: largest free block %u < %u "
                      "needed (heap too fragmented) - BLE disabled this boot",
                      (unsigned)largest, (unsigned)kNeededContiguous);
            return;
        }

        s_rx_lock = xSemaphoreCreateMutex();

        String mac = wifi::make_mac();
        String _name = "Control_" + mac;

        NimBLEDevice::init(_name.c_str());
        pServer = NimBLEDevice::createServer();
        pServer->setCallbacks(new MyServerCallbacks());
        pServer->advertiseOnDisconnect(false);   // done in onDisconnect (after channel reset)

        bluetooth_services::init(pServer);
        pServer->start();

        // Advertise the name (the app filters on the "Control_" prefix).
        // Bluedroid put it in the advertising packet by default; NimBLE does not.
        NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
        adv->setName(_name.c_str());
        adv->enableScanResponse(false);
        adv->start();
        log_info("[Bluetooth] advertising started (NimBLE)");
        log_info("[Bluetooth] name: %s", _name.c_str());
        s_ble_ready = true;
    }

    void poll()
    {
        if (!s_ble_ready) return;   // init skipped (low heap) -> nothing to poll
        if (!s_ble_task) s_ble_task = xTaskGetCurrentTaskHandle();
        // Sleep until a write arrives (onWrite notifies us), at most 100 ms.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        bluetooth_services::poll();
    }
}

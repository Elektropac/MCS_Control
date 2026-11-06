#include "bluetooth.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ArduinoJson.h>

#include "logging.h"
#include "memreport.h"
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
    BLECharacteristic *input;
    BLECharacteristic *output;
    bool is_available;
    String encryption_key;
    unsigned long last_message_millis;
};

namespace bluetooth_services
{
    bluetooth_serial main_serial = {
        input : NULL,
        output : NULL,
        is_available : true,
        encryption_key : "",
        last_message_millis : 0
    };
    bluetooth_serial secondary_serial = {
        input : NULL,
        output : NULL,
        is_available : true,
        encryption_key : "",
        last_message_millis : 0
    };

    // selecter service characteristics
    BLECharacteristic *availableCharacteristic = NULL;
    // state service characteristics
    BLECharacteristic *stateCharacteristic = NULL;

    void init(BLEServer *server)
    {
        BLEService *mainService = server->createService(MAIN_SERVICE_UUID);
        BLEService *secondaryService = server->createService(SECONDARY_SERVICE_UUID);
        BLEService *selecterService = server->createService(SELECTER_SERVICE_UUID);
        BLEService *stateService = server->createService(STATE_SERVICE_UUID);

        // Create main service characteristics
        main_serial.input = mainService->createCharacteristic(
            MAIN_INPUT_CHARACTERISTIC_UUID,
            BLECharacteristic::PROPERTY_WRITE);

        main_serial.output = mainService->createCharacteristic(
            MAIN_OUTPUT_CHARACTERISTIC_UUID,
            BLECharacteristic::PROPERTY_READ |
                BLECharacteristic::PROPERTY_NOTIFY);
        main_serial.output->addDescriptor(new BLE2902());

        // Create secondary service characteristics
        secondary_serial.input = secondaryService->createCharacteristic(
            SECONDARY_INPUT_CHARACTERISTIC_UUID,
            BLECharacteristic::PROPERTY_WRITE);

        secondary_serial.output = secondaryService->createCharacteristic(
            SECONDARY_OUTPUT_CHARACTERISTIC_UUID,
            BLECharacteristic::PROPERTY_READ |
                BLECharacteristic::PROPERTY_NOTIFY);
        secondary_serial.output->addDescriptor(new BLE2902());

        // Create selecter service characteristics
        availableCharacteristic = selecterService->createCharacteristic(
            AVAILABLE_CHARACTERISTIC_UUID,
            BLECharacteristic::PROPERTY_READ |
                BLECharacteristic::PROPERTY_NOTIFY);
        availableCharacteristic->addDescriptor(new BLE2902());

        // Create state service characteristics
        stateCharacteristic = stateService->createCharacteristic(
            STATE_CHARACTERISTIC_UUID,
            BLECharacteristic::PROPERTY_READ |
                BLECharacteristic::PROPERTY_NOTIFY);
        stateCharacteristic->addDescriptor(new BLE2902());

        mainService->start();
        secondaryService->start();
        selecterService->start();
        stateService->start();
    }

    void run_main_and_secondary(bluetooth_serial &serial)
    {
        auto _value = serial.input->getValue();
        auto inputValue = String(_value.c_str());
        serial.input->setValue("");

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
                        log_info("[Bluetooth] Serial marked as unavailable and encryption key stored.");
                        
                        JsonDocument doc;
                        doc["subject"] = "take";
                        doc["data"]["encryption_key"] = encryption_key;
                        String response = Encrypt_Json(doc, encryption_key);
                        serial.output->setValue(response.c_str());
                        serial.output->notify();
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
                    log_info("[Bluetooth] Serial marked as available again.");
                }

                return;
            }
            else if (serial.is_available == false)
            {
                JsonDocument doc = Decrypt_Json(inputValue, serial.encryption_key);
                if (doc.isNull())
                {
                    log_error("[Bluetooth] Failed to decrypt JSON input: %s", inputValue.c_str());
                    return;
                }

                String result = function_silo::run_function_silo(doc);
                String encrypted_result = Encrypt_Json(doc, serial.encryption_key);
                if (encrypted_result.length() > 0)
                {
                    serial.output->setValue(encrypted_result.c_str());
                    serial.output->notify();
                }
            }


            if (serial.is_available == false && (millis() - serial.last_message_millis) > 10000) /// Timeout after 10 seconds of inactivity
            {
                log_error("[Bluetooth] Serial timeout, marking as available.");
                serial.is_available = true;
                serial.encryption_key = "";
            }
            
            serial.last_message_millis = millis();
        }
    }

    void poll()
    {
        run_main_and_secondary(main_serial);
        run_main_and_secondary(secondary_serial);

        if (main_serial.is_available == true)
        {
            availableCharacteristic->setValue("main");
            availableCharacteristic->notify();
        }
        else if (secondary_serial.is_available == true)
        {
            availableCharacteristic->setValue("secondary");
            availableCharacteristic->notify();
        }
        else
        {
            availableCharacteristic->setValue("none");
            availableCharacteristic->notify();
        }
    }
}

namespace bluetooth
{
    BLEServer *pServer = NULL;

    class MyServerCallbacks : public BLEServerCallbacks
    {
        void onConnect(BLEServer *_server)
        {
            log_info("[Bluetooth] Device Connected");
            _server->getAdvertising()->start();
        }

        void onDisconnect(BLEServer *_server)
        {
            log_info("[Bluetooth] Device Disconnected");
            _server->getAdvertising()->start();
        }
    };

    static bool s_ble_ready = false;

    void init()
    {
        // BLE's controller + stack need a large *contiguous* block. If we are
        // called after the network stack has fragmented the heap, that block
        // may not exist and operator new would throw -> abort() -> device reset.
        // Guard it: skip BLE rather than crash the whole device. (The real fix
        // is to call init() early, before the network stack fragments the heap.)
        const size_t kNeededContiguous = 40 * 1024;
        size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        if (largest < kNeededContiguous) {
            log_error("[Bluetooth] skipping init: largest free block %u < %u "
                      "needed (heap too fragmented) - BLE disabled this boot",
                      (unsigned)largest, (unsigned)kNeededContiguous);
            return;
        }

        String mac = wifi::make_mac();
        String _name = "Control_" + mac;

        memreport_mark("BLE: before init");
        // arduino-esp32 3.2.0's BLEDevice::init() takes an Arduino String.
        BLEDevice::init(_name);
        memreport_mark("BLE: after BLEDevice::init");
        // Create server
        pServer = BLEDevice::createServer();
        pServer->setCallbacks(new MyServerCallbacks());
        memreport_mark("BLE: after createServer");

        // Create services
        bluetooth_services::init(pServer);
        memreport_mark("BLE: after services");

        // Start advertising
        pServer->getAdvertising()->start();
        log_info("[Bluetooth] advertising started");
        log_info("[Bluetooth] name: %s", _name.c_str());
        s_ble_ready = true;
    }

    void poll()
    {
        if (!s_ble_ready) return;   // init skipped (low heap) -> nothing to poll
        bluetooth_services::poll();
    }
}
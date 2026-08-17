#include "hal.h"

static SemaphoreHandle_t s_i2c_mutex = nullptr;

void i2c_init(uint8_t sda, uint8_t scl, uint32_t freq_hz) {
    s_i2c_mutex = xSemaphoreCreateMutex();
    Wire.begin(sda, scl);
    Wire.setClock(freq_hz);
}

bool i2c_probe(uint8_t addr) {
    if (!i2c_take(100)) return false;
    Wire.beginTransmission(addr);
    bool ok = (Wire.endTransmission() == 0);
    i2c_give();
    return ok;
}

bool i2c_take(uint32_t timeout_ms) {
    if (s_i2c_mutex == nullptr) return false;
    return xSemaphoreTake(s_i2c_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void i2c_give() {
    if (s_i2c_mutex != nullptr) {
        xSemaphoreGive(s_i2c_mutex);
    }
}

#include "pca9557_button_manager.h"
#include "esp_timer.h"

Pca9557ButtonManager::Pca9557ButtonManager(i2c_master_bus_handle_t bus, uint8_t addr)
    : bus_(bus), addr_(addr), dev_(nullptr) {}
Pca9557ButtonManager(i2c_master_dev_handle_t dev) : dev_(dev) {}
esp_err_t Pca9557ButtonManager::Init()
{
    i2c_device_config_t dev_cfg = {
        .device_address = addr_,
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(bus_, &dev_cfg, &dev_);
}

void Pca9557ButtonManager::AddButton(uint8_t bit, Pca9557Button* btn)
{
    buttons_.push_back({bit, btn});
}

bool Pca9557ButtonManager::ReadInput(uint8_t& value)
{
    return i2c_master_receive(dev_, &value, 1, 50) == ESP_OK;
}

void Pca9557ButtonManager::Poll()
{
    uint8_t reg = 0;
    if (!ReadInput(reg)) return;

    uint32_t now = esp_timer_get_time() / 1000;

    for (auto& e : buttons_) {
        bool pressed = !(reg & (1 << e.bit)); // PCA9557：低电平=按下
        e.btn->UpdateRawState(pressed, now);
    }
}

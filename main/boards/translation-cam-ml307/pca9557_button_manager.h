#pragma once
#include <vector>
#include "pca9557_button.h"
#include "driver/i2c_master.h"

class Pca9557ButtonManager {
public:
    Pca9557ButtonManager(i2c_master_bus_handle_t bus, uint8_t addr);
    Pca9557ButtonManager(i2c_master_dev_handle_t dev);
    esp_err_t Init();
    void AddButton(uint8_t bit, Pca9557Button* btn);
    void Poll();  // 每 10ms 调用

private:
    struct BtnMap {
        uint8_t bit;
        Pca9557Button* btn;
    };
    std::vector<BtnMap> buttons_;

    i2c_master_bus_handle_t bus_;
    uint8_t addr_;
    i2c_master_dev_handle_t dev_;

    bool ReadInput(uint8_t& value);
};

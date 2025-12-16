#pragma once
#include <functional>
#include <stdint.h>

class Pca9557Button {
public:
    Pca9557Button();

    void OnPressDown(std::function<void()> cb);
    void OnPressUp(std::function<void()> cb);
    void OnLongPress(std::function<void()> cb);
    void OnClick(std::function<void()> cb);
    void OnDoubleClick(std::function<void()> cb);
    void OnMultipleClick(std::function<void()> cb, uint8_t count = 3);

    // manager 调用
    void UpdateRawState(bool pressed, uint32_t now_ms);

private:
    bool last_state_ = false;
    uint32_t last_change_time_ = 0;
    uint32_t press_start_time_ = 0;

    uint32_t last_click_time_ = 0;
    uint8_t click_count_ = 0;

    const uint32_t DebounceMs = 30;
    const uint32_t ClickIntervalMs = 250;
    const uint32_t LongPressMs = 1200;

    // 回调
    std::function<void()> cb_down_;
    std::function<void()> cb_up_;
    std::function<void()> cb_long_;
    std::function<void()> cb_click_;
    std::function<void()> cb_double_;
    std::function<void()> cb_multi_;
    uint8_t multi_click_target_ = 3;
};

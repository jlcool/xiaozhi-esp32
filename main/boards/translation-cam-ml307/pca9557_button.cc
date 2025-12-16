#include "pca9557_button.h"

Pca9557Button::Pca9557Button() {}

void Pca9557Button::OnPressDown(std::function<void()> cb) { cb_down_ = cb; }
void Pca9557Button::OnPressUp(std::function<void()> cb) { cb_up_ = cb; }
void Pca9557Button::OnLongPress(std::function<void()> cb) { cb_long_ = cb; }
void Pca9557Button::OnClick(std::function<void()> cb) { cb_click_ = cb; }
void Pca9557Button::OnDoubleClick(std::function<void()> cb) { cb_double_ = cb; }
void Pca9557Button::OnMultipleClick(std::function<void()> cb, uint8_t c)
{
    cb_multi_ = cb;
    multi_click_target_ = c;
}

void Pca9557Button::UpdateRawState(bool pressed, uint32_t now_ms)
{
    // 去抖
    if (pressed != last_state_) {
        if (now_ms - last_change_time_ < DebounceMs) {
            return;
        }
        last_change_time_ = now_ms;
        last_state_ = pressed;

        if (pressed) {
            // 按下
            press_start_time_ = now_ms;
            if (cb_down_) cb_down_();
        } else {
            // 松开
            uint32_t duration = now_ms - press_start_time_;

            if (cb_up_) cb_up_();

            if (duration >= LongPressMs) {
                if (cb_long_) cb_long_();
                return; // 长按不再触发单击
            }

            // 点击统计
            if (now_ms - last_click_time_ > ClickIntervalMs) {
                click_count_ = 1;
            } else {
                click_count_++;
            }

            last_click_time_ = now_ms;

            if (click_count_ == 1 && cb_click_) cb_click_();
            if (click_count_ == 2 && cb_double_) cb_double_();
            if (click_count_ == multi_click_target_ && cb_multi_) cb_multi_();
        }
    }
}

#include "dual_network_board.h"
#include "audio/codecs/box_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "led/single_led.h"
#include "config.h"
#include "i2c_device.h"
#include "esp32_camera.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <wifi_station.h>
#include "power_manager.h"
#include <esp_lvgl_port.h>
#include <lvgl.h>
#include "settings.h"
#include "wifi_board.h"
#include "audio_file_cache.h"
#include "assets/lang_config.h"

#define FIRST_BOOT_NS "boot_config"  
#define FIRST_BOOT_KEY "is_first"    


#define TAG "Translation_ML307"

//控制器初始化函数声明
void InitializeMCPController();

LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);

class Pca9557 : public I2cDevice {
public:
    Pca9557(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        WriteReg(0x01, 0x03);
        WriteReg(0x03, 0xf8);
    }

    void SetOutputState(uint8_t bit, uint8_t level) {
        uint8_t data = ReadReg(0x01);
        data = (data & ~(1 << bit)) | (level << bit);
        WriteReg(0x01, data);
    }
};

class CustomAudioCodec : public BoxAudioCodec {
private:
    Pca9557* pca9557_;

public:
    CustomAudioCodec(i2c_master_bus_handle_t i2c_bus, Pca9557* pca9557) 
        : BoxAudioCodec(i2c_bus, 
                       AUDIO_INPUT_SAMPLE_RATE, 
                       AUDIO_OUTPUT_SAMPLE_RATE,
                       AUDIO_I2S_GPIO_MCLK, 
                       AUDIO_I2S_GPIO_BCLK, 
                       AUDIO_I2S_GPIO_WS, 
                       AUDIO_I2S_GPIO_DOUT, 
                       AUDIO_I2S_GPIO_DIN,
                       GPIO_NUM_NC, 
                       AUDIO_CODEC_ES8311_ADDR, 
                       AUDIO_CODEC_ES7210_ADDR, 
                       AUDIO_INPUT_REFERENCE),
          pca9557_(pca9557) {
    }

    virtual void EnableOutput(bool enable) override {
        BoxAudioCodec::EnableOutput(enable);
        if (enable) {
            pca9557_->SetOutputState(1, 1);
        } else {
            pca9557_->SetOutputState(1, 0);
        }
    }
};

class TranslationCamBoard_ML307 : public DualNetworkBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    i2c_master_dev_handle_t pca9557_handle_;
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    LcdDisplay* display_ = nullptr;
    Pca9557* pca9557_;
    Esp32Camera* camera_;
    PowerManager* power_manager_ = new PowerManager(GPIO_NUM_47);

    void InitializeI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)1,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));

        // Initialize PCA9557
        pca9557_ = new Pca9557(i2c_bus_, 0x19);
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = GPIO_NUM_40;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = GPIO_NUM_41;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            app.SetDeviceState(kDeviceStateSpeaking);
            display_->SetChatMessage("assistant", app.GetCombinedSentence().c_str());

            AudioFileCache& cache = AudioFileCache::GetInstance();

            // 1️⃣ 开始回放
            cache.ResetRead();
            vTaskDelay(pdMS_TO_TICKS(100));
            auto& audio_service = app.GetAudioService();
            // 创建独立任务处理音频播放和状态切换，避免阻塞主线程
            xTaskCreatePinnedToCore(
                [](void* arg) {
                    // 解析参数
                    auto* audio_service_ptr = static_cast<AudioService*>(arg);
                    AudioFileCache& cache = AudioFileCache::GetInstance();

                    // 循环推送音频包
                    while (true) {
                        auto packet = cache.ReadNextPacket();
                        if (!packet) {
                            // 等待音频服务播放完成（此时不会阻塞主线程）
                            while (!audio_service_ptr->IsIdle()) {
                                vTaskDelay(pdMS_TO_TICKS(100));
                            }
                            // 播放完成后切换状态
                            Application::GetInstance().SetDeviceState(kDeviceStateIdle);
                            break; // 音频包读取完毕
                        }
                        audio_service_ptr->PushPacketToDecodeQueue(std::move(packet));
                    }

                    

                    
                    vTaskDelete(nullptr); // 销毁任务
                },
                "AudioPlaybackTask", // 任务名称
                4096,                // 栈大小
                &audio_service,      // 传递音频服务指针
                5,                   // 任务优先级
                nullptr,
                0                    // 运行的核心
            );
            
            // auto& app = Application::GetInstance();
            // if (GetNetworkType() == NetworkType::WIFI) {
            //     if (app.GetDeviceState() == kDeviceStateStarting) {
            //         auto& wifi_board = static_cast<WifiBoard&>(GetCurrentBoard());
            //         wifi_board.EnterWifiConfigMode();
            //         return;
            //     }
            // }

            // app.ToggleChatState();
        });

        boot_button_.OnDoubleClick([this]() {
            Settings settings(FIRST_BOOT_NS, true);
            bool is_first_boot = settings.GetInt(FIRST_BOOT_KEY, 1) != 0;
            if (is_first_boot) {
                ESP_LOGI(TAG, "首次启动，启用双击拍照功能");
                auto camera = GetCamera();
                if (!camera->Capture()) {
                    ESP_LOGE(TAG, "Camera capture failed");
                }
                settings.SetInt(FIRST_BOOT_KEY, 0);

                
            } else {
                ESP_LOGI(TAG, "非首次启动，禁用双击拍照功能");
                auto& app = Application::GetInstance();
                if (app.GetDeviceState() == kDeviceStateIdle) {
                    app.SetAecMode(app.GetAecMode() == kAecOff ? kAecOnDeviceSide : kAecOff);
                    GetAudioCodec()->SetOutputVolume(60);
                }
            }
        });

        boot_button_.OnLongPress([this]() {
            //按下就启动监听
            auto& app = Application::GetInstance();
            //播放就绪提示音，
            app.PlaySound(Lang::Sounds::OGG_SUCCESS);

             //重置缓存音频文件
            AudioFileCache::GetInstance().ResetWrite();
            //重置翻译结果
            app.ClearCombinedSentence();
            app.StartListening();
        });
        boot_button_.OnPressUp([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateListening){
                //松开就停止监听
                app.StopListening();
            }
        });

        volume_up_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume/10));
            codec->SetOutputVolume(volume);
            
        });

        volume_up_button_.OnLongPress([this]() {
            SwitchNetworkType();
            // GetAudioCodec()->SetOutputVolume(100);
            // GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });
        volume_up_button_.OnDoubleClick([this]() {
            auto camera = GetCamera();
            if (!camera->Capture()) {
                ESP_LOGE(TAG, "Camera capture failed");
            }
            std::string return_value = camera->Explain("翻译图片中内容");
            // 1. 解析 JSON 字符串
            cJSON* root = cJSON_Parse(return_value.c_str());
            if (root == nullptr) {
                // 解析失败，可能是纯文本格式，直接使用 return_value
                const char* text = return_value.c_str();
                ESP_LOGI(TAG, "解析结果: %s", text);
                return;
            }

            // 2. 提取 "text" 字段
            cJSON* text_node = cJSON_GetObjectItem(root, "text");
            if (text_node != nullptr && cJSON_IsString(text_node)) {
                const char* text = text_node->valuestring; // 得到 text 字段的值
                ESP_LOGI(TAG, "提取的 text: %s", text);
                display_->SetChatMessage("assistant", text);
            } else {
                // 若没有 "text" 字段，可能是其他结构，可根据实际格式调整
                ESP_LOGW(TAG, "未找到 text 字段，原始结果: %s", return_value.c_str());
            }

            // 3. 释放 cJSON 对象，避免内存泄漏
            cJSON_Delete(root);
        });
        volume_down_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume/10));
        });

        volume_down_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
    }

    void InitializeSt7789Display() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = GPIO_NUM_NC;
        io_config.dc_gpio_num = GPIO_NUM_39;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 80 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片ST7789
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
        
        esp_lcd_panel_reset(panel);
        pca9557_->SetOutputState(0, 0);

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, true);

        Settings settings("lcd_display", true);
        
        // 竖屏模式
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY_1);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X_1, DISPLAY_MIRROR_Y_1);
        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH_1, DISPLAY_HEIGHT_1, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X_1, DISPLAY_MIRROR_Y_1, DISPLAY_SWAP_XY_1);
       
        
    }

    void InitializeCamera() {
        // Open camera power
        pca9557_->SetOutputState(2, 0);
        vTaskDelay(pdMS_TO_TICKS(100)); // 等待电源稳定

        static esp_cam_ctlr_dvp_pin_config_t dvp_pin_config = {
            .data_width = CAM_CTLR_DATA_WIDTH_8,
            .data_io = {
                [0] = CAMERA_PIN_D0,
                [1] = CAMERA_PIN_D1,
                [2] = CAMERA_PIN_D2,
                [3] = CAMERA_PIN_D3,
                [4] = CAMERA_PIN_D4,
                [5] = CAMERA_PIN_D5,
                [6] = CAMERA_PIN_D6,
                [7] = CAMERA_PIN_D7,
            },
            .vsync_io = CAMERA_PIN_VSYNC,
            .de_io = CAMERA_PIN_HREF,
            .pclk_io = CAMERA_PIN_PCLK,
            .xclk_io = CAMERA_PIN_XCLK,
        };

        esp_video_init_sccb_config_t sccb_config = {
            .init_sccb = false,
            .i2c_handle = i2c_bus_,
            .freq = 100000,
        };

        esp_video_init_dvp_config_t dvp_config = {
            .sccb_config = sccb_config,
            .reset_pin = CAMERA_PIN_RESET,
            .pwdn_pin = CAMERA_PIN_PWDN,
            .dvp_pin = dvp_pin_config,
            .xclk_freq = XCLK_FREQ_HZ,
        };

        esp_video_init_config_t video_config = {
            .dvp = &dvp_config,
        };

        camera_ = new Esp32Camera(video_config);
        if (!camera_) {
            ESP_LOGE(TAG, "Camera instance is null");
        }
    }

	void InitializeController() { InitializeMCPController(); }

public:
    TranslationCamBoard_ML307() : 
	DualNetworkBoard(ML307_TX_PIN, ML307_RX_PIN),
    boot_button_(BOOT_BUTTON_GPIO,false,1000),
    volume_up_button_(VOLUME_UP_BUTTON_GPIO),
    volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        InitializeI2c();
        InitializeSpi();
        InitializeSt7789Display();
        InitializeButtons();
        InitializeCamera();
		InitializeController();
        GetBacklight()->RestoreBrightness();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static CustomAudioCodec audio_codec(
            i2c_bus_, 
            pca9557_);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging)  override {
        static bool last_discharging = false;
        charging = power_manager_->IsCharging();
        discharging = power_manager_->IsDischarging();
        if (discharging != last_discharging) {
            last_discharging = discharging;
        }
        level = std::max<uint32_t>(power_manager_->GetBatteryLevel(), 20);
        return true;
    }
    
    virtual bool GetTemperature(float& esp32temp)  override {
        esp32temp = power_manager_->GetTemperature();
        return true;
    }
    
    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual Camera* GetCamera() override {
        return camera_;
    }
};

DECLARE_BOARD(TranslationCamBoard_ML307);

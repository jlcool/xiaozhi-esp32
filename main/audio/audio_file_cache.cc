#include "audio_file_cache.h"
#include <esp_log.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include "esp_spiffs.h"

#define TAG "AudioFileCache"
#define CACHE_FILE "/cache/audio_cache.dat"

AudioFileCache& AudioFileCache::GetInstance() {
    static AudioFileCache instance;
    return instance;
}

AudioFileCache::AudioFileCache() {}

void AudioFileCache::Start() {
    if (queue_) return;

    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/cache",      // 这里的路径对应你 mkdir 的根目录
      .partition_label = "cache", // 必须对应分区表中的 Name
      .max_files = 5,             // 同时打开的最大文件数
      .format_if_mount_failed = true // 如果分区未格式化，自动格式化
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            printf("挂载失败：无法格式化或挂载文件系统\n");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            printf("挂载失败：找不到名为 cache 的分区\n");
        }
        return;
    }

    queue_ = xQueueCreate(14, sizeof(AudioStreamPacket*));

    if (!queue_ ) {
        ESP_LOGE(TAG, "queue create failed");
        return;
    }

    xTaskCreate(
        CacheTaskEntry,
        "audio_cache_ingress",
        4096,
        this,
        2,
        &cache_task_
    );
    StartWorker();
}

void AudioFileCache::StartWorker() {
    std::thread worker([this]() {
        while (!stop_worker_) {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]() { 
                return stop_worker_ || !write_queue_.empty(); 
            });
            if (stop_worker_) break;

            // 取出队列中的数据包并写入
            auto pkt = std::move(write_queue_.front());
            write_queue_.pop();
            lock.unlock();

            // 执行实际写入（包含 payload）
            int32_t sr = htonl(pkt.sample_rate);
            int32_t fd = htonl(pkt.frame_duration);
            uint32_t ts = htonl(pkt.timestamp);
            uint32_t sz = htonl(pkt.payload.size());

            std::lock_guard<std::mutex> file_lock(file_mutex_);
            fwrite(&sr, sizeof(sr), 1, write_fp_);
            fwrite(&fd, sizeof(fd), 1, write_fp_);
            fwrite(&ts, sizeof(ts), 1, write_fp_);
            fwrite(&sz, sizeof(sz), 1, write_fp_);
            fwrite(pkt.payload.data(), 1, pkt.payload.size(), write_fp_);

            // 优化刷新策略：每 50 包刷新一次，或定期刷新
            static int count = 0;
            if (++count >= 50) {
                fflush(write_fp_);
                count = 0;
            }
        }
    });
    worker.detach(); // 或用成员变量管理线程生命周期
}

void AudioFileCache::PushIncomingPacket(const std::unique_ptr<AudioStreamPacket>& pkt) {
    if (!queue_ || !pkt) return;

    // 深拷贝，payload 数据独立
    auto* copy = new AudioStreamPacket(*pkt);

    if (xQueueSend(queue_, &copy, 0) != pdPASS) {
        ESP_LOGW(TAG, "cache queue full, drop packet");
        delete copy;
    }
}

void AudioFileCache::CacheTaskEntry(void* arg) {
    static_cast<AudioFileCache*>(arg)->CacheTask();
}

void AudioFileCache::CacheTask() {

    ESP_LOGI(TAG, "audio cache task started");
    write_fp_ = fopen(CACHE_FILE, "wb");
    if (!write_fp_) {
        ESP_LOGE(TAG, "open cache file failed");
        vTaskDelete(nullptr);
        return;
    }
    while (true) {
        AudioStreamPacket* pkt = nullptr;
        if (xQueueReceive(queue_, &pkt, portMAX_DELAY) == pdPASS) {
            WritePacketToFile(*pkt);
            delete pkt;
        }
    }
}
bool AudioFileCache::WritePacketToFile(const AudioStreamPacket& pkt) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    // 避免队列堆积过多导致内存溢出，可设置最大长度
    if (write_queue_.size() < MAX_QUEUE_SIZE) {
        write_queue_.push(pkt);
        queue_cv_.notify_one();
        return true;
    } else {
        ESP_LOGW(TAG, "Write queue full, drop packet");
        return false;
    }
}

std::unique_ptr<AudioStreamPacket> AudioFileCache::ReadNextPacket() {
    std::lock_guard<std::mutex> lock(file_mutex_);

    if (!read_fp_) {
        read_fp_ = fopen(CACHE_FILE, "rb");
        if (!read_fp_) return nullptr;
    }

    int32_t sr, fd;
    uint32_t ts, sz;

    if (fread(&sr, sizeof(sr), 1, read_fp_) != 1) return nullptr;
    if (fread(&fd, sizeof(fd), 1, read_fp_) != 1) return nullptr;
    if (fread(&ts, sizeof(ts), 1, read_fp_) != 1) return nullptr;
    if (fread(&sz, sizeof(sz), 1, read_fp_) != 1) return nullptr;

    auto pkt = std::make_unique<AudioStreamPacket>();
    pkt->sample_rate = ntohl(sr);
    pkt->frame_duration = ntohl(fd);
    pkt->timestamp = ntohl(ts);

    sz = ntohl(sz);
    pkt->payload.resize(sz);

    if (fread(pkt->payload.data(), 1, sz, read_fp_) != sz) {
        return nullptr;
    }

    return pkt;
}

void AudioFileCache::ResetRead() {
    std::lock_guard<std::mutex> lock(file_mutex_);
    if (read_fp_) {
        fclose(read_fp_);
        read_fp_ = nullptr;
    }
}

void AudioFileCache::ResetWrite() {
    std::lock_guard<std::mutex> lock(file_mutex_);
    // 关闭旧写句柄
    if (write_fp_) {
        fclose(write_fp_);
        write_fp_ = nullptr;
    }

    pkt_write_count_ = 0; // 重置计数
    // 清空文件，准备新的一次语音
    write_fp_ = fopen(CACHE_FILE, "wb");
}

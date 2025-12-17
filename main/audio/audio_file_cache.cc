#include "audio_file_cache.h"
#include <esp_log.h>
#include <sys/stat.h>
#include <arpa/inet.h>

#define TAG "AudioFileCache"
#define CACHE_DIR  "/cache"
#define CACHE_FILE "/cache/audio_cache.dat"

AudioFileCache& AudioFileCache::GetInstance() {
    static AudioFileCache instance;
    return instance;
}

AudioFileCache::AudioFileCache() {}

void AudioFileCache::Start() {
    if (queue_) return;

    mkdir(CACHE_DIR, 0755);

    queue_ = xQueueCreate(8, sizeof(AudioStreamPacket*));
    if (!queue_) {
        ESP_LOGE(TAG, "queue create failed");
        return;
    }

    xTaskCreate(
        CacheTaskEntry,
        "audio_cache_task",
        4096,
        this,
        5,
        &task_
    );
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
    write_fp_ = fopen(CACHE_FILE, "wb");
    if (!write_fp_) {
        ESP_LOGE(TAG, "open cache file failed");
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "audio cache task started");

    while (true) {
        AudioStreamPacket* pkt = nullptr;
        if (xQueueReceive(queue_, &pkt, portMAX_DELAY) == pdPASS) {
            WritePacketToFile(*pkt);
            delete pkt;
        }
    }
}

bool AudioFileCache::WritePacketToFile(const AudioStreamPacket& pkt) {
    std::lock_guard<std::mutex> lock(file_mutex_);

    int32_t sr = htonl(pkt.sample_rate);
    int32_t fd = htonl(pkt.frame_duration);
    uint32_t ts = htonl(pkt.timestamp);
    uint32_t sz = htonl(pkt.payload.size());

    fwrite(&sr, sizeof(sr), 1, write_fp_);
    fwrite(&fd, sizeof(fd), 1, write_fp_);
    fwrite(&ts, sizeof(ts), 1, write_fp_);
    fwrite(&sz, sizeof(sz), 1, write_fp_);
    fwrite(pkt.payload.data(), 1, pkt.payload.size(), write_fp_);

    fflush(write_fp_);
    return true;
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

    // 清空文件，准备新的一次语音
    write_fp_ = fopen(CACHE_FILE, "wb");
}

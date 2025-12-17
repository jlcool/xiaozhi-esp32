#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <mutex>
#include <memory>
#include <vector>
#include "protocol.h"
class AudioFileCache {
public:
    static AudioFileCache& GetInstance();

    void Start();

    // OnIncomingAudio 调用
    void PushIncomingPacket(const std::unique_ptr<AudioStreamPacket>& pkt);

    // 播放缓存
    std::unique_ptr<AudioStreamPacket> ReadNextPacket();
    void ResetRead();
    void ResetWrite();

private:
    AudioFileCache();
    static void CacheTaskEntry(void* arg);
    void CacheTask();

    bool WritePacketToFile(const AudioStreamPacket& pkt);

private:
    QueueHandle_t queue_{nullptr};
    TaskHandle_t task_{nullptr};

    FILE* write_fp_{nullptr};
    FILE* read_fp_{nullptr};

    std::mutex file_mutex_;
};

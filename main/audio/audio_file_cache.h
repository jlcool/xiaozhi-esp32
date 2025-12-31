#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <mutex>
#include <memory>
#include <vector>
#include "protocol.h"
#include "freertos/ringbuf.h"
#include <condition_variable>
#include <queue>
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

    void StartWorker();
private:
    AudioFileCache();
    static void CacheTaskEntry(void* arg);
    void CacheTask();

    bool WritePacketToFile(const AudioStreamPacket& pkt);

private:
    int pkt_write_count_ = 0;
    QueueHandle_t queue_{nullptr};
    std::queue<AudioStreamPacket> write_queue_;
    TaskHandle_t cache_task_{nullptr};
    // TaskHandle_t writer_task_{nullptr};

    FILE* write_fp_{nullptr};
    FILE* read_fp_{nullptr};

    std::mutex file_mutex_;

    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    bool stop_worker_ = false;
    const int MAX_QUEUE_SIZE = 10;
};

#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <cstdint>
#include "protocol.h"
#include <freertos/FreeRTOS.h>
class AudioFileCache {
private:
    // 私有构造函数，防止外部实例化
    AudioFileCache();
    ~AudioFileCache() = default;

    // 禁止拷贝和赋值
    AudioFileCache(const AudioFileCache&) = delete;
    AudioFileCache& operator=(const AudioFileCache&) = delete;

    std::mutex cache_mutex_;
    std::vector<std::string> file_paths_;  // 存储最近3个文件路径
    const int MAX_CACHE_COUNT = 3;         // 最大缓存数量
    const std::string CACHE_DIR = "/cache/";  // 缓存目录

    // 确保缓存目录存在
    void EnsureCacheDir();
    // 生成唯一文件名
    std::string GenerateFileName();
    // 删除最旧的缓存文件
    void RemoveOldestFile();

    size_t read_offset_ = 0;  // 读取偏移量，用于顺序读取

public:
    QueueHandle_t g_audio_cache_queue = NULL;
    // 全局唯一实例获取接口
    static AudioFileCache& GetInstance() {
        static AudioFileCache instance;  // 局部静态变量，线程安全（C++11及以上）
        return instance;
    }

    // 保存音频数据到文件
    bool SaveAudio(const AudioStreamPacket& packet);
    // 获取最近的音频文件路径（index=0为最新）
    std::unique_ptr<AudioStreamPacket> GetRecentFile(int index = 0);
    // 检查是否有缓存
    bool HasCache() { 
        std::lock_guard<std::mutex> lock(cache_mutex_);
        return !file_paths_.empty(); 
    }
    QueueHandle_t GetQueue() const {
        return g_audio_cache_queue;
    }

    // 清空缓存
    void ClearCache();
    void SaveAudioTask();
};
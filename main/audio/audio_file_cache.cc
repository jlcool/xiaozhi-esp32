#include "audio_file_cache.h"
#include <esp_vfs.h>
#include <esp_spiffs.h>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <sstream>
#include "protocol.h"
#include <esp_log.h>
#include <arpa/inet.h>
#include "esp_vfs.h"
#include <sys/stat.h>
namespace fs = std::filesystem;
#define TAG "AudioFileCache"


AudioFileCache::AudioFileCache() {
    // 创建音频缓存队列（最大缓存 5 个包）
    g_audio_cache_queue = xQueueCreate(5, sizeof(AudioStreamPacket));
    if (g_audio_cache_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create audio cache queue");
    }

    EnsureCacheDir();
    // 初始化时清理残留文件
    ClearCache();
}
 

void AudioFileCache::EnsureCacheDir() {
    struct stat st;
    if (stat("/cache/audio", &st) != 0) {
        mkdir("/cache/audio", 0755);
    }
}

std::string AudioFileCache::GenerateFileName() {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();
    return CACHE_DIR + "voice_" + std::to_string(timestamp) + ".opus";
}

void AudioFileCache::RemoveOldestFile() {
    if (file_paths_.empty()) return;
    std::string oldest = file_paths_.front();
    if (fs::exists(oldest)) {
        fs::remove(oldest);
    }
    file_paths_.erase(file_paths_.begin());
}
void AudioService::SaveAudioTask() {
    AudioStreamPacket packet;
    ESP_LOGI(TAG, "Audio cache task start");

    // 任务主循环：持续从队列取数据写文件
    while (1) {
        // 阻塞等待队列数据（队列为空时挂起，不占用CPU）
        BaseType_t ret = xQueueReceive(
            g_audio_cache_queue,   // 队列句柄
            &packet,               // 接收数据的缓冲区
            portMAX_DELAY          // 永久等待，直到有数据
        );

        if (ret == pdPASS) {
            // 执行异步写文件（耗时操作，不阻塞音频回调）
            AudioFileCache::GetInstance().SaveAudio(packet);
        }

        // 主动让出CPU，提升系统响应性（ESP32推荐）
        taskYIELD();
    }

    // 理论上不会走到这里，如需退出可加退出逻辑
    vTaskDelete(NULL);

}
bool AudioFileCache::SaveAudio(const AudioStreamPacket& packet) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    EnsureCacheDir();

    // 超过最大数量时删除最旧文件
    if (file_paths_.size() >= MAX_CACHE_COUNT) {
        RemoveOldestFile();
    }

    // 生成新文件路径
    std::string file_path = GenerateFileName();

    // 打开目标文件（二进制写入模式）
    FILE* f = fopen(file_path.c_str(), "wb");
    if (!f) {
        ESP_LOGE("AUDIO_FILE_CACHE", "Failed to open file for writing: %s", file_path.c_str());
        return false;
    }

    // 序列化 packet 并写入文件
    // 格式：[sample_rate][frame_duration][timestamp][payload_size][payload]
    // 注意：使用 htonl 进行大小端转换，保证跨平台兼容性
    int32_t sample_rate = htonl(packet.sample_rate);
    int32_t frame_duration = htonl(packet.frame_duration);
    uint32_t timestamp = htonl(packet.timestamp);
    uint32_t payload_size = htonl(packet.payload.size());

    // 依次写入头部信息和 payload
    fwrite(&sample_rate, sizeof(sample_rate), 1, f);
    fwrite(&frame_duration, sizeof(frame_duration), 1, f);
    fwrite(&timestamp, sizeof(timestamp), 1, f);
    fwrite(&payload_size, sizeof(payload_size), 1, f);
    fwrite(packet.payload.data(), 1, packet.payload.size(), f);

    // 关闭文件
    fclose(f);

    // 将新文件路径添加到列表（最新的放最后）
    file_paths_.push_back(file_path);
    return true;
}

std::unique_ptr<AudioStreamPacket> AudioFileCache::GetRecentFile(int index) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    if (index < 0 || index >= file_paths_.size()) {
        return nullptr;
    }

    FILE* f = fopen(file_paths_[file_paths_.size() - 1 - index].c_str(), "rb");
    if (!f) return nullptr;

    // 移动到上次读取位置（需维护文件指针偏移量）
    fseek(f, read_offset_, SEEK_SET);

    // 读取基本信息
    int32_t sample_rate, frame_duration;
    uint32_t timestamp, payload_size;
    if (fread(&sample_rate, sizeof(sample_rate), 1, f) != 1) goto end;
    if (fread(&frame_duration, sizeof(frame_duration), 1, f) != 1) goto end;
    if (fread(&timestamp, sizeof(timestamp), 1, f) != 1) goto end;
    if (fread(&payload_size, sizeof(payload_size), 1, f) != 1) goto end;

    // 转换回主机字节序
    sample_rate = ntohl(sample_rate);
    frame_duration = ntohl(frame_duration);
    timestamp = ntohl(timestamp);
    payload_size = ntohl(payload_size);
    {
        // 读取 payload
        std::vector<uint8_t> payload(payload_size);
        if (fread(payload.data(), 1, payload_size, f) != payload_size) goto end;

        // 更新读取偏移量
        read_offset_ = ftell(f);

        fclose(f);
        return std::make_unique<AudioStreamPacket>(AudioStreamPacket{
            .sample_rate = sample_rate,
            .frame_duration = frame_duration,
            .timestamp = timestamp,
            .payload = payload
        });
    }
    end:
        fclose(f);  // 确保文件被关闭，避免资源泄漏
        return nullptr;
}

void AudioFileCache::ClearCache() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    for (const auto& path : file_paths_) {
        if (fs::exists(path)) {
            fs::remove(path);
        }
    }
    file_paths_.clear();
}
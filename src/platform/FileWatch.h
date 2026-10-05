#pragma once

#include <filesystem>
#include <string>

class FileWatch
{
public:
    FileWatch() = default;
    explicit FileWatch(std::string path);

    // 开始监视 path 并将它的当前时间戳作为基准
    // ConsumeChanged() 会报告从现在开始的更改
    void Reset(std::string path);

    // 检测到变化时返回 true。如果监视的文件消失，也返回 true。
    bool ConsumeChanged();

private:
    std::string m_path;
    std::filesystem::file_time_type m_lastWriteTime{};
    bool m_hasLastWriteTime = false;
};

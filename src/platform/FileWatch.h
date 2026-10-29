#pragma once

#include <filesystem>
#include <string>

class FileWatch
{
public:
    FileWatch() = default;
    explicit FileWatch(std::string path);

    void Reset(std::string path);

    bool ConsumeChanged();

private:
    std::string m_path;
    std::filesystem::file_time_type m_lastWriteTime{};
    bool m_hasLastWriteTime = false;
};

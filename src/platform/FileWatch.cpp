#include "platform/FileWatch.h"

#include <system_error>
#include <utility>

FileWatch::FileWatch(std::string path)
{
    Reset(std::move(path));
}

void FileWatch::Reset(std::string path)
{
    m_path = std::move(path);

    std::error_code error;
    m_lastWriteTime = std::filesystem::last_write_time(m_path, error);
    m_hasLastWriteTime = !error;
}

bool FileWatch::ConsumeChanged()
{
    if (m_path.empty()) {
        return false;
    }

    std::error_code error;
    const std::filesystem::file_time_type writeTime =
        std::filesystem::last_write_time(m_path, error);

    if (error) {
        // 文件丢失或无法读取。只报告一次 —— 否则一个被删除的资源在每次轮询时都会报告变化，而日志会成为唯一有人注意到的东西。
        const bool wasKnown = m_hasLastWriteTime;
        m_hasLastWriteTime = false;
        return wasKnown;
    }

    if (!m_hasLastWriteTime || writeTime != m_lastWriteTime) {
        m_lastWriteTime = writeTime;
        m_hasLastWriteTime = true;
        return true;
    }

    return false;
}

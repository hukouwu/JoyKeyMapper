#pragma once

#include <string>
#include <cctype>
#include <vector>

#include <fstream>
#include <filesystem>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <chrono>
#include <format>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <source_location>


template<typename... Args>
consteval void check_format_string(const char* fmt) {
    int count = 0;
    for (const char* p = fmt; *p; ++p) {
        if (*p == '{' && *(p + 1) == '}') {
            ++count;
            ++p;
        }
    }
    if (count != sizeof...(Args)) {
        throw "Format string placeholder count mismatch!";
    }
}


class SimpleLogger {
public:
    enum class LogLevel { M_DEBUG, M_INFO, M_WARM, M_ERROR };
    friend bool operator<(LogLevel lhs, LogLevel rhs) {
        return static_cast<int>(lhs) < static_cast<int>(rhs);
    }

    static SimpleLogger& GetInstance() {
        static SimpleLogger instance;
        return instance;
    }

    inline static LogLevel m_level = LogLevel::M_INFO;

    template<typename... Args>
    static void LOG_DEBUG(const std::string fmt, Args&&... args) {
        if (LogLevel::M_DEBUG < m_level) return;
        GetInstance().log(LogLevel::M_DEBUG, fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    static void LOG_INFO(const std::string fmt, Args&&... args) {
        if (LogLevel::M_INFO < m_level) return;
        GetInstance().log(LogLevel::M_INFO, fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    static void LOG_WARM(const std::string fmt, Args&&... args) {
        if (LogLevel::M_WARM < m_level) return;
        GetInstance().log(LogLevel::M_WARM, fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    static void LOG_ERROR(const std::string fmt, Args&&... args) {
        if (LogLevel::M_ERROR < m_level) return;
        GetInstance().log(LogLevel::M_ERROR, fmt, std::forward<Args>(args)...);
    }

    void init(const std::string& filename) {
        std::lock_guard<std::mutex> lock(m_mutex);

        std::filesystem::path filePath(filename);
        std::filesystem::path directory = filePath.parent_path();

        if (!directory.empty() && !std::filesystem::exists(directory)) {
            try {
                std::filesystem::create_directories(directory);
            }
            catch (const std::exception& e) {
                OutputDebugStringA(("Failed to create directory: " + directory.string() + "\n").c_str());
                return;
            }
        }

        m_ofs.open(filename, std::ios::out | std::ios::trunc);
        m_stop = false;
        m_thread = std::thread(&SimpleLogger::processQueue, this);
    }

    template<typename... Args>
    void log(LogLevel level, const std::string fmt, Args&&... args) {
        //check_format_string<Args...>(fmt);

        auto tuple = std::make_tuple(std::forward<Args>(args)...);
        auto task = [fmt, tuple = std::move(tuple)]() mutable {
            try {
                return std::apply([fmt](const auto&... unpacked_args) {
                    return std::vformat(fmt, std::make_format_args(unpacked_args...));
                    }, tuple);
            }
            catch (const std::exception& e) {
                return std::string("Log Format Error: ") + e.what();
            }
        };

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.emplace(LogEntry{ 
                level,
                fmt,
                std::chrono::system_clock::now(),
                std::move(task)
            });
        }
        m_cv.notify_one();
    }

    void setLevle(LogLevel level) {
        m_level = level;
    }

    ~SimpleLogger() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = true;
        }
        m_cv.notify_all();

        if (m_thread.joinable()) {
            m_thread.join();
        }

        if (m_ofs.is_open()) {
            m_ofs.flush();
            m_ofs.close();
        }
    }

private:
    struct LogEntry {
        LogLevel level;
        std::string fmt;
        std::chrono::system_clock::time_point timestamp;
        std::function<std::string()> fmtTask;
    };

    void processQueue() {
        auto last_flush_time = std::chrono::steady_clock::now();
        size_t bytes_since_flush = 0;
        const auto FLUSH_INTERVAL = std::chrono::seconds(10);
        const size_t FLUSH_BYTE_THRESHOLD = 1024;            // 1KB

        while (true) {
            LogEntry msgEntry;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait_for(lock, std::chrono::seconds(3), [this] {
                    return m_stop || !m_queue.empty();
                });

                if (m_stop && m_queue.empty()) {
                    break;
                }

            }

            while (!m_queue.empty()) {
                msgEntry = std::move(m_queue.front());
                m_queue.pop();

                std::function<std::string()> fmtTask = msgEntry.fmtTask;
                if (!fmtTask) {
                    continue;
                }
                std::string fmtMsg = fmtTask();

                auto now_sec = std::chrono::time_point_cast<std::chrono::seconds>(msgEntry.timestamp);
                std::time_t now_c = std::chrono::system_clock::to_time_t(now_sec);
                std::tm tm_now{};
                localtime_s(&tm_now, &now_c);
                auto micros = std::chrono::duration_cast<std::chrono::microseconds>(msgEntry.timestamp - now_sec).count();
                char time_buf[32] = { 0 };
                std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_now);

                const char* level_str = "";
                switch (msgEntry.level) {
                case LogLevel::M_DEBUG:
                    level_str = "DEBUG";
                    break;
                case LogLevel::M_INFO:
                    level_str = "INFO";
                    break;
                case LogLevel::M_WARM:
                    level_str = "WARN";
                    break;
                case LogLevel::M_ERROR:
                    level_str = "ERROR";
                    break;
                default:
                    break;
                }

                char log_buf[512] = {0};
                int written = snprintf(log_buf, sizeof(log_buf),
                    "[%s.%06lld] [%s] %s\n",
                    time_buf, static_cast<long long>(micros),
                    level_str,
                    fmtMsg.c_str()
                );

                if (written > 0) {
                    OutputDebugStringA(log_buf);
                    m_ofs.write(log_buf, written);
                    bytes_since_flush += written;
                }
            }
            auto now = std::chrono::steady_clock::now();
            bool time_expired = (now - last_flush_time) >= FLUSH_INTERVAL;
            bool size_expired = bytes_since_flush >= FLUSH_BYTE_THRESHOLD;

            if (time_expired || size_expired) {
                m_ofs.flush();
                last_flush_time = now;      // 重置时间
                bytes_since_flush = 0;      // 重置计数
            }

        }
    }

    std::ofstream m_ofs;
    std::queue<LogEntry> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_thread;
    std::atomic<bool> m_stop{ false };
    SimpleLogger() = default;
};
/*
#define LOG_INFO(fmt, ...) \
    SimpleLogger::GetInstance().log(SimpleLogger::LogLevel::M_INFO, fmt __VA_OPT__(, ) __VA_ARGS__)

#define LOG_ERROR(fmt, ...) \
    SimpleLogger::GetInstance().log(SimpleLogger::LogLevel::M_ERROR, fmt __VA_OPT__(, ) __VA_ARGS__)
*/
inline std::string myreplaceAll(const std::string& str, const std::string& from, const std::string& to) {
    if (from.empty()) return str;

    std::string result;
    result.reserve(str.size()); // 预分配内存，提高性能

    size_t start_pos = 0;
    size_t pos;

    while ((pos = str.find(from, start_pos)) != std::string::npos) {
        result.append(str, start_pos, pos - start_pos);
        result.append(to);
        start_pos = pos + from.length();
    }

    result.append(str, start_pos, std::string::npos);

    return result;
}
inline std::vector<std::string> mysplit(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

inline std::string WStringToUTF8(const std::wstring& wstr) {
    if (wstr.empty()) return "";

    // 计算所需的缓冲区大小
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    if (size_needed <= 0) return "";

    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);

    return strTo;
}

inline std::wstring UTF8ToWString(const std::string& str) {
    if (str.empty()) return L"";

    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    if (size_needed <= 0) return L"";

    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);

    std::wstring result;
    result.reserve(wstrTo.size());
    for (size_t i = 0; i < wstrTo.size(); ++i) {
        if (wstrTo[i] == L'\n') {
            if (i == 0 || wstrTo[i - 1] != L'\r') {
                result += L'\r';
            }
        }
        result += wstrTo[i];
    }

    return result;
}


inline std::tuple<std::string, std::string> parseKeyAndValue(const std::string& line) {
    std::string key;
    std::string value;

    size_t equalPos = line.find('=');
    if (equalPos != std::string::npos) {
        key = line.substr(0, equalPos);
        value = line.substr(equalPos + 1);
    }

    return std::make_tuple(key, value);
}


inline bool isPositiveInteger(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

static GUID stringToGuid(const char* uuidStr) {
    GUID guid = { 0 };
    if (strlen(uuidStr) != 36) return guid;

    unsigned char d4[8] = { 0 };
    sscanf(uuidStr,
        "%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX",
        &guid.Data1, &guid.Data2, &guid.Data3,
        &d4[0], &d4[1], &d4[2], &d4[3], &d4[4], &d4[5], &d4[6], &d4[7]);
    memcpy(guid.Data4, d4, 8);
    return guid;
}

static std::string mapToString(const std::map<std::string, std::string>& tmap) {
    if (tmap.empty()) return "";

    size_t totalSize = 0;
    for (const auto& pair : tmap) {
        totalSize += pair.first.size() + pair.second.size() + 4;
    }

    std::vector<char> buffer;
    buffer.reserve(totalSize);

    for (const auto& pair : tmap) {
        buffer.insert(buffer.end(), pair.first.begin(), pair.first.end());
        buffer.push_back(':');

        buffer.insert(buffer.end(), pair.second.begin(), pair.second.end());
        buffer.push_back(',');
        buffer.push_back(' ');
    }

    if (!buffer.empty()) {
        while (!buffer.empty() && (buffer.back() == ',' || buffer.back() == ' ')) {
            buffer.pop_back();
        }
        buffer.push_back('\0');
    }
    return std::string(buffer.data());
}
static std::string mapToString(const std::map<std::string, int>& tmap) {
    if (tmap.empty()) return "";

    size_t totalSize = 0;
    for (const auto& pair : tmap) {
        std::string valStr = std::to_string(pair.second);
        totalSize += pair.first.size() + valStr.size() + 4;
    }

    std::vector<char> buffer;
    buffer.reserve(totalSize);

    for (const auto& pair : tmap) {
        buffer.insert(buffer.end(), pair.first.begin(), pair.first.end());
        buffer.push_back(':');

        std::string valStr = std::to_string(pair.second);
        buffer.insert(buffer.end(), valStr.begin(), valStr.end());
        buffer.push_back(',');
        buffer.push_back(' ');
    }

    if (!buffer.empty()) {
        while (!buffer.empty() && (buffer.back() == ',' || buffer.back() == ' ')) {
            buffer.pop_back();
        }
        buffer.push_back('\0');
    }
    return std::string(buffer.data());
}

static std::string myjoin(const std::vector<std::string>& vec, const std::string& delim) {
    std::string ret;
    for (auto& str : vec) {
        if (!ret.empty()) {
            ret += delim;
        }
        ret += str;
    }
    return ret;
}

static std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}
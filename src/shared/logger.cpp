#include "logger.hpp"
#include <windows.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <string>
#include <vector>
#include <cstdarg>
#include <cwchar>
#include <atomic>
#include "config.hpp"

namespace vn_ime::logger {

static bool DetectSecureDesktop() {
    HDESK hInputDesktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (hInputDesktop == nullptr) {
        return true; 
    }

    wchar_t szInputDesktopName[256] = { 0 };
    DWORD dwNeeded = 0;
    if (!GetUserObjectInformationW(hInputDesktop, UOI_NAME, szInputDesktopName, sizeof(szInputDesktopName), &dwNeeded)) {
        CloseDesktop(hInputDesktop);
        return true;
    }
    CloseDesktop(hInputDesktop);

    HDESK hThreadDesktop = GetThreadDesktop(GetCurrentThreadId());
    if (hThreadDesktop == nullptr) {
        return true;
    }
    wchar_t szThreadDesktopName[256] = { 0 };
    if (!GetUserObjectInformationW(hThreadDesktop, UOI_NAME, szThreadDesktopName, sizeof(szThreadDesktopName), &dwNeeded)) {
        return true;
    }

    if (_wcsicmp(szInputDesktopName, szThreadDesktopName) != 0) {
        return true;
    }
    if (_wcsicmp(szInputDesktopName, L"Winlogon") == 0 || 
        _wcsicmp(szInputDesktopName, L"Screen-saver") == 0) {
        return true;
    }
    if (_wcsicmp(szThreadDesktopName, L"Winlogon") == 0 || 
        _wcsicmp(szThreadDesktopName, L"Screen-saver") == 0) {
        return true;
    }

    return false;
}

static bool DetectAppContainer() {
    HANDLE hToken = nullptr;
    DWORD isAppContainer = 0;
    DWORD dwSize = 0;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        if (GetTokenInformation(hToken, TokenIsAppContainer, &isAppContainer, sizeof(isAppContainer), &dwSize)) {
            CloseHandle(hToken);
            return (isAppContainer != 0);
        }
        CloseHandle(hToken);
    }
    return false;
}

static bool g_is_secure_desktop = false;
static bool g_is_app_container = false;
static bool g_detected = false;
static std::atomic<bool> g_log_enabled{false};
inline constexpr LONGLONG MAX_LOG_FILE_BYTES = 100LL * 1024LL * 1024LL;

static void EnsureDetection() {
    if (!g_detected) {
        g_is_secure_desktop = DetectSecureDesktop();
        g_is_app_container = DetectAppContainer();
        g_detected = true;
    }
}

bool IsSecureDesktop() {
    EnsureDetection();
    return g_is_secure_desktop;
}

bool IsAppContainer() {
    EnsureDetection();
    return g_is_app_container;
}

void SetEnabled(bool enabled) {
    g_log_enabled = enabled;
}

bool IsEnabled() {
    return g_log_enabled && !IsSecureDesktop();
}

struct LogEntry {
    DWORD pid;
    DWORD tid;
    SYSTEMTIME time;
    Level level;
    std::wstring message;
};

class AsyncLogger {
public:
    AsyncLogger() = default;
    ~AsyncLogger() {
        Shutdown();
    }

    void Initialize() {
        std::lock_guard<std::mutex> lock(mutex_);
        InitializeInternal();
    }

    void InitializeInternal() {
        if (initialized_) return;
        
        // Load initial state from registry
        g_log_enabled = LoadConfigFromRegistry().enable_log;

        if (IsSecureDesktop()) return; // Disable file logs/threads on Secure Desktop
        if (!g_log_enabled) return;   // Skip initialization if logging is disabled

        // Get temp path
        wchar_t temp_path[MAX_PATH];
        DWORD len = GetTempPathW(MAX_PATH, temp_path);
        if (len > 0 && len < MAX_PATH) {
            log_path_ = temp_path;
            log_path_ += L"neokey.log";
        } else {
            log_path_ = L"C:\\Temp\\neokey.log"; // Fallback
        }

        should_exit_ = false;
        worker_thread_ = std::jthread([this](std::stop_token st) {
            WorkerLoop(st);
        });

        initialized_ = true;
    }

    void Shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_) return;
            should_exit_ = true;
        }
        cv_.notify_all();
        
        // jthread will join on destruction or when stop is requested
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }

        std::lock_guard<std::mutex> lock(mutex_);
        initialized_ = false;
    }

    void Enqueue(Level level, std::wstring_view message) {
        if (!IsEnabled()) return;

        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_ && !should_exit_) {
            InitializeInternal();
        }
        if (should_exit_ || !initialized_) return;

        LogEntry entry;
        entry.pid = GetCurrentProcessId();
        entry.tid = GetCurrentThreadId();
        GetLocalTime(&entry.time);
        entry.level = level;
        entry.message = message;

        queue_.push(std::move(entry));
        cv_.notify_one();
    }

private:
    const wchar_t* GetLevelString(Level level) {
        switch (level) {
            case Level::Debug:   return L"DEBUG";
            case Level::Info:    return L"INFO";
            case Level::Warning: return L"WARN";
            case Level::Error:   return L"ERROR";
        }
        return L"UNKNOWN";
    }

    void WorkerLoop(std::stop_token st) {
        while (!st.stop_requested()) {
            std::vector<LogEntry> local_entries;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this, &st] {
                    return !queue_.empty() || should_exit_ || st.stop_requested();
                });

                if (queue_.empty() && (should_exit_ || st.stop_requested())) {
                    break;
                }

                while (!queue_.empty()) {
                    local_entries.push_back(std::move(queue_.front()));
                    queue_.pop();
                }
            }

            if (!local_entries.empty()) {
                WriteToFileAndDebug(local_entries);
            }
        }

        // Write remaining entries on exit
        std::vector<LogEntry> remaining;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            while (!queue_.empty()) {
                remaining.push_back(std::move(queue_.front()));
                queue_.pop();
            }
        }
        if (!remaining.empty()) {
            WriteToFileAndDebug(remaining);
        }
    }

    void WriteToFileAndDebug(const std::vector<LogEntry>& entries) {
        bool skip_file = IsAppContainer();
        HANDLE file = INVALID_HANDLE_VALUE;

        if (!skip_file) {
            RotateLogIfTooLarge();
            file = CreateFileW(
                log_path_.c_str(),
                FILE_APPEND_DATA,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                nullptr
            );
        }

        for (const auto& entry : entries) {
            wchar_t time_str[64];
            swprintf_s(time_str, L"%04d-%02d-%02d %02d:%02d:%02d.%03d",
                entry.time.wYear, entry.time.wMonth, entry.time.wDay,
                entry.time.wHour, entry.time.wMinute, entry.time.wSecond,
                entry.time.wMilliseconds);

            std::wstring line = std::wstring(L"[") + time_str + L"] [" +
                                std::to_wstring(entry.pid) + L":" + std::to_wstring(entry.tid) + L"] [" +
                                GetLevelString(entry.level) + L"] " +
                                entry.message + L"\r\n";

            OutputDebugStringW(line.c_str());

            if (file != INVALID_HANDLE_VALUE) {
                int utf8_len = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, nullptr, 0, nullptr, nullptr);
                if (utf8_len > 0) {
                    std::vector<char> utf8_buf(utf8_len);
                    WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, utf8_buf.data(), utf8_len, nullptr, nullptr);
                    
                    DWORD bytes_written;
                    // utf8_len includes null terminator, we don't want to write it
                    WriteFile(file, utf8_buf.data(), utf8_len - 1, &bytes_written, nullptr);
                }
            }
        }

        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
    }

    void RotateLogIfTooLarge() {
        if (log_path_.empty()) return;

        WIN32_FILE_ATTRIBUTE_DATA attrs;
        if (!GetFileAttributesExW(log_path_.c_str(), GetFileExInfoStandard, &attrs)) {
            return;
        }

        LARGE_INTEGER size;
        size.HighPart = static_cast<LONG>(attrs.nFileSizeHigh);
        size.LowPart = attrs.nFileSizeLow;
        if (size.QuadPart <= MAX_LOG_FILE_BYTES) {
            return;
        }

        if (!DeleteFileW(log_path_.c_str())) {
            OutputDebugStringW(L"Neokey logger: log file exceeded 100MB but could not be deleted.\r\n");
        }
    }

    std::wstring log_path_;
    std::queue<LogEntry> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::jthread worker_thread_;
    bool initialized_ = false;
    bool should_exit_ = false;
};

static AsyncLogger g_logger;

void Initialize() {
    g_logger.Initialize();
}

void Shutdown() {
    g_logger.Shutdown();
}

void Log(Level level, std::wstring_view message) {
    if (!IsEnabled()) return;
    g_logger.Enqueue(level, message);
}

void LogFormat(Level level, const wchar_t* format, ...) {
    if (!IsEnabled()) return;

    wchar_t buffer[1024];
    va_list args;
    va_start(args, format);
    int len = vswprintf_s(buffer, format, args);
    va_end(args);

    if (len >= 0) {
        Log(level, buffer);
    } else {
        Log(Level::Error, L"Failed to format log message.");
    }
}

} // namespace vn_ime::logger

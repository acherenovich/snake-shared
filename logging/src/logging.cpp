#include "logging.hpp"
#include <iostream>
#include <format>

namespace Utils::Logging {

    // -------- ANSI COLORS (все жирные) ------------
    static constexpr std::string_view COLOR_RESET       = "\033[0m";
    static constexpr std::string_view COLOR_GRAY        = "\033[1;90m";
    static constexpr std::string_view COLOR_BOLD_WHITE  = "\033[1;97m";
    static constexpr std::string_view COLOR_PURPLE      = "\033[1;95m";

    static constexpr std::string_view COLOR_DEBUG       = "\033[1;94m";
    static constexpr std::string_view COLOR_INFO        = "\033[1;92m";
    static constexpr std::string_view COLOR_WARNING     = "\033[1;93m";
    static constexpr std::string_view COLOR_ERROR       = "\033[1;91m";
    static constexpr std::string_view COLOR_FATAL       = "\033[1;91m";

    // -------- LoggerImpl ---------------

    LoggerImpl::LoggerImpl(const std::string& prefix,
                           const std::shared_ptr<LoggerImpl>& parent)
        : prefix_(prefix)
        , parent_(parent)
    {
    }

    void LoggerImpl::SetPrefix(const std::string& prefix)
    {
        prefix_ = prefix;
    }

    Logger::Shared LoggerImpl::CreateChild(const std::string& prefix)
    {
        const auto self = shared_from_this();
        return std::make_shared<LoggerImpl>(prefix, self);
    }

    std::string_view LoggerImpl::LevelToString(const Level level)
    {
        switch (level)
        {
        case Level::Msg:     return "INFO";
        case Level::Debug:   return "DEBUG";
        case Level::Warning: return "WARNING";
        case Level::Error:   return "ERROR";
        case Level::Fatal:   return "FATAL";
        }
        return "UNK";
    }

    std::string_view LoggerImpl::LevelColor(const Level level)
    {
        switch (level)
        {
        case Level::Msg:     return COLOR_INFO;
        case Level::Debug:   return COLOR_DEBUG;
        case Level::Warning: return COLOR_WARNING;
        case Level::Error:   return COLOR_ERROR;
        case Level::Fatal:   return COLOR_FATAL;
        }
        return COLOR_GRAY;
    }

    std::string LoggerImpl::CurrentTimeString()
    {
        using namespace std::chrono;

        const auto now = system_clock::now();
        const auto timeT = system_clock::to_time_t(now);

        std::tm tm{};
    #if defined(_WIN32)
        localtime_s(&tm, &timeT);
    #else
        localtime_r(&timeT, &tm);
    #endif

        return std::format("{:02}:{:02}:{:02}", tm.tm_hour, tm.tm_min, tm.tm_sec);
    }

    std::string LoggerImpl::BuildPrefixChain() const
    {
        std::string result;

        if (const auto parent = parent_.lock())
            result = parent->BuildPrefixChain();

        if (!prefix_.empty())
        {
            if (!result.empty())
                result += ' ';

            result += std::format("{}[{}]{}", COLOR_BOLD_WHITE, prefix_, COLOR_RESET);
        }

        return result;
    }

    void LoggerImpl::Log(const Level level, const std::string_view message)
    {
        const std::string timeStr = CurrentTimeString();
        const std::string_view levelName = LevelToString(level);

        const int width = 7;
        const int len = static_cast<int>(levelName.size());
        const int left = (width - len) / 2;
        const int right = width - len - left;

        const std::string levelPadded = std::format(
            "{}{}{}",
            std::string(left, ' '),
            levelName,
            std::string(right, ' ')
        );

        const std::string prefixChain = BuildPrefixChain();
        const std::string prefixPart = prefixChain.empty()
            ? std::string{}
            : std::format("{} ", prefixChain);

        const std::string_view textColor =
            (level == Level::Error || level == Level::Fatal)
            ? COLOR_ERROR
            : COLOR_GRAY;

        const std::string_view bulletColor =
            (level == Level::Error || level == Level::Fatal)
            ? COLOR_ERROR
            : COLOR_GRAY;

        const std::string finalMessage = std::format(
            "{}[{}]{} {}[{}]{}  {}•{}  {}{}{}{}",
            COLOR_PURPLE, timeStr, COLOR_RESET,
            LevelColor(level), levelPadded, COLOR_RESET,
            bulletColor, COLOR_RESET,
            prefixPart,
            textColor,
            message,
            COLOR_RESET
        );

        std::lock_guard lock(globalMutex_);

        std::ostream& stream = std::cout;
        stream << finalMessage << '\n';
        stream.flush();

        if (level == Level::Fatal)
            std::abort();
    }

    Logger::Shared Logger::Create(const std::string& name)
    {
        return std::make_shared<LoggerImpl>(name, nullptr);
    }

} // namespace Utils::Logging

// ======== Глобальный дефолтный логгер Utils::Log() =========

namespace {

    // Статический дефолтный логгер для всего приложения.
    Utils::Logging::Logger::Shared gDefaultLogger =
        Utils::Logging::Logger::Create("CORE");

} // namespace

namespace Utils {

    Utils::Logging::Logger::Shared& Log()
    {
        return gDefaultLogger;
    }

    void SetDefaultLogger(const Utils::Logging::Logger::Shared& logger)
    {
        gDefaultLogger = logger;
    }

} // namespace Utils
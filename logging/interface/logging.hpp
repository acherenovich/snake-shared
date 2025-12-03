#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <format>

namespace Utils::Logging {

    enum class Level
    {
        Msg,
        Debug,
        Warning,
        Error,
        Fatal
    };

    class Logger
    {
    public:
        using Shared = std::shared_ptr<Logger>;

        virtual ~Logger() = default;

        virtual void SetPrefix(const std::string& prefix) = 0;
        virtual Shared CreateChild(const std::string& prefix) = 0;
        virtual void Log(Level level, std::string_view message) = 0;

        template<typename... Args>
        void Msg(std::string_view fmt, const Args&... args)
        {
            Log(Level::Msg, std::vformat(fmt, std::make_format_args(args...)));
        }

        template<typename... Args>
        void Debug(std::string_view fmt, const Args&... args)
        {
            Log(Level::Debug, std::vformat(fmt, std::make_format_args(args...)));
        }

        template<typename... Args>
        void Warning(std::string_view fmt, const Args&... args)
        {
            Log(Level::Warning, std::vformat(fmt, std::make_format_args(args...)));
        }

        template<typename... Args>
        void Error(std::string_view fmt, const Args&... args)
        {
            Log(Level::Error, std::vformat(fmt, std::make_format_args(args...)));
        }

        template<typename... Args>
        void Fatal(std::string_view fmt, const Args&... args)
        {
            Log(Level::Fatal, std::vformat(fmt, std::make_format_args(args...)));
        }

        static Shared Create(const std::string& name);
    };

} // namespace Utils::Logging

// ======== Глобальный дефолтный логгер Utils::Log() =========

namespace Utils {

    /// Дефолтный логгер приложения.
    /// По умолчанию создаётся как Logger::Create("CORE").
    /// Можно переопределить через SetDefaultLogger(...).
    [[nodiscard]] Utils::Logging::Logger::Shared& Log();

    /// Установить новый дефолтный логгер.
    void SetDefaultLogger(const Utils::Logging::Logger::Shared& logger);

} // namespace Utils
#pragma once

#include "../interface/logging.hpp"
#include <mutex>
#include <chrono>

namespace Utils::Logging {

    class LoggerImpl final :
        public Logger,
        public std::enable_shared_from_this<LoggerImpl>
    {
    public:
        LoggerImpl(const std::string& prefix,
                   const std::shared_ptr<LoggerImpl>& parent);

        void SetPrefix(const std::string& prefix) override;
        Shared CreateChild(const std::string& prefix) override;

        void Log(const Level level, const std::string_view message) override;

    private:
        [[nodiscard]] std::string BuildPrefixChain() const;
        [[nodiscard]] static std::string CurrentTimeString();
        [[nodiscard]] static std::string_view LevelToString(const Level level);
        [[nodiscard]] static std::string_view LevelColor(const Level level);

    private:
        std::string prefix_;
        std::weak_ptr<LoggerImpl> parent_;

        inline static std::mutex globalMutex_;
    };

} // namespace Utils::Logging
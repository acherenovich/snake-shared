#pragma once

#include <memory>
#include <string>
#include <functional>
#include <chrono>
#include <cstddef>

namespace Utils::Threading {

    class Strand;

    struct ThreaderConfig
    {
        std::size_t threadCount { 0 };  // 0 -> auto по hardware_concurrency
        std::string name;
    };

    class Threader
    {
    public:
        using Shared = std::shared_ptr<Threader>;

        virtual ~Threader() = default;

        /// Запустить задачу в пуле потоков.
        virtual void Post(const std::function<void()>& task) = 0;

        /// Создать "стрэнд" – последовательную очередь задач поверх пула потоков.
        virtual std::shared_ptr<Strand> CreateStrand(const std::string& name = {}) = 0;

        /// Количество потоков в пуле (реальное).
        [[nodiscard]] virtual std::size_t ThreadCount() const = 0;

        /// Фабрика.
        static Shared Create(const ThreaderConfig& config = {});
    };

    class Strand
    {
    public:
        using Shared = std::shared_ptr<Strand>;

        virtual ~Strand() = default;

        /// Постинг задач в один и тот же стрэнд гарантирует последовательное выполнение.
        virtual void Post(const std::function<void()>& task) = 0;
    };

} // namespace Utils::Threading
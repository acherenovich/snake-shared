#pragma once

#include "../interface/threader.hpp"
#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>
#include <thread>

namespace Utils::Threading {

    class ThreaderImpl;

    class StrandImpl final :
        public Strand,
        public std::enable_shared_from_this<StrandImpl>
    {
    public:
        StrandImpl(const std::shared_ptr<ThreaderImpl>& parent,
                   const std::string& name);

        void Post(const std::function<void()>& task) override;

    private:
        void ScheduleNext();

    private:
        std::weak_ptr<ThreaderImpl> parent_;
        std::string name_;

        std::mutex mutex_;
        std::deque<std::function<void()>> queue_;
        bool running_ { false };
    };

    class ThreaderImpl final :
        public Threader,
        public std::enable_shared_from_this<ThreaderImpl>
    {
    public:
        explicit ThreaderImpl(const ThreaderConfig& config);
        ~ThreaderImpl() override;

        void Post(const std::function<void()>& task) override;
        std::shared_ptr<Strand> CreateStrand(const std::string& name) override;
        [[nodiscard]] std::size_t ThreadCount() const override;

    private:
        void WorkerLoop();

    private:
        std::string name_;
        std::size_t threadCount_ { 0 };
        std::vector<std::jthread> threads_;

        std::mutex queueMutex_;
        std::condition_variable_any queueCv_;
        bool stopRequested_ { false };

        std::deque<std::function<void()>> tasks_;
    };

} // namespace Utils::Threading
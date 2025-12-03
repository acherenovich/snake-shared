#include "threader.hpp"

namespace Utils::Threading {

    // ========= StrandImpl =========

    StrandImpl::StrandImpl(const std::shared_ptr<ThreaderImpl>& parent,
                           const std::string& name)
        : parent_(parent)
        , name_(name)
    {
    }

    void StrandImpl::Post(const std::function<void()>& task)
    {
        {
            std::lock_guard lock(mutex_);
            queue_.push_back(task);
            if (running_)
            {
                // Уже кто-то выполняет – просто добавили в очередь.
                return;
            }
            running_ = true;
        }

        ScheduleNext();
    }

    void StrandImpl::ScheduleNext()
    {
        const auto self = shared_from_this();

        if (const auto parent = parent_.lock())
        {
            parent->Post([self]()
            {
                std::function<void()> current;

                {
                    std::lock_guard lock(self->mutex_);
                    if (self->queue_.empty())
                    {
                        self->running_ = false;
                        return;
                    }

                    current = self->queue_.front();
                    self->queue_.pop_front();
                }

                if (current)
                {
                    current();
                }

                // После выполнения – планируем следующее.
                self->ScheduleNext();
            });
        }
    }

    // ========= ThreaderImpl =========

    ThreaderImpl::ThreaderImpl(const ThreaderConfig& config)
        : name_(config.name)
    {
        const std::size_t requestedCount = config.threadCount;
        threadCount_ = requestedCount != 0
            ? requestedCount
            : std::max<std::size_t>(1, std::thread::hardware_concurrency());

        threads_.reserve(threadCount_);
        for (std::size_t index = 0; index < threadCount_; ++index)
        {
            threads_.emplace_back([this]()
            {
                WorkerLoop();
            });
        }
    }

    ThreaderImpl::~ThreaderImpl()
    {
        {
            std::lock_guard lock(queueMutex_);
            stopRequested_ = true;
        }

        queueCv_.notify_all();
        // std::jthread сам join-ится в деструкторе.
    }

    void ThreaderImpl::Post(const std::function<void()>& task)
    {
        {
            std::lock_guard lock(queueMutex_);
            if (stopRequested_)
            {
                return;
            }

            tasks_.push_back(task);
        }

        queueCv_.notify_one();
    }

    std::shared_ptr<Strand> ThreaderImpl::CreateStrand(const std::string& name)
    {
        return std::make_shared<StrandImpl>(shared_from_this(), name);
    }

    std::size_t ThreaderImpl::ThreadCount() const
    {
        return threadCount_;
    }

    void ThreaderImpl::WorkerLoop()
    {
        while (true)
        {
            std::function<void()> task;

            {
                std::unique_lock lock(queueMutex_);
                queueCv_.wait(lock, [this]()
                {
                    return stopRequested_ || !tasks_.empty();
                });

                if (stopRequested_ && tasks_.empty())
                {
                    return;
                }

                task = std::move(tasks_.front());
                tasks_.pop_front();
            }

            if (task)
            {
                task();
            }
        }
    }

    // ======= Threader factory =======

    Threader::Shared Threader::Create(const ThreaderConfig& config)
    {
        return std::make_shared<ThreaderImpl>(config);
    }

} // namespace Utils::Threading
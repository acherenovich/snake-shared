#pragma once

#include <iostream>
#include <functional>
#include <exception>
#include <optional>
#include <memory>
#include <utility>
#include <vector>
#include <mutex>

#include <coroutine>

#include "logging.hpp"

// #define CORO_DEBUG_ENABLED

#ifdef CORO_DEBUG_ENABLED
    #define CoroDebug(...) Utils::Log()->Debug(__VA_ARGS__);
#else
    #define CoroDebug(...)
#endif

namespace Utils {
    template <typename T> class Task;

    struct SharedPromise
    {
        std::coroutine_handle<> parentHandle = nullptr;

        void SetupParent(const std::coroutine_handle<> handle)
        {
            parentHandle = handle;
        }
    };

    struct FinalAwaiter {
        bool await_ready() const noexcept { return false; }

        template <typename P>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<P> handle) noexcept
        {
            auto parent = handle.promise().parentHandle;
            if (!parent) {
                CoroDebug("FinalSuspend: no parentHandle, returning noop");
                return std::noop_coroutine();
            }
            return parent;
        }

        void await_resume() const noexcept { }
    };

    // Base promise for Task<T> and Task<void>
    template <typename T, typename DerivedPromise>
    struct BasePromise: SharedPromise {
        using Coroutine = std::coroutine_handle<DerivedPromise>;

        auto initial_suspend() {
            CoroDebug("[Promise: 0x%x] initial_suspend", this);
            return std::suspend_always{};
        }

        auto final_suspend() noexcept {
            CoroDebug("[Promise: 0x%x] final_suspend", this);
            return FinalAwaiter{};
        }

        void unhandled_exception() {
            std::terminate();
        }
    };

    // Awaiter shared for Task<T> and Task<void>
    template <typename T, typename Promise>
    struct TaskAwaiter {
        using Coroutine = std::coroutine_handle<Promise>;
        Coroutine coroutine;

        [[nodiscard]] bool await_ready() const noexcept {
            return !coroutine || coroutine.done();
        }

        auto await_suspend(std::coroutine_handle<> h) const {
            coroutine.promise().SetupParent(h);
            CoroDebug("[Awaiter: 0x%x] Suspend parent: 0x%x waiting for child: 0x%x", this, h.address(), coroutine.address());

            return coroutine;
        }

        T await_resume(); // To be specialized
    };

    class ITask {
    public:
        virtual ~ITask() = default;
        virtual bool IsFinished() const = 0;

        virtual void Resume() = 0;
    };

    class CoroTaskManager {
        using TaskPtr = std::unique_ptr<ITask>;
        using LambdaPtr = std::shared_ptr<void>;
        std::vector<std::pair<TaskPtr, LambdaPtr>> activeTasks;
        std::mutex mutex;

        CoroTaskManager() = default;

        friend CoroTaskManager & GetTaskManager();

    public:
        CoroTaskManager(const CoroTaskManager&) = delete;
        CoroTaskManager& operator=(const CoroTaskManager&) = delete;

        CoroTaskManager(CoroTaskManager&&) = delete;
        CoroTaskManager& operator=(CoroTaskManager&&) = delete;

        template <typename F>
        auto& AddLambda(F&& fn) {
            using LambdaType = std::decay_t<F>;
            using TaskReturnT = decltype(fn());
            using ReturnType = typename TaskReturnT::value_type;

            auto lambdaPtr = std::make_shared<LambdaType>(std::forward<F>(fn));

            TaskReturnT task = (*lambdaPtr)();

            auto taskPtr = std::make_unique<Task<ReturnType>>(std::move(task));
            Task<ReturnType>& taskRef = *taskPtr;

            {
                std::lock_guard lock(mutex);
                activeTasks.emplace_back(std::move(taskPtr), std::move(lambdaPtr));
            }

            taskRef.Resume();

            return taskRef;
        }

        template <typename T>
        Task<T> & AddRootTask(Task<T>&& task) {
            auto taskPtr = std::make_unique<Task<T>>(std::move(task));
            Task<T>& taskRef = *taskPtr;

            std::lock_guard lock(mutex);
            activeTasks.emplace_back(std::move(taskPtr), nullptr);
            activeTasks.back().first->Resume();

            return taskRef;
        }

        void ClearFinishedTasks() {
            std::lock_guard lock(mutex);
            std::erase_if(activeTasks, [](const auto& pair) {
                return pair.first->IsFinished();
            });
        }

        size_t Count() {
            std::lock_guard lock(mutex);
            return activeTasks.size();
        }
    };

    CoroTaskManager & GetTaskManager();

    // Task<void> specialization
    template <>
    class Task<void> final : public ITask {
    public:
        using value_type = void;

        struct promise_type : BasePromise<void, promise_type> {
            std::function<void()> finishCallback;
            bool finished = false;

            Task get_return_object() {
                return Task(std::coroutine_handle<promise_type>::from_promise(*this));
            }

            void return_void()
            {
                finished = true;

                if (finishCallback)
                {
                    finishCallback();
                }
            }
        };

        using Coroutine = BasePromise<void, promise_type>::Coroutine;

        explicit Task(Coroutine c) : coroutine(std::move(c)) {
            CoroDebug("Task<void> 0x%x created, coro: 0x%x", this, coroutine.address());
        }

        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;

        Task(Task&& other) noexcept {
            coroutine = std::move(other.coroutine);
            other.coroutine = nullptr;
            CoroDebug("Task<void> 0x%x moved from 0x%x, coro: 0x%x", this, &other, coroutine.address());
        }

        Task& operator=(Task&&) noexcept = delete;

        ~Task() {
            CoroDebug("Task<void> 0x%x destructed", this);

            if (coroutine)
            {
                if (!coroutine.done())
                {
                    CoroDebug("~Task destructed, but coro not finished! Saving...");

                    GetTaskManager().AddRootTask(std::move(*this));
                }

                if (coroutine)
                    coroutine.destroy();
            }
        }

        auto operator co_await() const {
            struct Awaiter : TaskAwaiter<void, promise_type> {
                void await_resume() const {}
            };
            return Awaiter{coroutine};
        }

        template <typename F>
        Task& operator=(F&& cb) {
            if (coroutine.promise().finished)
            {
                cb();
                return *this;
            }

            coroutine.promise().finishCallback = std::forward<F>(cb);
            return *this;
        }

        bool IsFinished() const override
        {
            return !coroutine || coroutine.done();
        }

        void Resume() override {
            coroutine.resume();
        }
    private:
        Coroutine coroutine;
    };

    // Task<T> general case
    template <typename T>
    class Task final : public ITask {
    public:
        using value_type = T;

        struct promise_type : BasePromise<T, promise_type> {
            std::optional<T> value_;
            std::function<void(T &)> finishCallback;

            Task get_return_object() {
                return Task(std::coroutine_handle<promise_type>::from_promise(*this));
            }

            void return_value(T && v) {
                value_ = std::forward<T>(v);

                if (finishCallback)
                {
                    finishCallback(*value_);
                }
            }

            void return_value(const T & v) {
                value_ = v;

                if (finishCallback)
                {
                    finishCallback(*value_);
                }
            }
        };

        using Coroutine = typename BasePromise<T, promise_type>::Coroutine;

        explicit Task(Coroutine c) : coroutine(std::move(c)) {
            CoroDebug("Task<T> 0x%x created, coro: 0x%x", this, coroutine.address());
        }

        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;

        Task(Task&& other) noexcept {
            coroutine = std::move(other.coroutine);
            other.coroutine = nullptr;
            CoroDebug("Task<void> 0x%x moved from 0x%x, coro: 0x%x", this, &other, coroutine.address());
        }

        Task& operator=(Task&&) noexcept = delete;

        ~Task() {
            CoroDebug("Task<T> 0x%x destructed", this);

            if (coroutine)
            {
                if (!coroutine.done())
                {
                    CoroDebug("~Task destructed, but coro not finished! Saving...");

                    GetTaskManager().AddRootTask(std::move(*this));
                }

                if (coroutine)
                    coroutine.destroy();
            }
        }

        auto operator co_await() {
            struct Awaiter : TaskAwaiter<T, promise_type> {
                using TaskAwaiter<T, promise_type>::coroutine;
                T await_resume() {
                    auto& promise = coroutine.promise();
                    if (!promise.value_.has_value()) {
                        Log()->Fatal("No value in Task<T>");
                    }
                    return std::move(*promise.value_);
                }
            };
            return Awaiter{coroutine};
        }

        template <typename F>
        Task& operator=(F&& cb) {
            if (coroutine.promise().value_.has_value())
            {
                cb(*coroutine.promise().value_);
                return *this;
            }

            coroutine.promise().finishCallback = std::forward<F>(cb);
            return *this;
        }

        bool IsFinished() const override
        {
            return !coroutine || coroutine.done();
        }

        void Resume() override {
            coroutine.resume();
        }
    private:
        Coroutine coroutine;
    };

    template <typename F>
    auto & Async(F&& call) {
        CoroDebug("Async run!");
        return GetTaskManager().AddLambda(std::forward<F>(call));
    }

    // Simple awaitable using lambda and external event
    class AwaitablePromiseTask {
        using ResolveFunc = std::function<void()>;
    public:
        class Resolver
        {
            ResolveFunc fn;
        public:
            using Shared = std::shared_ptr<Resolver>;

            explicit Resolver(const ResolveFunc & fn): fn(fn) {}

            ~Resolver()
            {
                if (fn)
                    fn();
            }

            void Resolve()
            {
                fn();
                fn = {};
            }

            static Shared Create(const ResolveFunc & fn)
            {
                return std::make_shared<Resolver>(fn);
            }
        };

        using Callback = std::function<void(const Resolver::Shared &)>;
        explicit AwaitablePromiseTask(Callback c) : callback(std::move(c)) {}

        [[nodiscard]] bool await_ready() const noexcept { return finished; }

        void await_suspend(std::coroutine_handle<> h) {
            CoroDebug("AwaitablePromiseTask suspend coro: 0x%x", h.address());
            callback(Resolver::Create([this, h] {
                finished = true;
                h.resume();
            }));
        }

        void await_resume() noexcept {}
    private:
        Callback callback;
        bool finished = false;
    };

    using TaskResolver = AwaitablePromiseTask::Resolver::Shared;
} // namespace Utils
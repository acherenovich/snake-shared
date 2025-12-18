#pragma once

#include <vector>
#include <map>
#include <unordered_map>
#include <functional>
#include <thread>
#include <mutex>
#include <cxxabi.h>

#include "logging.hpp"

namespace Utils::Event
{
    template<class FN>
    class CustomFunctionWrapper {
        FN callback;
        const char * typeName = nullptr;

    public:
        explicit CustomFunctionWrapper(const FN & f) {
            callback = f;
            typeName = abi::__cxa_demangle(typeid(f).name(), 0, 0, nullptr);
        }

        template<class... Types>
        void Call(Types... args) const
        {
            if(callback)
            {
                callback(args...);
            }
        }
    };


    class SystemEntry {
    protected:
        std::unordered_map<std::thread::id, std::shared_ptr<bool>> activeCalls;
        std::vector<std::shared_ptr<CustomFunctionWrapper<std::function<void()>>>> callbacks;
    public:
        ~SystemEntry()
        {
            for(auto & [id, status]: activeCalls)
            {
                if(status)
                    *status = false;
            }
        }
        
        void Stop()
        {
            const auto threadID = std::this_thread::get_id();
            if(!activeCalls.contains(threadID) || !activeCalls[threadID] ) {
                return;
            }

            if(*activeCalls[threadID])
            {
                *activeCalls[threadID] = false;
            }
        }
        
        template<class... Params>
        void Call(Params... values)
        {
            const auto threadID = std::this_thread::get_id();
            if (!activeCalls[threadID])
                activeCalls[threadID] = std::make_shared<bool>();

            const auto status = activeCalls[threadID];

            *status = true;

            for (size_t i = 0; i < callbacks.size(); ++i)
            {
                auto ptr = (CustomFunctionWrapper<std::function<void(Params...)>> *)(callbacks[i].get());
                ptr->Call(values...);

                if (!*status)
                    return;
            }
        }
        
        template <typename R, typename...A>
        SystemEntry & operator = (std::function<R(A...)> f) {
            auto wr = new CustomFunctionWrapper<std::function<R(A...)>>(f);

            callbacks.emplace_back((CustomFunctionWrapper<std::function<void()>> *)wr);

            return *this;
        }

        template <typename ...A>
        SystemEntry & operator = (void (*func)(A...)) {

            auto fn = reinterpret_cast<std::function<void(A...)>>(&func);

            auto wr = new CustomFunctionWrapper<std::function<void(A...)>>(fn);

            callbacks.emplace_back((CustomFunctionWrapper<std::function<void()>> *)wr);

            return *this;
        }
    };

    template<class EventType>
    class System
    {
    private:
        std::map<EventType, std::shared_ptr<SystemEntry>> events;
        SystemEntry hooks;
    protected:
        std::recursive_mutex eventMutex;
    public:
        using Shared = std::shared_ptr<System>;

        ~System()
        {
            ClearEvents();
        }

        SystemEntry & HookEvents()
        {
            std::lock_guard l(eventMutex);
            return hooks;
        }

        template<class... Unused>
        SystemEntry & HookEvent(EventType event)
        {
            std::lock_guard l(eventMutex);

            if(!events.count(event))
            {
                events[event] = std::make_shared<SystemEntry>();
            }

            return *(events[event].get());
        }

        template<class... Types>
        void CallEvent(EventType event, Types... values)
        {
            std::shared_ptr<SystemEntry> call;

            hooks.Call(event, values...);
            {
                std::lock_guard l(eventMutex);

                if (!events.count(event))
                    return;

                call = events[event];
            }

            ((SystemEntry *)call.get())->Call(values...);
        }

        void StopEvents(EventType event)
        {
            std::lock_guard l(eventMutex);

            if(!events.count(event))
            {
                //LogError("StopEvents not found");
                return;
            }
            events[event]->Stop();
        }

        void ClearEvents()
        {
            std::lock_guard l(eventMutex);
            events.clear();
        }
    };
}
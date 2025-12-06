#pragma once

#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include <type_traits>

namespace Utils::Service {
    class BaseServiceContainerTemplate
    {
    public:
        virtual ~BaseServiceContainerTemplate() = default;

        virtual std::string GetServiceContainerName() const
        {
            return "unknown";
        }
    };

    class Loader;
    struct SubLoader
    {
        Loader & loader;
        std::function<void(BaseServiceContainerTemplate*)> containerTransform;
    };

    class Instance {
    public:
        using Shared = std::shared_ptr<Instance>;
        virtual ~Instance() = default;

        /// Здесь уже без аргументов. Всё берём из полей, настроенных в PreInitialise.
        virtual void Initialise() {}

        /// Все сервисы созданы и Initialised, но ещё не все интерфейсы.
        virtual void OnAllServicesLoaded() {}

        /// Всё загружено, можно начинать основную работу.
        virtual void OnAllInterfacesLoaded() {}

        virtual void ProcessTick() {}

        virtual std::vector<SubLoader> SubLoaders() { return {}; }
    };

    // template<class BaseInstance>
    class Loader {
    public:
        using InstanceShared   = typename Instance::Shared;
        using InstanceFactory  = std::function<InstanceShared()>;

        template<class T>
        class Add {
        public:
            explicit Add(Loader & loader)
            {
                loader.template AddService<T>();
            }
        };

        template<class BaseContainer>
        void PreInitialise(BaseContainer & container)
        {
            services_.reserve(factories_.size());
            for (const auto & [key, factory] : factories_) {
                (void)key;
                services_.emplace_back(factory());
            }

            for (const InstanceShared & service : services_) {
                auto casted = std::dynamic_pointer_cast<BaseContainer>(service);
                if (!casted)
                    Log()->Fatal("Failed to cast service to BaseContainer");

                if (containerTransformFn_)
                    containerTransformFn_(casted.get());
                else
                    casted->SetupContainer(container);

                for (auto & [subLoader, transform]: service->SubLoaders())
                {
                    subLoader.SetCustomSetupContainer(transform);
                    subLoader.PreInitialise(container);

                    subLoaders_.push_back(&subLoader);
                }
            }
        }

        void SetCustomSetupContainer(const std::function<void(BaseServiceContainerTemplate*)> &containerTransform)
        {
            containerTransformFn_ = containerTransform;
        }

        void Initialise() const
        {
            for (const InstanceShared & service : services_)
            {
                service->Initialise();
            }

            for (const auto & subLoader: subLoaders_)
            {
                subLoader->Initialise();
            }
        }

        void OnAllServicesLoaded() const
        {
            for (const InstanceShared & service : services_)
            {
                service->OnAllServicesLoaded();
            }

            for (const auto & subLoader: subLoaders_)
            {
                subLoader->OnAllServicesLoaded();
            }
        }

        void OnAllInterfacesLoaded() const
        {
            for (const InstanceShared & service : services_)
            {
                service->OnAllInterfacesLoaded();
            }

            for (const auto & subLoader: subLoaders_)
            {
                subLoader->OnAllInterfacesLoaded();
            }
        }

        void ProcessTick() const
        {
            for (const InstanceShared & service : services_)
            {
                service->ProcessTick();
            }

            for (const auto & subLoader: subLoaders_)
            {
                subLoader->ProcessTick();
            }
        }


        template<class T>
        void AddService()
        {
            static_assert(std::is_base_of_v<Instance, T>,
                            "T must inherit from Loader<Args...>::Instance");

            const std::type_index key(typeid(T));
            factories_[key] = []() -> InstanceShared {
                return std::make_shared<T>();
            };
        }

        [[nodiscard]] const std::vector<InstanceShared> & Services() const noexcept
        {
            return services_;
        }
    private:
        std::unordered_map<std::type_index, InstanceFactory> factories_;
        std::vector<InstanceShared>                          services_;
        std::function<void(BaseServiceContainerTemplate*)> containerTransformFn_;
        std::vector<Loader*> subLoaders_;
    };

} // namespace Utils::Service
#pragma once

#include <functional>
#include <memory>
#include <map>

namespace Utils::Service {
    class BaseServiceInterface
    {
    public:
        using Shared = std::shared_ptr<BaseServiceInterface>;

        virtual ~BaseServiceInterface() = default;
    };


    class InterfaceController
    {
        std::map<std::type_index, BaseServiceInterface::Shared> interfaces_;
    public:

        template <typename Interface>
        bool Register(const BaseServiceInterface::Shared & interface)
        {
            const std::type_index key(typeid(Interface));
            if (interfaces_.contains(key))
            {
                Log()->Error("Failed to register instance '{}'", key.name());
                return false;
            }

            Log()->Debug("Interface '{}' registered", key.name());

            interfaces_[key] = interface;
            return true;
        }

        template <typename Interface>
        std::shared_ptr<Interface> Get()
        {
            const std::type_index key(typeid(Interface));
            if (!interfaces_.contains(key))
            {
                Log()->Warning("Failed to get instance '{}'", key.name());
                return {};
            }

            return std::dynamic_pointer_cast<Interface>(interfaces_[key]);
        }
    };

} // namespace Utils::Service
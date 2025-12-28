#pragma once

#include "legacy_game_math.hpp"
#include "legacy_entities.hpp"

#include <SFML/System/Vector2.hpp>

namespace Utils::Legacy::Game::Entity {
    class BaseEntity: public virtual Interface::Entity::BaseEntity {
    protected:
        uint32_t frameCreated = 0, frameKilled = 0;
        std::uint32_t EntityIDValue = 0;
    public:
        using Shared = std::shared_ptr<BaseEntity>;

        void SetEntityID(const std::uint32_t entityID)
        {
            EntityIDValue = entityID;
        }

        [[nodiscard]] std::uint32_t EntityID() const override
        {
            return EntityIDValue;
        }

        [[nodiscard]] uint32_t FrameCreated() const override
        {
            return frameCreated;
        }

        [[nodiscard]] uint32_t FrameKilled() const override
        {
            return frameKilled;
        }

        void Kill(const uint32_t frame)
        {
            frameKilled = frame;
        }

        [[nodiscard]] bool IsKilled() const override
        {
            return !!frameKilled;
        }
    };
}
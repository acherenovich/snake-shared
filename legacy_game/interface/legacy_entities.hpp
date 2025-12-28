#pragma once

#include <list>
#include <memory>
#include <SFML/System/Vector2.hpp>

#include "legacy_common.hpp"

namespace Utils::Legacy::Game::Interface::Entity {
    class BaseEntity {
    public:
        virtual ~BaseEntity() = default;

        using Shared = std::shared_ptr<BaseEntity>;

        struct Color {
            uint8_t r = 255;
            uint8_t g = 255;
            uint8_t b = 255;
            uint8_t a = 255;
        };

        [[nodiscard]] virtual std::uint32_t EntityID() const = 0;

        [[nodiscard]] virtual uint32_t FrameCreated() const = 0;
        [[nodiscard]] virtual uint32_t FrameKilled() const = 0;

        [[nodiscard]] virtual bool IsKilled() const = 0;

        [[nodiscard]] virtual const sf::Vector2f & GetPosition() const = 0;
    };

    class Food: public virtual BaseEntity{
    public:
        using Shared = std::shared_ptr<Food>;

        [[nodiscard]] virtual float GetRadius() const = 0;
        [[nodiscard]] virtual const uint8_t & GetPower() const = 0;
        [[nodiscard]] virtual const Color & GetColor() const = 0;
    };

    class Snake: public virtual BaseEntity {
    public:
        using Shared = std::shared_ptr<Snake>;

        virtual void SetDestination(const sf::Vector2f & dest) = 0;

        [[nodiscard]] virtual float GetRadius(bool head = false) const = 0;
        [[nodiscard]] virtual std::string GetName() const = 0;
        [[nodiscard]] virtual const uint32_t & GetExperience() const = 0;

        [[nodiscard]] virtual float GetZoom() const = 0;
        [[nodiscard]] virtual const std::list<sf::Vector2f> & Segments() const = 0;
    };

}

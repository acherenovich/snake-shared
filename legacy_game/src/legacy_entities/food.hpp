#pragma once

#include "base_entity.hpp"

namespace Utils::Legacy::Game::Entity {
    class Food: public BaseEntity, public Interface::Entity::Food {
        sf::Vector2f position;

        uint8_t power = 1;
        Color color;
    public:
        using Shared = std::shared_ptr<Food>;

        Food() = default;

        explicit Food(uint32_t frame, const sf::Vector2f & pos, int max = 10): position(pos) {
            frameCreated = frame;

            power = Math::GetRandomInt(1, max);
            // Минимальное значение для компоненты (яркость)
            constexpr int minValue = 100; // Нижняя граница для исключения тусклых цветов
            constexpr int maxValue = 200; // Верхняя граница (максимальная яркость)

            // Генерация ярких цветов
            color.r = Math::GetRandomInt(minValue, maxValue);
            color.g = Math::GetRandomInt(minValue, maxValue);
            color.b = Math::GetRandomInt(minValue, maxValue);
        }

        virtual ~Food() = default;

        [[nodiscard]] float GetRadius() const override
        {
            return FoodRadius * (1 + (float(power) / 10.f));
        }


        [[nodiscard]] const sf::Vector2f & GetPosition() const override
        {
            return position;
        }

        [[nodiscard]] const uint8_t & GetPower() const override
        {
            return power;
        }

        [[nodiscard]] const Color & GetColor() const override
        {
            return color;
        }

        // Message::DataUpdate::Food GetDataUpdate() {
        //     Message::DataUpdate::Food data;
        //
        //     data.color = *(Message::DataUpdate::Color*)&color;
        //     data.power = power;
        //     data.position = position;
        //     data.frameCreated = frameCreated;
        //     data.frameKilled = frameKilled;
        //
        //     return data;
        // }
        //
        // void SetDataUpdate(const Message::DataUpdate::Food & data) {
        //     color = *(Color*)&data.color;
        //     power = data.power;
        //     position = data.position;
        //     frameCreated = data.frameCreated;
        //     frameKilled = data.frameKilled;
        // }
    };
}
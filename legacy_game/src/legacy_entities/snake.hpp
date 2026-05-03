#pragma once

#include "base_entity.hpp"

#include "food.hpp"

#include <cmath>

namespace Utils::Legacy::Game::Entity {
    class Snake: public Interface::Entity::Snake, public BaseEntity {
        std::list<sf::Vector2f> segments;
        uint32_t experience = 30;
        sf::Vector2f destination;
        std::string name;
        Color color_ {};
    public:
        using Shared = std::shared_ptr<Snake>;

        static constexpr float camera_radius = 1000.f;  // Радиус видимости камеры
        static constexpr float step_distance = 50.f;    // Расстояние между сегментами змейки
        static constexpr float speed = 20.f;            // Скорость передвижения головы

        Snake() = default;

        Snake(uint32_t frame, const sf::Vector2f & start)
        {
            frameCreated = frame;

            for(auto & a: {0, 1, 2})
                segments.push_back(start);
        }

        virtual ~Snake() = default;

        [[nodiscard]] sf::Vector2f GetDestination() const override
        {
            return destination;
        }

        void SetDestination(const sf::Vector2f & dest) override
        {
            destination = dest;
        }

        sf::Vector2f TryMove()
        {
            return Math::MoveHeadToDestination(segments, destination, speed, 45.f);
        }

        void AcceptMove(const sf::Vector2f & head)
        {
            *segments.begin() = head;

            Math::MoveEverySegmentToTop(segments, step_distance);
        }

        [[nodiscard]] float GetRadius(bool head) const override
        {
            if(head)
            {
                return SnakeHeadRadius * (1.f + float(experience) / 500.f);
            }
            else
            {
                return SnakePartRadius * (1.f + float(experience) / 500.f);
            }
        }

        void AddExperience(uint32_t count)
        {
            experience += count;

            RecalculateLength();
        }

        bool TakeExperience(uint32_t count)
        {
            if(experience - 30 <= count)
                return false;

            experience -= count;

            RecalculateLength();

            return true;
        }

        [[nodiscard]] const uint32_t & GetExperience() const override
        {
            return experience;
        }

        void RecalculateLength()
        {
            int change = 0;

            change = (int)(experience / 10) - (int)segments.size();

            for(int i = 0; i < abs(change); i++)
            {
                if(change > 0)
                    segments.insert(std::next(segments.begin()), *std::next(segments.begin()));
                else
                    segments.pop_back();
            }
        }

        [[nodiscard]] bool CanRespawn(const uint32_t frame) const
        {
            return frame - frameKilled > 64;
        }

        void SetName(const std::string & n)
        {
            name = n;
        }

        [[nodiscard]] std::string GetName() const override
        {
            return name;
        }

        [[nodiscard]] const sf::Vector2f & GetPosition() const override
        {
            return *segments.begin();
        }

        void SetPosition(const sf::Vector2f & pos)
        {
            for (auto & segment : segments)
                segment = pos;
        }

        void Respawn(const uint32_t frame, const sf::Vector2f & pos)
        {
            frameKilled = 0;
            frameCreated = frame;

            experience = 30;

            RecalculateLength();

            for (auto & segment : segments)
                segment = pos;
        }

        void NetSetFullSegments(const std::vector<sf::Vector2f>& segs)
        {
            segments.clear();
            for (auto& v : segs)
                segments.push_back(v);
        }

        // Set only head pos (used before reconstruction step)
        void NetSetHead(const sf::Vector2f& head)
        {
            if (segments.empty())
            {
                segments.push_back(head);
                return;
            }
            *segments.begin() = head;
        }

        // Move segments forward using the same rules as server
        void NetStepBody()
        {
            Math::MoveEverySegmentToTop(segments, step_distance);
        }

        // Utility: ensure length according to experience
        void NetApplyExperience(const std::uint32_t exp)
        {
            experience = exp;
            RecalculateLength();
        }

        // Message::DataUpdate::Snake GetDataUpdate() {
        //     Message::DataUpdate::Snake data;
        //
        //     data.segments = segments;
        //     data.experience = experience;
        //     data.name = name;
        //     data.frameCreated = frameCreated;
        //     data.frameKilled = frameKilled;
        //
        //     return data;
        // }
        //
        // void SetDataUpdate(const Message::DataUpdate::Snake & data) {
        //     segments = data.segments;
        //     experience = data.experience;
        //     name = data.name;
        //     frameCreated = data.frameCreated;
        //     frameKilled = data.frameKilled;
        // }

        [[nodiscard]] bool CanSee(const Shared & snake) const {
            // Получаем позицию текущей змейки (головы)
            const sf::Vector2f& myPosition = *segments.begin();

            // Проверяем расстояние до каждого сегмента переданной змейки
            for (const sf::Vector2f& segment : snake->segments) {
                float distance = std::hypot(segment.x - myPosition.x, segment.y - myPosition.y);

                // Если расстояние меньше радиуса видимости, возвращаем true
                if (distance < camera_radius * GetZoom()) {
                    return true;
                }
            }

            // Если ни один сегмент не попал в радиус, возвращаем false
            return false;
        }

        [[nodiscard]] bool CanSee(const Food::Shared & food) const {
            const sf::Vector2f& myPosition = *segments.begin();

            auto pos = food->GetPosition();
            float distance = std::hypot(pos.x - myPosition.x, pos.y - myPosition.y);

            if (distance < camera_radius * GetZoom()) {
                return true;
            }

            return false;
        }

        [[nodiscard]] float GetZoom() const final {
            return 1.f + static_cast<float>(experience) / 10 * 0.01f;
        }

        [[nodiscard]] std::list<sf::Vector2f> & Segments() final
        {
            return segments;
        }

        [[nodiscard]] Color GetColor() const override
        {
            return color_;
        }

        void SetColor(const Color & c) override
        {
            color_ = c;
        }
    };
}
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <format>
#include <list>
#include <map>
#include <unordered_set>
#include <vector>


#include "legacy_common.hpp"
#include "legacy_entities.hpp"

#include "../src/legacy_entities/food.hpp"
#include "../src/legacy_entities/snake.hpp"


namespace Utils::Legacy::Game {
    class Logic
    {
    protected:
        std::unordered_set<Entity::Snake::Shared> snakes_;
        std::unordered_set<Entity::Food::Shared> foods_;

        uint32_t frame_ = 0;
    public:
        Logic();

        virtual void ProcessTick();

        [[nodiscard]] std::unordered_set<Interface::Entity::Snake::Shared> Snakes() const;

        [[nodiscard]] std::unordered_set<Interface::Entity::Food::Shared> Foods() const;
    };
}

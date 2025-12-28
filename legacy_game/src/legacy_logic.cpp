#include "legacy_logic.hpp"
#include <iostream>
#include <format>
#include <random>

#include "logging.hpp"


namespace Utils::Legacy::Game {
    Logic::Logic()
    {
    }

    void Logic::ProcessTick()
    {
        frame_++;
    }

    std::unordered_set<Interface::Entity::Snake::Shared> Logic::Snakes() const
    {
        std::unordered_set<Interface::Entity::Snake::Shared> snakes;

        snakes.reserve(snakes_.size());
        for (auto& snake : snakes_)
            snakes.insert(snake);

        return snakes;
    }

    std::unordered_set<Interface::Entity::Food::Shared> Logic::Foods() const
    {
        std::unordered_set<Interface::Entity::Food::Shared> foods;

        foods.reserve(foods_.size());
        for (auto& snake : foods_)
            foods.insert(snake);

        return foods;
    }

} // namespace Utils::Legacy::Game
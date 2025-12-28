#pragma once

#include <SFML/System/Vector2.hpp>

namespace Utils::Legacy::Game {
    constexpr static float AreaWidth = 10000.0;
    constexpr static float AreaHeight = 10000.0;

    constexpr static float AreaRadius = 5000.0;
    static const auto AreaCenter = sf::Vector2f(AreaRadius, AreaRadius);
    constexpr static int FoodCount = 500;

    constexpr static float Width = 1600.f;
    constexpr static float Height = 900.f;
    constexpr static float SnakePartRadius = 30.f;
    constexpr static float SnakeHeadRadius = 45.f;
    constexpr static float FoodRadius = 10.f;
    constexpr static float SmoothDuration = 10.f;
}

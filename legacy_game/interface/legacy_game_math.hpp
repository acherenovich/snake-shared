#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <format>
#include <list>

#include <SFML/System/Vector2.hpp>

#include "legacy_common.hpp"

namespace Utils::Legacy::Game::Math {
    sf::Vector2f MoveHeadToDestination(std::list<sf::Vector2f> & segments, const sf::Vector2f & dest, float limit = 0.f, float max_turn_angle = 30.f);

    void MoveEverySegmentToTop(std::list<sf::Vector2f> & segments, float limit = 10.f);

    sf::Vector2f GetRandomVector2f(float min, float max);
    sf::Vector2f GetRandomVector2fInSphere(const sf::Vector2f & center, float radius);

    int GetRandomInt(int min, int max);

    bool CheckCollision(const sf::Vector2f & a, const sf::Vector2f & b, float radius = 10.f);

    bool LineIntersectsCircle(const sf::Vector2f& p1, const sf::Vector2f& p2, const sf::Vector2f& center, float radius);

    sf::Vector2f RestrictToBounds(const sf::Vector2f& position, const sf::Vector2f& fieldSize);

    bool CheckBoundsCollision(const sf::Vector2f& position, const sf::Vector2f & center, const float & radius);

    sf::Vector2f CalculateCameraMove(sf::Vector2f center, const sf::Vector2f& head);

    std::vector<sf::Vector2f> GenerateBezierCurve(const std::list<sf::Vector2f>& points, int resolution = 20);
} // namespace Utils::Legacy::Game

#include "legacy_game_math.hpp"
#include <iostream>
#include <format>
#include <random>


namespace Utils::Legacy::Game::Math {

    sf::Vector2f MoveHeadToDestination(std::list<sf::Vector2f> &segments, const sf::Vector2f &dest, float limit, float max_turn_angle)
    {
        if (segments.size() < 2)
            return {}; // У змейки недостаточно сегментов для вычисления направления

        auto head = *segments.begin();
        auto second = *(++segments.begin()); // Второй сегмент змейки

        // Вычисляем текущее направление змейки
        float current_dx = head.x - second.x;
        float current_dy = head.y - second.y;
        float current_angle = std::atan2(current_dy, current_dx); // Угол в радианах

        // Вычисляем направление к цели
        float target_dx = dest.x - head.x;
        float target_dy = dest.y - head.y;
        float target_angle = std::atan2(target_dy, target_dx); // Угол в радианах

        // Нормализуем разницу углов в диапазон [-π, π]
        float angle_diff = target_angle - current_angle;
        if (angle_diff > M_PI)
            angle_diff -= 2 * M_PI;
        if (angle_diff < -M_PI)
            angle_diff += 2 * M_PI;

        // Ограничиваем разницу углов до допустимого диапазона
        float max_turn_angle_radians = max_turn_angle * M_PI / 180.f;
        if (std::abs(angle_diff) > max_turn_angle_radians)
        {
            // Ограничиваем изменение угла
            angle_diff = (angle_diff > 0 ? 1.f : -1.f) * max_turn_angle_radians;
        }

        // Вычисляем новый угол движения
        float new_angle = current_angle + angle_diff;

        // Рассчитываем направление движения головы
        float dx = std::cos(new_angle);
        float dy = std::sin(new_angle);

//        printf("angle_diff: %.2f | current_angle: %.2f | dx: %.2f | dy: %.2f\n", angle_diff, current_angle, dx, dy);

        // Ограничиваем расстояние движения головы
        head.x += limit * dx;
        head.y += limit * dy;

        return head;
    }

    void MoveEverySegmentToTop(std::list<sf::Vector2f> & segments, float limit)
    {
        auto prev = *segments.begin();
        for (auto iter = std::next(segments.begin()); iter != segments.end(); ++iter) {
            sf::Vector2f& current = *iter;

            // Считаем расстояние между текущим сегментом и предыдущим
            float distance = std::hypot(prev.x - current.x, prev.y - current.y);

            // Если расстояние больше допустимого, двигаем текущий сегмент ближе к предыдущему
            if (distance > limit) {
                float dx = prev.x - current.x;
                float dy = prev.y - current.y;
                float angle_to_prev = std::atan2(dy, dx);

                current.x += (distance - limit) * cos(angle_to_prev);
                current.y += (distance - limit) * sin(angle_to_prev);
            }

            prev = *iter; // Текущий сегмент становится предыдущим для следующего
        }
    }

    sf::Vector2f GetRandomVector2f(float mx, float my)
    {
        static std::random_device rd;
        static std::mt19937       rng(rd());

        auto x = std::uniform_real_distribution(1.f, mx)(rng);
        auto y = std::uniform_real_distribution(1.f, my)(rng);

        return {x, y};
    }

    sf::Vector2f GetRandomVector2fInSphere(const sf::Vector2f& center, float radius)
    {
        static std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution distAngle(0.f, 2.f * 3.14159265f);
        std::uniform_real_distribution distRadius(0.f, 1.f);

        // Генерируем случайный угол от 0 до 2π
        float angle = distAngle(rng);

        // Генерируем случайное расстояние от центра, корректируя для равномерного распределения
        float distance = radius * std::sqrt(distRadius(rng));

        // Вычисляем координаты точки
        float x = center.x + distance * std::cos(angle);
        float y = center.y + distance * std::sin(angle);

        return {x, y};
    }

    int GetRandomInt(int min, int max)
    {
        static std::random_device rd;
        static std::mt19937       rng(rd());

        return std::uniform_int_distribution<int>(min, max)(rng);
    }

    bool CheckCollision(const sf::Vector2f & a, const sf::Vector2f & b, float radius)
    {
        float distance = std::hypot(a.x - b.x, a.y - b.y);
        return (distance <= radius);
    }

    bool LineIntersectsCircle(const sf::Vector2f& p1, const sf::Vector2f& p2, const sf::Vector2f& center, float radius)
    {
        // Вектор направления линии
        sf::Vector2f d = p2 - p1;
        // Вектор от центра круга до начала линии
        sf::Vector2f f = p1 - center;

        float a = d.x * d.x + d.y * d.y;
        float b = 2 * (f.x * d.x + f.y * d.y);
        float c = f.x * f.x + f.y * f.y - radius * radius;

        float discriminant = b * b - 4 * a * c;

        if (discriminant < 0)
        {
            // Нет пересечений
            return false;
        }
        else
        {
            discriminant = std::sqrt(discriminant);

            float t1 = (-b - discriminant) / (2 * a);
            float t2 = (-b + discriminant) / (2 * a);

            // Проверяем, пересекает ли линия отрезок
            if ((t1 >= 0 && t1 <= 1) || (t2 >= 0 && t2 <= 1))
            {
                return true;
            }
            return false;
        }
    }

    sf::Vector2f RestrictToBounds(const sf::Vector2f& position, const sf::Vector2f& fieldSize)
    {
        sf::Vector2f restrictedPosition = position;

        // Ограничиваем позицию в пределах игрового поля
        restrictedPosition.x = std::clamp(restrictedPosition.x, 0.f, fieldSize.x);
        restrictedPosition.y = std::clamp(restrictedPosition.y, 0.f, fieldSize.y);

        return restrictedPosition;
    }

    bool CheckBoundsCollision(const sf::Vector2f& position, const sf::Vector2f & center, const float & radius)
    {
        return (center.x - position.x) * (center.x - position.x) + (center.y - position.y) * (center.y - position.y) > std::pow(radius, 2);
    }

    sf::Vector2f CalculateCameraMove(sf::Vector2f center, const sf::Vector2f& head)
    {
        if(center.x > head.x)
            center.x -= center.x - head.x;
        else
            center.x += head.x - center.x;


        if(center.y > head.y)
            center.y -= center.y - head.y;
        else
            center.y += head.y - center.y;

        return center;
    }

    std::vector<sf::Vector2f> GenerateBezierCurve(const std::list<sf::Vector2f>& points, const int resolution)
    {
        std::vector<sf::Vector2f> curve;

        auto it1 = points.begin();
        auto it2 = std::next(it1);
        auto it3 = std::next(it2);

        while (it3 != points.end())
        {
            for (int i = 0; i <= resolution; ++i)
            {
                float t = static_cast<float>(i) / resolution;
                float oneMinusT = 1.0f - t;

                sf::Vector2f p =
                    oneMinusT * oneMinusT * (*it1) +
                    2 * oneMinusT * t * (*it2) +
                    t * t * (*it3);

                curve.push_back(p);
            }

            ++it1;
            ++it2;
            ++it3;
        }

        return curve;
    }
} // namespace Utils::Legacy::Game
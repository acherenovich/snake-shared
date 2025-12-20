#pragma once

#include <cstdlib>
#include <random>
#include <string>

namespace Utils {
    inline void SetEnv(const std::string & key, const std::string & value)
    {
#if defined(_WIN32)
        _putenv_s(key.c_str(), value.c_str());
#else
        setenv(key.c_str(), value.c_str(), 1);
#endif
    }

    inline std::string Env(const std::string & key)
    {
        char * val = getenv( key.c_str() );
        return val == nullptr ? std::string("") : std::string(val);
    }

    inline long EnvInt(const std::string & key, const long defaultValue = 0)
    {
        const char * val = getenv( key.c_str() );
        return val == nullptr ? defaultValue : std::strtol(val, nullptr, 10);
    }

    inline std::string GenerateRandomCode(const size_t size) {
        static constexpr char characters[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
        constexpr size_t charactersCount = sizeof(characters) - 1;

        std::string code;
        code.reserve(size);

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, charactersCount - 1);

        for (int i = 0; i < size; ++i) {
            code.push_back(characters[dis(gen)]);
        }
        return code;
    }
} // namespace Utils::Service
# snake-shared/legacy_game/CMakeLists.txt

find_package(Boost REQUIRED COMPONENTS json)

find_package(SFML 3 REQUIRED COMPONENTS System)

snake_shared_add_static_module(legacy_game
        DEPS
        logging
        coroutine
        PUBLIC_LIBS
        Boost::json
        SFML::System
)

# snake-shared/legacy_game/CMakeLists.txt

find_package(Boost REQUIRED COMPONENTS json)

find_package(SFML 2.6
        COMPONENTS system
        CONFIG REQUIRED
)

snake_shared_add_static_module(legacy_game
        DEPS
        logging
        coroutine
        PUBLIC_LIBS
        Boost::json
        ${MYSQLCLIENT_LIBRARY}
)
#include "coroutine.hpp"

namespace Utils {

    CoroTaskManager & GetTaskManager()
    {
        static CoroTaskManager manager;

        return manager;
    }

}

#include "Vst3RunLoop.h"

namespace gigchain::engine {

std::unique_ptr<Vst3RunLoop> makeVst3RunLoop()
{
    return nullptr; // Mac plugins run on the main thread's run loop, which Qt runs
}

} // namespace gigchain::engine

#include "Vst3RunLoop.h"

namespace gigchain::engine {

std::unique_ptr<Vst3RunLoop> makeVst3RunLoop()
{
    return nullptr; // Windows plugins run their own message loop
}

} // namespace gigchain::engine

#pragma once

#include <functional>

#if defined(_MSC_VER) && defined(_DEBUG) && !defined(__SANITIZE_ADDRESS__)
#include <crtdbg.h>
#define GIGCHAIN_LEAK_CHECK_AVAILABLE 1
#endif

namespace gigchain::test {

// Runs `op` once to warm up one-time caches, then `iterations` more times,
// and returns how many more CRT heap blocks are alive afterwards than before
// the repeated runs. A positive number means `op` leaks. Only meaningful in
// Debug builds (CRT debug heap); always 0 elsewhere.
inline long long leakedBlocks(const std::function<void()>& op, int iterations = 50)
{
#ifdef GIGCHAIN_LEAK_CHECK_AVAILABLE
    op();
    _CrtMemState before{};
    _CrtMemState after{};
    _CrtMemCheckpoint(&before);
    for (int i = 0; i < iterations; ++i) {
        op();
    }
    _CrtMemCheckpoint(&after);
    const auto grown = static_cast<long long>(after.lCounts[_NORMAL_BLOCK]) -
                       static_cast<long long>(before.lCounts[_NORMAL_BLOCK]);
    return grown > 0 ? grown : 0;
#else
    (void)op;
    (void)iterations;
    return 0;
#endif
}

inline constexpr bool leakCheckAvailable()
{
#ifdef GIGCHAIN_LEAK_CHECK_AVAILABLE
    return true;
#else
    return false;
#endif
}

} // namespace gigchain::test

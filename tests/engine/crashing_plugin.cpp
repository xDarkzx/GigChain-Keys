// A fake VST3 plugin for tst_plugin_scanner: reading it crashes the process
// (an access violation where the host asks for its factory), as a broken
// plugin would. Never loaded by the app.
#include <cstdint>

extern "C" __declspec(dllexport) void* GetPluginFactory()
{
    volatile std::uintptr_t nowhere = 0;
    // cppcheck-suppress nullPointer ; the crash is the point
    *reinterpret_cast<volatile int*>(nowhere) = 1; // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr): the crash is the point
    return nullptr;
}

add_library(gigchain_engine STATIC
    include/gigchain/engine/EngineTypes.h
    include/gigchain/engine/IEngine.h
    include/gigchain/engine/MidiSetup.h
    include/gigchain/engine/FakeEngineFactory.h
    internal/FakeEngine.h
    internal/FakeEngine.cpp
    internal/MidiEvent.h
    internal/INode.h
    internal/MidiRouter.h
    internal/MidiRouter.cpp
    internal/RenderGraph.h
    internal/RenderGraph.cpp
    internal/GraphExchange.h
    internal/GraphExchange.cpp
    internal/MidiQueue.h
    internal/Vst3Node.h
    internal/Vst3Node.cpp
    internal/EngineLog.h
    internal/EngineLog.cpp
    internal/AudioDevice.h
    internal/AudioDevice.cpp
    internal/MidiInput.h
    internal/MidiInput.cpp
    internal/MidiSetup.cpp
    internal/ArturiaWindowSize.h
    internal/ArturiaWindowSize.cpp
    internal/LoaderErrors.h
    internal/PluginCatalog.h
    internal/PluginCatalog.cpp
    internal/PluginModules.h
    internal/PluginModules.cpp
    internal/PluginLoadGuard.h
    internal/PluginLoadGuard.cpp
    internal/ComponentHandler.h
    internal/SafetyLimiter.h
    internal/RealEngine.h
    internal/RealEngine.cpp
    include/gigchain/engine/RealEngineFactory.h
    include/gigchain/engine/IPluginEditor.h
)
add_library(gigchain::engine ALIAS gigchain_engine)
target_include_directories(gigchain_engine
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/internal)
target_link_libraries(gigchain_engine PUBLIC gigchain::core PRIVATE vst3_host_support RtAudio::rtaudio RtMidi::rtmidi)
target_compile_definitions(gigchain_engine PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
gigchain_target_defaults(gigchain_engine)

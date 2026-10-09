add_library(gigchain_engine STATIC
    include/gigchain/engine/EngineTypes.h
    include/gigchain/engine/IEngine.h
    include/gigchain/engine/MidiSetup.h
    include/gigchain/engine/MidiControl.h
    include/gigchain/engine/Notice.h
    include/gigchain/engine/FakeEngineFactory.h
    internal/FakeEngine.h
    internal/FakeEngine.cpp
    internal/MidiEvent.h
    internal/INode.h
    internal/MidiRouter.h
    internal/MidiRouter.cpp
    internal/MidiEffects.cpp
    internal/ExternalMidiOut.cpp
    internal/HardwareSender.cpp
    internal/PerformanceRecorder.cpp
    internal/RenderGraph.h
    internal/RenderGraph.cpp
    internal/LoopStation.h
    internal/LoopStation.cpp
    internal/SongTransport.h
    internal/GraphExchange.h
    internal/Metronome.h
    internal/MidiMonitor.h
    internal/AudioFile.h
    internal/AudioFile.cpp
    internal/MidiClockOut.h
    internal/MidiClockOut.cpp
    internal/MidiQueue.h
    internal/Vst3Node.h
    internal/Vst3Node.cpp
    internal/EngineLog.h
    internal/EngineLog.cpp
    internal/AudioDevice.h
    internal/AudioDevice.cpp
    internal/AudioApis.h
    internal/Vst3RunLoop.h
    internal/MidiInput.h
    internal/MidiInput.cpp
    internal/MidiSetup.cpp
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
target_link_libraries(gigchain_engine PUBLIC gigchain::core gigchain::platform
    PRIVATE vst3_host_support RtAudio::rtaudio RtMidi::rtmidi Qt6::Multimedia)
if(WIN32)
    target_compile_definitions(gigchain_engine PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
# What differs by system inside the engine (next to the code that needs it).
gigchain_platform_sources(gigchain_engine internal/AudioApis internal/Vst3RunLoop)
gigchain_target_defaults(gigchain_engine)

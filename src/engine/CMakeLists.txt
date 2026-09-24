add_library(openstage_engine STATIC
    include/openstage/engine/EngineTypes.h
    include/openstage/engine/IEngine.h
    include/openstage/engine/MidiSetup.h
    include/openstage/engine/FakeEngineFactory.h
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
    internal/LoaderErrors.h
    internal/PluginCatalog.h
    internal/PluginCatalog.cpp
    internal/RealEngine.h
    internal/RealEngine.cpp
    include/openstage/engine/RealEngineFactory.h
    include/openstage/engine/IPluginEditor.h
)
add_library(openstage::engine ALIAS openstage_engine)
target_include_directories(openstage_engine
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/internal)
target_link_libraries(openstage_engine PUBLIC openstage::core PRIVATE vst3_host_support RtAudio::rtaudio RtMidi::rtmidi)
target_compile_definitions(openstage_engine PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
openstage_target_defaults(openstage_engine)

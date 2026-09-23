# THROWAWAY SPIKE — answers "does MIDI -> VST3 instrument -> ASIO feel playable?"
# Not part of the product. Uses the SDK set up by cmake/Vst3Sdk.cmake.
add_executable(engine_spike main.cpp)
target_link_libraries(engine_spike PRIVATE vst3_host_support RtAudio::rtaudio RtMidi::rtmidi)

add_library(sessionflow_engine
    audio_device.cpp
    metronome.cpp
    time_map.cpp
    transport.cpp
)
target_link_libraries(sessionflow_engine
    PUBLIC sessionflow_core sessionflow_model
    PRIVATE rtaudio sessionflow_warnings
)

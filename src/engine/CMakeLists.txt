add_library(sessionflow_engine
    audio_device.cpp
    audio_io.cpp
    device_block.cpp
)
target_link_libraries(sessionflow_engine
    PUBLIC sessionflow_core sessionflow_model
    PRIVATE rtaudio sessionflow_warnings
)

add_library(sessionflow_engine
    audio_device.cpp
)
target_link_libraries(sessionflow_engine
    PUBLIC sessionflow_core sessionflow_model
    PRIVATE rtaudio sessionflow_warnings
)

add_library(sessionflow_devices
    gain.cpp
)
target_link_libraries(sessionflow_devices
    PUBLIC sessionflow_core sessionflow_engine
    PRIVATE sessionflow_warnings
)

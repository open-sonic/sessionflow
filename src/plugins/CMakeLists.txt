add_library(sessionflow_plugins
    plugin_folders.cpp
)
target_link_libraries(sessionflow_plugins
    PUBLIC sessionflow_core sessionflow_engine
    PRIVATE sessionflow_warnings
)

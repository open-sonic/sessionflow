add_library(sessionflow_plugins
    child_process.cpp
    ipc_signal.cpp
    plugin_folders.cpp
    shared_memory.cpp
)
target_link_libraries(sessionflow_plugins
    PUBLIC sessionflow_core sessionflow_engine
    PRIVATE Threads::Threads sessionflow_warnings
)

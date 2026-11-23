add_executable(sessionflow
    main.cpp
)
target_link_libraries(sessionflow PRIVATE
    sessionflow_core
    sessionflow_model
    sessionflow_engine
    sessionflow_media
    sessionflow_midi
    sessionflow_plugins
    sessionflow_devices
    sessionflow_library
    imgui
    SDL3::SDL3
    sessionflow_warnings
)

add_executable(sessionflow
    main.cpp
)
target_link_libraries(sessionflow PRIVATE sessionflow_core sessionflow_warnings)

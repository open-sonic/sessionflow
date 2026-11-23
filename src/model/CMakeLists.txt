add_library(sessionflow_model
    project.cpp
)
target_link_libraries(sessionflow_model
    PUBLIC sessionflow_core immer
    PRIVATE sessionflow_warnings
)

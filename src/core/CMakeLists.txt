add_library(sessionflow_core
    version.cpp
)
target_include_directories(sessionflow_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(sessionflow_core PRIVATE SESSIONFLOW_VERSION="${PROJECT_VERSION}")
target_link_libraries(sessionflow_core PRIVATE sessionflow_warnings)

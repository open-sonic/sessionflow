add_library(sessionflow_media
    audio_file.cpp
)
target_link_libraries(sessionflow_media
    PUBLIC sessionflow_core
    PRIVATE dr_libs sessionflow_warnings
)

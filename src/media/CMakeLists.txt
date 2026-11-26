add_library(sessionflow_media
    audio_file.cpp
    peak_cache.cpp
    resampler.cpp
)
target_link_libraries(sessionflow_media
    PUBLIC sessionflow_core
    PRIVATE dr_libs r8brain sessionflow_warnings
)

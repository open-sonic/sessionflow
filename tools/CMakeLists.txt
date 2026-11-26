add_executable(sessionflow_audio_check
    audio_check.cpp
)
target_link_libraries(sessionflow_audio_check PRIVATE sessionflow_engine sessionflow_warnings)

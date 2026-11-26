include(FetchContent)

FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.8.1
    GIT_SHALLOW TRUE
    SYSTEM
)
FetchContent_MakeAvailable(Catch2)

add_executable(sessionflow_tests
    audio_buffer_tests.cpp
    audio_file_tests.cpp
    database_tests.cpp
    device_listing_tests.cpp
    gain_tests.cpp
    peak_cache_tests.cpp
    plugin_folders_tests.cpp
    project_tests.cpp
    resampler_tests.cpp
    smoother_tests.cpp
    spsc_queue_tests.cpp
    time_tests.cpp
    version_tests.cpp
)
target_include_directories(sessionflow_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(sessionflow_tests PRIVATE SESSIONFLOW_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
target_link_libraries(sessionflow_tests PRIVATE
    sessionflow_core
    sessionflow_model
    sessionflow_engine
    sessionflow_media
    sessionflow_midi
    sessionflow_plugins
    sessionflow_devices
    sessionflow_library
    sessionflow_warnings
    Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(sessionflow_tests)

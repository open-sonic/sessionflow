include(FetchContent)

# All third-party code, pinned. Each module links only the libraries it owns.
function(sessionflow_fetch_dependencies)
    # Third-party code builds with its own flags. Our warnings-as-errors must not fail on it.
    set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)
    set(BUILD_SHARED_LIBS OFF)
    # Lets the set() calls below override option() defaults in projects with old minimum versions.
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

    FetchContent_Declare(spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.15.1
        GIT_SHALLOW TRUE
        SYSTEM
    )

    # Header-only. SOURCE_SUBDIR skips its CMake project, which builds tests and docs.
    FetchContent_Declare(immer
        GIT_REPOSITORY https://github.com/arximboldi/immer.git
        GIT_TAG v0.8.1
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR none
        SYSTEM
    )

    set(RTAUDIO_BUILD_TESTING OFF)
    set(RTAUDIO_API_ASIO ${WIN32})
    set(RTAUDIO_TARGETNAME_UNINSTALL rtaudio_uninstall)
    FetchContent_Declare(rtaudio
        GIT_REPOSITORY https://github.com/thestk/rtaudio.git
        GIT_TAG 6.0.1
        GIT_SHALLOW TRUE
        SYSTEM
    )

    # No release tags upstream, so pinned to a commit.
    FetchContent_Declare(dr_libs
        GIT_REPOSITORY https://github.com/mackron/dr_libs.git
        GIT_TAG b7aaa89c6c4979c086e04dcb357ea3b20a3d6afe
        SOURCE_SUBDIR none
        SYSTEM
    )

    # PipeWire support would fetch an unpinned readerwriterqueue.
    set(LIBREMIDI_NO_PIPEWIRE ON)
    set(LIBREMIDI_NO_BOOST ON)
    FetchContent_Declare(libremidi
        GIT_REPOSITORY https://github.com/celtera/libremidi.git
        GIT_TAG v4.5.0
        GIT_SHALLOW TRUE
        SYSTEM
    )

    FetchContent_Declare(sqlite3
        URL https://www.sqlite.org/2025/sqlite-amalgamation-3490100.zip
        URL_HASH SHA256=6cebd1d8403fc58c30e93939b246f3e6e58d0765a5cd50546f16c00fd805d2c3
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SYSTEM
    )

    set(SDL_SHARED OFF)
    set(SDL_STATIC ON)
    set(SDL_TEST_LIBRARY OFF)
    set(SDL_EXAMPLES OFF)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.2.10
        GIT_SHALLOW TRUE
        SYSTEM
    )

    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.91.9
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR none
        SYSTEM
    )

    FetchContent_MakeAvailable(spdlog immer rtaudio dr_libs libremidi sqlite3 SDL3 imgui)

    add_library(immer INTERFACE)
    target_include_directories(immer SYSTEM INTERFACE ${immer_SOURCE_DIR})

    add_library(dr_libs INTERFACE)
    target_include_directories(dr_libs SYSTEM INTERFACE ${dr_libs_SOURCE_DIR})

    find_package(Threads REQUIRED)
    add_library(sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
    target_include_directories(sqlite3 SYSTEM PUBLIC ${sqlite3_SOURCE_DIR})
    target_compile_definitions(sqlite3 PRIVATE SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0)
    target_link_libraries(sqlite3 PRIVATE Threads::Threads)

    add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
    )
    target_include_directories(imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_link_libraries(imgui PUBLIC SDL3::SDL3)
endfunction()

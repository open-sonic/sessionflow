#include "core/version.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <spdlog/spdlog.h>

#include <string_view>

namespace
{

// Navy from brand/tokens.json.
constexpr Uint8 s_background[] = {0x07, 0x20, 0x59};

// ImGui resolves hover and layout one frame after the input that changed them, so a single
// frame per event would leave the window one step behind.
constexpr int s_framesAfterInput = 3;

// The native API per platform. OpenGL on Linux because SDL's Vulkan renderer is the newest
// and least tested, and GL works on every desktop driver.
#if defined(_WIN32)
constexpr const char* s_rendererDriver = "direct3d11";
#elif defined(__APPLE__)
constexpr const char* s_rendererDriver = "metal";
#else
constexpr const char* s_rendererDriver = "opengl";
#endif

SDL_Renderer* createRenderer(SDL_Window* window)
{
    if (SDL_Renderer* renderer = SDL_CreateRenderer(window, s_rendererDriver))
    {
        return renderer;
    }
    spdlog::warn("{} renderer unavailable, using SDL's default: {}", s_rendererDriver, SDL_GetError());
    return SDL_CreateRenderer(window, nullptr);
}

} // namespace

int main(int, char**)
{
    const std::string_view version = sessionflow::version();
    spdlog::info("sessionflow {}", version);

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        spdlog::error("SDL_Init failed: {}", SDL_GetError());
        return 1;
    }

    SDL_Window* window =
        SDL_CreateWindow("Sessionflow", 1280, 800, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    SDL_Renderer* renderer = window != nullptr ? createRenderer(window) : nullptr;
    if (renderer == nullptr)
    {
        spdlog::error("window creation failed: {}", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    spdlog::info("renderer {}", SDL_GetRendererName(renderer));
    SDL_SetRenderVSync(renderer, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    bool running = true;
    int framesToDraw = s_framesAfterInput;
    while (running)
    {
        // Blocks while nothing changed, so an idle window costs no CPU or GPU.
        SDL_Event event;
        const bool hasEvent = framesToDraw > 0 ? SDL_PollEvent(&event) : SDL_WaitEvent(&event);
        if (hasEvent)
        {
            do
            {
                ImGui_ImplSDL3_ProcessEvent(&event);
                if (event.type == SDL_EVENT_QUIT)
                {
                    running = false;
                }
            } while (SDL_PollEvent(&event));
            framesToDraw = s_framesAfterInput;
        }
        if (framesToDraw == 0)
        {
            continue;
        }
        --framesToDraw;

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Sessionflow");
        ImGui::Text("Version %.*s", static_cast<int>(version.size()), version.data());
        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, s_background[0], s_background[1], s_background[2], SDL_ALPHA_OPAQUE);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

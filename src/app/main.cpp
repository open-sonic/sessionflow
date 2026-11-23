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

// Waits for input instead of spinning so an idle window costs no CPU.
constexpr Sint32 s_idleTimeoutMs = 500;

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
    SDL_Renderer* renderer = window != nullptr ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (renderer == nullptr)
    {
        spdlog::error("window creation failed: {}", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    bool running = true;
    while (running)
    {
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, s_idleTimeoutMs))
        {
            do
            {
                ImGui_ImplSDL3_ProcessEvent(&event);
                if (event.type == SDL_EVENT_QUIT)
                {
                    running = false;
                }
            } while (SDL_PollEvent(&event));
        }

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

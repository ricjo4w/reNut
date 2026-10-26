#include "renut_engine/app/mac_sdl.h"
#include <SDL3/SDL.h>
#include <cstdio>

int main() {
    renut::ConfigureMacSDL();
    std::printf("reNut controller check: %s\nPress buttons, move sticks and triggers. Listening for 30 seconds.\n",
                SDL_GetRevision());
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    bool found = false;
    const auto end = SDL_GetTicks() + 30000;
    while (SDL_GetTicks() < end) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
                auto* pad = SDL_OpenGamepad(event.gdevice.which);
                if (pad) {
                    found = true;
                    std::printf("Connected: %s [%04x:%04x]\n", SDL_GetGamepadName(pad),
                        SDL_GetGamepadVendor(pad), SDL_GetGamepadProduct(pad));
                }
            } else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                std::printf("Button: %s\n", SDL_GetGamepadStringForButton(
                    static_cast<SDL_GamepadButton>(event.gbutton.button)));
            } else if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
                if (SDL_abs(event.gaxis.value) > 8000)
                    std::printf("Axis: %s = %d\n", SDL_GetGamepadStringForAxis(
                        static_cast<SDL_GamepadAxis>(event.gaxis.axis)), event.gaxis.value);
            } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                SDL_CloseGamepad(SDL_GetGamepadFromID(event.gdevice.which));
                std::printf("Controller disconnected\n");
            }
        }
        std::fflush(stdout);
        SDL_Delay(10);
    }
    if (!found) std::fprintf(stderr, "No controller detected. Check USB connection.\n");
    SDL_Quit();
    return found ? 0 : 2;
}

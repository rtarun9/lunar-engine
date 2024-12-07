#include <stdint.h>
#include <stdio.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef float f32;
typedef double f64;

// Use this #define so SDL_main doesn't need to be used.
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <vulkan/vulkan.h>

#define VK_CHECK(x)                                                                                                    \
    if (VkResult(x) != VK_SUCCESS)                                                                                     \
    {                                                                                                                  \
        int *ptr = NULL;                                                                                               \
        *ptr = 0;                                                                                                      \
    }

int main(int argc, char *argv[])
{
    // Reference for vulkan initialization.
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        SDL_Log("SDL_Init failed (%s).", SDL_GetError());
        return -1;
    }

    SDL_Window *window =
        SDL_CreateWindow("lunar-engine", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1080, 720, SDL_WINDOW_VULKAN);
    if (!window)
    {
        SDL_Log("Failed to create SDL window. Error : (%s).", SDL_GetError());
        return -1;
    }

    bool quit = false;
    while (!quit)
    {
        SDL_Event event = {};
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                quit = true;
            }

            u8 *keyboard_state = (u8 *)SDL_GetKeyboardState(NULL);
            if (keyboard_state[SDL_SCANCODE_ESCAPE])
            {
                quit = true;
            }
        }
    }

    SDL_Quit();
}

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
#include <SDL2/SDL_Vulkan.h>
#include <vulkan/vulkan.h>

#include <VkBootstrap.h>

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

    VkExtent2D window_extent = {};
    window_extent.width = 1080;
    window_extent.height = 720;

    SDL_Window *window = SDL_CreateWindow("lunar-engine", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          window_extent.width, window_extent.height, SDL_WINDOW_VULKAN);
    if (!window)
    {
        SDL_Log("Failed to create SDL window. Error : (%s).", SDL_GetError());
        return -1;
    }

    // Core VK objects.
    VkInstance instance = {};
    VkDebugUtilsMessengerEXT debug_messenger = {};
    VkPhysicalDevice physical_device = {};
    VkDevice device = {};
    VkSurfaceKHR surface = {};

    // Initialize core VK objects (using vk boostrap for now).
    vkb::InstanceBuilder builder;
    auto inst_ret = builder.set_app_name("lunar-engine")
                        .request_validation_layers(true)
                        .use_default_debug_messenger()
                        .require_api_version(1, 3, 0)
                        .build();

    vkb::Instance vkb_inst = inst_ret.value();
    instance = vkb_inst.instance;
    debug_messenger = vkb_inst.debug_messenger;

    SDL_Vulkan_CreateSurface(window, instance, &surface);

    // vulkan 1.3 features
    VkPhysicalDeviceVulkan13Features features_13 = {};
    features_13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features_13.dynamicRendering = true;
    features_13.synchronization2 = true;

    // vulkan 1.2 features
    VkPhysicalDeviceVulkan12Features features_12 = {};
    features_12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features_12.bufferDeviceAddress = true;
    features_12.descriptorIndexing = true;

    // use vkbootstrap to select a gpu.
    // We want a gpu that can write to the SDL surface and supports vulkan 1.3 with the correct features
    vkb::PhysicalDeviceSelector selector{vkb_inst};
    vkb::PhysicalDevice vkb_physical_device = selector.set_minimum_version(1, 3)
                                                  .set_required_features_13(features_13)
                                                  .set_required_features_12(features_12)
                                                  .set_surface(surface)
                                                  .select()
                                                  .value();

    // create the final vulkan device
    vkb::DeviceBuilder device_builder{vkb_physical_device};

    vkb::Device vkb_device = device_builder.build().value();

    // Get the VkDevice handle used in the rest of a vulkan application
    device = vkb_device.device;
    physical_device = vkb_physical_device.physical_device;

    i64 frame_number = 0;

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

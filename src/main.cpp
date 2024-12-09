#include <stdint.h>
#include <stdio.h>

#include <vector>

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

struct frame_data_t
{
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;

    // Used to let the CPU know when this frame's GPU side rendering has been completed.
    VkFence render_fence;

    // Used to let the GPU know when a swapchain image has been retrieved.
    VkSemaphore swapchain_semaphore;

    // Used to let the GPU know when to present the swapchain image (i.e only after rendering on the image has been
    // completed).
    VkSemaphore render_semaphore;
};

#define FRAME_OVERLAP (u32)2

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
    // Swapchain related objects and init code.
    VkSwapchainKHR swapchain = {};
    VkFormat swapchain_image_format = VK_FORMAT_R8G8B8A8_UNORM;
    std::vector<VkImage> swapchain_images = {};
    std::vector<VkImageView> swapchain_image_views = {};
    VkExtent2D swapchain_extent = {};

    VkSurfaceFormatKHR surface_format = {};
    surface_format.format = swapchain_image_format;
    surface_format.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

    vkb::SwapchainBuilder vkb_swapchain_builder(physical_device, device, surface);
    vkb::Swapchain vkb_swapchain = vkb_swapchain_builder.set_desired_format(surface_format)
                                       .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
                                       .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                                       .set_desired_extent(window_extent.width, window_extent.height)
                                       .build()
                                       .value();

    swapchain_extent = vkb_swapchain.extent;
    swapchain = vkb_swapchain.swapchain;
    swapchain_images = vkb_swapchain.get_images().value();
    swapchain_image_views = vkb_swapchain.get_image_views().value();

    VkQueue graphics_queue = {};
    u32 graphics_queue_family = 0;

    frame_data_t frame_data[FRAME_OVERLAP];

    graphics_queue = vkb_device.get_queue(vkb::QueueType::graphics).value();
    graphics_queue_family = vkb_device.get_queue_index(vkb::QueueType::graphics).value();

    // Create the command pool and buffer.
    VkCommandPoolCreateInfo command_pool_create_info = {};
    command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    command_pool_create_info.queueFamilyIndex = graphics_queue_family;

    for (i32 i = 0; i < FRAME_OVERLAP; i++)
    {
        VK_CHECK(vkCreateCommandPool(device, &command_pool_create_info, NULL, &frame_data[i].command_pool));

        // Now that command pool is created, allocate command buffers from it.
        VkCommandBufferAllocateInfo command_buffer_allocate_info = {};
        command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        command_buffer_allocate_info.commandBufferCount = 1;
        command_buffer_allocate_info.commandPool = frame_data[i].command_pool;
        command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

        VK_CHECK(vkAllocateCommandBuffers(device, &command_buffer_allocate_info, &frame_data[i].command_buffer));

        // Create sync primitives.
        VkFenceCreateInfo fence_create_info = {};
        fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_create_info.pNext = NULL;

        VK_CHECK(vkCreateFence(device, &fence_create_info, NULL, &frame_data[i].render_fence));

        VkSemaphoreCreateInfo semaphore_create_info = {};
        semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VK_CHECK(vkCreateSemaphore(device, &semaphore_create_info, NULL, &frame_data[i].render_semaphore));
        VK_CHECK(vkCreateSemaphore(device, &semaphore_create_info, NULL, &frame_data[i].swapchain_semaphore));
    }

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

        // Main render loop.
        {
            frame_data_t *current_frame_data = &frame_data[frame_number % FRAME_OVERLAP];

            VK_CHECK(vkWaitForFences(device, 1, &(current_frame_data->render_fence), true, 1e9));
            VK_CHECK(vkResetFences(device, 1, &(current_frame_data->render_fence)));

            // Request the swapchain for a image.
            u32 swapchain_image_index = 0;
            VK_CHECK(vkAcquireNextImageKHR(device, swapchain, 1e9, current_frame_data->swapchain_semaphore, NULL,
                                           &swapchain_image_index));

            VkCommandBuffer cmd = current_frame_data->command_buffer;

            VK_CHECK(vkResetCommandBuffer(cmd, 0));

            VkCommandBufferBeginInfo command_buffer_begin_info = {};
            command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            command_buffer_begin_info.pInheritanceInfo = NULL;
            command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            VK_CHECK(vkBeginCommandBuffer(cmd, &command_buffer_begin_info));

            VkImageSubresourceRange subresource_range = {};
            subresource_range.aspectMask = VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT;
            subresource_range.baseMipLevel = 1;
            subresource_range.levelCount = 1;
            subresource_range.baseArrayLayer = 1;
            subresource_range.layerCount = 1;

            // Transition the swapchain image into a layout where we can draw into it.
            {
                VkImageMemoryBarrier2 image_memory_barrier = {};
                image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                image_memory_barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                image_memory_barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
                image_memory_barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                image_memory_barrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
                image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;

                image_memory_barrier.image = swapchain_images[swapchain_image_index];

                image_memory_barrier.subresourceRange = subresource_range;

                VkDependencyInfo dependency_info = {};
                dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                dependency_info.imageMemoryBarrierCount = 1;
                dependency_info.pImageMemoryBarriers = &image_memory_barrier;

                vkCmdPipelineBarrier2(cmd, &dependency_info);
            }

            VkClearColorValue clear_color = {};
            clear_color.float32[2] = sinf(frame_number / 120.0f);

            vkCmdClearColorImage(cmd, swapchain_images[swapchain_image_index], VK_IMAGE_LAYOUT_GENERAL, &clear_color, 1,
                                 &subresource_range);

            // Transition the swapchain image back to a format that can be presented.
            {
                VkImageMemoryBarrier2 image_memory_barrier = {};
                image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                image_memory_barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                image_memory_barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
                image_memory_barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                image_memory_barrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
                image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

                image_memory_barrier.image = swapchain_images[swapchain_image_index];

                image_memory_barrier.subresourceRange = subresource_range;

                VkDependencyInfo dependency_info = {};
                dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                dependency_info.imageMemoryBarrierCount = 1;
                dependency_info.pImageMemoryBarriers = &image_memory_barrier;

                vkCmdPipelineBarrier2(cmd, &dependency_info);
            }

            VK_CHECK(vkEndCommandBuffer(cmd));
        }
        ++frame_number;
    }

    // Wait for all gpu operations to be completed.
    vkDeviceWaitIdle(device);

    for (i32 i = 0; i < FRAME_OVERLAP; i++)
    {
        vkDestroyCommandPool(device, frame_data[i].command_pool, NULL);

        vkDestroySemaphore(device, frame_data[i].render_semaphore, NULL);
        vkDestroySemaphore(device, frame_data[i].swapchain_semaphore, NULL);

        vkDestroyFence(device, frame_data[i].render_fence, NULL);
    }

    vkDestroySwapchainKHR(device, swapchain, NULL);
    for (i32 i = 0; i < swapchain_image_views.size(); i++)
    {
        vkDestroyImageView(device, swapchain_image_views[i], NULL);
    }

    vkDestroySurfaceKHR(instance, surface, NULL);

    vkDestroyDevice(device, NULL);

    vkb::destroy_debug_utils_messenger(instance, debug_messenger);
    vkDestroyInstance(instance, NULL);

    SDL_Quit();
}

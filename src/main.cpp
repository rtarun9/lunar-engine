#include "common.h"
#include "dynamic_array.h"

#include <stdio.h>
#include <vector>

// Use this #define so SDL_main doesn't need to be used.
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_Vulkan.h>
#include <vulkan/vulkan.h>

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include <VkBootstrap.h>

#define VK_CHECK(x) ASSERT((VkResult)x == VK_SUCCESS)

#define FRAME_OVERLAP (u32)2

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

struct allocated_image_t
{
    VkImage image;
    VkImageView image_view;
    VmaAllocation allocation;
    VkExtent3D extent;
    VkFormat format;
};

void transition_image(VkCommandBuffer cmd, VkImage image, VkPipelineStageFlags2 src_pipeline_stage_flag,
                      VkPipelineStageFlags2 dst_pipeline_stage_flag, VkImageLayout old_layout, VkImageLayout new_layout)
{
    VkImageSubresourceRange subresource_range = {};
    subresource_range.aspectMask = VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT;
    subresource_range.baseMipLevel = 0;
    subresource_range.levelCount = VK_REMAINING_MIP_LEVELS;
    subresource_range.baseArrayLayer = 0;
    subresource_range.layerCount = VK_REMAINING_ARRAY_LAYERS;

    VkImageMemoryBarrier2 image_memory_barrier = {};
    image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    image_memory_barrier.srcStageMask = src_pipeline_stage_flag;
    image_memory_barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    image_memory_barrier.dstStageMask = dst_pipeline_stage_flag;
    image_memory_barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    image_memory_barrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
    image_memory_barrier.oldLayout = old_layout;
    image_memory_barrier.newLayout = new_layout;

    image_memory_barrier.image = image;

    image_memory_barrier.subresourceRange = subresource_range;

    VkDependencyInfo dependency_info = {};
    dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency_info.imageMemoryBarrierCount = 1;
    dependency_info.pImageMemoryBarriers = &image_memory_barrier;

    vkCmdPipelineBarrier2(cmd, &dependency_info);
}
void blit_image(VkCommandBuffer cmd, VkImage source, VkExtent2D source_extent, VkImage dest, VkExtent2D dest_extent)
{
    VkImageBlit2 image_blit = {};
    image_blit.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;

    image_blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_blit.srcSubresource.mipLevel = 0;
    image_blit.srcSubresource.baseArrayLayer = 0;
    image_blit.srcSubresource.layerCount = 1;

    image_blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_blit.dstSubresource.mipLevel = 0;
    image_blit.dstSubresource.baseArrayLayer = 0;
    image_blit.dstSubresource.layerCount = 1;

    image_blit.srcOffsets[0].x = 0;
    image_blit.srcOffsets[0].y = 0;
    image_blit.srcOffsets[0].z = 0;

    image_blit.srcOffsets[1].x = source_extent.width;
    image_blit.srcOffsets[1].y = source_extent.height;
    image_blit.srcOffsets[1].z = 1;

    image_blit.dstOffsets[0].x = 0;
    image_blit.dstOffsets[0].y = 0;
    image_blit.dstOffsets[0].z = 0;

    image_blit.dstOffsets[1].x = dest_extent.width;
    image_blit.dstOffsets[1].y = dest_extent.height;
    image_blit.dstOffsets[1].z = 1;

    VkImageSubresourceLayers default_subresource_layers = {};
    default_subresource_layers.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    default_subresource_layers.mipLevel = 0;
    default_subresource_layers.baseArrayLayer = 0;
    default_subresource_layers.layerCount = 1;

    image_blit.srcSubresource = default_subresource_layers;
    image_blit.dstSubresource = default_subresource_layers;

    VkBlitImageInfo2 blit_image_info = {};
    blit_image_info.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
    blit_image_info.srcImage = source;
    blit_image_info.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    blit_image_info.dstImage = dest;
    blit_image_info.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    blit_image_info.filter = VK_FILTER_LINEAR;
    blit_image_info.regionCount = 1;
    blit_image_info.pRegions = &image_blit;

    vkCmdBlitImage2(cmd, &blit_image_info);
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

    graphics_queue = vkb_device.get_queue(vkb::QueueType::graphics).value();
    graphics_queue_family = vkb_device.get_queue_index(vkb::QueueType::graphics).value();

    frame_data_t frame_data[FRAME_OVERLAP];

    for (i32 i = 0; i < FRAME_OVERLAP; i++)
    {
        // Create the command pool and buffer.
        VkCommandPoolCreateInfo command_pool_create_info = {};
        command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        command_pool_create_info.queueFamilyIndex = graphics_queue_family;

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

    // Initialize vma.
    VmaAllocator vma_allocator = {};

    VmaAllocatorCreateInfo vma_allocator_create_info = {};
    vma_allocator_create_info.physicalDevice = physical_device;
    vma_allocator_create_info.device = device;
    vma_allocator_create_info.instance = instance;
    vma_allocator_create_info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    VK_CHECK(vmaCreateAllocator(&vma_allocator_create_info, &vma_allocator));

    allocated_image_t draw_image = {};

    draw_image.format = VkFormat::VK_FORMAT_R16G16B16A16_SFLOAT;

    VkImageCreateInfo draw_image_create_info = {};
    draw_image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    draw_image_create_info.flags = 0;
    draw_image_create_info.imageType = VkImageType::VK_IMAGE_TYPE_2D;
    draw_image_create_info.format = draw_image.format;

    VkExtent3D draw_image_extent = {};
    draw_image_extent.width = swapchain_extent.width;
    draw_image_extent.height = swapchain_extent.height;
    draw_image_extent.depth = 1;

    draw_image_create_info.extent = draw_image_extent;
    draw_image_create_info.mipLevels = 1;
    draw_image_create_info.arrayLayers = 1;
    draw_image_create_info.samples = VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT;
    draw_image_create_info.tiling = VkImageTiling::VK_IMAGE_TILING_OPTIMAL;
    draw_image_create_info.usage = VkImageUsageFlagBits::VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                   VkImageUsageFlagBits::VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                   VkImageUsageFlagBits::VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                   VkImageUsageFlagBits::VK_IMAGE_USAGE_STORAGE_BIT;
    draw_image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    draw_image_create_info.queueFamilyIndexCount = 1;
    draw_image_create_info.pQueueFamilyIndices = &graphics_queue_family;
    draw_image_create_info.initialLayout = VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo draw_image_vma_allocation_create_info = {};
    draw_image_vma_allocation_create_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    draw_image_vma_allocation_create_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VK_CHECK(vmaCreateImage(vma_allocator, &draw_image_create_info, &draw_image_vma_allocation_create_info,
                            &draw_image.image, &draw_image.allocation, NULL));

    // Create the draw image view.
    VkImageViewCreateInfo draw_image_view_create_info = {};
    draw_image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    draw_image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    draw_image_view_create_info.image = draw_image.image;
    draw_image_view_create_info.format = draw_image.format;
    draw_image_view_create_info.subresourceRange.baseMipLevel = 0;
    draw_image_view_create_info.subresourceRange.levelCount = 1;
    draw_image_view_create_info.subresourceRange.baseArrayLayer = 0;
    draw_image_view_create_info.subresourceRange.layerCount = 1;
    draw_image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    VK_CHECK(vkCreateImageView(device, &draw_image_view_create_info, NULL, &draw_image.image_view));

    // Create description set layout with a single RW texture 2d.
    VkDescriptorSetLayoutBinding descriptor_set_layout_binding = {};
    descriptor_set_layout_binding.binding = 0;
    descriptor_set_layout_binding.descriptorCount = 1;
    descriptor_set_layout_binding.descriptorType = VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptor_set_layout_binding.stageFlags = VkShaderStageFlagBits::VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = {};
    descriptor_set_layout_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptor_set_layout_create_info.flags = 0;
    descriptor_set_layout_create_info.bindingCount = 1;
    descriptor_set_layout_create_info.pBindings = &descriptor_set_layout_binding;

    VkDescriptorSetLayout descriptor_set_layout = {};
    VK_CHECK(vkCreateDescriptorSetLayout(device, &descriptor_set_layout_create_info, NULL, &descriptor_set_layout));

    // Create descriptor pool that will be used to allocate descriptor sets.
    VkDescriptorPoolCreateInfo descriptor_pool_create_info = {};
    descriptor_pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptor_pool_create_info.maxSets = 1;
    descriptor_pool_create_info.poolSizeCount = 1;

    VkDescriptorPoolSize storage_buffer_descriptor_pool_size = {};
    storage_buffer_descriptor_pool_size.type = VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    storage_buffer_descriptor_pool_size.descriptorCount = 10;

    descriptor_pool_create_info.pPoolSizes = &storage_buffer_descriptor_pool_size;

    VkDescriptorPool descriptor_pool = {};
    VK_CHECK(vkCreateDescriptorPool(device, &descriptor_pool_create_info, NULL, &descriptor_pool));

    // Allocate a descriptor from the pool.
    VkDescriptorSet descriptor_set = {};

    VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {};
    descriptor_set_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptor_set_allocate_info.descriptorPool = descriptor_pool;
    descriptor_set_allocate_info.descriptorSetCount = 1;
    descriptor_set_allocate_info.pSetLayouts = &descriptor_set_layout;

    VK_CHECK(vkAllocateDescriptorSets(device, &descriptor_set_allocate_info, &descriptor_set));

    // Update the descriptor so that it points to the draw image.
    VkWriteDescriptorSet draw_image_descriptor_write = {};
    draw_image_descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    draw_image_descriptor_write.dstSet = descriptor_set;
    draw_image_descriptor_write.dstBinding = 0;
    draw_image_descriptor_write.descriptorCount = 1;
    draw_image_descriptor_write.descriptorType = VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

    VkDescriptorImageInfo descriptor_image_info = {};
    descriptor_image_info.imageView = draw_image.image_view;
    descriptor_image_info.imageLayout = VkImageLayout::VK_IMAGE_LAYOUT_GENERAL;

    draw_image_descriptor_write.pImageInfo = &descriptor_image_info;

    vkUpdateDescriptorSets(device, 1, &draw_image_descriptor_write, 0, NULL);

    // Create the shader module for gradient compute shader.
    SDL_RWops *comp_shader_spirv_rw_ops = SDL_RWFromFile("shaders/gradient.comp.spv", "br");
    ASSERT(comp_shader_spirv_rw_ops);

    u64 size = comp_shader_spirv_rw_ops->size(comp_shader_spirv_rw_ops);
    printf("size is : %d", size);

    dynamic_array_t comp_shader_data = create_dynamic_array(1, size);
    comp_shader_spirv_rw_ops->read(comp_shader_spirv_rw_ops, comp_shader_data.data, size, size);

    VkShaderModuleCreateInfo compute_shader_module_create_info = {};
    compute_shader_module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    compute_shader_module_create_info.codeSize = size;
    compute_shader_module_create_info.pCode = (u32 *)comp_shader_data.data;

    VkShaderModule compute_shader_module = {};
    VK_CHECK(vkCreateShaderModule(device, &compute_shader_module_create_info, NULL, &compute_shader_module));

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
            VK_CHECK(vkAcquireNextImageKHR(device, swapchain, SECONDS_IN_NS(1), current_frame_data->swapchain_semaphore,
                                           NULL, &swapchain_image_index));

            VkCommandBuffer cmd = current_frame_data->command_buffer;

            VK_CHECK(vkResetCommandBuffer(cmd, 0));

            VkCommandBufferBeginInfo command_buffer_begin_info = {};
            command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            command_buffer_begin_info.pInheritanceInfo = NULL;
            command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            VK_CHECK(vkBeginCommandBuffer(cmd, &command_buffer_begin_info));

            VkImageSubresourceRange subresource_range = {};
            subresource_range.aspectMask = VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT;
            subresource_range.baseMipLevel = 0;
            subresource_range.levelCount = VK_REMAINING_MIP_LEVELS;
            subresource_range.baseArrayLayer = 0;
            subresource_range.layerCount = VK_REMAINING_ARRAY_LAYERS;

            // Transition draw image so that it can be cleared into.
            transition_image(cmd, draw_image.image, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_GENERAL);

            VkClearColorValue clear_color = {};
            clear_color.float32[2] = sinf(frame_number / 120.0f);

            vkCmdClearColorImage(cmd, draw_image.image, VK_IMAGE_LAYOUT_GENERAL, &clear_color, 1, &subresource_range);

            transition_image(cmd, draw_image.image, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_2_BLIT_BIT, VK_IMAGE_LAYOUT_GENERAL,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

            transition_image(cmd, swapchain_images[swapchain_image_index],
                             VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT,
                             VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            VkExtent2D src_extent = {};
            src_extent.width = draw_image.extent.width;
            src_extent.height = draw_image.extent.height;

            VkExtent2D dst_extent = {};
            dst_extent.width = swapchain_extent.width;
            dst_extent.height = swapchain_extent.height;

            blit_image(cmd, draw_image.image, src_extent, swapchain_images[swapchain_image_index], dst_extent);

            transition_image(cmd, swapchain_images[swapchain_image_index], VK_PIPELINE_STAGE_2_BLIT_BIT,
                             VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

            VK_CHECK(vkEndCommandBuffer(cmd));

            // Now the commands we want to execute and recorded in the command buffer. Time to submit the command buffer
            // to the queue.
            VkCommandBufferSubmitInfo cmd_submit_info = {};
            cmd_submit_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            cmd_submit_info.deviceMask = 0;
            cmd_submit_info.commandBuffer = cmd;

            // Fill the semaphore wait and signal info.
            // Wait until swapchain image has been acquired, and signal the render semaphore so that only once rendering
            // is complete, presentation can be done.
            VkSemaphoreSubmitInfo swapchain_semaphore_submit_info = {};
            swapchain_semaphore_submit_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            swapchain_semaphore_submit_info.semaphore = current_frame_data->swapchain_semaphore;
            swapchain_semaphore_submit_info.deviceIndex = 0;
            swapchain_semaphore_submit_info.value = 1;
            swapchain_semaphore_submit_info.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

            VkSemaphoreSubmitInfo render_semaphore_submit_info = {};
            render_semaphore_submit_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            render_semaphore_submit_info.semaphore = current_frame_data->render_semaphore;
            render_semaphore_submit_info.deviceIndex = 0;
            render_semaphore_submit_info.value = 1;
            render_semaphore_submit_info.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;

            VkSubmitInfo2 submit_info = {};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submit_info.waitSemaphoreInfoCount = 1;
            submit_info.pWaitSemaphoreInfos = &swapchain_semaphore_submit_info;
            submit_info.signalSemaphoreInfoCount = 1;
            submit_info.pSignalSemaphoreInfos = &render_semaphore_submit_info;
            submit_info.commandBufferInfoCount = 1;
            submit_info.pCommandBufferInfos = &cmd_submit_info;

            VK_CHECK(vkQueueSubmit2(graphics_queue, 1, &submit_info, current_frame_data->render_fence));

            VkPresentInfoKHR present_info = {};
            present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            present_info.swapchainCount = 1;
            present_info.pSwapchains = &swapchain;
            present_info.pWaitSemaphores = &(current_frame_data->render_semaphore);
            present_info.waitSemaphoreCount = 1;
            present_info.pImageIndices = &swapchain_image_index;

            VK_CHECK(vkQueuePresentKHR(graphics_queue, &present_info));
        }
        ++frame_number;
    }

    // Wait for all gpu operations to be completed.
    vkDeviceWaitIdle(device);

    vkDestroyImageView(device, draw_image.image_view, NULL);
    vmaDestroyImage(vma_allocator, draw_image.image, draw_image.allocation);

    vmaDestroyAllocator(vma_allocator);

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

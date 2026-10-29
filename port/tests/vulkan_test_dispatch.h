// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cassert>
#include "video_core/renderer_vulkan/vk_instance.h"

// Match test_scene_resolution: do not emit Vulkan-Hpp init() in the executable.
// Its platform-dependent dispatcher layout can interpose the shared library's
// init() and call a null vkGetInstanceProcAddr during Instance construction.
inline vk::detail::DispatchLoaderDynamic VulkanTestDispatch(const Vulkan::Instance& instance) {
    static vk::detail::DynamicLoader loader;
    const auto get_instance = loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    assert(get_instance);
    const auto get_device = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        get_instance(instance.GetInstance(), "vkGetDeviceProcAddr"));
    assert(get_device);
    vk::detail::DispatchLoaderDynamic dispatch;
#define LOAD_TEST_VK(name) \
    dispatch.name = reinterpret_cast<PFN_##name>(get_device(instance.GetDevice(), #name)); \
    assert(dispatch.name)
    LOAD_TEST_VK(vkCreateImageView);
    LOAD_TEST_VK(vkDestroyImageView);
    LOAD_TEST_VK(vkCreateDescriptorSetLayout);
    LOAD_TEST_VK(vkDestroyDescriptorSetLayout);
    LOAD_TEST_VK(vkCreatePipelineLayout);
    LOAD_TEST_VK(vkDestroyPipelineLayout);
    LOAD_TEST_VK(vkCreateShaderModule);
    LOAD_TEST_VK(vkDestroyShaderModule);
    LOAD_TEST_VK(vkCreateComputePipelines);
    LOAD_TEST_VK(vkDestroyPipeline);
    LOAD_TEST_VK(vkCreateSampler);
    LOAD_TEST_VK(vkDestroySampler);
    LOAD_TEST_VK(vkCmdPipelineBarrier2);
    LOAD_TEST_VK(vkCmdCopyBufferToImage);
    LOAD_TEST_VK(vkCmdCopyImageToBuffer);
    LOAD_TEST_VK(vkCmdBindPipeline);
    LOAD_TEST_VK(vkCmdPushDescriptorSetKHR);
    LOAD_TEST_VK(vkCmdPushConstants);
    LOAD_TEST_VK(vkCmdDispatch);
#undef LOAD_TEST_VK
    return dispatch;
}

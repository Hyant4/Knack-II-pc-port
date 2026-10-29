// SPDX-License-Identifier: GPL-2.0-or-later
// Synthetic, offscreen Vulkan regression. No game files or shader-cache input.
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

static void Check(VkResult result) {
    if (result != VK_SUCCESS) { std::fprintf(stderr,"Vulkan error %d\n",result); std::exit(2); }
}
static std::vector<uint32_t> Read(const std::filesystem::path& path) {
    std::ifstream f(path,std::ios::binary|std::ios::ate); assert(f && f.tellg()%4==0);
    std::vector<uint32_t> words(size_t(f.tellg())/4); f.seekg(0);
    f.read(reinterpret_cast<char*>(words.data()),words.size()*4); assert(f); return words;
}
int main(int argc,char**argv) {
    assert(argc==2); const std::filesystem::path dir(argv[1]);
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ici.pApplicationInfo=&app;
    VkInstance instance;Check(vkCreateInstance(&ici,nullptr,&instance));
    uint32_t n=0;Check(vkEnumeratePhysicalDevices(instance,&n,nullptr));assert(n);
    std::vector<VkPhysicalDevice> devices(n);Check(vkEnumeratePhysicalDevices(instance,&n,devices.data()));
    const auto physical=devices[0]; VkPhysicalDeviceProperties properties;vkGetPhysicalDeviceProperties(physical,&properties);
    std::printf("GPU: %s\n",properties.deviceName);
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan11Features f11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};f11.pNext=&f12;
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext=&f11;
    vkGetPhysicalDeviceFeatures2(physical,&features);assert(features.features.geometryShader);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,nullptr);std::vector<VkQueueFamilyProperties> families(n);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,families.data());
    uint32_t family=0;while(!(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))++family;
    float priority=1; VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dci.queueCreateInfoCount=1;dci.pQueueCreateInfos=&qi;
    dci.pNext=&features;VkDevice device;Check(vkCreateDevice(physical,&dci,nullptr,&device));
    VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
    VkPhysicalDeviceMemoryProperties memory;vkGetPhysicalDeviceMemoryProperties(physical,&memory);
    const auto memory_type=[&](uint32_t bits,VkMemoryPropertyFlags flags){
        for(uint32_t i=0;i<memory.memoryTypeCount;++i)if((bits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&flags)==flags)return i;
        std::abort();
    };
    constexpr uint32_t W=32,H=32;constexpr VkDeviceSize bytes=W*H*4*sizeof(float);
    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image_info.imageType=VK_IMAGE_TYPE_2D;
    image_info.format=VK_FORMAT_R32G32B32A32_SFLOAT;image_info.extent={W,H,1};image_info.mipLevels=1;image_info.arrayLayers=1;
    image_info.samples=VK_SAMPLE_COUNT_1_BIT;image_info.tiling=VK_IMAGE_TILING_OPTIMAL;
    image_info.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkImage image;Check(vkCreateImage(device,&image_info,nullptr,&image));VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device,image,&requirements);VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=memory_type(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory image_memory;Check(vkAllocateMemory(device,&allocation,nullptr,&image_memory));Check(vkBindImageMemory(device,image,image_memory,0));
    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view_info.image=image;view_info.viewType=VK_IMAGE_VIEW_TYPE_2D;
    view_info.format=image_info.format;view_info.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    VkImageView view;Check(vkCreateImageView(device,&view_info,nullptr,&view));
    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};buffer_info.size=bytes;buffer_info.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer buffer;Check(vkCreateBuffer(device,&buffer_info,nullptr,&buffer));vkGetBufferMemoryRequirements(device,buffer,&requirements);
    allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=memory_type(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory buffer_memory;Check(vkAllocateMemory(device,&allocation,nullptr,&buffer_memory));Check(vkBindBufferMemory(device,buffer,buffer_memory,0));
    VkAttachmentDescription attachment{};attachment.format=image_info.format;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachment.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference reference{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription subpass{};
    subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&reference;
    VkSubpassDependency dependency{0,VK_SUBPASS_EXTERNAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,0};
    VkRenderPassCreateInfo rp_info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rp_info.attachmentCount=1;rp_info.pAttachments=&attachment;
    rp_info.subpassCount=1;rp_info.pSubpasses=&subpass;rp_info.dependencyCount=1;rp_info.pDependencies=&dependency;
    VkRenderPass render_pass;Check(vkCreateRenderPass(device,&rp_info,nullptr,&render_pass));
    VkFramebufferCreateInfo fb_info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb_info.renderPass=render_pass;fb_info.attachmentCount=1;
    fb_info.pAttachments=&view;fb_info.width=W;fb_info.height=H;fb_info.layers=1;VkFramebuffer framebuffer;Check(vkCreateFramebuffer(device,&fb_info,nullptr,&framebuffer));
    VkPipelineLayoutCreateInfo pl_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};VkPipelineLayout layout;
    Check(vkCreatePipelineLayout(device,&pl_info,nullptr,&layout));
    VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool_info.queueFamilyIndex=family;pool_info.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool;Check(vkCreateCommandPool(device,&pool_info,nullptr,&pool));
    VkCommandBufferAllocateInfo cb_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};cb_info.commandPool=pool;cb_info.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;cb_info.commandBufferCount=1;
    VkCommandBuffer command;Check(vkAllocateCommandBuffers(device,&cb_info,&command));
    bool failed=false;
    for(const char* label: {"smooth","sample","centroid","linear","linear-sample","linear-centroid","pull"}) {
        std::array<VkShaderModule,3> modules{}; std::array<VkPipelineShaderStageCreateInfo,3> stages{};
        std::array paths{dir/"synthetic.vert.spv",dir/(std::string(label)+".geom.spv"),dir/(std::string(label)+".frag.spv")};
        const VkShaderStageFlagBits stage_bits[]={VK_SHADER_STAGE_VERTEX_BIT,VK_SHADER_STAGE_GEOMETRY_BIT,VK_SHADER_STAGE_FRAGMENT_BIT};
        for(uint32_t j=0;j<3;++j){const auto words=Read(paths[j]);VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=words.size()*4;sm.pCode=words.data();
            Check(vkCreateShaderModule(device,&sm,nullptr,&modules[j]));stages[j]={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};stages[j].stage=stage_bits[j];stages[j].module=modules[j];stages[j].pName="main";}
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{0,0,float(W),float(H),0,1};VkRect2D scissor{{0,0},{W,H}};
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=1;vp.pViewports=&viewport;vp.scissorCount=1;vp.pScissors=&scissor;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode=VK_POLYGON_MODE_FILL;raster.lineWidth=1;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=15;
        VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};bs.attachmentCount=1;bs.pAttachments=&blend;
        VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};pi.stageCount=3;pi.pStages=stages.data();pi.pVertexInputState=&vi;
        pi.pInputAssemblyState=&ia;pi.pViewportState=&vp;pi.pRasterizationState=&raster;pi.pMultisampleState=&ms;pi.pColorBlendState=&bs;pi.layout=layout;pi.renderPass=render_pass;
        VkPipeline pipeline;Check(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pi,nullptr,&pipeline));
        Check(vkResetCommandBuffer(command,0));VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};Check(vkBeginCommandBuffer(command,&begin));
        VkClearValue clear{};VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};rp.renderPass=render_pass;rp.framebuffer=framebuffer;rp.renderArea=scissor;rp.clearValueCount=1;rp.pClearValues=&clear;
        vkCmdBeginRenderPass(command,&rp,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);vkCmdDraw(command,3,1,0,0);vkCmdEndRenderPass(command);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={W,H,1};
        vkCmdCopyImageToBuffer(command,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        Check(vkEndCommandBuffer(command));VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;Check(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));Check(vkQueueWaitIdle(queue));
        float* pixels;Check(vkMapMemory(device,buffer_memory,0,bytes,0,reinterpret_cast<void**>(&pixels)));
        double max_error=0;uint32_t tested=0,wrong=0;
        const bool linear=std::string(label).starts_with("linear");
        for(uint32_t y=2;y<H-2;++y)for(uint32_t x=2;x<W-2;++x){
            const double b1=(x+0.5)/W,b2=(y+0.5)/H,b0=1-b1-b2;if(b0<0.10)continue;
            const double d=b0+b1/2+b2/4;
            const std::array<double,4> expected{linear?b0:b0/d,linear?b1:b1/2/d,linear?b2:b2/4/d,(0.25*b0+0.5*b1/2+0.75*b2/4)/d};
            bool bad=false;for(uint32_t c=0;c<4;++c){const double got=pixels[(y*W+x)*4+c],error=std::abs(got-expected[c]);if(!std::isfinite(got)||error>0.0001)bad=true;max_error=std::max(max_error,error);}
            ++tested;wrong+=bad;
        }
        vkUnmapMemory(device,buffer_memory);std::printf("%s: %u pixels, wrong=%u, max_error=%.9g\n",label,tested,wrong,max_error);failed|=wrong!=0||tested<200;
        vkDestroyPipeline(device,pipeline,nullptr);for(auto module:modules)vkDestroyShaderModule(device,module,nullptr);
    }
    vkDeviceWaitIdle(device);vkDestroyCommandPool(device,pool,nullptr);vkDestroyPipelineLayout(device,layout,nullptr);
    vkDestroyFramebuffer(device,framebuffer,nullptr);vkDestroyRenderPass(device,render_pass,nullptr);vkDestroyImageView(device,view,nullptr);
    vkDestroyImage(device,image,nullptr);vkFreeMemory(device,image_memory,nullptr);vkDestroyBuffer(device,buffer,nullptr);vkFreeMemory(device,buffer_memory,nullptr);
    vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);return failed?1:0;
}

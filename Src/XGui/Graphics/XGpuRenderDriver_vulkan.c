/******************************************************************************
 * @file       XGpuRenderDriver_vulkan.c
 * @brief      XGui GPU 渲染驱动——Vulkan 实现。
 * @details    实现 XGpuRenderDriver.h 的驱动操作表：离屏会话（渲染目标
 *             VkImage + framebuffer + renderPass + solid/texture 两个
 *             pipeline）与窗口会话（VK_KHR_xlib_surface + swapchain，
 *             每交换链图像一个 framebuffer）。执行模型：帧内绘制原语
 *             按调用顺序录制进单个 primary 命令缓冲，顶点数据写入
 *             HOST_VISIBLE|COHERENT 顶点缓冲游标（vkCmdDraw 以
 *             firstVertex 偏移绘制），endFrame 提交并等待 fence 后执行
 *             挂起的 readback 拷贝。Vulkan 图像行序为上到下，与 XImage
 *             一致，readback 无需行翻转。系统头（vulkan_core.h/
 *             vulkan_xlib.h）只出现在本文件内。
 * @note       仅在 XPLATFORMINTEGRATION_ON && XGPU_ON && XINYUE_C_HAS_VULKAN
 *             时编译；窗口会话依赖 VK_KHR_xlib_surface 扩展，离屏会话
 *             无扩展依赖。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XGpuRenderDriver.h"

#if XPLATFORMINTEGRATION_ON && XGPU_ON && defined(XINYUE_C_HAS_VULKAN)

#include "XImage.h"
#include "XMemory.h"
#include "XPlatformNativeWindow.h"
#include "XWindow.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Xlib 的 XImage/XColor/XKeyEvent 等与公共类型同名：沿用 Drive 层的
   改名 include 约定（用后即 undef）。rename 宏必须先于 vulkan.h——
   VK_USE_PLATFORM_XLIB_KHR 会让 vulkan_xlib.h 拉入 X11/Xlib.h。本文件
   只使用 Display/Window（无同名冲突，不 rename）。 */
#define XImage X11_XImage
#define XPoint X11_XPoint
#define XEvent X11_XEvent
#define XColor X11_XColor
#define XKeyEvent X11_XKeyEvent
#define XExposeEvent X11_XExposeEvent
/* XMemory.h 的 #define XFree XMemory_free 会污染 Xlib 的 XFree 系列
   声明（vulkan_xlib.h 也会拉入 Xlib.h）：先解除，全部 include 后恢复。 */
#undef XFree
#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan.h>
#include <X11/Xlib.h>
#undef XImage
#undef XPoint
#undef XEvent
#undef XColor
#undef XKeyEvent
#undef XExposeEvent
#define XFree XMemory_free

#include "XGpuRenderDriver_vulkan_shaders.h"

/* ==================== 会话结构 ==================== */

struct XGpuRenderDriverSession
{
    bool m_window;               /**< 是否窗口会话（swapchain 上屏）。 */
    XWindow* m_windowObject;     /**< 窗口对象（借用，仅用于创建 surface）。 */
    int m_width;                 /**< 渲染宽度（像素）。 */
    int m_height;                /**< 渲染高度（像素）。 */
    bool m_recording;            /**< 帧命令缓冲是否在录制中。 */
    VkFormat m_targetFormat;     /**< 渲染目标格式（swapchain 需一致）。 */

    VkInstance m_instance;       /**< Vulkan 实例（每会话一个，简化）。 */
    VkPhysicalDevice m_physical; /**< 物理设备（队列族 0 含 GRAPHICS）。 */
    VkDevice m_device;           /**< 逻辑设备。 */
    VkQueue m_queue;             /**< 图形队列。 */
    uint32_t m_queueFamily;      /**< 图形/窗口 present 所在队列族。 */
    VkCommandPool m_cmdPool;     /**< 命令池。 */
    VkCommandBuffer m_cmd;       /**< 帧命令缓冲（每帧重置重录）。 */
    VkCommandBuffer m_transferCmd; /**< 同步上传/读回专用命令缓冲。 */
    VkFence m_frameFence;        /**< 帧提交完成 fence。 */

    VkRenderPass m_renderPass;   /**< 单子通道渲染通道（loadOp=LOAD）。 */
    VkPipelineLayout m_solidLayout;   /**< 纯色管线布局（push constant）。 */
    VkPipeline m_solidPipeline;  /**< 纯色填充管线。 */
    VkPipeline m_solidSourcePipeline; /**< 纯色 Source 覆盖管线。 */
    VkPipelineLayout m_texLayout;     /**< 纹理管线布局（set0 采样器）。 */
    VkPipeline m_texPipeline;    /**< 纹理采样管线。 */
    VkPipeline m_texSourcePipeline; /**< 纹理 Source 覆盖管线。 */
    VkSampler m_sampler;         /**< 最近邻纹理采样器。 */
    VkDescriptorSetLayout m_texSetLayout; /**< 纹理描述符布局。 */
    VkDescriptorPool m_descPool; /**< 描述符池。 */
    VkDescriptorSet m_sourceSet; /**< 源纹理描述符。 */
    VkDescriptorSet m_atlasSet;  /**< 图集描述符。 */

    /* 离屏渲染目标。 */
    VkImage m_colorImage;
    VkDeviceMemory m_colorMemory;
    VkImageView m_colorView;
    VkImageLayout m_colorLayout; /**< 离屏图像当前布局。 */

    /* 窗口 swapchain。 */
    VkSurfaceKHR m_surface;      /**< Xlib 窗口表面。 */
    VkSwapchainKHR m_swapchain;  /**< 交换链。 */
    uint32_t m_swapCount;        /**< 交换链图像数。 */
    VkImage m_swapImages[8];     /**< 交换链图像（借用，来自 swapchain）。 */
    VkImageView m_swapViews[8];  /**< 交换链图像视图（拥有）。 */
    VkFramebuffer m_swapFbs[8];  /**< 每图像 framebuffer（拥有）。 */
    VkImageLayout m_swapLayouts[8]; /**< 交换链图像当前布局。 */
    uint32_t m_imageIndex;       /**< 当前获取的交换链图像下标。 */
    VkSemaphore m_imageReady;    /**< acquire 信号量。 */
    VkSemaphore m_renderDone;    /**< present 信号量。 */
    bool m_imageWaitConsumed;    /**< 本帧 acquire 信号量是否已消费。 */

    /* 几何：HOST_VISIBLE 顶点缓冲（pos2+uv2+color4，8 floats/顶点）。 */
    VkBuffer m_vertexBuffer;
    VkDeviceMemory m_vertexMemory;
    float* m_vertexMapped;       /**< 持久映射（拥有期间有效）。 */
    uint32_t m_vertexCapacity;   /**< 顶点容量（个）。 */
    uint32_t m_vertexCursor;     /**< 本帧已写顶点数。 */

    /* 一次性上传 staging（HOST_VISIBLE，按需扩容）。 */
    VkBuffer m_stagingBuffer;
    VkDeviceMemory m_stagingMemory;
    void* m_stagingMapped;       /**< 持久映射。 */
    size_t m_stagingCapacity;    /**< staging 容量（字节）。 */

    /* 源纹理与图集（复用单图像，绘制时整幅重传）。 */
    VkImage m_sourceImage;
    VkDeviceMemory m_sourceMemory;
    VkImageView m_sourceView;
    VkImageLayout m_sourceLayout;
    int m_sourceWidth;           /**< 当前源纹理尺寸（0=未创建）。 */
    int m_sourceHeight;
    VkImage m_atlasImage;
    VkDeviceMemory m_atlasMemory;
    VkImageView m_atlasView;
    VkImageLayout m_atlasLayout;

    XImage* m_pendingReadback;   /**< 帧末待回读目标（借用）。 */
};

/* ==================== 内存与工具 ==================== */

/**
 * @brief      在给定内存类型上分配并绑定缓冲/图像内存。
 * @param      device 逻辑设备。
 * @param      physical 物理设备。
 * @param      requirements 内存需求。
 * @param      flags 期望属性位（HOST_VISIBLE/DEVICE_LOCAL 等）。
 * @param      outMemory 输出设备内存。
 * @return     true 成功；false 找不到兼容内存类型或分配失败。
 */
static bool xvkl_alloc_memory(VkDevice device, VkPhysicalDevice physical,
                              const VkMemoryRequirements* req,
                              VkMemoryPropertyFlags flags,
                              VkDeviceMemory* outMemory)
{
    VkPhysicalDeviceMemoryProperties props;
    uint32_t i;
    vkGetPhysicalDeviceMemoryProperties(physical, &props);
    for (i = 0; i < props.memoryTypeCount; ++i)
    {
        if ((req->memoryTypeBits & (1u << i)) &&
            (props.memoryTypes[i].propertyFlags & flags) == flags)
        {
            VkMemoryAllocateInfo ai;
            memset(&ai, 0, sizeof(ai));
            ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.allocationSize = req->size;
            ai.memoryTypeIndex = i;
            return vkAllocateMemory(device, &ai, NULL, outMemory) ==
                   VK_SUCCESS;
        }
    }
    return false;
}

/** @brief 创建 HOST_VISIBLE|COHERENT 缓冲并持久映射。 */
static bool xvkl_create_host_buffer(VkDevice device, VkPhysicalDevice physical,
                                    VkDeviceSize bytes, VkBuffer* outBuffer,
                                    VkDeviceMemory* outMemory, void** outMapped)
{
    VkBufferCreateInfo bi;
    VkMemoryRequirements req;
    if (!outBuffer || !outMemory) return false;
    memset(&bi, 0, sizeof(bi));
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = bytes;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
               VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bi, NULL, outBuffer) != VK_SUCCESS)
        return false;
    vkGetBufferMemoryRequirements(device, *outBuffer, &req);
    if (!xvkl_alloc_memory(device, physical, &req,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           outMemory))
        return false;
    if (vkBindBufferMemory(device, *outBuffer, *outMemory, 0) != VK_SUCCESS)
        return false;
    return vkMapMemory(device, *outMemory, 0, bytes, 0, outMapped) ==
           VK_SUCCESS;
}

/** @brief 创建设备本地图像及其内存与视图。 */
static bool xvkl_create_color_image(VkDevice device,
                                    VkPhysicalDevice physical, int width,
                                    int height, VkFormat format,
                                    VkImageUsageFlags usage, VkImage* outImage,
                                    VkDeviceMemory* outMemory,
                                    VkImageView* outView)
{
    VkImageCreateInfo ii;
    VkMemoryRequirements req;
    VkImageViewCreateInfo vi;
    if (!outImage || !outMemory || !outView) return false;
    memset(&ii, 0, sizeof(ii));
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format;
    ii.extent.width = (uint32_t)width;
    ii.extent.height = (uint32_t)height;
    ii.extent.depth = 1;
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = usage;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &ii, NULL, outImage) != VK_SUCCESS)
        return false;
    vkGetImageMemoryRequirements(device, *outImage, &req);
    if (!xvkl_alloc_memory(device, physical, &req,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, outMemory))
        return false;
    if (vkBindImageMemory(device, *outImage, *outMemory, 0) != VK_SUCCESS)
        return false;
    memset(&vi, 0, sizeof(vi));
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = *outImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &vi, NULL, outView) != VK_SUCCESS)
        return false;
    return true;
}

/** @brief 创建最近邻、边缘钳位的组合采样器。 */
static bool xvkl_create_sampler(XGpuRenderDriverSession* self)
{
    VkSamplerCreateInfo si;
    if (!self) return false;
    memset(&si, 0, sizeof(si));
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_NEAREST;
    si.minFilter = VK_FILTER_NEAREST;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = 0.0f;
    return vkCreateSampler(self->m_device, &si, NULL, &self->m_sampler) ==
           VK_SUCCESS;
}

/** @brief 为纹理描述符写入采样器与图像视图。 */
static void xvkl_update_texture_descriptor(XGpuRenderDriverSession* self,
                                            VkDescriptorSet set,
                                            VkImageView view)
{
    VkDescriptorImageInfo info;
    VkWriteDescriptorSet write;
    if (!self || !set || !view || !self->m_sampler) return;
    memset(&info, 0, sizeof(info));
    info.sampler = self->m_sampler;
    info.imageView = view;
    info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    memset(&write, 0, sizeof(write));
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &info;
    vkUpdateDescriptorSets(self->m_device, 1, &write, 0, NULL);
}

/* ==================== render pass 与 pipeline ==================== */

static bool xvkl_create_render_pass(VkDevice device, VkFormat format,
                                    bool presentSrc, VkRenderPass* outPass)
{
    VkAttachmentDescription attachment;
    VkAttachmentReference colorRef;
    VkSubpassDescription subpass;
    VkRenderPassCreateInfo ci;
    memset(&attachment, 0, sizeof(attachment));
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;  /* 帧首显式清/上传。 */
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    /* 窗口会话最终布局必须为 PRESENT_SRC（present 的规范要求）；
       离屏保持 COLOR_ATTACHMENT（下一帧 LOAD 读取）。 */
    attachment.finalLayout = presentSrc
        ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    memset(&colorRef, 0, sizeof(colorRef));
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    memset(&subpass, 0, sizeof(subpass));
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments = &attachment;
    ci.subpassCount = 1;
    ci.pSubpasses = &subpass;
    ci.dependencyCount = 0;
    return vkCreateRenderPass(device, &ci, NULL, outPass) == VK_SUCCESS;
}

/**
 * @brief      创建图形管线：顶点布局 pos(2F)+uv(2F)+color(4F)。
 * @param      textured true 片段输出 texture(uv)*color；false 输出 color。
 */
static bool xvkl_create_pipeline(VkDevice device, VkRenderPass pass,
                                 VkPipelineLayout layout, bool textured,
                                 bool sourceOver, VkPipeline* outPipeline)
{
    VkPipelineShaderStageCreateInfo stages[2];
    VkVertexInputBindingDescription binding;
    VkVertexInputAttributeDescription attrs[3];
    VkPipelineVertexInputStateCreateInfo vertexInput;
    VkPipelineInputAssemblyStateCreateInfo assembly;
    VkPipelineViewportStateCreateInfo viewport;
    VkPipelineRasterizationStateCreateInfo raster;
    VkPipelineMultisampleStateCreateInfo multisample;
    VkPipelineColorBlendAttachmentState blendAttachment;
    VkPipelineColorBlendStateCreateInfo blend;
    VkPipelineDynamicStateCreateInfo dynamic;
    VkDynamicState dynStates[2];
    VkGraphicsPipelineCreateInfo ci;
    VkShaderModule vs = 0, fs = 0;
    bool ok = false;
    VkShaderModuleCreateInfo sci;
    memset(&sci, 0, sizeof(sci));
    sci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    sci.codeSize = KSPVVERTEX_WORDS * sizeof(uint32_t);
    sci.pCode = kSpvVertex;
    if (vkCreateShaderModule(device, &sci, NULL, &vs) != VK_SUCCESS)
        return false;
    sci.codeSize = (textured ? KSPVFRAGMENTTEXTURE_WORDS
                             : KSPVFRAGMENTSOLID_WORDS) * sizeof(uint32_t);
    sci.pCode = textured ? kSpvFragmentTexture : kSpvFragmentSolid;
    if (vkCreateShaderModule(device, &sci, NULL, &fs) != VK_SUCCESS)
    {
        vkDestroyShaderModule(device, vs, NULL);
        return false;
    }
    memset(stages, 0, sizeof(stages));
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";
    memset(&binding, 0, sizeof(binding));
    binding.binding = 0;
    binding.stride = sizeof(float) * 8u;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    memset(attrs, 0, sizeof(attrs));
    attrs[0].binding = 0; attrs[0].location = 0;
    attrs[0].format = VK_FORMAT_R32G32_SFLOAT; attrs[0].offset = 0;
    attrs[1].binding = 0; attrs[1].location = 1;
    attrs[1].format = VK_FORMAT_R32G32_SFLOAT; attrs[1].offset = 8;
    attrs[2].binding = 0; attrs[2].location = 2;
    attrs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[2].offset = 16;
    memset(&vertexInput, 0, sizeof(vertexInput));
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions = attrs;
    memset(&assembly, 0, sizeof(assembly));
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    memset(&viewport, 0, sizeof(viewport));
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    memset(&raster, 0, sizeof(raster));
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    memset(&multisample, 0, sizeof(multisample));
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    memset(&blendAttachment, 0, sizeof(blendAttachment));
    blendAttachment.blendEnable = sourceOver ? VK_TRUE : VK_FALSE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    memset(&blend, 0, sizeof(blend));
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;
    memset(&dynamic, 0, sizeof(dynamic));
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynStates[0] = VK_DYNAMIC_STATE_VIEWPORT;
    dynStates[1] = VK_DYNAMIC_STATE_SCISSOR;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynStates;
    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vertexInput;
    ci.pInputAssemblyState = &assembly;
    ci.pViewportState = &viewport;
    ci.pRasterizationState = &raster;
    ci.pMultisampleState = &multisample;
    ci.pColorBlendState = &blend;
    ci.pDynamicState = &dynamic;
    ci.layout = layout;
    ci.renderPass = pass;
    ci.subpass = 0;
    ok = vkCreateGraphicsPipelines(device, 0, 1, &ci, NULL, outPipeline) ==
         VK_SUCCESS;
    vkDestroyShaderModule(device, vs, NULL);
    vkDestroyShaderModule(device, fs, NULL);
    return ok;
}

/* ==================== 会话创建/销毁 ==================== */

static bool xvkl_available(void)
{
    return true; /* 由 sessionCreate 最终裁定。 */
}

/** @brief 从物理设备选择同时支持图形的队列族；窗口时还需 present。 */
static bool xvkl_find_queue_family(XGpuRenderDriverSession* self,
                                   uint32_t* outFamily)
{
    uint32_t count = 0;
    uint32_t i;
    VkQueueFamilyProperties* props;
    if (!self || !outFamily) return false;
    vkGetPhysicalDeviceQueueFamilyProperties(self->m_physical, &count, NULL);
    if (!count) return false;
    props = (VkQueueFamilyProperties*)XMalloc_System(
        (size_t)count * sizeof(*props));
    if (!props) return false;
    vkGetPhysicalDeviceQueueFamilyProperties(self->m_physical, &count, props);
    for (i = 0; i < count; ++i)
    {
        VkBool32 present = VK_TRUE;
        if (!(props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
        if (self->m_window)
        {
            XPlatformNativeWindowConnectionType type;
            Display* display = (Display*)XPlatformNativeWindow_nativeConnection(&type);
            if (!display || type != XPlatformNativeWindowConnection_X11 ||
                !self->m_windowObject || !XWindow_winId(self->m_windowObject) ||
                !self->m_surface ||
                vkGetPhysicalDeviceSurfaceSupportKHR(self->m_physical, i,
                                                     self->m_surface,
                                                     &present) != VK_SUCCESS ||
                !present)
                continue;
        }
        *outFamily = i;
        XFree_System(props);
        return true;
    }
    XFree_System(props);
    return false;
}

static bool xvkl_create_device_objects(XGpuRenderDriverSession* self,
                                       VkFormat targetFormat)
{
    VkDescriptorPoolSize poolSize;
    VkDescriptorPoolCreateInfo poolCi;
    VkDescriptorSetLayoutBinding binding;
    VkDescriptorSetLayoutCreateInfo setCi;
    VkPushConstantRange pushRange;
    VkPipelineLayoutCreateInfo layoutCi;
    VkDescriptorSetAllocateInfo allocCi;
    self->m_targetFormat = targetFormat;
    if (!xvkl_create_render_pass(self->m_device, targetFormat, self->m_window,
                                 &self->m_renderPass))
        return false;
    memset(&pushRange, 0, sizeof(pushRange));
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(float) * 4u;
    memset(&layoutCi, 0, sizeof(layoutCi));
    layoutCi.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutCi.pushConstantRangeCount = 1;
    layoutCi.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(self->m_device, &layoutCi, NULL,
                               &self->m_solidLayout) != VK_SUCCESS)
        return false;
    memset(&binding, 0, sizeof(binding));
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    memset(&setCi, 0, sizeof(setCi));
    setCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setCi.bindingCount = 1;
    setCi.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(self->m_device, &setCi, NULL,
                                    &self->m_texSetLayout) != VK_SUCCESS)
        return false;
    {
        VkPipelineLayoutCreateInfo texLayoutCi = layoutCi;
        texLayoutCi.pushConstantRangeCount = 0;
        texLayoutCi.pPushConstantRanges = NULL;
        texLayoutCi.setLayoutCount = 1;
        texLayoutCi.pSetLayouts = &self->m_texSetLayout;
        if (vkCreatePipelineLayout(self->m_device, &texLayoutCi, NULL,
                                   &self->m_texLayout) != VK_SUCCESS)
            return false;
    }
    memset(&poolSize, 0, sizeof(poolSize));
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 4;
    memset(&poolCi, 0, sizeof(poolCi));
    poolCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCi.maxSets = 4;
    poolCi.poolSizeCount = 1;
    poolCi.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(self->m_device, &poolCi, NULL,
                               &self->m_descPool) != VK_SUCCESS)
        return false;
    if (!xvkl_create_sampler(self))
        return false;
    if (!xvkl_create_pipeline(self->m_device, self->m_renderPass,
                              self->m_solidLayout, false, true,
                              &self->m_solidPipeline))
        return false;
    if (!xvkl_create_pipeline(self->m_device, self->m_renderPass,
                              self->m_solidLayout, false, false,
                              &self->m_solidSourcePipeline))
        return false;
    if (!xvkl_create_pipeline(self->m_device, self->m_renderPass,
                              self->m_texLayout, true, true,
                              &self->m_texPipeline))
        return false;
    if (!xvkl_create_pipeline(self->m_device, self->m_renderPass,
                              self->m_texLayout, true, false,
                              &self->m_texSourcePipeline))
        return false;
    memset(&allocCi, 0, sizeof(allocCi));
    allocCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocCi.descriptorPool = self->m_descPool;
    allocCi.descriptorSetCount = 1;
    allocCi.pSetLayouts = &self->m_texSetLayout;
    if (vkAllocateDescriptorSets(self->m_device, &allocCi,
                                 &self->m_sourceSet) != VK_SUCCESS)
        return false;
    if (vkAllocateDescriptorSets(self->m_device, &allocCi,
                                 &self->m_atlasSet) != VK_SUCCESS)
        return false;
    return true;
}

static void xvkl_destroy_device_objects(XGpuRenderDriverSession* self)
{
    if (!self) return;
    if (self->m_solidPipeline)
        vkDestroyPipeline(self->m_device, self->m_solidPipeline, NULL);
    if (self->m_solidSourcePipeline)
        vkDestroyPipeline(self->m_device, self->m_solidSourcePipeline, NULL);
    if (self->m_texPipeline)
        vkDestroyPipeline(self->m_device, self->m_texPipeline, NULL);
    if (self->m_texSourcePipeline)
        vkDestroyPipeline(self->m_device, self->m_texSourcePipeline, NULL);
    if (self->m_solidLayout)
        vkDestroyPipelineLayout(self->m_device, self->m_solidLayout, NULL);
    if (self->m_texLayout)
        vkDestroyPipelineLayout(self->m_device, self->m_texLayout, NULL);
    if (self->m_texSetLayout)
        vkDestroyDescriptorSetLayout(self->m_device, self->m_texSetLayout,
                                     NULL);
    if (self->m_descPool)
        vkDestroyDescriptorPool(self->m_device, self->m_descPool, NULL);
    if (self->m_sampler)
        vkDestroySampler(self->m_device, self->m_sampler, NULL);
    if (self->m_renderPass)
        vkDestroyRenderPass(self->m_device, self->m_renderPass, NULL);
}

/** @brief 创建 X11 surface（窗口 Vulkan 会话）。 */
static bool xvkl_create_surface(XGpuRenderDriverSession* self,
                                XWindow* window)
{
    XPlatformNativeWindowConnectionType type;
    Display* display;
    VkXlibSurfaceCreateInfoKHR ci;
    if (!self || !window) return false;
    display = (Display*)XPlatformNativeWindow_nativeConnection(&type);
    if (!display || type != XPlatformNativeWindowConnection_X11 ||
        !XWindow_winId(window))
        return false;
    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    ci.dpy = display;
    ci.window = (Window)XWindow_winId(window);
    return vkCreateXlibSurfaceKHR(self->m_instance, &ci, NULL,
                                  &self->m_surface) == VK_SUCCESS;
}

/** @brief 创建离屏颜色图像与 framebuffer。 */
static bool xvkl_create_offscreen_target(XGpuRenderDriverSession* self)
{
    VkFramebufferCreateInfo fi;
    VkImageView attachments[1];
    if (!self || self->m_window) return false;
    if (!xvkl_create_color_image(
            self->m_device, self->m_physical, self->m_width, self->m_height,
            self->m_targetFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            &self->m_colorImage, &self->m_colorMemory, &self->m_colorView))
        return false;
    memset(&fi, 0, sizeof(fi));
    fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass = self->m_renderPass;
    fi.attachmentCount = 1;
    attachments[0] = self->m_colorView;
    fi.pAttachments = attachments;
    fi.width = (uint32_t)self->m_width;
    fi.height = (uint32_t)self->m_height;
    fi.layers = 1;
    if (vkCreateFramebuffer(self->m_device, &fi, NULL, &self->m_swapFbs[0]) !=
        VK_SUCCESS)
        return false;
    self->m_swapCount = 1;
    self->m_colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}

/** @brief 创建窗口 swapchain、图像视图与 framebuffer。 */
static bool xvkl_create_swapchain(XGpuRenderDriverSession* self)
{
    VkSurfaceCapabilitiesKHR caps;
    VkSurfaceFormatKHR formats[32];
    uint32_t formatCount = 0;
    VkSurfaceFormatKHR chosen;
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    VkSwapchainCreateInfoKHR ci;
    VkExtent2D extent;
    uint32_t imageCount;
    uint32_t i;
    VkFramebufferCreateInfo fi;
    if (!self || !self->m_window || !self->m_surface) return false;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(self->m_physical,
                                                  self->m_surface, &caps) !=
        VK_SUCCESS)
        return false;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(self->m_physical, self->m_surface,
                                             &formatCount, NULL) != VK_SUCCESS ||
        formatCount == 0 || formatCount > 32)
        return false;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(self->m_physical, self->m_surface,
                                             &formatCount, formats) != VK_SUCCESS)
        return false;
    chosen = formats[0];
    for (i = 0; i < formatCount; ++i)
    {
        if (formats[i].format == self->m_targetFormat)
        {
            chosen = formats[i];
            break;
        }
    }
    for (i = 0; i < formatCount; ++i)
    {
        if (chosen.format == self->m_targetFormat) break;
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM ||
            formats[i].format == VK_FORMAT_R8G8B8A8_UNORM)
        {
            chosen = formats[i];
            break;
        }
    }
    if (caps.currentExtent.width != UINT32_MAX)
        extent = caps.currentExtent;
    else
    {
        extent.width = (uint32_t)self->m_width;
        extent.height = (uint32_t)self->m_height;
        if (extent.width < caps.minImageExtent.width)
            extent.width = caps.minImageExtent.width;
        if (extent.width > caps.maxImageExtent.width)
            extent.width = caps.maxImageExtent.width;
        if (extent.height < caps.minImageExtent.height)
            extent.height = caps.minImageExtent.height;
        if (extent.height > caps.maxImageExtent.height)
            extent.height = caps.maxImageExtent.height;
    }
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
        return false;
    imageCount = caps.minImageCount + 1u;
    if (caps.maxImageCount && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;
    memset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = self->m_surface;
    ci.minImageCount = imageCount;
    ci.imageFormat = chosen.format;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
        ci.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = presentMode;
    ci.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(self->m_device, &ci, NULL, &self->m_swapchain) !=
        VK_SUCCESS)
        return false;
    if (vkGetSwapchainImagesKHR(self->m_device, self->m_swapchain,
                                &self->m_swapCount, NULL) != VK_SUCCESS ||
        self->m_swapCount == 0 || self->m_swapCount > 8)
        return false;
    if (vkGetSwapchainImagesKHR(self->m_device, self->m_swapchain,
                                &self->m_swapCount, self->m_swapImages) !=
        VK_SUCCESS)
        return false;
    self->m_targetFormat = chosen.format;
    self->m_width = (int)extent.width;
    self->m_height = (int)extent.height;
    for (i = 0; i < self->m_swapCount; ++i)
    {
        VkImageViewCreateInfo vi;
        memset(&vi, 0, sizeof(vi));
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = self->m_swapImages[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = self->m_targetFormat;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        if (vkCreateImageView(self->m_device, &vi, NULL, &self->m_swapViews[i]) !=
            VK_SUCCESS)
            return false;
        self->m_swapLayouts[i] = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    memset(&fi, 0, sizeof(fi));
    fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass = self->m_renderPass;
    fi.attachmentCount = 1;
    fi.width = extent.width;
    fi.height = extent.height;
    fi.layers = 1;
    for (i = 0; i < self->m_swapCount; ++i)
    {
        fi.pAttachments = &self->m_swapViews[i];
        if (vkCreateFramebuffer(self->m_device, &fi, NULL,
                                &self->m_swapFbs[i]) != VK_SUCCESS)
            return false;
    }
    return true;
}

const XGpuRenderDriverProcs* XGpuRenderDriver_vulkan_procs(void);

static VkFormat xvkl_surface_format(XGpuRenderDriverSession* self)
{
    VkSurfaceFormatKHR formats[32];
    uint32_t count = 0;
    uint32_t i;
    if (!self || !self->m_surface ||
        vkGetPhysicalDeviceSurfaceFormatsKHR(self->m_physical, self->m_surface,
                                             &count, NULL) != VK_SUCCESS ||
        count == 0 || count > 32 ||
        vkGetPhysicalDeviceSurfaceFormatsKHR(self->m_physical, self->m_surface,
                                             &count, formats) != VK_SUCCESS)
        return VK_FORMAT_B8G8R8A8_UNORM;
    for (i = 0; i < count; ++i)
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM)
            return formats[i].format;
    return formats[0].format;
}

static XGpuRenderDriverSession* xvkl_session_create(XWindow* window,
                                                    int width, int height)
{
    XGpuRenderDriverSession* self;
    self =
        (XGpuRenderDriverSession*)XCalloc_System(1u, sizeof(*self));
    VkApplicationInfo app;
    VkInstanceCreateInfo ici;
    VkDeviceQueueCreateInfo qci;
    VkDeviceCreateInfo dci;
    float priority = 1.0f;
    VkCommandPoolCreateInfo poolCi;
    VkCommandBufferAllocateInfo cbAi;
    VkFenceCreateInfo fci;
    VkSemaphoreCreateInfo sci;
    if (!self) return NULL;
    self->m_window = window != NULL;
    self->m_width = width;
    self->m_height = height;
    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "XinYueC";
    app.apiVersion = VK_API_VERSION_1_0;
    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    self->m_windowObject = window;
    if (window)
    {
        /* 窗口会话需要 surface 扩展；实例创建在 surface 之前完成。 */
        static const char* const extensions[] = {
            "VK_KHR_surface", "VK_KHR_xlib_surface"
        };
        ici.enabledExtensionCount = 2;
        ici.ppEnabledExtensionNames = extensions;
    }
    if (vkCreateInstance(&ici, NULL, &self->m_instance) != VK_SUCCESS)
        goto fail;
    if (self->m_window && !xvkl_create_surface(self, window))
        goto fail;
    {
        uint32_t count = 0;
        VkPhysicalDevice devices[8];
        if (vkEnumeratePhysicalDevices(self->m_instance, &count, NULL) !=
                VK_SUCCESS ||
            count == 0 || count > 8 ||
            vkEnumeratePhysicalDevices(self->m_instance, &count, devices) !=
                VK_SUCCESS)
            goto fail;
        self->m_physical = devices[0];
    }
    if (!xvkl_find_queue_family(self, &self->m_queueFamily)) goto fail;
    memset(&qci, 0, sizeof(qci));
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = self->m_queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    memset(&dci, 0, sizeof(dci));
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    if (self->m_window)
    {
        static const char* const extensions[] = { "VK_KHR_swapchain" };
        dci.enabledExtensionCount = 1;
        dci.ppEnabledExtensionNames = extensions;
    }
    if (vkCreateDevice(self->m_physical, &dci, NULL, &self->m_device) !=
        VK_SUCCESS)
        goto fail;
    vkGetDeviceQueue(self->m_device, self->m_queueFamily, 0, &self->m_queue);
    if (!self->m_queue) goto fail;
    memset(&poolCi, 0, sizeof(poolCi));
    poolCi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolCi.queueFamilyIndex = self->m_queueFamily;
    if (vkCreateCommandPool(self->m_device, &poolCi, NULL,
                            &self->m_cmdPool) != VK_SUCCESS)
        goto fail;
    memset(&cbAi, 0, sizeof(cbAi));
    cbAi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAi.commandPool = self->m_cmdPool;
    cbAi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAi.commandBufferCount = 2;
    {
        VkCommandBuffer buffers[2];
        if (vkAllocateCommandBuffers(self->m_device, &cbAi, buffers) !=
            VK_SUCCESS)
            goto fail;
        self->m_cmd = buffers[0];
        self->m_transferCmd = buffers[1];
    }
    memset(&fci, 0, sizeof(fci));
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(self->m_device, &fci, NULL, &self->m_frameFence) !=
        VK_SUCCESS)
        goto fail;
    memset(&sci, 0, sizeof(sci));
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (self->m_window)
    {
        if (vkCreateSemaphore(self->m_device, &sci, NULL,
                              &self->m_imageReady) != VK_SUCCESS)
            goto fail;
        if (vkCreateSemaphore(self->m_device, &sci, NULL,
                              &self->m_renderDone) != VK_SUCCESS)
            goto fail;
    }
    if (!xvkl_create_device_objects(
            self, self->m_window ? xvkl_surface_format(self)
                                 : VK_FORMAT_B8G8R8A8_UNORM))
        goto fail;
    if (self->m_window)
    {
        if (!xvkl_create_swapchain(self)) goto fail;
    }
    else if (!xvkl_create_offscreen_target(self))
        goto fail;
    if (!xvkl_create_host_buffer(self->m_device, self->m_physical,
                                 1024u * 1024u, &self->m_vertexBuffer,
                                 &self->m_vertexMemory,
                                 (void**)&self->m_vertexMapped))
        goto fail;
    self->m_vertexCapacity = 1024u * 1024u / (sizeof(float) * 8u);
    return self;

fail:
    XGpuRenderDriver_vulkan_procs()->sessionDestroy(self);
    return NULL;
}

static void xvkl_session_destroy(XGpuRenderDriverSession* self)
{
    if (!self) return;
    if (self->m_device) vkDeviceWaitIdle(self->m_device);
    if (!self->m_device)
    {
        if (self->m_surface && self->m_instance)
            vkDestroySurfaceKHR(self->m_instance, self->m_surface, NULL);
        if (self->m_instance) vkDestroyInstance(self->m_instance, NULL);
        XFree_System(self);
        return;
    }
    /* Framebuffers must outlive neither their image views nor the render pass. */
    {
        uint32_t i;
        for (i = 0; i < self->m_swapCount; ++i)
            if (self->m_swapFbs[i])
                vkDestroyFramebuffer(self->m_device, self->m_swapFbs[i], NULL);
    }
    xvkl_destroy_device_objects(self);
    if (self->m_vertexBuffer)
        vkDestroyBuffer(self->m_device, self->m_vertexBuffer, NULL);
    if (self->m_vertexMemory)
        vkFreeMemory(self->m_device, self->m_vertexMemory, NULL);
    if (self->m_stagingBuffer)
        vkDestroyBuffer(self->m_device, self->m_stagingBuffer, NULL);
    if (self->m_stagingMemory)
        vkFreeMemory(self->m_device, self->m_stagingMemory, NULL);
    if (self->m_sourceView)
        vkDestroyImageView(self->m_device, self->m_sourceView, NULL);
    if (self->m_sourceImage)
        vkDestroyImage(self->m_device, self->m_sourceImage, NULL);
    if (self->m_sourceMemory)
        vkFreeMemory(self->m_device, self->m_sourceMemory, NULL);
    if (self->m_atlasView)
        vkDestroyImageView(self->m_device, self->m_atlasView, NULL);
    if (self->m_atlasImage)
        vkDestroyImage(self->m_device, self->m_atlasImage, NULL);
    if (self->m_atlasMemory)
        vkFreeMemory(self->m_device, self->m_atlasMemory, NULL);
    if (self->m_colorView)
        vkDestroyImageView(self->m_device, self->m_colorView, NULL);
    if (self->m_colorImage)
        vkDestroyImage(self->m_device, self->m_colorImage, NULL);
    if (self->m_colorMemory)
        vkFreeMemory(self->m_device, self->m_colorMemory, NULL);
    {
        uint32_t i;
        for (i = 0; i < self->m_swapCount; ++i)
            if (self->m_swapViews[i])
                vkDestroyImageView(self->m_device, self->m_swapViews[i], NULL);
    }
    if (self->m_swapchain)
        vkDestroySwapchainKHR(self->m_device, self->m_swapchain, NULL);
    if (self->m_surface)
        vkDestroySurfaceKHR(self->m_instance, self->m_surface, NULL);
    if (self->m_imageReady)
        vkDestroySemaphore(self->m_device, self->m_imageReady, NULL);
    if (self->m_renderDone)
        vkDestroySemaphore(self->m_device, self->m_renderDone, NULL);
    if (self->m_frameFence)
        vkDestroyFence(self->m_device, self->m_frameFence, NULL);
    if (self->m_cmdPool)
        vkDestroyCommandPool(self->m_device, self->m_cmdPool, NULL);
    if (self->m_device) vkDestroyDevice(self->m_device, NULL);
    if (self->m_instance) vkDestroyInstance(self->m_instance, NULL);
    XFree_System(self);
}

static bool xvkl_make_current(XGpuRenderDriverSession* self)
{
    return self != NULL; /* Vulkan 无 current 概念：恒真（会话即上下文）。 */
}

static void xvkl_done_current(XGpuRenderDriverSession* self)
{
    (void)self;
}

/* ==================== 帧控制与录制 ==================== */

static bool xvkl_stage_pixels(XGpuRenderDriverSession* self, size_t bytes);
static void xvkl_clear(XGpuRenderDriverSession* self, uint32_t argb);

static void xvkl_image_barrier(VkCommandBuffer cmd, VkImage image,
                               VkImageLayout oldLayout,
                               VkImageLayout newLayout,
                               VkAccessFlags srcAccess,
                               VkAccessFlags dstAccess,
                               VkPipelineStageFlags srcStage,
                               VkPipelineStageFlags dstStage)
{
    VkImageMemoryBarrier barrier;
    memset(&barrier, 0, sizeof(barrier));
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, NULL, 0, NULL, 1,
                         &barrier);
}

static VkImage xvkl_frame_image(const XGpuRenderDriverSession* self)
{
    return self->m_window ? self->m_swapImages[self->m_imageIndex]
                          : self->m_colorImage;
}

static VkFramebuffer xvkl_framebuffer(const XGpuRenderDriverSession* self)
{
    return self->m_window ? self->m_swapFbs[self->m_imageIndex]
                          : self->m_swapFbs[0];
}

static VkImageLayout xvkl_frame_layout(const XGpuRenderDriverSession* self)
{
    return self->m_window ? self->m_swapLayouts[self->m_imageIndex]
                          : self->m_colorLayout;
}

static void xvkl_set_frame_layout(XGpuRenderDriverSession* self,
                                  VkImageLayout layout)
{
    if (self->m_window)
        self->m_swapLayouts[self->m_imageIndex] = layout;
    else
        self->m_colorLayout = layout;
}

static bool xvkl_submit_transfer(XGpuRenderDriverSession* self)
{
    VkSubmitInfo submit;
    if (!self || !self->m_transferCmd) return false;
    if (vkEndCommandBuffer(self->m_transferCmd) != VK_SUCCESS) return false;
    memset(&submit, 0, sizeof(submit));
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &self->m_transferCmd;
    if (vkQueueSubmit(self->m_queue, 1, &submit, 0) != VK_SUCCESS)
        return false;
    return vkQueueWaitIdle(self->m_queue) == VK_SUCCESS;
}

static bool xvkl_begin_transfer(XGpuRenderDriverSession* self)
{
    VkCommandBufferBeginInfo begin;
    if (!self || !self->m_transferCmd) return false;
    if (vkResetCommandBuffer(self->m_transferCmd, 0) != VK_SUCCESS)
        return false;
    memset(&begin, 0, sizeof(begin));
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    return vkBeginCommandBuffer(self->m_transferCmd, &begin) == VK_SUCCESS;
}

static bool xvkl_copy_initial_image(XGpuRenderDriverSession* self,
                                    const XImage* initialImage)
{
    size_t bytes;
    int row;
    if (!self || !initialImage || XImage_width(initialImage) != self->m_width ||
        XImage_height(initialImage) != self->m_height)
        return false;
    bytes = (size_t)self->m_width * (size_t)self->m_height * 4u;
    if (!xvkl_stage_pixels(self, bytes)) return false;
    for (row = 0; row < self->m_height; ++row)
    {
        int col;
        uint8_t* dst = (uint8_t*)self->m_stagingMapped +
            (size_t)row * (size_t)self->m_width * 4u;
        for (col = 0; col < self->m_width; ++col)
        {
            uint32_t argb = XImage_pixel(initialImage, col, row);
            unsigned a = (argb >> 24) & 0xffu;
            dst[col * 4 + 0] = (uint8_t)(((argb & 0xffu) * a + 127u) / 255u);
            dst[col * 4 + 1] = (uint8_t)((((argb >> 8) & 0xffu) * a + 127u) /
                                         255u);
            dst[col * 4 + 2] = (uint8_t)((((argb >> 16) & 0xffu) * a + 127u) /
                                         255u);
            dst[col * 4 + 3] = (uint8_t)a;
        }
    }
    if (!xvkl_begin_transfer(self)) return false;
    xvkl_image_barrier(self->m_transferCmd, xvkl_frame_image(self),
                       xvkl_frame_layout(self), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                       VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT);
    {
        VkBufferImageCopy region;
        memset(&region, 0, sizeof(region));
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent.width = (uint32_t)self->m_width;
        region.imageExtent.height = (uint32_t)self->m_height;
        region.imageExtent.depth = 1;
        vkCmdCopyBufferToImage(self->m_transferCmd, self->m_stagingBuffer,
                               xvkl_frame_image(self),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &region);
    }
    xvkl_image_barrier(self->m_transferCmd, xvkl_frame_image(self),
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    if (!xvkl_submit_transfer(self)) return false;
    xvkl_set_frame_layout(self, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    return true;
}

static bool xvkl_prepare_frame_target(XGpuRenderDriverSession* self)
{
    if (!xvkl_begin_transfer(self)) return false;
    if (xvkl_frame_layout(self) != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        xvkl_image_barrier(self->m_transferCmd, xvkl_frame_image(self),
                           xvkl_frame_layout(self),
                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                           0, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    }
    if (!xvkl_submit_transfer(self)) return false;
    xvkl_set_frame_layout(self, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    return true;
}

static bool xvkl_begin_render_pass(XGpuRenderDriverSession* self)
{
    VkCommandBufferBeginInfo begin;
    VkRenderPassBeginInfo rp;
    VkViewport viewport;
    VkRect2D scissor;
    if (!self) return false;
    memset(&begin, 0, sizeof(begin));
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(self->m_cmd, &begin) != VK_SUCCESS) return false;
    memset(&rp, 0, sizeof(rp));
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = self->m_renderPass;
    rp.framebuffer = xvkl_framebuffer(self);
    rp.renderArea.extent.width = (uint32_t)self->m_width;
    rp.renderArea.extent.height = (uint32_t)self->m_height;
    vkCmdBeginRenderPass(self->m_cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)self->m_width;
    viewport.height = (float)self->m_height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = (uint32_t)self->m_width;
    scissor.extent.height = (uint32_t)self->m_height;
    vkCmdSetViewport(self->m_cmd, 0, 1, &viewport);
    vkCmdSetScissor(self->m_cmd, 0, 1, &scissor);
    vkCmdBindVertexBuffers(self->m_cmd, 0, 1, &self->m_vertexBuffer,
                           (VkDeviceSize[1]){ 0 });
    return true;
}

/**
 * @brief      渲染目标转入 COLOR_ATTACHMENT_OPTIMAL（帧首绘制前置屏障）。
 * @param      cmd 目标命令缓冲（调用方已 begin）。
 * @param      image 渲染目标图像。
 * @return     无。
 */
static void xvkl_transition_color_for_draw(VkCommandBuffer cmd, VkImage image)
{
    VkImageMemoryBarrier barrier;
    memset(&barrier, 0, sizeof(barrier));
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                         NULL, 0, NULL, 1, &barrier);
}

static bool xvkl_begin_frame(XGpuRenderDriverSession* self,
                             const XImage* initialImage)
{
    VkResult acquired;
    if (!self) return false;
    if (self->m_recording)
    {
        vkCmdEndRenderPass(self->m_cmd);
        vkEndCommandBuffer(self->m_cmd);
        self->m_recording = false;
    }
    vkWaitForFences(self->m_device, 1, &self->m_frameFence, VK_TRUE,
                    UINT64_MAX);
    vkResetFences(self->m_device, 1, &self->m_frameFence);
    self->m_vertexCursor = 0;
    self->m_pendingReadback = NULL;
    self->m_imageWaitConsumed = false;
    if (self->m_window)
    {
        acquired = vkAcquireNextImageKHR(self->m_device, self->m_swapchain,
                                         UINT64_MAX, self->m_imageReady, 0,
                                         &self->m_imageIndex);
        if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR)
            return false;
    }
    if (initialImage && XImage_width(initialImage) == self->m_width &&
        XImage_height(initialImage) == self->m_height)
    {
        if (!xvkl_copy_initial_image(self, initialImage)) return false;
    }
    else if (!xvkl_prepare_frame_target(self))
        return false;
    if (!xvkl_begin_render_pass(self)) return false;
    self->m_recording = true;
    if (!initialImage) xvkl_clear(self, 0u);
    return true;
}

static void xvkl_end_frame(XGpuRenderDriverSession* self)
{
    VkSubmitInfo si;
    if (!self || !self->m_recording) return;
    vkCmdEndRenderPass(self->m_cmd);
    vkEndCommandBuffer(self->m_cmd);
    self->m_recording = false;
    memset(&si, 0, sizeof(si));
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &self->m_cmd;
    if (self->m_window && !self->m_imageWaitConsumed)
    {
        VkPipelineStageFlags waitStage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &self->m_imageReady;
        si.pWaitDstStageMask = &waitStage;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &self->m_renderDone;
    }
    if (self->m_window)
    {
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &self->m_renderDone;
    }
    if (vkQueueSubmit(self->m_queue, 1, &si, self->m_frameFence) != VK_SUCCESS)
        return;
    vkWaitForFences(self->m_device, 1, &self->m_frameFence, VK_TRUE,
                    UINT64_MAX);
    xvkl_set_frame_layout(self, self->m_window
        ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    if (self->m_window)
    {
        VkPresentInfoKHR pi;
        memset(&pi, 0, sizeof(pi));
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &self->m_renderDone;
        pi.swapchainCount = 1;
        pi.pSwapchains = &self->m_swapchain;
        pi.pImageIndices = &self->m_imageIndex;
        vkQueuePresentKHR(self->m_queue, &pi);
    }
}

/**
 * @brief      暂停主图形命令并等待执行完成；随后由调用方执行一次同步
 *             transfer。该路径只用于离屏会话，窗口会话必须维持 acquire /
 *             present 信号量的单次提交协议。
 */
static bool xvkl_suspend_for_transfer(XGpuRenderDriverSession* self)
{
    VkSubmitInfo submit;
    if (!self || !self->m_recording || self->m_window) return false;
    vkCmdEndRenderPass(self->m_cmd);
    if (vkEndCommandBuffer(self->m_cmd) != VK_SUCCESS) return false;
    self->m_recording = false;
    memset(&submit, 0, sizeof(submit));
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &self->m_cmd;
    if (vkQueueSubmit(self->m_queue, 1, &submit, 0) != VK_SUCCESS)
        return false;
    return vkQueueWaitIdle(self->m_queue) == VK_SUCCESS;
}

static bool xvkl_resume_after_transfer(XGpuRenderDriverSession* self)
{
    if (!self || self->m_window) return false;
    if (vkResetCommandBuffer(self->m_cmd, 0) != VK_SUCCESS) return false;
    if (!xvkl_prepare_frame_target(self) || !xvkl_begin_render_pass(self))
        return false;
    self->m_recording = true;
    return true;
}

static bool xvkl_copy_frame_to_image(XGpuRenderDriverSession* self,
                                     XImage* target)
{
    size_t bytes;
    VkBufferImageCopy region;
    int x;
    int y;
    if (!self || !target || self->m_window ||
        XImage_width(target) != self->m_width ||
        XImage_height(target) != self->m_height)
        return false;
    bytes = (size_t)self->m_width * (size_t)self->m_height * 4u;
    if (!xvkl_stage_pixels(self, bytes) || !xvkl_begin_transfer(self))
        return false;
    xvkl_image_barrier(self->m_transferCmd, self->m_colorImage,
                       self->m_colorLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                       VK_ACCESS_TRANSFER_READ_BIT,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT);
    memset(&region, 0, sizeof(region));
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = (uint32_t)self->m_width;
    region.imageExtent.height = (uint32_t)self->m_height;
    region.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(self->m_transferCmd, self->m_colorImage,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           self->m_stagingBuffer, 1, &region);
    xvkl_image_barrier(self->m_transferCmd, self->m_colorImage,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       VK_ACCESS_TRANSFER_READ_BIT,
                       VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    if (!xvkl_submit_transfer(self)) return false;
    self->m_colorLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    if (XImage_format(target) == XImageFormat_ARGB32 ||
        XImage_format(target) == XImageFormat_ARGB32_Premultiplied)
    {
        uint8_t* dst = XImage_bits(target);
        int bpl = XImage_bytesPerLine(target);
        const uint8_t* src = (const uint8_t*)self->m_stagingMapped;
        if (!dst || bpl < self->m_width * 4) return false;
        for (y = 0; y < self->m_height; ++y)
        {
            uint8_t* line = dst + (size_t)y * (size_t)bpl;
            const uint8_t* row = src + (size_t)y *
                (size_t)self->m_width * 4u;
            for (x = 0; x < self->m_width; ++x)
            {
                line[x * 4 + 0] = row[x * 4 + 0];
                line[x * 4 + 1] = row[x * 4 + 1];
                line[x * 4 + 2] = row[x * 4 + 2];
                line[x * 4 + 3] = row[x * 4 + 3];
            }
        }
        return true;
    }
    for (y = 0; y < self->m_height; ++y)
        for (x = 0; x < self->m_width; ++x)
        {
            const uint8_t* pixel = (const uint8_t*)self->m_stagingMapped +
                ((size_t)y * (size_t)self->m_width + (size_t)x) * 4u;
            uint8_t a = pixel[3];
            uint8_t r = a ? (uint8_t)(((unsigned)pixel[2] * 255u + a / 2u) / a) : 0;
            uint8_t g = a ? (uint8_t)(((unsigned)pixel[1] * 255u + a / 2u) / a) : 0;
            uint8_t b = a ? (uint8_t)(((unsigned)pixel[0] * 255u + a / 2u) / a) : 0;
            XImage_setPixel(target, x, y, ((uint32_t)a << 24) |
                            ((uint32_t)r << 16) | ((uint32_t)g << 8) | b);
        }
    return true;
}

static bool xvkl_readback(XGpuRenderDriverSession* self, XImage* target)
{
    bool active;
    bool ok;
    if (!self || !target || self->m_window) return false;
    active = self->m_recording;
    if (active && !xvkl_suspend_for_transfer(self)) return false;
    ok = xvkl_copy_frame_to_image(self, target);
    if (active && !xvkl_resume_after_transfer(self)) return false;
    return ok;
}

/* ==================== 原语（帧内录制） ==================== */

/**
 * @brief      写 4 顶点（NDC：Vulkan y 轴向下，ny = y*2/h - 1）并录制
 *             指定管线的 draw 调用。
 */
static bool xvkl_record_quad(XGpuRenderDriverSession* self, float x1, float y1,
                             float x2, float y2, float x3, float y3,
                             float x4, float y4, uint32_t premulColor,
                             bool sourceOver, bool textured, VkImageView view)
{
    float* v;
    uint32_t first;
    unsigned i;
    VkPipeline pipeline;
    if (!self || !self->m_recording) return false;
    pipeline = textured
        ? (sourceOver ? self->m_texPipeline : self->m_texSourcePipeline)
        : (sourceOver ? self->m_solidPipeline : self->m_solidSourcePipeline);
    if (!pipeline) return false;
    if (self->m_vertexCursor + 4u > self->m_vertexCapacity) return false;
    first = self->m_vertexCursor;
    v = self->m_vertexMapped + (size_t)first * 8u;
    {
        float xs[4] = { x1, x2, x3, x4 };
        float ys[4] = { y1, y2, y3, y4 };
        for (i = 0; i < 4; ++i)
        {
            float* d = v + (size_t)i * 8u;
            d[0] = xs[i] * 2.0f / (float)self->m_width - 1.0f;
            d[1] = ys[i] * 2.0f / (float)self->m_height - 1.0f;
            d[2] = textured ? (i == 1 || i == 3 ? 1.0f : 0.0f) : 0.0f;
            d[3] = textured ? (i >= 2 ? 1.0f : 0.0f) : 0.0f;
            d[4] = (float)((premulColor >> 16) & 0xffu) / 255.0f;
            d[5] = (float)((premulColor >> 8) & 0xffu) / 255.0f;
            d[6] = (float)(premulColor & 0xffu) / 255.0f;
            d[7] = (float)((premulColor >> 24) & 0xffu) / 255.0f;
        }
    }
    vkCmdBindPipeline(self->m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    if (textured)
    {
        VkDescriptorSet sets[1];
        sets[0] = view == self->m_atlasView ? self->m_atlasSet
                                            : self->m_sourceSet;
        vkCmdBindDescriptorSets(self->m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                self->m_texLayout, 0, 1, sets, 0, NULL);
    }
    self->m_vertexCursor += 4u;
    vkCmdDraw(self->m_cmd, 4, 1, first, 0);
    return true;
}

static void xvkl_clear(XGpuRenderDriverSession* self, uint32_t argb)
{
    VkClearAttachment attachment;
    VkClearRect rect;
    if (!self || !self->m_recording) return;
    memset(&attachment, 0, sizeof(attachment));
    attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    attachment.clearValue.color.float32[0] =
        (float)((argb >> 16) & 0xffu) / 255.0f;
    attachment.clearValue.color.float32[1] =
        (float)((argb >> 8) & 0xffu) / 255.0f;
    attachment.clearValue.color.float32[2] =
        (float)(argb & 0xffu) / 255.0f;
    attachment.clearValue.color.float32[3] =
        (float)((argb >> 24) & 0xffu) / 255.0f;
    memset(&rect, 0, sizeof(rect));
    rect.rect.extent.width = (uint32_t)self->m_width;
    rect.rect.extent.height = (uint32_t)self->m_height;
    rect.layerCount = 1;
    vkCmdClearAttachments(self->m_cmd, 1, &attachment, 1, &rect);
}

static void xvkl_set_clip_rect(XGpuRenderDriverSession* self, const XRect* rect)
{
    VkRect2D scissor;
    if (!self || !self->m_recording) return;
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = (uint32_t)self->m_width;
    scissor.extent.height = (uint32_t)self->m_height;
    if (rect && rect->width > 0 && rect->height > 0)
    {
        int x0 = rect->x < 0 ? 0 : rect->x;
        int y0 = rect->y < 0 ? 0 : rect->y;
        int x1 = rect->x + rect->width;
        int y1 = rect->y + rect->height;
        if (x1 > self->m_width) x1 = self->m_width;
        if (y1 > self->m_height) y1 = self->m_height;
        if (x1 > x0 && y1 > y0)
        {
            scissor.offset.x = x0;
            scissor.offset.y = y0;
            scissor.extent.width = (uint32_t)(x1 - x0);
            scissor.extent.height = (uint32_t)(y1 - y0);
        }
        else
        {
            scissor.extent.width = 0;
            scissor.extent.height = 0;
        }
    }
    vkCmdSetScissor(self->m_cmd, 0, 1, &scissor);
}

static bool xvkl_fill_rect(XGpuRenderDriverSession* self, const XRect* rect,
                           uint32_t premulColor, float opacity,
                           bool sourceOver)
{
    unsigned a = (unsigned)((premulColor >> 24) & 0xffu);
    if (!self || !self->m_recording || !rect || rect->width <= 0 ||
        rect->height <= 0)
        return false;
    (void)opacity; /* 透明度已折入 premulColor（通用层保证）。 */
    a = (unsigned)(a * (unsigned)(opacity * 255.0f + 0.5f) + 127u) / 255u;
    premulColor = ((uint32_t)a << 24) | (premulColor & 0x00ffffffu);
    return xvkl_record_quad(
        self, (float)rect->x, (float)rect->y,
        (float)(rect->x + rect->width), (float)rect->y,
        (float)rect->x, (float)(rect->y + rect->height),
        (float)(rect->x + rect->width), (float)(rect->y + rect->height),
        premulColor, sourceOver, false, NULL);
}

static bool xvkl_draw_solid_quad(XGpuRenderDriverSession* self, float x1,
                                 float y1, float x2, float y2, float x3,
                                 float y3, float x4, float y4,
                                 uint32_t premulColor, bool sourceOver)
{
    return xvkl_record_quad(self, x1, y1, x2, y2, x3, y3, x4, y4, premulColor,
                            sourceOver, false, NULL);
}

/**
 * @brief      确保 staging 缓冲容量并把像素数据写入映射区。
 * @return     true 成功；false 扩容失败。
 */
static bool xvkl_stage_pixels(XGpuRenderDriverSession* self, size_t bytes)
{
    if (self->m_stagingBuffer && self->m_stagingCapacity >= bytes) return true;
    if (self->m_stagingBuffer)
        vkDestroyBuffer(self->m_device, self->m_stagingBuffer, NULL);
    if (self->m_stagingMemory)
        vkFreeMemory(self->m_device, self->m_stagingMemory, NULL);
    self->m_stagingBuffer = 0;
    self->m_stagingMemory = 0;
    self->m_stagingMapped = NULL;
    if (!xvkl_create_host_buffer(self->m_device, self->m_physical, bytes,
                                 &self->m_stagingBuffer, &self->m_stagingMemory,
                                 &self->m_stagingMapped))
        return false;
    self->m_stagingCapacity = bytes;
    return true;
}

/** @brief 确保源纹理存在且尺寸匹配（丢弃重建）。 */
static bool xvkl_ensure_source_image(XGpuRenderDriverSession* self, int width,
                                     int height)
{
    if (self->m_sourceImage && self->m_sourceWidth == width &&
        self->m_sourceHeight == height)
        return true;
    if (self->m_sourceView)
        vkDestroyImageView(self->m_device, self->m_sourceView, NULL);
    if (self->m_sourceImage)
        vkDestroyImage(self->m_device, self->m_sourceImage, NULL);
    if (self->m_sourceMemory)
        vkFreeMemory(self->m_device, self->m_sourceMemory, NULL);
    self->m_sourceImage = 0;
    self->m_sourceView = 0;
    self->m_sourceMemory = 0;
    self->m_sourceLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!xvkl_create_color_image(self->m_device, self->m_physical, width,
                                 height, VK_FORMAT_B8G8R8A8_UNORM,
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                     VK_IMAGE_USAGE_SAMPLED_BIT,
                                 &self->m_sourceImage, &self->m_sourceMemory,
                                 &self->m_sourceView))
        return false;
    self->m_sourceWidth = width;
    self->m_sourceHeight = height;
    xvkl_update_texture_descriptor(self, self->m_sourceSet, self->m_sourceView);
    return true;
}

static bool xvkl_draw_image(XGpuRenderDriverSession* self, const XImage* image,
                            int x, int y, int width, int height,
                            float opacity, bool sourceOver)
{
    VkBufferImageCopy region;
    VkImageMemoryBarrier barrier;
    VkCommandBufferBeginInfo bi;
    unsigned alpha;
    uint32_t premul;
    if (!self || !self->m_recording || !image || width <= 0 || height <= 0 ||
        XImage_width(image) != width || XImage_height(image) != height)
        return false;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    alpha = (unsigned)(opacity * 255.0f + 0.5f);
    /* drawImage 语义：opacity 同时缩放预乘 RGB 与 alpha。源图像已是预乘
       布局，CPU 侧按 alpha 缩放后经 staging 上传。 */
    {
        size_t bytes = (size_t)width * (size_t)height * 4u;
        uint8_t* mapped;
        const uint8_t* src = XImage_constBits(image);
        int bpl = XImage_bytesPerLine(image);
        int row;
        if (!xvkl_stage_pixels(self, bytes)) return false;
        mapped = (uint8_t*)self->m_stagingMapped;
        for (row = 0; row < height; ++row)
        {
            const uint8_t* srow = src + (size_t)row * (size_t)bpl;
            uint8_t* drow = mapped + (size_t)row * (size_t)width * 4u;
            int col;
            for (col = 0; col < width; ++col)
            {
                drow[col * 4 + 0] = srow[col * 4 + 2]; /* R <- B */
                drow[col * 4 + 1] = srow[col * 4 + 1]; /* G */
                drow[col * 4 + 2] = srow[col * 4 + 0]; /* B <- R */
                drow[col * 4 + 3] = srow[col * 4 + 3]; /* A */
            }
        }
        if (alpha != 255u)
        {
            for (row = 0; row < height; ++row)
            {
                uint8_t* drow = mapped + (size_t)row * (size_t)width * 4u;
                int col;
                for (col = 0; col < width; ++col)
                {
                    drow[col * 4 + 0] =
                        (uint8_t)((drow[col * 4 + 0] * alpha + 127) / 255);
                    drow[col * 4 + 1] =
                        (uint8_t)((drow[col * 4 + 1] * alpha + 127) / 255);
                    drow[col * 4 + 2] =
                        (uint8_t)((drow[col * 4 + 2] * alpha + 127) / 255);
                    drow[col * 4 + 3] =
                        (uint8_t)((drow[col * 4 + 3] * alpha + 127) / 255);
                }
            }
        }
    }
    if (!xvkl_ensure_source_image(self, width, height)) return false;
    memset(&bi, 0, sizeof(bi));
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    /* 上传需要在当前渲染通道外执行：打断当前录制，先做 transfer，再
       重新开始渲染通道并重放已录制的绘制——实现复杂度高。简化：本帧
       的绘制尚未提交（命令缓冲仍在录制），直接在渲染通道内执行
       transfer 非法。因此 drawImage 的上传改到帧首无法预知——最终
       方案：本帧内临时结束渲染通道，上传后重新开始渲染通道。 */
    vkCmdEndRenderPass(self->m_cmd);
    vkEndCommandBuffer(self->m_cmd);
    {
        VkSubmitInfo si;
        memset(&si, 0, sizeof(si));
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &self->m_cmd;
        vkQueueSubmit(self->m_queue, 1, &si, 0);
        vkQueueWaitIdle(self->m_queue);
    }
    vkResetCommandBuffer(self->m_cmd, 0);
    vkBeginCommandBuffer(self->m_cmd, &bi);
    xvkl_transition_color_for_draw(self->m_cmd,
                                   self->m_window
                                       ? self->m_swapImages[self->m_imageIndex]
                                       : self->m_colorImage);
    {
        VkImageMemoryBarrier barrier;
        memset(&barrier, 0, sizeof(barrier));
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = self->m_sourceImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(self->m_cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0,
                             NULL, 1, &barrier);
    }
    memset(&region, 0, sizeof(region));
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = (uint32_t)width;
    region.imageExtent.height = (uint32_t)height;
    region.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(self->m_cmd, self->m_stagingBuffer,
                           self->m_sourceImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    {
        VkImageMemoryBarrier barrier;
        memset(&barrier, 0, sizeof(barrier));
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = self->m_sourceImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(self->m_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                             NULL, 0, NULL, 1, &barrier);
    }
    {
        VkRenderPassBeginInfo rp;
        memset(&rp, 0, sizeof(rp));
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = self->m_renderPass;
        rp.framebuffer = self->m_window
                             ? self->m_swapFbs[self->m_imageIndex]
                             : self->m_swapFbs[0];
        rp.renderArea.extent.width = (uint32_t)self->m_width;
        rp.renderArea.extent.height = (uint32_t)self->m_height;
        rp.clearValueCount = 0;
        vkCmdBeginRenderPass(self->m_cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    }
    {
        VkViewport viewport;
        VkRect2D scissor;
        viewport.x = 0.0f; viewport.y = 0.0f;
        viewport.width = (float)self->m_width;
        viewport.height = (float)self->m_height;
        viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
        scissor.offset.x = 0; scissor.offset.y = 0;
        scissor.extent.width = (uint32_t)self->m_width;
        scissor.extent.height = (uint32_t)self->m_height;
        vkCmdSetViewport(self->m_cmd, 0, 1, &viewport);
        vkCmdSetScissor(self->m_cmd, 0, 1, &scissor);
    }
    vkCmdBindVertexBuffers(self->m_cmd, 0, 1, &self->m_vertexBuffer,
                           (VkDeviceSize[1]){ 0 });
    {
        /* 重新绑定源纹理描述符（图像视图内容已更新）。 */
        VkDescriptorImageInfo imageInfo;
        VkWriteDescriptorSet write;
        memset(&imageInfo, 0, sizeof(imageInfo));
        imageInfo.sampler = 0;
        imageInfo.imageView = self->m_sourceView;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        memset(&write, 0, sizeof(write));
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = self->m_sourceSet;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(self->m_device, 1, &write, 0, NULL);
    }
    (void)barrier;
    premul = 0xffffffffu; /* 源已含透明度（上面按 opacity 缩放过）。 */
    return xvkl_record_quad(self, (float)x, (float)y,
                            (float)(x + width), (float)y,
                            (float)x, (float)(y + height),
                            (float)(x + width), (float)(y + height),
                            premul, sourceOver, true, self->m_sourceView);
}

static bool xvkl_draw_alpha_bitmap(XGpuRenderDriverSession* self,
                                   const uint8_t* alpha, int width,
                                   int height, int stride, int x, int y,
                                   uint32_t premulColor, float opacity,
                                   bool sourceOver)
{
    /* 覆盖图经通用层展开不足——此处直接以覆盖度构造 RGBA 灰度再复用
       drawImage 的上传绘制（premulColor 折入顶点色）。 */
    size_t bytes = (size_t)width * (size_t)height * 4u;
    uint8_t* gray;
    uint8_t* mapped;
    XImage proxy;
    int row;
    bool ok;
    if (!self || !self->m_recording || !alpha || width <= 0 || height <= 0 ||
        stride < width)
        return false;
    gray = (uint8_t*)XMalloc_System(bytes);
    if (!gray) return false;
    for (row = 0; row < height; ++row)
    {
        const uint8_t* src = alpha + (size_t)row * (size_t)stride;
        uint8_t* dst = gray + (size_t)row * (size_t)width * 4u;
        int col;
        for (col = 0; col < width; ++col)
        {
            dst[col * 4 + 0] = premulColor & 0xffu;         /* B */
            dst[col * 4 + 1] = (premulColor >> 8) & 0xffu;  /* G */
            dst[col * 4 + 2] = (premulColor >> 16) & 0xffu; /* R */
            dst[col * 4 + 3] = src[col];                    /* A=覆盖度 */
        }
    }
    XImage_init_ex(&proxy, width, height, XImageFormat_ARGB32);
    if (!XImage_isNull(&proxy))
    {
        memcpy(XImage_bits(&proxy), gray, bytes);
        ok = xvkl_draw_image(self, &proxy, x, y, width, height, 1.0f,
                             sourceOver);
    }
    XImage_deinit_base(&proxy);
    XFree_System(gray);
    return ok;
}

static bool xvkl_glyph_atlas_upload(XGpuRenderDriverSession* self,
                                    const uint8_t* coverage, int width,
                                    int height, int atlasX, int atlasY)
{
    size_t bytes = (size_t)width * (size_t)height * 4u;
    uint8_t* mapped;
    VkBufferImageCopy region;
    VkImageMemoryBarrier barrier;
    VkCommandBufferBeginInfo bi;
    VkSubmitInfo si;
    int row;
    if (!self || !coverage || width <= 0 || height <= 0 || atlasX < 0 ||
        atlasY < 0 || atlasX + width > XGPU_RENDER_GLYPH_ATLAS_SIZE ||
        atlasY + height > XGPU_RENDER_GLYPH_ATLAS_SIZE)
        return false;
    if (!xvkl_stage_pixels(self, bytes)) return false;
    mapped = (uint8_t*)self->m_stagingMapped;
    for (row = 0; row < height; ++row)
    {
        const uint8_t* src = coverage + (size_t)row * (size_t)width;
        uint8_t* dst = mapped + (size_t)row * (size_t)width * 4u;
        int col;
        for (col = 0; col < width; ++col)
        {
            dst[col * 4] = src[col];
            dst[col * 4 + 1] = src[col];
            dst[col * 4 + 2] = src[col];
            dst[col * 4 + 3] = src[col];
        }
    }
    if (!self->m_atlasImage &&
        !xvkl_create_color_image(self->m_device, self->m_physical,
                                 XGPU_RENDER_GLYPH_ATLAS_SIZE,
                                 XGPU_RENDER_GLYPH_ATLAS_SIZE,
                                 VK_FORMAT_B8G8R8A8_UNORM,
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                     VK_IMAGE_USAGE_SAMPLED_BIT,
                                 &self->m_atlasImage, &self->m_atlasMemory,
                                 &self->m_atlasView))
        return false;
    memset(&bi, 0, sizeof(bi));
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    /* 图集上传在帧外即时执行（独立提交，阻塞等待——字形上传频率低）。 */
    vkResetCommandBuffer(self->m_cmd, 0);
    vkBeginCommandBuffer(self->m_cmd, &bi);
    memset(&barrier, 0, sizeof(barrier));
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = self->m_atlasImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(self->m_cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL,
                         1, &barrier);
    memset(&region, 0, sizeof(region));
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageOffset.x = atlasX;
    region.imageOffset.y = atlasY;
    region.imageExtent.width = (uint32_t)width;
    region.imageExtent.height = (uint32_t)height;
    region.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(self->m_cmd, self->m_stagingBuffer,
                           self->m_atlasImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    {
        VkImageMemoryBarrier toRead = barrier;
        toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(self->m_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                             NULL, 0, NULL, 1, &toRead);
    }
    vkEndCommandBuffer(self->m_cmd);
    memset(&si, 0, sizeof(si));
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &self->m_cmd;
    vkQueueSubmit(self->m_queue, 1, &si, 0);
    vkQueueWaitIdle(self->m_queue);
    return true;
}

static bool xvkl_glyph_atlas_draw(XGpuRenderDriverSession* self, int atlasX,
                                  int atlasY, int width, int height, int x,
                                  int y, uint32_t premulColor, bool sourceOver)
{
    if (!self || width <= 0 || height <= 0 || atlasX < 0 || atlasY < 0)
        return false;
    return xvkl_record_quad(self, (float)x, (float)y,
                            (float)(x + width), (float)y,
                            (float)x, (float)(y + height),
                            (float)(x + width), (float)(y + height),
                            premulColor, sourceOver, true, self->m_atlasView);
}

static bool xvkl_glyph_atlas_readback(XGpuRenderDriverSession* self,
                                      int atlasX, int atlasY, int atlasWidth,
                                      int atlasHeight, uint8_t* outCoverage)
{
    size_t bytes = (size_t)atlasWidth * (size_t)atlasHeight * 4u;
    VkCommandBufferBeginInfo bi;
    VkBufferImageCopy region;
    VkSubmitInfo si;
    int y;
    if (!self || atlasX < 0 || atlasY < 0 || atlasWidth <= 0 ||
        atlasHeight <= 0 || !outCoverage || !self->m_atlasImage)
        return false;
    if (!xvkl_stage_pixels(self, bytes)) return false;
    memset(&bi, 0, sizeof(bi));
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkResetCommandBuffer(self->m_cmd, 0);
    vkBeginCommandBuffer(self->m_cmd, &bi);
    memset(&region, 0, sizeof(region));
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageOffset.x = atlasX;
    region.imageOffset.y = atlasY;
    region.imageExtent.width = (uint32_t)atlasWidth;
    region.imageExtent.height = (uint32_t)atlasHeight;
    region.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(self->m_cmd, self->m_atlasImage,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           self->m_stagingBuffer, 1, &region);
    vkEndCommandBuffer(self->m_cmd);
    memset(&si, 0, sizeof(si));
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &self->m_cmd;
    vkQueueSubmit(self->m_queue, 1, &si, 0);
    vkQueueWaitIdle(self->m_queue);
    for (y = 0; y < atlasHeight; ++y)
    {
        const uint8_t* src = (const uint8_t*)self->m_stagingMapped +
            (size_t)y * (size_t)atlasWidth * 4u;
        uint8_t* dst = outCoverage + (size_t)y * (size_t)atlasWidth;
        memcpy(dst, src, (size_t)atlasWidth);
    }
    return true;
}

static bool xvkl_present_to_window(XGpuRenderDriverSession* self)
{
    (void)self;
    return true; /* present 已在 endFrame 内执行（swapchain 模型）。 */
}

/* ==================== 驱动操作表 ==================== */

/*
 * Vulkan 的会话没有 OpenGL 那样的 current/uncurrent 操作，
 * 但仍通过统一操作表暴露完整生命周期。resize 暂留 NULL：通用层
 * 会在窗口尺寸变化时销毁并重建 Vulkan 会话，避免在当前简化的
 * swapchain 生命周期中留下悬挂的 framebuffer/view。
 */
static bool xvkl_upload_target_image(XGpuRenderDriverSession* self,
                                     const XImage* image);

static const XGpuRenderDriverProcs g_xvklProcs =
{
    .available = xvkl_available,
    .sessionCreate = xvkl_session_create,
    .sessionDestroy = xvkl_session_destroy,
    .makeCurrent = xvkl_make_current,
    .doneCurrent = xvkl_done_current,
    .beginFrame = xvkl_begin_frame,
    .endFrame = xvkl_end_frame,
    .resize = NULL,
    .presentToWindow = xvkl_present_to_window,
    .readback = xvkl_readback,
    .clear = xvkl_clear,
    .setClipRect = xvkl_set_clip_rect,
    .fillRect = xvkl_fill_rect,
    .drawImage = xvkl_draw_image,
    .drawAlphaBitmap = xvkl_draw_alpha_bitmap,
    .glyphAtlasUpload = xvkl_glyph_atlas_upload,
    .glyphAtlasDraw = xvkl_glyph_atlas_draw,
    .drawSolidQuad = xvkl_draw_solid_quad,
    .glyphAtlasReadback = xvkl_glyph_atlas_readback,
    .uploadTargetImage = xvkl_upload_target_image
};

/**
 * @brief      录制中即时读回：打断渲染通道并提交已录命令，拷贝渲染
 *             目标到 staging 后按原布局恢复，重新开始渲染通道与命令
 *             录制（后续原语继续追加；已画内容经 LOAD 保留）。
 */
static bool xvkl_upload_target_image(XGpuRenderDriverSession* self,
                                     const XImage* image)
{
    VkCommandBufferBeginInfo bi;
    VkSubmitInfo si;
    VkBufferImageCopy region;
    VkImageMemoryBarrier toDst;
    size_t bytes;
    if (!self || !image || XImage_width(image) != self->m_width ||
        XImage_height(image) != self->m_height)
        return false;
    bytes = (size_t)self->m_width * (size_t)self->m_height * 4u;
    if (!xvkl_stage_pixels(self, bytes)) return false;
    memcpy(self->m_stagingMapped, XImage_constBits(image), bytes);
    /* 录制中：结束渲染通道并提交已录命令，再整帧拷贝（渲染通道外）。 */
    if (self->m_recording)
    {
        vkCmdEndRenderPass(self->m_cmd);
        vkEndCommandBuffer(self->m_cmd);
        memset(&si, 0, sizeof(si));
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &self->m_cmd;
        vkQueueSubmit(self->m_queue, 1, &si, 0);
        vkQueueWaitIdle(self->m_queue);
        self->m_recording = false;
    }
    vkResetCommandBuffer(self->m_cmd, 0);
    vkBeginCommandBuffer(self->m_cmd, &bi);
    {
        VkImage imageHandle = self->m_window
            ? self->m_swapImages[self->m_imageIndex] : self->m_colorImage;
        memset(&toDst, 0, sizeof(toDst));
        toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toDst.srcAccessMask = 0;
        toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toDst.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.image = imageHandle;
        toDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toDst.subresourceRange.levelCount = 1;
        toDst.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(self->m_cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0,
                             NULL, 1, &toDst);
        memset(&region, 0, sizeof(region));
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent.width = (uint32_t)self->m_width;
        region.imageExtent.height = (uint32_t)self->m_height;
        region.imageExtent.depth = 1;
        vkCmdCopyBufferToImage(self->m_cmd, self->m_stagingBuffer, imageHandle,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1,
                               &region);
    }
    vkEndCommandBuffer(self->m_cmd);
    memset(&si, 0, sizeof(si));
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &self->m_cmd;
    vkQueueSubmit(self->m_queue, 1, &si, 0);
    vkQueueWaitIdle(self->m_queue);
    return true;
}

const XGpuRenderDriverProcs* XGpuRenderDriver_vulkan_procs(void)
{
    return &g_xvklProcs;
}

#endif /* XPLATFORMINTEGRATION_ON && XGPU_ON && XINYUE_C_HAS_VULKAN */

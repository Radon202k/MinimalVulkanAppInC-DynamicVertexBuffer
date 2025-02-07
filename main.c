/*
*  Includes and helpful utilities
*/

#include <windows.h>
#include <vulkan\vulkan.h>
#include <vulkan\vulkan_win32.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef int32_t s32;

typedef float f32;

#define array_count(array) (sizeof(array) / sizeof((array)[0]))

/*
*  VulkanContext struct
*/

typedef struct
{
    HWND window;
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    u32 graphicsAndPresentQueueFamily;
    VkQueue graphicsAndPresentQueue;
    VkSwapchainKHR swapchain;
    VkFormat swapchainImageFormat;
    VkImage swapchainImages[2];
    VkImageView swapchainImageViews[2];
    VkExtent2D swapchainExtents;
    
    VkCommandPool graphicsCommandPool;
    
} VulkanContext;

/*
*  File loading utility
*/

typedef struct
{
    void *data;
    size_t size;
    
} LoadedFile;

LoadedFile
load_entire_file(char *fileName)
{
    LoadedFile result = {NULL};
    
    FILE *handle;
    fopen_s(&handle, fileName, "rb");
    assert(handle);
    
    fseek(handle, 0, SEEK_END);
    result.size = ftell(handle);
    fseek(handle, 0, SEEK_SET);
    
    assert(result.size > 0);
    
    result.data = malloc(result.size);
    assert(result.data);
    
    size_t bytesRead = fread(result.data, 1, result.size, handle);
    assert(bytesRead == result.size);
    
    fclose(handle);
    
    return result;
}

/*
*  globalRunning and WindowProc
*/

static bool globalRunning;

LRESULT CALLBACK
vulkan_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
        case WM_CREATE:
        {
            OutputDebugString("Window created\n");
        } break;
        
        case WM_SIZE: 
        {
            OutputDebugString("Window resized\n");
        } break;
        
        case WM_CLOSE:
        case WM_DESTROY:
        {
            globalRunning = false;
        } break;
        
        default:
        {
            return DefWindowProc(window, message, wparam, lparam);
        } break;
    }
    
    return 0;
}

/*
*  Vulkan Validation layer's Debug Callback
*/

static VKAPI_ATTR VkBool32 VKAPI_CALL
vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                      VkDebugUtilsMessageTypeFlagsEXT messageType,
                      const VkDebugUtilsMessengerCallbackDataEXT *callbackData,
                      void *userData)
{
    char buffer[4096] = {0};
    sprintf_s(buffer, sizeof(buffer), "Vulkan Validation layer: %s\n",
              callbackData->pMessage);
    OutputDebugString(buffer);
    
    return VK_FALSE;
}

/*
*  Create Image View function
*/

VkImageView
vk_create_image_view(VulkanContext *vk, VkImage image, VkFormat format)
{
    VkImageView imageView;
    
    VkComponentMapping swizzle =
    {
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY
    };
    
    VkImageSubresourceRange subRange =
    {
        VK_IMAGE_ASPECT_COLOR_BIT,
        0, // baseMipLevel
        1, // levelCount
        0, // baseArrayLayer
        1  // layerCount
    };
    
    VkImageViewCreateInfo viewInfo =
    {
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        NULL,
        0,
        image,
        VK_IMAGE_VIEW_TYPE_2D,
        format,
        swizzle,
        subRange
    };
    
    // Create the image view
    vkCreateImageView(vk->device, &viewInfo, NULL,
                      &imageView);
    
    assert(imageView);
    
    return imageView;
}

/*
*  Vulkan Initialization Function
*/

VulkanContext
win32_init_vulkan(HINSTANCE instance, s32 windowX, s32 windowY, u32 windowWidth,
                  u32 windowHeight, char *windowTitle)
{
    VulkanContext vk = {NULL};
    
    /*
    *  Create window
    */
    
    // Register window class
    WNDCLASSEX winClass =
    {
        sizeof(WNDCLASSEX),
        0, // style
        vulkan_window_proc, // window procedure
        0, // cbClsExtra
        0, // cbWndExtra
        instance, // hInstance
        NULL, // hIcon
        NULL, // hCursor
        NULL, // hbrBackground
        NULL, // lpszMenuName
        "MyUniqueVulkanWindowClassName",
        NULL, // hIconSm
    };
    
    if (!RegisterClassEx(&winClass))
    {
        assert(!"Failed to register window class");
    }
    
    // Make sure the window is not resizable for simplicity
    DWORD windowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    
    RECT windowRect =
    {
        windowX, // left
        windowY, // top
        windowX + windowWidth, // right
        windowY + windowHeight, // bottom
    };
    
    AdjustWindowRect(&windowRect, windowStyle, 0);
    
    windowWidth = windowRect.right - windowRect.left;
    windowHeight = windowRect.bottom - windowRect.top;
    windowX = windowRect.left;
    windowY = windowRect.top;
    
    // Create window
    vk.window = CreateWindowEx(0, // Extended style
                               winClass.lpszClassName,
                               windowTitle,
                               windowStyle,
                               windowX, windowY, windowWidth, windowHeight,
                               NULL, NULL, instance, NULL);
    
    if (!vk.window)
    {
        assert(!"Failed to create window");
    }
    
    ShowWindow(vk.window, SW_SHOW);
    
    /*
    *  Set up enabled layers and extensions
    */
    
    // Query available instance layers
    u32 propertyCount = 0;
    vkEnumerateInstanceLayerProperties(&propertyCount, NULL);
    assert(propertyCount <= 32); // Ensure we don't exceed our fixed-size array
    
    VkLayerProperties layerProperties[32];
    vkEnumerateInstanceLayerProperties(&propertyCount, layerProperties);
    
    char *validationLayerName = "VK_LAYER_KHRONOS_validation";
    
    // Check if the requested validation layer is available
    bool validationLayerFound = false;
    for (u32 i = 0; i < propertyCount; i++)
    {
        if (strcmp(validationLayerName, layerProperties[i].layerName) == 0)
        {
            validationLayerFound = true;
            break;
        }
    }
    
    assert(validationLayerFound && "Validation layer not found!");
    char *enabledLayers[] = { validationLayerName };
    
    char *extensions[] =
    {
        // These defines are used instead of raw strings for future compatibility
        VK_KHR_SURFACE_EXTENSION_NAME, // "VK_KHR_surface"
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME, // "VK_KHR_win32_surface"
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME // "VK_EXT_debug_utils"
    };
    
    /*
    *  Create Vulkan Instance
    */
    
    /* This struct is technically optional, but it's worth adding for a nicer
       display when inspecting the app with tools like RenderDoc. */
    VkApplicationInfo appInfo =
    {
        VK_STRUCTURE_TYPE_APPLICATION_INFO,
        NULL,
        "My Clever App Name",
        1, // application Version
        "My Even Cleverer Engine Name",
        1, // engine Version
        VK_API_VERSION_1_3
    };
    
    /* This struct is necessary. The main purpose of this is to inform the
       Vulkan driver about which layers and extensions to load when calling
       vkCreateInstance. */
    VkInstanceCreateInfo createInfo =
    {
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        NULL,
        0, // flags (this is the only time I'm commenting on this)
        &appInfo,
        array_count(enabledLayers), // layer count
        enabledLayers, // layers to enable
        array_count(extensions), // extension count
        extensions // extension names
    };
    
    if (vkCreateInstance(&createInfo, NULL,
                         &vk.instance) != VK_SUCCESS)
    {
        assert(!"Failed to create vulkan instance");
    }
    
    /*
    *  Set up debug callback
    */
    
    VkDebugUtilsMessageSeverityFlagsEXT messageSeverity = 
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    
    VkDebugUtilsMessageTypeFlagsEXT messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo =
    {
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        NULL,
        0,
        messageSeverity,
        messageType,
        vulkan_debug_callback,
        NULL // user data
    };
    
    // Load the debug utils extension function
    PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT =
    (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(vk.instance, "vkCreateDebugUtilsMessengerEXT");
    
    VkDebugUtilsMessengerEXT debugMessenger;
    if (vkCreateDebugUtilsMessengerEXT(vk.instance, &debugCreateInfo, NULL,
                                       &debugMessenger) != VK_SUCCESS)
    {
        assert(!"Failed to create debug messenger!");
    }
    
    /* 
    *  Create surface
    */
    
    VkWin32SurfaceCreateInfoKHR surfaceCreateInfo =
    {
        VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        NULL,
        0,
        instance, // HINSTANCE
        vk.window // HWND
    };
    
    if (vkCreateWin32SurfaceKHR(vk.instance, &surfaceCreateInfo, NULL,
                                &vk.surface) != VK_SUCCESS)
    {
        assert(!"Failed to create surface");
    }
    
    /*
    *  Pick a physical device and the graphicsAndPresent queue family
    */
    
    u32 deviceCount = 0;
    vkEnumeratePhysicalDevices(vk.instance, &deviceCount, NULL);
    assert(deviceCount <= 8); // Ensure there are no more than 8 devices
    
    VkPhysicalDevice devices[8] = {NULL};
    vkEnumeratePhysicalDevices(vk.instance, &deviceCount, devices);
    
    // Choose the first available device as a fallback
    vk.physicalDevice = devices[0];
    
    // Search for a dedicated GPU (discrete GPU)
    for (u32 i = 0; i < deviceCount; i++)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[i], &props);
        
        // If the device is a dedicated (discrete) GPU, prefer it
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            // Choose it as the physical device and break the loop
            vk.physicalDevice = devices[i];
            break;
        }
    }
    assert(vk.physicalDevice); // Ensure a physical device has been selected
    
    // Query the queue family properties for the chosen physical device
    u32 queueFamilyPropertyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(vk.physicalDevice,
                                             &queueFamilyPropertyCount, NULL);
    // Ensure there are no more than 3 queue families
    assert(queueFamilyPropertyCount <= 3); 
    
    VkQueueFamilyProperties queueFamilyProperties[3] = {0};
    vkGetPhysicalDeviceQueueFamilyProperties(vk.physicalDevice,
                                             &queueFamilyPropertyCount,
                                             queueFamilyProperties);
    
    // Assume first queue family supports Graphics and Present capabilities
    u32 queueFamilyIndex = 0;
    
    // Ensure the queue family supports graphics
    assert(queueFamilyProperties[queueFamilyIndex].queueFlags
           & VK_QUEUE_GRAPHICS_BIT);
    
    VkBool32 presentSupport = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(vk.physicalDevice, queueFamilyIndex,
                                         vk.surface,
                                         &presentSupport);
    assert(presentSupport); // Ensure present support is available
    
    // Store the queue family index that supports both graphics and present
    vk.graphicsAndPresentQueueFamily = queueFamilyIndex;
    
    /*
    *  Create logical device
    */
    
    f32 queuePriorities[] = { 1.0f };
    VkDeviceQueueCreateInfo queueCreateInfo =
    {
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        NULL,
        0,
        vk.graphicsAndPresentQueueFamily,
        array_count(queuePriorities),
        queuePriorities
    };
    
    VkDeviceQueueCreateInfo queueCreateInfos[] = {queueCreateInfo};
    
    // Enable required device extensions (swapchain)
    char *deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    
    VkDeviceCreateInfo deviceCreateInfo =
    {
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        NULL,
        0,
        array_count(queueCreateInfos),
        queueCreateInfos,
        0, // enabledLayerCount deprecated
        NULL, // ppEnabledLayerNames deprecated
        array_count(deviceExtensions),
        deviceExtensions,
        NULL // pEnabledFeatures
    };
    
    // Create the actual logical device finally
    if (vkCreateDevice(vk.physicalDevice, &deviceCreateInfo, NULL,
                       &vk.device) != VK_SUCCESS)
    {
        assert(!"Failed to create logical device");
    }
    
    /*
    *  Get graphicsAndPresentQueue from device
    */
    
    vkGetDeviceQueue(vk.device, vk.graphicsAndPresentQueueFamily, 0,
                     &vk.graphicsAndPresentQueue);
    assert(vk.graphicsAndPresentQueue);
    
    /*
    *  Create swapchain 
    */
    
    // Query surface capabilities
    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physicalDevice, vk.surface,
                                              &surfaceCapabilities);
    
    // Save the swapchain image format and extents
    vk.swapchainImageFormat = VK_FORMAT_B8G8R8A8_SRGB;
    vk.swapchainExtents = surfaceCapabilities.currentExtent;
    
    VkSwapchainCreateInfoKHR swapchainCreateInfo =
    {
        VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        NULL,
        0,
        vk.surface,
        array_count(vk.swapchainImages), // minImageCount (2)
        vk.swapchainImageFormat,
        VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, // imageColorSpace
        vk.swapchainExtents, // imageExtent
        1, // imageArrayLayers
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, // imageUsage
        VK_SHARING_MODE_EXCLUSIVE,
        0, // queueFamilyIndexCount
        NULL, // pQueueFamilyIndices
        surfaceCapabilities.currentTransform, // preTransform
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_PRESENT_MODE_FIFO_KHR,
        VK_TRUE, // clipped
        NULL // oldSwapchain
    };
    
    if (vkCreateSwapchainKHR(vk.device, &swapchainCreateInfo, NULL,
                             &vk.swapchain) != VK_SUCCESS)
    {
        assert(!"Failed to create the swapchain");
    }
    
    /*
    *  Get swapchain images and create their views
    */
    
    u32 imageCount = 0;
    vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &imageCount, NULL);
    assert(imageCount == array_count(vk.swapchainImages));
    
    vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &imageCount,
                            vk.swapchainImages);
    
    // For each swapchain image
    for (u32 i = 0; i < imageCount; i++)
    {
        assert(vk.swapchainImages[i]);
        
        vk.swapchainImageViews[i] =
            vk_create_image_view(&vk, vk.swapchainImages[i],
                                 vk.swapchainImageFormat);
        
        assert(vk.swapchainImageViews[i]);
    }
    
    return vk;
}

/*
*  Find Memory Type function
*/

u32
vk_find_memory_type(VulkanContext *vk, u32 typeFilter,
                    VkMemoryPropertyFlags memPropFlags)
{
    VkPhysicalDeviceMemoryProperties memProperties = { 0 };
    vkGetPhysicalDeviceMemoryProperties(vk->physicalDevice, &memProperties);
    
    u32 memoryTypeIndex = UINT32_MAX;
    
    for (u32 i = 0; i < memProperties.memoryTypeCount; i++)
    {
        bool hasMemoryType = typeFilter & (1 << i);
        
        u32 propFlags = memProperties.memoryTypes[i].propertyFlags;
        bool propsMatch = (propFlags & memPropFlags) == memPropFlags;
        
        if (hasMemoryType && propsMatch)
        {
            memoryTypeIndex = i;
            break;
        }
    }
    
    assert(memoryTypeIndex != UINT32_MAX);
    
    return memoryTypeIndex;
}

/*
*  Create Buffer function
*/

void
vk_create_buffer(VulkanContext *vk, VkDeviceSize size,
                 VkBufferUsageFlags usage,
                 VkMemoryPropertyFlags properties,
                 VkBuffer *buffer, VkDeviceMemory *bufferMemory)
{
    VkBufferCreateInfo bufferInfo =
    {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        NULL,
        0,
        size,
        usage,
        VK_SHARING_MODE_EXCLUSIVE,
        0, NULL
    };
    
    if (vkCreateBuffer(vk->device, &bufferInfo, NULL,
                       buffer) != VK_SUCCESS)
    {
        assert(!"Failed to create buffer!");
    }
    
    // Get Memory Requirements
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(vk->device, *buffer, &memRequirements);
    
    // Allocate Memory
    VkMemoryAllocateInfo allocInfo =
    {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        NULL,
        memRequirements.size,
        vk_find_memory_type(vk, memRequirements.memoryTypeBits, properties)
    };
    
    if (vkAllocateMemory(vk->device, &allocInfo, NULL,
                         bufferMemory) != VK_SUCCESS)
    {
        assert(!"Failed to allocate buffer memory!");
    }
    
    // Bind Memory
    vkBindBufferMemory(vk->device, *buffer, *bufferMemory, 0);
}

/*
*  Helper functions for single time use command buffers
*/

VkCommandBuffer
vk_begin_single_time_commands(VulkanContext *vk)
{
    VkCommandBuffer commandBuffer;
    
    VkCommandBufferAllocateInfo allocInfo =
    {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        NULL,
        vk->graphicsCommandPool, // TODO: change to transfer command pool
        VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        1
    };
    
    vkAllocateCommandBuffers(vk->device, &allocInfo,
                             &commandBuffer);
    
    VkCommandBufferBeginInfo beginInfo =
    {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        NULL,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        NULL
    };
    
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    
    return commandBuffer;
}

void
vk_end_single_time_commands(VulkanContext *vk, VkCommandBuffer commandBuffer)
{
    vkEndCommandBuffer(commandBuffer);
    
    VkSubmitInfo submitInfo = {0};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    
    vkQueueSubmit(vk->graphicsAndPresentQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(vk->graphicsAndPresentQueue); // Wait for execution to finish
    
    // TODO: change to transfer command pool
    vkFreeCommandBuffers(vk->device, vk->graphicsCommandPool, 1,
                         &commandBuffer);
}


/*
*  Create shader module function
*/

VkShaderModule
vk_create_shader_module(VulkanContext *vk, void *code, size_t size)
{
    VkShaderModule result;
    
    VkShaderModuleCreateInfo createInfo =
    {
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        NULL,
        0,
        size,
        (u32 *)code
    };
    
    if (vkCreateShaderModule(vk->device, &createInfo, NULL,
                             &result) != VK_SUCCESS)
    {
        assert(!"Failed to create shader module!");
    }
    
    return result;
}

void
vk_create_graphics_pipeline(VulkanContext *vk,
                            u32 descSetLayoutCount,
                            VkDescriptorSetLayout *descSetLayouts,
                            u32 shaderStageInfoCount,
                            VkPipelineShaderStageCreateInfo *shaderStageInfos,
                            VkPipelineVertexInputStateCreateInfo vertInputStateInfo,
                            VkRenderPass renderPass,
                            VkPipelineLayout *pipelineLayout,
                            VkPipeline *graphicsPipeline)
{
    /*
    *  Define Dynamic State Crate Info
    */
    
    VkPipelineDynamicStateCreateInfo dynamicStateInfo =
    {
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        NULL,
        0,
        0, NULL // No dynamic states
    };
    
    /*
    *  Define Viewport State Create Info
    */
    
    VkViewport viewport =
    {
        0, 0, // x, y
        (f32)vk->swapchainExtents.width,
        (f32)vk->swapchainExtents.height,
        0, 0 // min, max depth
    };
    
    VkViewport viewports[] = { viewport };
    
    VkRect2D scissor =
    {
        {0, 0}, // offset
        vk->swapchainExtents
    };
    
    VkRect2D scissors[] = { scissor };
    
    VkPipelineViewportStateCreateInfo viewportStateInfo =
    {
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        NULL,
        0,
        array_count(viewports),
        viewports,
        array_count(scissors),
        scissors
    };
    
    /*
    *  Define Rasterization State Create Info
    */
    
    VkPipelineRasterizationStateCreateInfo rasterizationStateInfo =
    {
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        NULL,
        0,
        VK_FALSE, // depthClampEnable
        VK_FALSE, // rasterizerDiscardEnable
        VK_POLYGON_MODE_FILL, // polygonMode (solid triangles)
        VK_CULL_MODE_BACK_BIT, // cullMode
        VK_FRONT_FACE_CLOCKWISE, // frontFace
        VK_FALSE, 0, 0, 0, // no depth bias
        1.0f // lineWidth
    };
    
    /*
    *  Define Multisample State Create Info
    */
    
    VkPipelineMultisampleStateCreateInfo multisampleStateInfo =
    {
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        NULL,
        0,
        VK_SAMPLE_COUNT_1_BIT, // rasterizationSamples
        VK_FALSE, // sampleShadingEnable
        0, // minSampleShading
        NULL, // pSampleMask
        VK_FALSE, // alphaToCoverageEnable
        VK_FALSE, // alphaToOneEnable
    };
    
    /*
    *  Define Color Blend State Create Info
    */
    
    VkFlags colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    
    VkPipelineColorBlendAttachmentState colorBlendAttachment =
    {
        VK_TRUE, // blendEnable
        VK_BLEND_FACTOR_SRC_ALPHA,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        VK_BLEND_OP_ADD,
        VK_BLEND_FACTOR_SRC_ALPHA,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        VK_BLEND_OP_ADD,
        colorWriteMask,
    };
    
    VkPipelineColorBlendAttachmentState
        colorBlendAttachments[] = { colorBlendAttachment };
    
    VkPipelineColorBlendStateCreateInfo colorBlendStateInfo =
    {
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        NULL,
        0,
        VK_FALSE,
        VK_LOGIC_OP_CLEAR,
        array_count(colorBlendAttachments),
        colorBlendAttachments,
        {0, 0, 0, 0}
    };
    
    /*
    *  Create Pipeline Layout
    */
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo =
    {
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        NULL,
        0,
        descSetLayoutCount,
        descSetLayouts,
        0, NULL // (no push constant ranges)
    };
    
    if (vkCreatePipelineLayout(vk->device, &pipelineLayoutInfo, NULL,
                               pipelineLayout) != VK_SUCCESS)
    {
        assert(!"Failed to create pipeline layout!");
    }
    
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateInfo =
    {
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        NULL,
        0,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        VK_FALSE // primitiveRestartEnable
    };
    
    /*
    *  Create Graphics Pipeline
    */
    
    VkGraphicsPipelineCreateInfo pipelineInfo =
    {
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        NULL,
        0,
        shaderStageInfoCount,
        shaderStageInfos,
        &vertInputStateInfo,
        &inputAssemblyStateInfo,
        NULL, // pTessellationState
        &viewportStateInfo,
        &rasterizationStateInfo,
        &multisampleStateInfo,
        NULL, // pDepthStencilState
        &colorBlendStateInfo,
        &dynamicStateInfo,
        *pipelineLayout,
        renderPass,
        0, // subpass index
        NULL, 0 // (no base pipeline)
    };
    
    // Create the graphics pipeline
    if (vkCreateGraphicsPipelines(vk->device, VK_NULL_HANDLE, 1,
                                  &pipelineInfo, NULL,
                                  graphicsPipeline) != VK_SUCCESS)
    {
        assert(!"Failed to create graphics pipeline!");
    }
}

/*
*  Execute Graphics Pipeline
*/

void
vk_execute_graphics_pipeline(VkCommandBuffer *commandBuffer,
                             VkPipelineLayout *layout,
                             VkPipeline *pipeline,
                             VkDescriptorSet descSet,
                             VkBuffer *vertexBuffer,
                             u32 vertexCount)
{
    // Bind the Graphics Pipeline
    vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      *pipeline);
    
    // Bind the Descriptor Set
    VkDescriptorSet descSets[] = { descSet };
    vkCmdBindDescriptorSets(*commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            *layout, 0,
                            array_count(descSets),
                            descSets,
                            0, NULL);
    
    // Bind the Vertex Buffer
    
    VkDeviceSize offsets[] = { 0 };
    
    VkBuffer vertexBuffers[] = { *vertexBuffer };
    
    vkCmdBindVertexBuffers(*commandBuffer, 0,
                           array_count(vertexBuffers),
                           vertexBuffers,
                           offsets);
    
    // Draw vertices
    vkCmdDraw(*commandBuffer, vertexCount, 1, 0, 0);
}

/*
*  WinMain application entry point
*/

int CALLBACK
WinMain(HINSTANCE instance, HINSTANCE prevInstance, LPSTR cmdLine, int showCmd)
{
    u32 winWidth = 800;
    u32 winHeight = 600;
    
    VulkanContext vk = win32_init_vulkan(instance,
                                         100, 100, winWidth, winHeight,
                                         "My Shiny Vulkan Window");
    
    /*
    *  App-specific Vulkan objects
    */
    
    VkRenderPass renderPass;
    VkFramebuffer swapchainFramebuffers[2];
    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VkFence frameFence;
    
    VkCommandBuffer graphicsCommandBuffer;
    
    VkPipelineLayout staticPipelineLayout;
    VkPipeline staticGraphicsPipeline;
    
    VkPipelineLayout dynamicPipelineLayout;
    VkPipeline dynamicGraphicsPipeline;
    
    
    /*
    *  Texture-related Vulkan objects
    */
    
    // Descriptor System
    VkDescriptorSetLayout descSetLayout;
    VkDescriptorPool descPool;
    VkDescriptorSet descSet;
    
    // Texture
    VkImage texImage;
    VkDeviceMemory texImageMemory;
    VkImageView texImageView;
    VkSampler texSampler;
    
    /*
    *  Uniform Buffer Vulkan Objects
    */
    
    VkBuffer uniformBuffer;
    VkDeviceMemory uniformBufferMemory;
    
    /*
    *  Static Vertex Buffer Vulkan Objects
    */
    
    VkBuffer vertStagingBuffer;
    VkDeviceMemory vertStagingBufferMemory;
    
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    
    /*
    *  Dynamic Vertex Buffer Vulkan Objects
    */
    
    VkBuffer dynamicVertBuffer;
    VkDeviceMemory dynamicVertBufferMemory;
    
    /*
    *  Create the Render Pass
    */
    
    // Describe the color attachment (the swapchain image)
    VkAttachmentDescription colorAttachment =
    {
        0, // flags
        vk.swapchainImageFormat,
        VK_SAMPLE_COUNT_1_BIT, // no multisampling
        VK_ATTACHMENT_LOAD_OP_CLEAR, // load operation (clear the screen)
        VK_ATTACHMENT_STORE_OP_STORE, // store op (save the result)
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, // stencil load op (ignored)
        VK_ATTACHMENT_STORE_OP_DONT_CARE, // stencil store op (ignored)
        VK_IMAGE_LAYOUT_UNDEFINED, // initial image layout
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR // final layout (optimal to present)
    };
    
    VkAttachmentDescription colorAttachments[] = { colorAttachment };
    
    VkAttachmentReference colorAttachmentRef =
    {
        0, // index of the attachment in the render pass
        // layout during rendering (optimal for rendering color data)
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };
    
    VkAttachmentReference colorAttachmentRefs[] = { colorAttachmentRef };
    
    // Describe the render subpass
    VkSubpassDescription subpass =
    {
        0, // flags
        VK_PIPELINE_BIND_POINT_GRAPHICS, // pipeline bind point
        0, // input attachment count (ignored)
        NULL, // input attachments (ignored)
        array_count(colorAttachmentRefs),
        colorAttachmentRefs,
        NULL, // resolve attachments (ignored)
        NULL, // depth stencil attachment (ignored)
        0, // preserve attachment count (ignored)
        NULL // preserve attachments (ignored)
    };
    
    VkSubpassDescription subpasses[] = { subpass };
    
    VkRenderPassCreateInfo renderPassInfo =
    {
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        NULL,
        0,
        array_count(colorAttachments),
        colorAttachments,
        array_count(subpasses),
        subpasses,
        0, // dependency count (ignored)
        NULL  // dependencies (ignored)
    };
    
    if (vkCreateRenderPass(vk.device, &renderPassInfo, NULL,
                           &renderPass) != VK_SUCCESS)
    {
        assert(!"Failed to create render pass");
    }
    
    /*
    *  Create Swapchain image's Framebuffers
    */
    
    for (u32 i = 0; i < array_count(vk.swapchainImageViews); i++)
    {
        VkImageView frameBufferAttachments[] = { vk.swapchainImageViews[i] };
        
        // Fill framebuffer create info
        VkFramebufferCreateInfo framebufferInfo =
        {
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            NULL,
            0,
            renderPass,
            array_count(frameBufferAttachments),
            frameBufferAttachments,
            vk.swapchainExtents.width,
            vk.swapchainExtents.height,
            1, // layers
        };
        
        // Create the framebuffer
        if (vkCreateFramebuffer(vk.device, &framebufferInfo, NULL,
                                &swapchainFramebuffers[i]) != VK_SUCCESS)
        {
            assert(!"Failed to create framebuffer");
        }
    }
    
    /*
    *  Create Semaphores
    */
    
    VkSemaphoreCreateInfo semaphoreInfo =
    {
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        NULL,
        0
    };
    
    vkCreateSemaphore(vk.device, &semaphoreInfo, NULL,
                      &imageAvailableSemaphore);
    
    vkCreateSemaphore(vk.device, &semaphoreInfo, NULL,
                      &renderFinishedSemaphore);
    
    /*
    *  Create Command Pool and Command Buffer
    */
    
    VkCommandPoolCreateInfo commandPoolCreateInfo =
    {
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        NULL,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        vk.graphicsAndPresentQueueFamily
    };
    
    if (vkCreateCommandPool(vk.device, &commandPoolCreateInfo, NULL,
                            &vk.graphicsCommandPool) != VK_SUCCESS)
    {
        assert(!"Failed to create a command pool");
    }
    
    VkCommandBufferAllocateInfo allocInfo =
    {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        NULL,
        vk.graphicsCommandPool,
        VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        1 // commandBufferCount
    };
    
    vkAllocateCommandBuffers(vk.device, &allocInfo,
                             &graphicsCommandBuffer);
    
    /*
    *  Load SPIR-V and Create Shader Modules
    */
    
    LoadedFile vertexShader = load_entire_file("../shaders/vert.spv");
    assert(vertexShader.size > 0);
    
    LoadedFile fragmentShader = load_entire_file("../shaders/frag.spv");
    assert(fragmentShader.size > 0);
    
    // Create shader modules from loaded binaries
    VkShaderModule vertShaderModule =
        vk_create_shader_module(&vk, vertexShader.data, vertexShader.size);
    
    VkShaderModule fragShaderModule =
        vk_create_shader_module(&vk, fragmentShader.data, fragmentShader.size);
    
    /*
    *  Define Shader Stage Create Info
    */
    
    VkPipelineShaderStageCreateInfo vertShaderStageInfo =
    {
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        NULL,
        0,
        VK_SHADER_STAGE_VERTEX_BIT,
        vertShaderModule,
        "main", // entry point
        NULL // specialization info
    };
    
    VkPipelineShaderStageCreateInfo fragShaderStageInfo =
    {
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        NULL,
        0,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        fragShaderModule,
        "main", // entry point
        NULL, // specialization info
    };
    
    VkPipelineShaderStageCreateInfo shaderStageInfo[] =
    {
        vertShaderStageInfo,
        fragShaderStageInfo
    };
    
    /*
    *  Create the Descriptor Set Layout
    */
    
    VkDescriptorSetLayoutBinding descSetLayoutBinding1 =
    {
        0, // binding
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        1, // descriptorCount
        VK_SHADER_STAGE_FRAGMENT_BIT,
        NULL // pImmutableSamplers
    };
    
    VkDescriptorSetLayoutBinding descSetLayoutBinding2 =
    {
        1, // binding
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        1, // descriptorCount
        VK_SHADER_STAGE_VERTEX_BIT,
        NULL // pImmutableSamplers
    };
    
    VkDescriptorSetLayoutBinding descSetLayoutBindings[] =
    {
        descSetLayoutBinding1,
        descSetLayoutBinding2
    };
    
    VkDescriptorSetLayoutCreateInfo descSetLayoutInfo =
    {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        NULL,
        0,
        array_count(descSetLayoutBindings),
        descSetLayoutBindings
    };
    
    if (vkCreateDescriptorSetLayout(vk.device, &descSetLayoutInfo, NULL,
                                    &descSetLayout) != VK_SUCCESS)
    {
        assert(!"Failed to create descriptor set layout!");
    }
    
    /*
    *  Create the Descriptor Pool
    */
    
    VkDescriptorPoolSize descPoolSize1 =
    {
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        1 // descriptorCount
    };
    
    VkDescriptorPoolSize descPoolSize2 =
    {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        1 // descriptorCount
    };
    
    VkDescriptorPoolSize descPoolSizes[] =
    {
        descPoolSize1,
        descPoolSize2
    };
    
    VkDescriptorPoolCreateInfo descPoolInfo =
    {
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        NULL,
        0,
        1, // maxSets
        array_count(descPoolSizes),
        descPoolSizes
    };
    
    if (vkCreateDescriptorPool(vk.device, &descPoolInfo, NULL,
                               &descPool) != VK_SUCCESS)
    {
        assert(!"Failed to create descriptor pool!");
    }
    
    /*
    *  Allocate the Descriptor Set
    */
    
    VkDescriptorSetLayout descSetLayouts[] = { descSetLayout };
    
    VkDescriptorSetAllocateInfo descSetAllocInfo =
    {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        NULL,
        descPool,
        array_count(descSetLayouts),
        descSetLayouts
    };
    
    if (vkAllocateDescriptorSets(vk.device, &descSetAllocInfo,
                                 &descSet) != VK_SUCCESS)
    {
        assert(!"Failed to allocate descriptor set!");
    }
    
    /*
    *  Define Texture Data
    */
    
    u32 texData[] =
    {
        0xCC000000, 0xFF0000FF,
        0xFF0000FF, 0xCC000000
    };
    
    VkDeviceSize texDataSize = sizeof(texData);
    
    /*
    *  Create the Staging Buffer
    */
    
    VkBuffer texStagingBuffer;
    VkDeviceMemory texStagingBufferMemory;
    
    vk_create_buffer(&vk, texDataSize,
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     &texStagingBuffer, &texStagingBufferMemory);
    
    /*
    *  Map the Buffer Memory and Copy Data into it
    */
    
    void *mappedData = 0;
    vkMapMemory(vk.device, texStagingBufferMemory, 0, texDataSize, 0,
                &mappedData);
    
    memcpy(mappedData, texData, texDataSize);
    
    vkUnmapMemory(vk.device, texStagingBufferMemory);
    
    /*
    *  Create Texture Image
    */
    
    VkExtent3D imageExtent =
    {
        2, // width
        2, // height
        1  // depth
    };
    
    VkImageCreateInfo imageInfo =
    {
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        NULL,
        0,
        VK_IMAGE_TYPE_2D,
        VK_FORMAT_R8G8B8A8_SRGB,
        imageExtent,
        1, // mipLevels
        1, // arrayLayers
        VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0, NULL, // queue families ignored
        VK_IMAGE_LAYOUT_UNDEFINED
    };
    
    if (vkCreateImage(vk.device, &imageInfo, NULL,
                      &texImage) != VK_SUCCESS)
    {
        assert(!"Failed to create image");
    }
    
    /*
    *  Allocate Memory for the Texture Image
    */
    
    {
        VkMemoryRequirements memRequirements = { 0 };
        vkGetImageMemoryRequirements(vk.device, texImage,
                                     &memRequirements);
        
        VkMemoryAllocateInfo memAllocInfo =
        {
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            NULL,
            memRequirements.size,
            vk_find_memory_type(&vk, memRequirements.memoryTypeBits,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        };
        
        if (vkAllocateMemory(vk.device, &memAllocInfo, NULL,
                             &texImageMemory) != VK_SUCCESS)
        {
            assert(!"Failed to allocate texture image memory!");
        }
        
        // Bind the image to the allocated memory
        vkBindImageMemory(vk.device, texImage, texImageMemory, 0);
    }
    
    /*
    *  Begin Single Time Command Buffer
    */
    
    VkCommandBuffer texCommandBuffer = vk_begin_single_time_commands(&vk);
    
    /*
    *  Define the Subresource Range
    */
    
    VkImageSubresourceRange subResRange =
    {
        VK_IMAGE_ASPECT_COLOR_BIT,
        0, // baseMipLevel
        1, // levelCount
        0, // baseArrayLayer
        1, // layerCount
    };
    
    /*
    *  Change Image Layout to Transfer using a barrier
    */
    
    {
        VkImageMemoryBarrier barrier =
        {
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            NULL,
            0, // srcAccessMask
            VK_ACCESS_TRANSFER_WRITE_BIT, // dstAccessMask
            VK_IMAGE_LAYOUT_UNDEFINED, // oldLayout
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, // newLayout
            VK_QUEUE_FAMILY_IGNORED, // srcQueueFamilyIndex
            VK_QUEUE_FAMILY_IGNORED, // dstQueueFamilyIndex
            texImage,
            subResRange
        };
        
        VkImageMemoryBarrier barriers[] = { barrier };
        
        vkCmdPipelineBarrier(texCommandBuffer,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, NULL, 0, NULL,
                             array_count(barriers),
                             barriers);
    }
    
    /*
    *  Copy texture data from Staging Buffer to Image
    */
    
    VkImageSubresourceLayers subResLayers =
    {
        VK_IMAGE_ASPECT_COLOR_BIT,
        0, // mipLevel
        0, // baseArrayLayer
        1  // layerCount
    };
    
    VkBufferImageCopy imageCopy =
    {
        0, // bufferOfsset
        0, // bufferRowLength
        0, // bufferImageHeight
        subResLayers,
        {0, 0, 0},
        imageExtent
    };
    
    vkCmdCopyBufferToImage(texCommandBuffer,
                           texStagingBuffer,
                           texImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &imageCopy);
    
    /*
    *  Change Image Layout to Shader Read using a barrier
    */
    
    {
        VkImageMemoryBarrier barrier =
        {
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            NULL,
            VK_ACCESS_TRANSFER_WRITE_BIT, // srcAccessMask
            VK_ACCESS_SHADER_READ_BIT, // dstAccessMask
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, // oldLayout
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, // newLayout
            VK_QUEUE_FAMILY_IGNORED, // srcQueueFamilyIndex
            VK_QUEUE_FAMILY_IGNORED, // dstQueueFamilyIndex
            texImage,
            subResRange
        };
        
        vkCmdPipelineBarrier(texCommandBuffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, NULL, 0, NULL, 1, &barrier);
    }
    
    /*
    *  End and Execute Single Time Command Buffer
    */
    
    vk_end_single_time_commands(&vk, texCommandBuffer);
    
    /*
    *  Destroy Staging Buffer and Free its Memory
    */
    
    vkDestroyBuffer(vk.device, texStagingBuffer, NULL);
    vkFreeMemory(vk.device, texStagingBufferMemory, NULL);
    
    /*
    *  Create Texture Image View
    */
    
    texImageView = vk_create_image_view(&vk, texImage,
                                        VK_FORMAT_R8G8B8A8_SRGB);
    
    /*
    *  Create Texture Image Sampler
    */
    
    VkSamplerCreateInfo samplerInfo =
    {
        VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        NULL,
        0,
        VK_FILTER_NEAREST,
        VK_FILTER_NEAREST,
        VK_SAMPLER_MIPMAP_MODE_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        0.0f, // mipLodBias
        VK_FALSE, // anisotropyEnable
        16.0f, // maxAnisotropy
        VK_FALSE, // compareEnabled
        VK_COMPARE_OP_ALWAYS,
        0.0f, // minLod
        VK_LOD_CLAMP_NONE, // maxLod
        VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        VK_FALSE // unnormalizedCoordinates
    };
    
    if (vkCreateSampler(vk.device, &samplerInfo, NULL,
                        &texSampler) != VK_SUCCESS)
    {
        assert(!"Failed to create texture sampler!");
    }
    
    /*
    *  Create Uniform Buffer
    */
    
    VkDeviceSize uniBufferSize = sizeof(f32) * 4 * 4;
    
    // Create the buffer (standard Vulkan buffer creation)
    vk_create_buffer(&vk, uniBufferSize,
                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
                     &uniformBuffer, &uniformBufferMemory);
    
    // Map memory to update the projection matrix
    {
        void *data;
        vkMapMemory(vk.device, uniformBufferMemory, 0, uniBufferSize, 0,
                    &data);
        
        f32 projectionMatrix[] =
        {
            2.0f / (f32)winWidth, 0, 0, -1,
            0, 2.0f / (f32)winHeight, 0, -1,
            0, 0, 1, 0,
            0, 0, 0, 1
        };
        
        memcpy(data, &projectionMatrix, uniBufferSize);
        
        vkUnmapMemory(vk.device, uniformBufferMemory);
    }
    
    /*
    *  Update Descriptor Set
    */
    
    VkDescriptorImageInfo descImageInfo =
    {
        texSampler,
        texImageView,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    
    VkDescriptorBufferInfo descBufferInfo =
    {
        uniformBuffer,
        0, // offset
        uniBufferSize
    };
    
    VkWriteDescriptorSet writeDescSet1 =
    {
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        NULL,
        descSet,
        0, // dstBinding
        0, // dstArrayElement
        1, // descriptorCount
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        &descImageInfo,
        NULL, // pBufferInfo
        NULL // pTexelBufferView
    };
    
    VkWriteDescriptorSet writeDescSet2 =
    {
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        NULL,
        descSet,
        1, // dstBinding
        0, // dstArrayElement
        1, // descriptorCount
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        NULL, // pImageInfo
        &descBufferInfo, // pBufferInfo
        NULL // pTexelBufferView
    };
    
    VkWriteDescriptorSet writeDescSets[] =
    {
        writeDescSet1,
        writeDescSet2
    };
    
    vkUpdateDescriptorSets(vk.device,
                           array_count(writeDescSets),
                           writeDescSets,
                           0, NULL);
    
    /*
    *  Create Vertex Buffer Staging Buffer
    */
    
    float s = 100; // Size
    
    f32 vertices[] =
    {
        0, 0,    0, 0,
        s, 0,    1, 0,
        s, s,    1, 1,
        
        0, 0,    0, 0,
        s, s,    1, 1,
        0, s,    0, 1
    };
    
    u32 vertBufferSize = sizeof(vertices);
    
    vk_create_buffer(&vk,
                     vertBufferSize, 
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
                     &vertStagingBuffer, &vertStagingBufferMemory);
    
    {
        void *data;
        vkMapMemory(vk.device, vertStagingBufferMemory, 0, vertBufferSize, 0,
                    &data);
        
        memcpy(data, vertices, vertBufferSize);
        
        vkUnmapMemory(vk.device, vertStagingBufferMemory);
    }
    
    /*
    *  Create Vertex Buffer (GPU only memory)
    */
    
    vk_create_buffer(&vk, vertBufferSize, 
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT, 
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 
                     &vertexBuffer, &vertexBufferMemory);
    
    /*
    *  Copy Memory from Staging Buffer to Vertex Buffer
    */
    
    VkCommandBuffer vertCommandBuffer = vk_begin_single_time_commands(&vk);
    
    VkBufferCopy copyRegion =
    {
        0,
        0,
        vertBufferSize
    };
    
    VkBufferCopy copyRegions[] = { copyRegion };
    
    vkCmdCopyBuffer(vertCommandBuffer,
                    vertStagingBuffer,
                    vertexBuffer,
                    array_count(copyRegions),
                    copyRegions);
    
    vk_end_single_time_commands(&vk, vertCommandBuffer);
    
    /*
    *  Destroy Vertex Staging Buffer and Free its Memory
    */
    
    vkDestroyBuffer(vk.device, vertStagingBuffer, NULL);
    vkFreeMemory(vk.device, vertStagingBufferMemory, NULL);
    
    /*
    *  Create Dynamic Vertex Buffer (CPU accessible memory)
    */
    
    // Enough to draw 4096 quads
    u32 dynamicVertBufferMaxSize = 4096 * 6 * sizeof(f32);
    
    vk_create_buffer(&vk, dynamicVertBufferMaxSize, 
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT, 
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | 
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
                     &dynamicVertBuffer, &dynamicVertBufferMemory);
    
    /*
    *  Map its memory permanently
    */
    
    void *dynamicVertBufferMemoryMap = NULL;
    if (vkMapMemory(vk.device,
                    dynamicVertBufferMemory,
                    0,
                    dynamicVertBufferMaxSize,
                    0,
                    &dynamicVertBufferMemoryMap) != VK_SUCCESS)
    {
        assert(!"Failed to map memory");
    }
    
    /*
    *  Define Vertex Input Layout
    */
    
    u32 stride = sizeof(f32) * 4;
    
    VkVertexInputBindingDescription vertInputBindDesc =
    {
        0, // binding index
        stride,
        VK_VERTEX_INPUT_RATE_VERTEX // per-vertex data (not per-instance)
    };
    
    VkVertexInputBindingDescription vertInputBindDescs[] =
    {
        vertInputBindDesc
    };
    
    /*
    *  Input Attribute Descriptions
    */
    
    VkVertexInputAttributeDescription vertInputAttrDesc1 =
    {
        0, // location in the shader
        0, // binding
        VK_FORMAT_R32G32_SFLOAT,
        0 // byte offset in the struct
    };
    
    VkVertexInputAttributeDescription vertInputAttrDesc2 =
    {
        1, // location in the shader
        0, // binding
        VK_FORMAT_R32G32_SFLOAT,
        sizeof(f32) * 2 // byte offset
    };
    
    VkVertexInputAttributeDescription vertInputAttrDescs[] =
    {
        vertInputAttrDesc1,
        vertInputAttrDesc2
    };
    
    /*
    *  Define Static Vertex Input Create Info
    */
    
    VkPipelineVertexInputStateCreateInfo vertInputStateInfo =
    {
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        NULL,
        0,
        array_count(vertInputBindDescs),
        vertInputBindDescs,
        array_count(vertInputAttrDescs),
        vertInputAttrDescs
    };
    
    /*
    *  Create Graphics Pipelines
    */
    
    vk_create_graphics_pipeline(&vk,
                                array_count(descSetLayouts),
                                descSetLayouts,
                                array_count(shaderStageInfo),
                                shaderStageInfo,
                                vertInputStateInfo,
                                renderPass,
                                &staticPipelineLayout,
                                &staticGraphicsPipeline);
    
    vk_create_graphics_pipeline(&vk,
                                array_count(descSetLayouts),
                                descSetLayouts,
                                array_count(shaderStageInfo),
                                shaderStageInfo,
                                vertInputStateInfo,
                                renderPass,
                                &dynamicPipelineLayout,
                                &dynamicGraphicsPipeline);
    
    /*
    *  Destroy Shader Modules and Create Frame Fence
    */
    
    vkDestroyShaderModule(vk.device, vertShaderModule, NULL);
    vkDestroyShaderModule(vk.device, fragShaderModule, NULL);
    
    VkFenceCreateInfo fenceInfo =
    {
        VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        NULL,
        0
    };
    
    vkCreateFence(vk.device, &fenceInfo, NULL,
                  &frameFence);
    
    /*
    *  Main Loop
    */
    
    globalRunning = true;
    while (globalRunning)
    {
        /*
        *  Wait for the Frame Fence, then Reset it
        */
        
        vkWaitForFences(vk.device, 1, &frameFence, VK_TRUE, UINT64_MAX);
        vkResetFences(vk.device, 1, &frameFence);
        
        /*
        *  Acquire the "Next" Swap Chain Image
        */
        
        u32 imageIndex = UINT32_MAX;
        if (vkAcquireNextImageKHR(vk.device, vk.swapchain,
                                  UINT64_MAX, // timeout
                                  imageAvailableSemaphore,
                                  VK_NULL_HANDLE, // fence (ignored)
                                  &imageIndex) == VK_ERROR_OUT_OF_DATE_KHR)
        {
            // TODO: Handle window resize - recreate swapchain
        }
        
        assert(imageIndex != UINT32_MAX);
        
        /*
        *  Process Windows' messages
        */
        
        MSG message;
        while (PeekMessage(&message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
        
        /*
        *  Capture Mouse Input
        */
        
        POINT mousePos;
        
        // Get cursor position relative to the screen
        GetCursorPos(&mousePos);
        
        // Convert to window-relative coordinates
        ScreenToClient(vk.window, &mousePos);
        
        f32 mx = mousePos.x - 0.5f * s;
        f32 my = mousePos.y - 0.5f * s;
        
        bool mouseLeftDown = GetAsyncKeyState(VK_LBUTTON) & 0x8000;
        
        /*
        *  Update Dynamic Vertex Buffer Data
        */
        
        u32 dynamicVertexCount = 0;
        if (mouseLeftDown)
        {
            dynamicVertexCount = 12;
            
            f32 x = 100; // displacement in x
            
            f32 dynamicVertices[] =
            {
                mx + 0, my + 0,    0, 0,
                mx + s, my + 0,    1, 0,
                mx + s, my + s,    1, 1,
                
                mx + 0, my + 0,    0, 0,
                mx + s, my + s,    1, 1,
                mx + 0, my + s,    0, 1,
                
                // Second quad
                x + 0, 0,    0, 0,
                x + s, 0,    1, 0,
                x + s, s,    1, 1,
                
                x + 0, 0,    0, 0,
                x + s, s,    1, 1,
                x + 0, s,    0, 1,
            };
            
            memcpy(dynamicVertBufferMemoryMap, dynamicVertices, sizeof(dynamicVertices));
        }
        else
        {
            dynamicVertexCount = 6;
            
            f32 dynamicVertices[] =
            {
                mx + 0, my + 0,    0, 0,
                mx + s, my + 0,    1, 0,
                mx + s, my + s,    1, 1,
                
                mx + 0, my + 0,    0, 0,
                mx + s, my + s,    1, 1,
                mx + 0, my + s,    0, 1
            };
            
            memcpy(dynamicVertBufferMemoryMap, dynamicVertices, sizeof(dynamicVertices));
        }
        
        /*
        *  Reset and Begin Command Buffer
        */
        
        vkResetCommandBuffer(graphicsCommandBuffer, 0);
        
        VkCommandBufferBeginInfo beginInfo =
        {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            NULL,
            0,
            NULL // pInheritanceInfo
        };
        
        vkBeginCommandBuffer(graphicsCommandBuffer, &beginInfo);
        
        /*
        *  Begin Render Pass
        */
        
        VkOffset2D renderAreaOffset = { 0, 0 };
        VkRect2D renderArea =
        {
            renderAreaOffset,
            vk.swapchainExtents
        };
        
        VkClearColorValue clearColor = {1, 1, 0, 1}; // yellow
        
        VkClearValue clearValue = { clearColor };
        
        VkClearValue clearValues[] = { clearValue };
        
        VkRenderPassBeginInfo renderPassBeginInfo =
        {
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            NULL,
            renderPass,
            swapchainFramebuffers[imageIndex], // framebuffer
            renderArea,
            array_count(clearValues),
            clearValues
        };
        
        vkCmdBeginRenderPass(graphicsCommandBuffer, &renderPassBeginInfo,
                             VK_SUBPASS_CONTENTS_INLINE);
        
        /*
        *  Execute Graphics Pipelines
        */
        
        vk_execute_graphics_pipeline(&graphicsCommandBuffer,
                                     &staticPipelineLayout,
                                     &staticGraphicsPipeline,
                                     descSet,
                                     &vertexBuffer,
                                     6); // vertexCount
        
        vk_execute_graphics_pipeline(&graphicsCommandBuffer,
                                     &dynamicPipelineLayout,
                                     &dynamicGraphicsPipeline,
                                     descSet,
                                     &dynamicVertBuffer,
                                     dynamicVertexCount);
        
        // End the render pass
        vkCmdEndRenderPass(graphicsCommandBuffer);
        
        // End the command buffer
        vkEndCommandBuffer(graphicsCommandBuffer);
        
        /*
        *  Submit Command Buffer
        */
        
        VkCommandBuffer commandBuffers[] = { graphicsCommandBuffer };
        
        VkSemaphore imageAvailableSemaphores[] = { imageAvailableSemaphore };
        
        VkPipelineStageFlags waitStages[] =
        {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        };
        
        VkSemaphore renderFinishedSemaphores[] = { renderFinishedSemaphore };
        
        VkSubmitInfo submitInfo =
        {
            VK_STRUCTURE_TYPE_SUBMIT_INFO,
            NULL,
            array_count(imageAvailableSemaphores),
            imageAvailableSemaphores,
            waitStages,
            array_count(commandBuffers),
            commandBuffers,
            array_count(renderFinishedSemaphores),
            renderFinishedSemaphores
        };
        
        if (vkQueueSubmit(vk.graphicsAndPresentQueue, 1, &submitInfo,
                          frameFence) != VK_SUCCESS)
        {
            assert(!"failed to submit draw command buffer!");
        }
        
        /*
        *  Present the image
        */
        
        VkSwapchainKHR swapchains[] = { vk.swapchain };
        u32 imageIndices[] = { imageIndex };
        
        VkPresentInfoKHR presentInfo =
        {
            VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            NULL,
            array_count(renderFinishedSemaphores), // waitSemaphoreCount
            renderFinishedSemaphores, // pWaitSemaphores
            array_count(swapchains),
            swapchains,
            imageIndices,
            NULL, // pResults
        };
        
        if (vkQueuePresentKHR(vk.graphicsAndPresentQueue, &presentInfo) ==
            VK_ERROR_OUT_OF_DATE_KHR)
        {
            // TODO: Handle window resize - recreate swapchain
        }
    }
    
    return 0;
}
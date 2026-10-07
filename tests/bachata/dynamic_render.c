/* Dynamic Rendering Execution Test for Mali PanVK.
 * Exercises Vulkan 1.3 dynamicRendering + synchronization2 together with
 * VK_EXT_custom_border_color and VK_EXT_depth_clip_control in a single rendering.
 *
 * Usage: dynamic_render <libvulkan_panfrost.so>
 *
 * Header regeneration:
 *   T=$(mktemp -d)
 *   glslangValidator -V --target-env vulkan1.3 --vn dynamic_render_vert_spv -o "$T/vert.h" tests/bachata/dynamic_render.vert
 *   glslangValidator -V --target-env vulkan1.3 --vn dynamic_render_frag_spv -o "$T/frag.h" tests/bachata/dynamic_render.frag
 *   cat "$T/vert.h" "$T/frag.h" | sed 's/^const uint32_t/static const uint32_t/' > tests/bachata/dynamic_render_spv.h
 *   rm -rf "$T"
 */
#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "dynamic_render_spv.h"

typedef PFN_vkVoidFunction (*icd_gipa_fn)(VkInstance, const char *);

struct exec_buf {
   VkBuffer buffer;
   VkDeviceMemory memory;
   VkDeviceSize size;
   void *map;
};

struct exec_image {
   VkImage image;
   VkDeviceMemory memory;
   VkImageView view;
};

static int
has_extension(const VkExtensionProperties *exts, uint32_t count, const char *name)
{
   for (uint32_t i = 0; i < count; i++) {
      if (!strcmp(exts[i].extensionName, name))
         return 1;
   }
   return 0;
}

static int
create_buffer(VkDevice dev,
              const VkPhysicalDeviceMemoryProperties *mp,
              VkDeviceSize size,
              VkBufferUsageFlags usage,
              PFN_vkCreateBuffer p_CreateBuffer,
              PFN_vkGetBufferMemoryRequirements p_GetBufferMemoryRequirements,
              PFN_vkAllocateMemory p_AllocateMemory,
              PFN_vkBindBufferMemory p_BindBufferMemory,
              PFN_vkMapMemory p_MapMemory,
              struct exec_buf *b)
{
   memset(b, 0, sizeof(*b));
   b->size = size;

   VkBufferCreateInfo bci = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = size,
      .usage = usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   };
   if (p_CreateBuffer(dev, &bci, NULL, &b->buffer) != VK_SUCCESS)
      return 1;

   VkMemoryRequirements mr;
   p_GetBufferMemoryRequirements(dev, b->buffer, &mr);

   uint32_t mi = ~0u;
   for (uint32_t i = 0; i < mp->memoryTypeCount; i++) {
      if ((mr.memoryTypeBits & (1u << i)) &&
          (mp->memoryTypes[i].propertyFlags &
           (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
          (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
         mi = i;
         break;
      }
   }
   if (mi == ~0u)
      return 1;

   VkMemoryAllocateInfo mai = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mr.size,
      .memoryTypeIndex = mi,
   };
   if (p_AllocateMemory(dev, &mai, NULL, &b->memory) != VK_SUCCESS)
      return 1;

   if (p_BindBufferMemory(dev, b->buffer, b->memory, 0) != VK_SUCCESS)
      return 1;

   if (p_MapMemory(dev, b->memory, 0, size, 0, &b->map) != VK_SUCCESS)
      return 1;

   return 0;
}

static void
destroy_buffer(VkDevice dev,
               PFN_vkUnmapMemory p_UnmapMemory,
               PFN_vkDestroyBuffer p_DestroyBuffer,
               PFN_vkFreeMemory p_FreeMemory,
               struct exec_buf *b)
{
   if (b->map) {
      p_UnmapMemory(dev, b->memory);
      b->map = NULL;
   }
   if (b->buffer) {
      p_DestroyBuffer(dev, b->buffer, NULL);
      b->buffer = VK_NULL_HANDLE;
   }
   if (b->memory) {
      p_FreeMemory(dev, b->memory, NULL);
      b->memory = VK_NULL_HANDLE;
   }
}

static int
create_image(VkDevice dev,
             const VkPhysicalDeviceMemoryProperties *mp,
             uint32_t width,
             uint32_t height,
             VkFormat format,
             VkImageUsageFlags usage,
             VkImageAspectFlags aspect_mask,
             PFN_vkCreateImage p_CreateImage,
             PFN_vkGetImageMemoryRequirements p_GetImageMemoryRequirements,
             PFN_vkAllocateMemory p_AllocateMemory,
             PFN_vkBindImageMemory p_BindImageMemory,
             PFN_vkCreateImageView p_CreateImageView,
             struct exec_image *img)
{
   memset(img, 0, sizeof(*img));

   VkImageCreateInfo ici = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = format,
      .extent = {
         .width = width,
         .height = height,
         .depth = 1,
      },
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };
   if (p_CreateImage(dev, &ici, NULL, &img->image) != VK_SUCCESS)
      return 1;

   VkMemoryRequirements mr;
   p_GetImageMemoryRequirements(dev, img->image, &mr);

   uint32_t mi = ~0u;
   for (uint32_t j = 0; j < mp->memoryTypeCount; j++) {
      if ((mr.memoryTypeBits & (1u << j)) &&
          (mp->memoryTypes[j].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
         mi = j;
         break;
      }
   }
   if (mi == ~0u) {
      for (uint32_t j = 0; j < mp->memoryTypeCount; j++) {
         if (mr.memoryTypeBits & (1u << j)) {
            mi = j;
            break;
         }
      }
   }
   if (mi == ~0u)
      return 1;

   VkMemoryAllocateInfo mai = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mr.size,
      .memoryTypeIndex = mi,
   };
   if (p_AllocateMemory(dev, &mai, NULL, &img->memory) != VK_SUCCESS)
      return 1;

   if (p_BindImageMemory(dev, img->image, img->memory, 0) != VK_SUCCESS)
      return 1;

   VkImageViewCreateInfo ivci = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = img->image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = format,
      .subresourceRange = {
         .aspectMask = aspect_mask,
         .baseMipLevel = 0,
         .levelCount = 1,
         .baseArrayLayer = 0,
         .layerCount = 1,
      },
   };
   if (p_CreateImageView(dev, &ivci, NULL, &img->view) != VK_SUCCESS)
      return 1;

   return 0;
}

static void
destroy_image(VkDevice dev,
              PFN_vkDestroyImageView p_DestroyImageView,
              PFN_vkDestroyImage p_DestroyImage,
              PFN_vkFreeMemory p_FreeMemory,
              struct exec_image *img)
{
   if (img->view) {
      p_DestroyImageView(dev, img->view, NULL);
      img->view = VK_NULL_HANDLE;
   }
   if (img->image) {
      p_DestroyImage(dev, img->image, NULL);
      img->image = VK_NULL_HANDLE;
   }
   if (img->memory) {
      p_FreeMemory(dev, img->memory, NULL);
      img->memory = VK_NULL_HANDLE;
   }
}

static int
create_graphics_pipe(VkDevice dev,
                     PFN_vkCreateShaderModule p_CreateShaderModule,
                     PFN_vkDestroyShaderModule p_DestroyShaderModule,
                     PFN_vkCreateGraphicsPipelines p_CreateGraphicsPipelines,
                     const uint32_t *vert_spv, size_t vert_bytes,
                     const uint32_t *frag_spv, size_t frag_bytes,
                     VkPipelineLayout layout,
                     int enable_depth_clip_control,
                     VkPipeline *pipeline)
{
   VkShaderModuleCreateInfo vsmci = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = vert_bytes,
      .pCode = vert_spv,
   };
   VkShaderModule vert_sm = VK_NULL_HANDLE;
   if (p_CreateShaderModule(dev, &vsmci, NULL, &vert_sm) != VK_SUCCESS)
      return 1;

   VkShaderModuleCreateInfo fsmci = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = frag_bytes,
      .pCode = frag_spv,
   };
   VkShaderModule frag_sm = VK_NULL_HANDLE;
   if (p_CreateShaderModule(dev, &fsmci, NULL, &frag_sm) != VK_SUCCESS) {
      p_DestroyShaderModule(dev, vert_sm, NULL);
      return 1;
   }

   VkPipelineShaderStageCreateInfo stages[2] = {
      {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = vert_sm,
         .pName = "main",
      },
      {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = frag_sm,
         .pName = "main",
      },
   };

   VkPipelineVertexInputStateCreateInfo visci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
   };

   VkPipelineInputAssemblyStateCreateInfo iasci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .primitiveRestartEnable = VK_FALSE,
   };

   VkViewport vp = {
      .x = 0.0f,
      .y = 0.0f,
      .width = 64.0f,
      .height = 64.0f,
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
   };
   VkRect2D scissor = {
      .offset = { 0, 0 },
      .extent = { 64, 64 },
   };

   VkPipelineViewportDepthClipControlCreateInfoEXT dcc_ci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT,
      .negativeOneToOne = VK_TRUE,
   };

   VkPipelineViewportStateCreateInfo vsci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .pNext = enable_depth_clip_control ? &dcc_ci : NULL,
      .viewportCount = 1,
      .pViewports = &vp,
      .scissorCount = 1,
      .pScissors = &scissor,
   };

   VkPipelineRasterizationStateCreateInfo rsci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_NONE,
      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .depthBiasEnable = VK_FALSE,
      .lineWidth = 1.0f,
   };

   VkPipelineMultisampleStateCreateInfo msci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
      .sampleShadingEnable = VK_FALSE,
   };

   VkPipelineDepthStencilStateCreateInfo dsci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = VK_TRUE,
      .depthWriteEnable = VK_TRUE,
      .depthCompareOp = VK_COMPARE_OP_LESS,
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable = VK_FALSE,
   };

   VkPipelineColorBlendAttachmentState cb_att = {
      .blendEnable = VK_FALSE,
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                        VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT |
                        VK_COLOR_COMPONENT_A_BIT,
   };
   VkPipelineColorBlendStateCreateInfo cbsci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &cb_att,
   };

   VkFormat color_format = VK_FORMAT_R8G8B8A8_UNORM;
   VkPipelineRenderingCreateInfo rci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &color_format,
      .depthAttachmentFormat = VK_FORMAT_D32_SFLOAT,
      .stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
   };

   VkGraphicsPipelineCreateInfo gpci = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &rci,
      .stageCount = 2,
      .pStages = stages,
      .pVertexInputState = &visci,
      .pInputAssemblyState = &iasci,
      .pViewportState = &vsci,
      .pRasterizationState = &rsci,
      .pMultisampleState = &msci,
      .pDepthStencilState = &dsci,
      .pColorBlendState = &cbsci,
      .layout = layout,
      .renderPass = VK_NULL_HANDLE,
      .subpass = 0,
   };

   VkResult r = p_CreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, pipeline);
   p_DestroyShaderModule(dev, vert_sm, NULL);
   p_DestroyShaderModule(dev, frag_sm, NULL);

   return r == VK_SUCCESS ? 0 : 1;
}

int
main(int argc, char **argv)
{
   if (argc < 2) {
      printf("usage: %s <libvulkan_panfrost.so>\n", argv[0]);
      return 2;
   }
   setvbuf(stdout, NULL, _IONBF, 0);

   void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
   if (!h) {
      printf("FAIL dlopen %s\n", dlerror());
      return 1;
   }
   icd_gipa_fn gipa = (icd_gipa_fn)dlsym(h, "vk_icdGetInstanceProcAddr");
   if (!gipa)
      gipa = (icd_gipa_fn)dlsym(h, "vkGetInstanceProcAddr");
   if (!gipa) {
      printf("FAIL gipa\n");
      dlclose(h);
      return 1;
   }

   PFN_vkCreateInstance CreateInstance =
      (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
   if (!CreateInstance) {
      printf("FAIL missing vkCreateInstance\n");
      dlclose(h);
      return 1;
   }

   const char *iexts[] = { "VK_KHR_get_physical_device_properties2" };
   VkApplicationInfo app = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .apiVersion = VK_API_VERSION_1_3,
   };
   VkInstanceCreateInfo ici = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app,
      .enabledExtensionCount = 1,
      .ppEnabledExtensionNames = iexts,
   };
   VkInstance inst = VK_NULL_HANDLE;
   VkResult r = CreateInstance(&ici, NULL, &inst);
   if (r != VK_SUCCESS) {
      ici.enabledExtensionCount = 0;
      ici.ppEnabledExtensionNames = NULL;
      r = CreateInstance(&ici, NULL, &inst);
   }
   if (r != VK_SUCCESS) {
      printf("FAIL CreateInstance r=%d\n", (int)r);
      dlclose(h);
      return 1;
   }

#define GI(n)                                                                  \
   PFN_vk##n p_##n = (PFN_vk##n)gipa(inst, "vk" #n);                          \
   if (!p_##n) {                                                               \
      printf("FAIL missing vk" #n "\n");                                       \
      return 1;                                                                \
   }
   GI(EnumeratePhysicalDevices)
   GI(GetPhysicalDeviceProperties)
   GI(GetPhysicalDeviceFeatures2)
   GI(GetPhysicalDeviceQueueFamilyProperties)
   GI(GetPhysicalDeviceMemoryProperties)
   GI(EnumerateDeviceExtensionProperties)
   GI(CreateDevice)
   GI(DestroyDevice)
   GI(GetDeviceProcAddr)
   GI(DestroyInstance)
#undef GI

   uint32_t nd = 0;
   if (p_EnumeratePhysicalDevices(inst, &nd, NULL) != VK_SUCCESS || nd == 0) {
      printf("FAIL EnumeratePhysicalDevices count=0\n");
      p_DestroyInstance(inst, NULL);
      dlclose(h);
      return 1;
   }
   VkPhysicalDevice *devs = calloc(nd, sizeof(*devs));
   if (!devs) {
      p_DestroyInstance(inst, NULL);
      dlclose(h);
      return 1;
   }
   p_EnumeratePhysicalDevices(inst, &nd, devs);

   VkPhysicalDevice phys = VK_NULL_HANDLE;
   for (uint32_t i = 0; i < nd; i++) {
      VkPhysicalDeviceProperties p;
      p_GetPhysicalDeviceProperties(devs[i], &p);
      printf("PHYS %u %s vendor=0x%x device=0x%x\n", i, p.deviceName,
             p.vendorID, p.deviceID);
      if (strstr(p.deviceName, "Mali"))
         phys = devs[i];
   }
   if (!phys && nd > 0)
      phys = devs[0];
   free(devs);

   if (!phys) {
      printf("FAIL no physical device\n");
      p_DestroyInstance(inst, NULL);
      dlclose(h);
      return 1;
   }

   /* Enumerate device extensions */
   uint32_t ext_count = 0;
   p_EnumerateDeviceExtensionProperties(phys, NULL, &ext_count, NULL);
   VkExtensionProperties *exts = calloc(ext_count, sizeof(*exts));
   if (ext_count > 0 && !exts) {
      p_DestroyInstance(inst, NULL);
      dlclose(h);
      return 1;
   }
   p_EnumerateDeviceExtensionProperties(phys, NULL, &ext_count, exts);

   int has_custom_border_color = has_extension(exts, ext_count, "VK_EXT_custom_border_color");
   int has_depth_clip_control = has_extension(exts, ext_count, "VK_EXT_depth_clip_control");
   free(exts);

   /* Query Features */
   VkPhysicalDeviceVulkan13Features supported_f13 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
   };
   VkPhysicalDeviceCustomBorderColorFeaturesEXT supported_cbc = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT,
   };
   VkPhysicalDeviceDepthClipControlFeaturesEXT supported_dcc = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT,
   };

   void **p_next_tail = &supported_f13.pNext;
   if (has_custom_border_color) {
      *p_next_tail = &supported_cbc;
      p_next_tail = &supported_cbc.pNext;
   }
   if (has_depth_clip_control) {
      *p_next_tail = &supported_dcc;
      p_next_tail = &supported_dcc.pNext;
   }

   VkPhysicalDeviceFeatures2 supported_f2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &supported_f13,
   };
   p_GetPhysicalDeviceFeatures2(phys, &supported_f2);

   /* Gating: dynamicRendering and synchronization2 are required */
   if (!supported_f13.dynamicRendering || !supported_f13.synchronization2) {
      printf("SKIP dynamic_render dynamicRendering/synchronization2 missing\n");
      printf("RESULT SKIP\n");
      p_DestroyInstance(inst, NULL);
      dlclose(h);
      return 0;
   }

   int cbc_supported = (has_custom_border_color && supported_cbc.customBorderColors);
   int dcc_supported = (has_depth_clip_control && supported_dcc.depthClipControl);

   /* Find graphics queue */
   uint32_t qn = 0;
   p_GetPhysicalDeviceQueueFamilyProperties(phys, &qn, NULL);
   VkQueueFamilyProperties *qp = calloc(qn, sizeof(*qp));
   if (qn > 0 && !qp) {
      p_DestroyInstance(inst, NULL);
      dlclose(h);
      return 1;
   }
   p_GetPhysicalDeviceQueueFamilyProperties(phys, &qn, qp);
   uint32_t qi = ~0u;
   for (uint32_t i = 0; i < qn; i++) {
      if (qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
         qi = i;
         break;
      }
   }
   free(qp);

   if (qi == ~0u) {
      printf("FAIL no graphics queue\n");
      printf("RESULT FAIL\n");
      p_DestroyInstance(inst, NULL);
      dlclose(h);
      return 1;
   }

   VkPhysicalDeviceMemoryProperties mp;
   p_GetPhysicalDeviceMemoryProperties(phys, &mp);

   /* Device creation: enable only what is supported */
   const char *dev_exts[2];
   uint32_t dev_ext_count = 0;
   if (cbc_supported)
      dev_exts[dev_ext_count++] = "VK_EXT_custom_border_color";
   if (dcc_supported)
      dev_exts[dev_ext_count++] = "VK_EXT_depth_clip_control";

   VkPhysicalDeviceFeatures2 dev_feat2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
   };
   VkPhysicalDeviceVulkan13Features dev_f13 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .dynamicRendering = VK_TRUE,
      .synchronization2 = VK_TRUE,
   };
   dev_feat2.pNext = &dev_f13;
   void **dev_next = &dev_f13.pNext;

   VkPhysicalDeviceCustomBorderColorFeaturesEXT dev_cbc = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT,
      .customBorderColors = VK_TRUE,
   };
   if (cbc_supported) {
      *dev_next = &dev_cbc;
      dev_next = &dev_cbc.pNext;
   }

   VkPhysicalDeviceDepthClipControlFeaturesEXT dev_dcc = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT,
      .depthClipControl = VK_TRUE,
   };
   if (dcc_supported) {
      *dev_next = &dev_dcc;
      dev_next = &dev_dcc.pNext;
   }

   float prio = 1.0f;
   VkDeviceQueueCreateInfo qci = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = qi,
      .queueCount = 1,
      .pQueuePriorities = &prio,
   };
   VkDeviceCreateInfo dci = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &dev_feat2,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &qci,
      .enabledExtensionCount = dev_ext_count,
      .ppEnabledExtensionNames = dev_exts,
   };

   VkDevice dev = VK_NULL_HANDLE;
   if (p_CreateDevice(phys, &dci, NULL, &dev) != VK_SUCCESS) {
      printf("FAIL CreateDevice\n");
      printf("RESULT FAIL\n");
      p_DestroyInstance(inst, NULL);
      dlclose(h);
      return 1;
   }

#define GD(n)                                                                  \
   PFN_vk##n p_##n = (PFN_vk##n)p_GetDeviceProcAddr(dev, "vk" #n);            \
   if (!p_##n)                                                                 \
      p_##n = (PFN_vk##n)gipa(inst, "vk" #n);                                  \
   if (!p_##n) {                                                               \
      printf("FAIL missing device vk" #n "\n");                                \
      printf("RESULT FAIL\n");                                                 \
      p_DestroyDevice(dev, NULL);                                              \
      p_DestroyInstance(inst, NULL);                                           \
      dlclose(h);                                                              \
      return 1;                                                                \
   }

#define GD_FALLBACK(n, fb)                                                     \
   PFN_vk##n p_##n = (PFN_vk##n)p_GetDeviceProcAddr(dev, "vk" #n);            \
   if (!p_##n)                                                                 \
      p_##n = (PFN_vk##n)gipa(inst, "vk" #n);                                  \
   if (!p_##n)                                                                 \
      p_##n = (PFN_vk##n)p_GetDeviceProcAddr(dev, "vk" #fb);                   \
   if (!p_##n)                                                                 \
      p_##n = (PFN_vk##n)gipa(inst, "vk" #fb);                                 \
   if (!p_##n) {                                                               \
      printf("FAIL missing device vk" #n "\n");                                \
      printf("RESULT FAIL\n");                                                 \
      p_DestroyDevice(dev, NULL);                                              \
      p_DestroyInstance(inst, NULL);                                           \
      dlclose(h);                                                              \
      return 1;                                                                \
   }

   GD(GetDeviceQueue)
   GD(CreateBuffer)
   GD(DestroyBuffer)
   GD(GetBufferMemoryRequirements)
   GD(AllocateMemory)
   GD(FreeMemory)
   GD(BindBufferMemory)
   GD(MapMemory)
   GD(UnmapMemory)
   GD(CreateImage)
   GD(DestroyImage)
   GD(GetImageMemoryRequirements)
   GD(BindImageMemory)
   GD(CreateImageView)
   GD(DestroyImageView)
   GD(CreateSampler)
   GD(DestroySampler)
   GD(CreateShaderModule)
   GD(DestroyShaderModule)
   GD(CreateDescriptorSetLayout)
   GD(DestroyDescriptorSetLayout)
   GD(CreatePipelineLayout)
   GD(DestroyPipelineLayout)
   GD(CreateGraphicsPipelines)
   GD(DestroyPipeline)
   GD(CreateDescriptorPool)
   GD(DestroyDescriptorPool)
   GD(AllocateDescriptorSets)
   GD(UpdateDescriptorSets)
   GD(CreateCommandPool)
   GD(DestroyCommandPool)
   GD(AllocateCommandBuffers)
   GD(BeginCommandBuffer)
   GD(EndCommandBuffer)
   GD(CmdCopyBufferToImage)
   GD(CmdCopyImageToBuffer)
   GD(CmdBindPipeline)
   GD(CmdBindDescriptorSets)
   GD(CmdPushConstants)
   GD(CmdDraw)
   GD(CreateFence)
   GD(DestroyFence)
   GD(WaitForFences)

   GD_FALLBACK(CmdPipelineBarrier2, CmdPipelineBarrier2KHR)
   GD_FALLBACK(CmdBeginRendering, CmdBeginRenderingKHR)
   GD_FALLBACK(CmdEndRendering, CmdEndRenderingKHR)
   GD_FALLBACK(QueueSubmit2, QueueSubmit2KHR)
#undef GD
#undef GD_FALLBACK

   VkQueue queue = VK_NULL_HANDLE;
   p_GetDeviceQueue(dev, qi, 0, &queue);

   VkCommandPool pool = VK_NULL_HANDLE;
   VkCommandPoolCreateInfo cpci = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = qi,
   };
   if (p_CreateCommandPool(dev, &cpci, NULL, &pool) != VK_SUCCESS) {
      printf("FAIL CreateCommandPool\n");
      printf("RESULT FAIL\n");
      p_DestroyDevice(dev, NULL);
      p_DestroyInstance(inst, NULL);
      dlclose(h);
      return 1;
   }

   VkCommandBufferAllocateInfo cbai = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
   };
   VkCommandBuffer cmd = VK_NULL_HANDLE;
   if (p_AllocateCommandBuffers(dev, &cbai, &cmd) != VK_SUCCESS) {
      printf("FAIL AllocateCommandBuffers\n");
      printf("RESULT FAIL\n");
      p_DestroyCommandPool(dev, pool, NULL);
      p_DestroyDevice(dev, NULL);
      p_DestroyInstance(inst, NULL);
      dlclose(h);
      return 1;
   }

   /* Create host-visible buffers: staging, color readback, depth readback */
   struct exec_buf staging_buf;
   struct exec_buf color_readback_buf;
   struct exec_buf depth_readback_buf;

   if (create_buffer(dev, &mp, 64, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     p_CreateBuffer, p_GetBufferMemoryRequirements,
                     p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                     &staging_buf) ||
       create_buffer(dev, &mp, 64 * 64 * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     p_CreateBuffer, p_GetBufferMemoryRequirements,
                     p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                     &color_readback_buf) ||
       create_buffer(dev, &mp, 64 * 64 * sizeof(float), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     p_CreateBuffer, p_GetBufferMemoryRequirements,
                     p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                     &depth_readback_buf)) {
      printf("FAIL buffer creation\n");
      printf("RESULT FAIL\n");
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &staging_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &color_readback_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &depth_readback_buf);
      p_DestroyCommandPool(dev, pool, NULL);
      p_DestroyDevice(dev, NULL);
      p_DestroyInstance(inst, NULL);
      dlclose(h);
      return 1;
   }

   /* Initialize staging buffer with 4x4 solid green: (0, 255, 0, 255) */
   uint8_t green_tex[64];
   for (int i = 0; i < 16; i++) {
      green_tex[i * 4 + 0] = 0;
      green_tex[i * 4 + 1] = 255;
      green_tex[i * 4 + 2] = 0;
      green_tex[i * 4 + 3] = 255;
   }
   memcpy(staging_buf.map, green_tex, sizeof(green_tex));

   /* Initialize readback buffers */
   memset(color_readback_buf.map, 0, 64 * 64 * 4);
   memset(depth_readback_buf.map, 0, 64 * 64 * sizeof(float));

   /* Create images: 4x4 texture, 64x64 color attachment, 64x64 depth attachment */
   struct exec_image tex_img;
   struct exec_image color_img;
   struct exec_image depth_img;

   if (create_image(dev, &mp, 4, 4, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    p_CreateImage, p_GetImageMemoryRequirements,
                    p_AllocateMemory, p_BindImageMemory, p_CreateImageView,
                    &tex_img) ||
       create_image(dev, &mp, 64, 64, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    p_CreateImage, p_GetImageMemoryRequirements,
                    p_AllocateMemory, p_BindImageMemory, p_CreateImageView,
                    &color_img) ||
       create_image(dev, &mp, 64, 64, VK_FORMAT_D32_SFLOAT,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                    VK_IMAGE_ASPECT_DEPTH_BIT,
                    p_CreateImage, p_GetImageMemoryRequirements,
                    p_AllocateMemory, p_BindImageMemory, p_CreateImageView,
                    &depth_img)) {
      printf("FAIL image creation\n");
      printf("RESULT FAIL\n");
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &tex_img);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &color_img);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &depth_img);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &staging_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &color_readback_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &depth_readback_buf);
      p_DestroyCommandPool(dev, pool, NULL);
      p_DestroyDevice(dev, NULL);
      p_DestroyInstance(inst, NULL);
      dlclose(h);
      return 1;
   }

   /* Create Sampler: CLAMP_TO_BORDER, NEAREST filter, custom border color or opaque white */
   VkSamplerCustomBorderColorCreateInfoEXT custom_border_ci = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT,
      .customBorderColor = {
         .float32 = { 1.0f, 0.0f, 1.0f, 1.0f },
      },
      .format = VK_FORMAT_R8G8B8A8_UNORM,
   };

   VkSamplerCreateInfo sci = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .pNext = cbc_supported ? &custom_border_ci : NULL,
      .magFilter = VK_FILTER_NEAREST,
      .minFilter = VK_FILTER_NEAREST,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
      .borderColor = cbc_supported ? VK_BORDER_COLOR_FLOAT_CUSTOM_EXT : VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
   };
   VkSampler sampler = VK_NULL_HANDLE;
   if (p_CreateSampler(dev, &sci, NULL, &sampler) != VK_SUCCESS) {
      printf("FAIL CreateSampler\n");
      printf("RESULT FAIL\n");
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &tex_img);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &color_img);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &depth_img);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &staging_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &color_readback_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &depth_readback_buf);
      p_DestroyCommandPool(dev, pool, NULL);
      p_DestroyDevice(dev, NULL);
      p_DestroyInstance(inst, NULL);
      dlclose(h);
      return 1;
   }

   /* Create descriptor set layout for combined image sampler at binding 0 */
   VkDescriptorSetLayoutBinding bnd = {
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
   };
   VkDescriptorSetLayoutCreateInfo dslci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 1,
      .pBindings = &bnd,
   };
   VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
   if (p_CreateDescriptorSetLayout(dev, &dslci, NULL, &dsl) != VK_SUCCESS) {
      printf("FAIL CreateDescriptorSetLayout\n");
      printf("RESULT FAIL\n");
      p_DestroySampler(dev, sampler, NULL);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &tex_img);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &color_img);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &depth_img);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &staging_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &color_readback_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &depth_readback_buf);
      p_DestroyCommandPool(dev, pool, NULL);
      p_DestroyDevice(dev, NULL);
      p_DestroyInstance(inst, NULL);
      dlclose(h);
      return 1;
   }

   /* Pipeline layout with push constant float z for vertex shader */
   VkPushConstantRange pcr = {
      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
      .offset = 0,
      .size = sizeof(float),
   };
   VkPipelineLayoutCreateInfo plci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &dsl,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &pcr,
   };
   VkPipelineLayout playout = VK_NULL_HANDLE;
   if (p_CreatePipelineLayout(dev, &plci, NULL, &playout) != VK_SUCCESS) {
      printf("FAIL CreatePipelineLayout\n");
      printf("RESULT FAIL\n");
      p_DestroyDescriptorSetLayout(dev, dsl, NULL);
      p_DestroySampler(dev, sampler, NULL);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &tex_img);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &color_img);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &depth_img);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &staging_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &color_readback_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &depth_readback_buf);
      p_DestroyCommandPool(dev, pool, NULL);
      p_DestroyDevice(dev, NULL);
      p_DestroyInstance(inst, NULL);
      dlclose(h);
      return 1;
   }

   /* Descriptor pool and descriptor set */
   VkDescriptorPoolSize pool_size = {
      .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = 1,
   };
   VkDescriptorPoolCreateInfo dpci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1,
      .poolSizeCount = 1,
      .pPoolSizes = &pool_size,
   };
   VkDescriptorPool dpool = VK_NULL_HANDLE;
   if (p_CreateDescriptorPool(dev, &dpci, NULL, &dpool) != VK_SUCCESS) {
      printf("FAIL CreateDescriptorPool\n");
      printf("RESULT FAIL\n");
      p_DestroyPipelineLayout(dev, playout, NULL);
      p_DestroyDescriptorSetLayout(dev, dsl, NULL);
      p_DestroySampler(dev, sampler, NULL);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &tex_img);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &color_img);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &depth_img);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &staging_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &color_readback_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &depth_readback_buf);
      p_DestroyCommandPool(dev, pool, NULL);
      p_DestroyDevice(dev, NULL);
      p_DestroyInstance(inst, NULL);
      dlclose(h);
      return 1;
   }

   VkDescriptorSetAllocateInfo dsai = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = dpool,
      .descriptorSetCount = 1,
      .pSetLayouts = &dsl,
   };
   VkDescriptorSet dset = VK_NULL_HANDLE;
   if (p_AllocateDescriptorSets(dev, &dsai, &dset) != VK_SUCCESS) {
      printf("FAIL AllocateDescriptorSets\n");
      printf("RESULT FAIL\n");
      p_DestroyDescriptorPool(dev, dpool, NULL);
      p_DestroyPipelineLayout(dev, playout, NULL);
      p_DestroyDescriptorSetLayout(dev, dsl, NULL);
      p_DestroySampler(dev, sampler, NULL);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &tex_img);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &color_img);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &depth_img);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &staging_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &color_readback_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &depth_readback_buf);
      p_DestroyCommandPool(dev, pool, NULL);
      p_DestroyDevice(dev, NULL);
      p_DestroyInstance(inst, NULL);
      dlclose(h);
      return 1;
   }

   VkDescriptorImageInfo dii = {
      .sampler = sampler,
      .imageView = tex_img.view,
      .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
   };
   VkWriteDescriptorSet wds = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = dset,
      .dstBinding = 0,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .pImageInfo = &dii,
   };
   p_UpdateDescriptorSets(dev, 1, &wds, 0, NULL);

   /* Create graphics pipeline */
   VkPipeline pipe = VK_NULL_HANDLE;
   if (create_graphics_pipe(dev, p_CreateShaderModule, p_DestroyShaderModule,
                            p_CreateGraphicsPipelines,
                            dynamic_render_vert_spv, sizeof(dynamic_render_vert_spv),
                            dynamic_render_frag_spv, sizeof(dynamic_render_frag_spv),
                            playout, dcc_supported, &pipe)) {
      printf("FAIL pipeline creation\n");
      printf("RESULT FAIL\n");
      p_DestroyDescriptorPool(dev, dpool, NULL);
      p_DestroyPipelineLayout(dev, playout, NULL);
      p_DestroyDescriptorSetLayout(dev, dsl, NULL);
      p_DestroySampler(dev, sampler, NULL);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &tex_img);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &color_img);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &depth_img);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &staging_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &color_readback_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &depth_readback_buf);
      p_DestroyCommandPool(dev, pool, NULL);
      p_DestroyDevice(dev, NULL);
      p_DestroyInstance(inst, NULL);
      dlclose(h);
      return 1;
   }

   /* Record command buffer */
   VkCommandBufferBeginInfo cbi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
   };
   p_BeginCommandBuffer(cmd, &cbi);

   /* 1. Staging copy: tex_img UNDEFINED -> TRANSFER_DST_OPTIMAL */
   VkImageMemoryBarrier2 imb_tex_dst = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
      .srcAccessMask = VK_ACCESS_2_NONE,
      .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
      .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .image = tex_img.image,
      .subresourceRange = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .baseMipLevel = 0,
         .levelCount = 1,
         .baseArrayLayer = 0,
         .layerCount = 1,
      },
   };
   VkDependencyInfo dep_tex_dst = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = 1,
      .pImageMemoryBarriers = &imb_tex_dst,
   };
   p_CmdPipelineBarrier2(cmd, &dep_tex_dst);

   VkBufferImageCopy tex_copy = {
      .imageSubresource = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .mipLevel = 0,
         .baseArrayLayer = 0,
         .layerCount = 1,
      },
      .imageExtent = { 4, 4, 1 },
   };
   p_CmdCopyBufferToImage(cmd, staging_buf.buffer, tex_img.image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &tex_copy);

   /* 2. Transitions before rendering:
    *    - tex_img: TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
    *    - color_img: UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL
    *    - depth_img: UNDEFINED -> DEPTH_ATTACHMENT_OPTIMAL
    */
   VkImageMemoryBarrier2 pre_barriers[3] = {
      {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
         .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
         .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
         .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
         .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
         .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
         .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
         .image = tex_img.image,
         .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
      },
      {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
         .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
         .srcAccessMask = VK_ACCESS_2_NONE,
         .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
         .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
         .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
         .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
         .image = color_img.image,
         .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
      },
      {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
         .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
         .srcAccessMask = VK_ACCESS_2_NONE,
         .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                         VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
         .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                          VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
         .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
         .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
         .image = depth_img.image,
         .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
      },
   };
   VkDependencyInfo pre_dep = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = 3,
      .pImageMemoryBarriers = pre_barriers,
   };
   p_CmdPipelineBarrier2(cmd, &pre_dep);

   /* 3. Begin rendering: color clear to (0,0,0,1), depth clear to 1.0 */
   VkClearValue clear_color = {
      .color = { .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } },
   };
   VkRenderingAttachmentInfo color_att = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = color_img.view,
      .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue = clear_color,
   };

   VkClearValue clear_depth = {
      .depthStencil = { .depth = 1.0f, .stencil = 0 },
   };
   VkRenderingAttachmentInfo depth_att = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = depth_img.view,
      .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue = clear_depth,
   };

   VkRenderingInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {
         .offset = { 0, 0 },
         .extent = { 64, 64 },
      },
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = &color_att,
      .pDepthAttachment = &depth_att,
   };

   p_CmdBeginRendering(cmd, &rendering_info);
   p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
   p_CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, playout, 0, 1, &dset, 0, NULL);

   /* Push constant z: -0.5 when depthClipControl is enabled, 0.25 otherwise */
   float z_val = dcc_supported ? -0.5f : 0.25f;
   p_CmdPushConstants(cmd, playout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float), &z_val);

   p_CmdDraw(cmd, 6, 1, 0, 0);
   p_CmdEndRendering(cmd);

   /* 4. Readback transitions:
    *    - color_img: COLOR_ATTACHMENT_OPTIMAL -> TRANSFER_SRC_OPTIMAL
    *    - depth_img: DEPTH_ATTACHMENT_OPTIMAL -> TRANSFER_SRC_OPTIMAL
    */
   VkImageMemoryBarrier2 post_barriers[2] = {
      {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
         .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
         .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
         .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
         .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
         .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
         .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
         .image = color_img.image,
         .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
      },
      {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
         .srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
         .srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
         .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
         .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
         .oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
         .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
         .image = depth_img.image,
         .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
      },
   };
   VkDependencyInfo post_dep = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = 2,
      .pImageMemoryBarriers = post_barriers,
   };
   p_CmdPipelineBarrier2(cmd, &post_dep);

   /* Copy color image to host buffer */
   VkBufferImageCopy color_copy = {
      .imageSubresource = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .mipLevel = 0,
         .baseArrayLayer = 0,
         .layerCount = 1,
      },
      .imageExtent = { 64, 64, 1 },
   };
   p_CmdCopyImageToBuffer(cmd, color_img.image,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          color_readback_buf.buffer, 1, &color_copy);

   /* Copy depth image to host buffer */
   VkBufferImageCopy depth_copy = {
      .imageSubresource = {
         .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
         .mipLevel = 0,
         .baseArrayLayer = 0,
         .layerCount = 1,
      },
      .imageExtent = { 64, 64, 1 },
   };
   p_CmdCopyImageToBuffer(cmd, depth_img.image,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          depth_readback_buf.buffer, 1, &depth_copy);

   /* Buffer barrier2: transfer write -> host read */
   VkBufferMemoryBarrier2 buf_barriers[2] = {
      {
         .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
         .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
         .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
         .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
         .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .buffer = color_readback_buf.buffer,
         .offset = 0,
         .size = VK_WHOLE_SIZE,
      },
      {
         .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
         .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
         .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
         .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
         .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .buffer = depth_readback_buf.buffer,
         .offset = 0,
         .size = VK_WHOLE_SIZE,
      },
   };
   VkDependencyInfo buf_dep = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .bufferMemoryBarrierCount = 2,
      .pBufferMemoryBarriers = buf_barriers,
   };
   p_CmdPipelineBarrier2(cmd, &buf_dep);

   p_EndCommandBuffer(cmd);

   /* Submit with vkQueueSubmit2 and fence */
   VkFence fence = VK_NULL_HANDLE;
   VkFenceCreateInfo fci = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
   };
   if (p_CreateFence(dev, &fci, NULL, &fence) != VK_SUCCESS) {
      printf("FAIL CreateFence\n");
      printf("RESULT FAIL\n");
      p_DestroyPipeline(dev, pipe, NULL);
      p_DestroyDescriptorPool(dev, dpool, NULL);
      p_DestroyPipelineLayout(dev, playout, NULL);
      p_DestroyDescriptorSetLayout(dev, dsl, NULL);
      p_DestroySampler(dev, sampler, NULL);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &tex_img);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &color_img);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &depth_img);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &staging_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &color_readback_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &depth_readback_buf);
      p_DestroyCommandPool(dev, pool, NULL);
      p_DestroyDevice(dev, NULL);
      p_DestroyInstance(inst, NULL);
      dlclose(h);
      return 1;
   }

   VkCommandBufferSubmitInfo cbsi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
      .commandBuffer = cmd,
   };
   VkSubmitInfo2 si2 = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .commandBufferInfoCount = 1,
      .pCommandBufferInfos = &cbsi,
   };
   VkResult sr = p_QueueSubmit2(queue, 1, &si2, fence);
   if (sr != VK_SUCCESS) {
      printf("FAIL QueueSubmit2 r=%d\n", (int)sr);
      printf("RESULT FAIL\n");
      p_DestroyFence(dev, fence, NULL);
      p_DestroyPipeline(dev, pipe, NULL);
      p_DestroyDescriptorPool(dev, dpool, NULL);
      p_DestroyPipelineLayout(dev, playout, NULL);
      p_DestroyDescriptorSetLayout(dev, dsl, NULL);
      p_DestroySampler(dev, sampler, NULL);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &tex_img);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &color_img);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &depth_img);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &staging_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &color_readback_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &depth_readback_buf);
      p_DestroyCommandPool(dev, pool, NULL);
      p_DestroyDevice(dev, NULL);
      p_DestroyInstance(inst, NULL);
      dlclose(h);
      return 1;
   }

   VkResult wr = p_WaitForFences(dev, 1, &fence, VK_TRUE, 10000000000ULL); /* 10 s timeout */
   if (wr != VK_SUCCESS) {
      printf("FAIL wait r=%d\n", (int)wr);
      printf("RESULT FAIL\n");
      p_DestroyFence(dev, fence, NULL);
      p_DestroyPipeline(dev, pipe, NULL);
      p_DestroyDescriptorPool(dev, dpool, NULL);
      p_DestroyPipelineLayout(dev, playout, NULL);
      p_DestroyDescriptorSetLayout(dev, dsl, NULL);
      p_DestroySampler(dev, sampler, NULL);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &tex_img);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &color_img);
      destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &depth_img);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &staging_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &color_readback_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &depth_readback_buf);
      p_DestroyCommandPool(dev, pool, NULL);
      p_DestroyDevice(dev, NULL);
      p_DestroyInstance(inst, NULL);
      dlclose(h);
      return 1;
   }

   int passes = 0;
   int fails = 0;
   int skips = 0;

   const uint8_t *p_color = (const uint8_t *)color_readback_buf.map;
   const float *p_depth = (const float *)depth_readback_buf.map;

#define PIX_R(x, y) p_color[((y) * 64 + (x)) * 4 + 0]
#define PIX_G(x, y) p_color[((y) * 64 + (x)) * 4 + 1]
#define PIX_B(x, y) p_color[((y) * 64 + (x)) * 4 + 2]
#define PIX_A(x, y) p_color[((y) * 64 + (x)) * 4 + 3]
#define DEPTH_VAL(x, y) p_depth[(y) * 64 + (x)]

   /* ──────────────────────────────────────────────────────────────────────────
    * Case 1: dynamic_rendering
    * Centre pixel (32,32) is green within +-2 per channel and the image is not
    * all clear-black.
    * ────────────────────────────────────────────────────────────────────────── */
   int r32 = PIX_R(32, 32);
   int g32 = PIX_G(32, 32);
   int b32 = PIX_B(32, 32);
   int a32 = PIX_A(32, 32);

   int green_centre = (abs(r32 - 0) <= 2 && abs(g32 - 255) <= 2 &&
                       abs(b32 - 0) <= 2 && abs(a32 - 255) <= 2);

   int all_clear_black = 1;
   for (int y = 0; y < 64 && all_clear_black; y++) {
      for (int x = 0; x < 64; x++) {
         int r_val = PIX_R(x, y);
         int g_val = PIX_G(x, y);
         int b_val = PIX_B(x, y);
         int a_val = PIX_A(x, y);
         if (r_val != 0 || g_val != 0 || b_val != 0 || a_val != 255) {
            all_clear_black = 0;
            break;
         }
      }
   }

   if (!green_centre) {
      printf("FAIL case dynamic_rendering centre pixel (32,32)=(%d,%d,%d,%d) exp green (0,255,0,255)\n",
             r32, g32, b32, a32);
      fails++;
   } else if (all_clear_black) {
      printf("FAIL case dynamic_rendering image is all clear-black\n");
      fails++;
   } else {
      printf("PASS case dynamic_rendering\n");
      passes++;
   }

   /* ──────────────────────────────────────────────────────────────────────────
    * Case 2: border_color
    * Pixels (2,2), (61,2), (2,61), (61,61) equal the expected border color within
    * +-2 per channel; centre region pixels (24..39) green.
    * ────────────────────────────────────────────────────────────────────────── */
   if (!cbc_supported) {
      printf("SKIP case border_color customBorderColors missing\n");
      skips++;
   } else {
      int exp_br = 255;
      int exp_bg = 0;
      int exp_bb = 255;
      int exp_ba = 255;

      static const struct { int x, y; } corners[4] = {
         { 2, 2 }, { 61, 2 }, { 2, 61 }, { 61, 61 }
      };

      int corner_ok = 1;
      int bad_cx = -1, bad_cy = -1, bad_cr = 0, bad_cg = 0, bad_cb = 0, bad_ca = 0;
      for (int i = 0; i < 4; i++) {
         int cx = corners[i].x;
         int cy = corners[i].y;
         int cr = PIX_R(cx, cy);
         int cg = PIX_G(cx, cy);
         int cb = PIX_B(cx, cy);
         int ca = PIX_A(cx, cy);
         if (abs(cr - exp_br) > 2 || abs(cg - exp_bg) > 2 ||
             abs(cb - exp_bb) > 2 || abs(ca - exp_ba) > 2) {
            corner_ok = 0;
            bad_cx = cx;
            bad_cy = cy;
            bad_cr = cr;
            bad_cg = cg;
            bad_cb = cb;
            bad_ca = ca;
            break;
         }
      }

      int centre_region_ok = 1;
      int bad_mx = -1, bad_my = -1, bad_mr = 0, bad_mg = 0, bad_mb = 0, bad_ma = 0;
      for (int y = 24; y <= 39 && centre_region_ok; y++) {
         for (int x = 24; x <= 39; x++) {
            int mr = PIX_R(x, y);
            int mg = PIX_G(x, y);
            int mb = PIX_B(x, y);
            int ma = PIX_A(x, y);
            if (abs(mr - 0) > 2 || abs(mg - 255) > 2 ||
                abs(mb - 0) > 2 || abs(ma - 255) > 2) {
               centre_region_ok = 0;
               bad_mx = x;
               bad_my = y;
               bad_mr = mr;
               bad_mg = mg;
               bad_mb = mb;
               bad_ma = ma;
               break;
            }
         }
      }

      if (!corner_ok) {
         printf("FAIL case border_color corner (%d,%d)=(%d,%d,%d,%d) exp border (%d,%d,%d,%d)\n",
                bad_cx, bad_cy, bad_cr, bad_cg, bad_cb, bad_ca, exp_br, exp_bg, exp_bb, exp_ba);
         fails++;
      } else if (!centre_region_ok) {
         printf("FAIL case border_color centre region (%d,%d)=(%d,%d,%d,%d) exp green (0,255,0,255)\n",
                bad_mx, bad_my, bad_mr, bad_mg, bad_mb, bad_ma);
         fails++;
      } else {
         printf("PASS case border_color\n");
         passes++;
      }
   }

   /* ──────────────────────────────────────────────────────────────────────────
    * Case 3: depth_clip_control (only when supported)
    * Depth at (32,32) and (2,2) is 0.25 within 1e-3.
    * If the quad got clipped the color is black and depth 1.0: report that explicitly.
    * ────────────────────────────────────────────────────────────────────────── */
   if (!dcc_supported) {
      printf("SKIP case depth_clip_control depthClipControl missing\n");
      skips++;
   } else {
      float d32 = DEPTH_VAL(32, 32);
      float d2 = DEPTH_VAL(2, 2);
      int d32_ok = fabsf(d32 - 0.25f) <= 1e-3f;
      int d2_ok = fabsf(d2 - 0.25f) <= 1e-3f;

      if (d32_ok && d2_ok) {
         printf("PASS case depth_clip_control\n");
         passes++;
      } else {
         int bad_x = !d32_ok ? 32 : 2;
         int bad_y = !d32_ok ? 32 : 2;
         float bad_d = !d32_ok ? d32 : d2;
         int bad_r = PIX_R(bad_x, bad_y);
         int bad_g = PIX_G(bad_x, bad_y);
         int bad_b = PIX_B(bad_x, bad_y);

         if (fabsf(bad_d - 1.0f) <= 1e-3f && bad_r == 0 && bad_g == 0 && bad_b == 0) {
            printf("FAIL case depth_clip_control quad clipped: color black and depth 1.0 at (%d,%d) (got %f exp 0.25)\n",
                   bad_x, bad_y, bad_d);
         } else {
            printf("FAIL case depth_clip_control depth mismatch at (%d,%d): got %f exp 0.25\n",
                   bad_x, bad_y, bad_d);
         }
         fails++;
      }
   }

#undef PIX_R
#undef PIX_G
#undef PIX_B
#undef PIX_A
#undef DEPTH_VAL

   (void)passes;
   (void)skips;

   /* Destroy all objects */
   p_DestroyFence(dev, fence, NULL);
   p_DestroyPipeline(dev, pipe, NULL);
   p_DestroyDescriptorPool(dev, dpool, NULL);
   p_DestroyPipelineLayout(dev, playout, NULL);
   p_DestroyDescriptorSetLayout(dev, dsl, NULL);
   p_DestroySampler(dev, sampler, NULL);
   destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &tex_img);
   destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &color_img);
   destroy_image(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &depth_img);
   destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &staging_buf);
   destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &color_readback_buf);
   destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &depth_readback_buf);
   p_DestroyCommandPool(dev, pool, NULL);
   p_DestroyDevice(dev, NULL);
   p_DestroyInstance(inst, NULL);
   dlclose(h);

   if (fails > 0) {
      printf("RESULT FAIL\n");
      return 1;
   }

   printf("RESULT PASS\n");
   return 0;
}

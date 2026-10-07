/* Vulkan Shader Arithmetic & Extended Types Tests.
 * Executes workloads verifying Vulkan 1.2+ / promoted shader arithmetic & types:
 *   (1) int8: 8-bit integer arithmetic (GL_EXT_shader_explicit_arithmetic_types_int8)
 *   (2) int16: 16-bit integer arithmetic (GL_EXT_shader_explicit_arithmetic_types_int16)
 *   (3) int64: 64-bit integer arithmetic (GL_EXT_shader_explicit_arithmetic_types_int64)
 *   (4) storage8: 8-bit SSBO storage access (GL_EXT_shader_8bit_storage)
 *   (5) storage16: 16-bit SSBO storage access (GL_EXT_shader_16bit_storage)
 *   (6) demote: demote to helper invocation (GL_EXT_demote_to_helper_invocation)
 *   (7) subgroup_size_control: subgroup size control & full subgroups
 *   (8) zero_init_wg: zero initialize workgroup memory (GL_EXT_null_initializer)
 *
 * Usage: shader_arith <libvulkan_panfrost.so>
 */
#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "shader_arith_spv.h"

typedef PFN_vkVoidFunction (*icd_gipa_fn)(VkInstance, const char *);

struct exec_buf {
   VkBuffer buffer;
   VkDeviceMemory memory;
   VkDeviceSize size;
   void *map;
   VkDeviceAddress address;
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
              VkMemoryAllocateFlags alloc_flags,
              PFN_vkCreateBuffer p_CreateBuffer,
              PFN_vkGetBufferMemoryRequirements p_GetBufferMemoryRequirements,
              PFN_vkAllocateMemory p_AllocateMemory,
              PFN_vkBindBufferMemory p_BindBufferMemory,
              PFN_vkMapMemory p_MapMemory,
              PFN_vkGetBufferDeviceAddress g_bda,
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

   VkMemoryAllocateFlagsInfo afi = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
      .flags = alloc_flags,
   };
   VkMemoryAllocateInfo mai = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = alloc_flags ? &afi : NULL,
      .allocationSize = mr.size,
      .memoryTypeIndex = mi,
   };
   if (p_AllocateMemory(dev, &mai, NULL, &b->memory) != VK_SUCCESS)
      return 1;

   if (p_BindBufferMemory(dev, b->buffer, b->memory, 0) != VK_SUCCESS)
      return 1;

   if (p_MapMemory(dev, b->memory, 0, size, 0, &b->map) != VK_SUCCESS)
      return 1;

   if ((usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) && g_bda) {
      VkBufferDeviceAddressInfo bdai = {
         .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
         .buffer = b->buffer,
      };
      b->address = g_bda(dev, &bdai);
   }

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
create_image_2d(VkDevice dev,
                const VkPhysicalDeviceMemoryProperties *mp,
                uint32_t width,
                uint32_t height,
                VkFormat format,
                VkImageUsageFlags usage,
                PFN_vkCreateImage p_CreateImage,
                PFN_vkGetImageMemoryRequirements p_GetImageMemoryRequirements,
                PFN_vkAllocateMemory p_AllocateMemory,
                PFN_vkBindImageMemory p_BindImageMemory,
                PFN_vkCreateImageView p_CreateImageView,
                struct exec_image *img_out)
{
   memset(img_out, 0, sizeof(*img_out));

   VkImageCreateInfo ici = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = format,
      .extent = { width, height, 1 },
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };
   if (p_CreateImage(dev, &ici, NULL, &img_out->image) != VK_SUCCESS)
      return 1;

   VkMemoryRequirements mr;
   p_GetImageMemoryRequirements(dev, img_out->image, &mr);

   uint32_t mi = ~0u;
   for (uint32_t i = 0; i < mp->memoryTypeCount; i++) {
      if ((mr.memoryTypeBits & (1u << i)) &&
          (mp->memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
         mi = i;
         break;
      }
   }
   if (mi == ~0u) {
      for (uint32_t i = 0; i < mp->memoryTypeCount; i++) {
         if (mr.memoryTypeBits & (1u << i)) {
            mi = i;
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
   if (p_AllocateMemory(dev, &mai, NULL, &img_out->memory) != VK_SUCCESS)
      return 1;

   if (p_BindImageMemory(dev, img_out->image, img_out->memory, 0) != VK_SUCCESS)
      return 1;

   VkImageViewCreateInfo ivci = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = img_out->image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = format,
      .subresourceRange = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .baseMipLevel = 0,
         .levelCount = 1,
         .baseArrayLayer = 0,
         .layerCount = 1,
      },
   };
   if (p_CreateImageView(dev, &ivci, NULL, &img_out->view) != VK_SUCCESS)
      return 1;

   return 0;
}

static void
destroy_image_2d(VkDevice dev,
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

static VkResult
create_compute_pipe(VkDevice dev,
                    PFN_vkCreateShaderModule p_CreateShaderModule,
                    PFN_vkDestroyShaderModule p_DestroyShaderModule,
                    PFN_vkCreateComputePipelines p_CreateComputePipelines,
                    const uint32_t *spv,
                    size_t spv_bytes,
                    VkPipelineLayout layout,
                    VkPipeline *pipeline)
{
   VkShaderModuleCreateInfo smci = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = spv_bytes,
      .pCode = spv,
   };
   VkShaderModule sm;
   VkResult r = p_CreateShaderModule(dev, &smci, NULL, &sm);
   if (r != VK_SUCCESS)
      return r;

   VkComputePipelineCreateInfo cpci = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_COMPUTE_BIT,
         .module = sm,
         .pName = "main",
      },
      .layout = layout,
   };
   r = p_CreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, pipeline);
   p_DestroyShaderModule(dev, sm, NULL);
   return r;
}

static VkResult
create_compute_pipe_subgroup(VkDevice dev,
                             PFN_vkCreateShaderModule p_CreateShaderModule,
                             PFN_vkDestroyShaderModule p_DestroyShaderModule,
                             PFN_vkCreateComputePipelines p_CreateComputePipelines,
                             const uint32_t *spv,
                             size_t spv_bytes,
                             VkPipelineLayout layout,
                             VkPipelineShaderStageCreateFlags stage_flags,
                             const void *stage_pnext,
                             const VkSpecializationInfo *spec_info,
                             VkPipeline *pipeline)
{
   VkShaderModuleCreateInfo smci = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = spv_bytes,
      .pCode = spv,
   };
   VkShaderModule sm;
   VkResult r = p_CreateShaderModule(dev, &smci, NULL, &sm);
   if (r != VK_SUCCESS)
      return r;

   VkComputePipelineCreateInfo cpci = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .pNext = stage_pnext,
         .flags = stage_flags,
         .stage = VK_SHADER_STAGE_COMPUTE_BIT,
         .module = sm,
         .pName = "main",
         .pSpecializationInfo = spec_info,
      },
      .layout = layout,
   };
   r = p_CreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, pipeline);
   p_DestroyShaderModule(dev, sm, NULL);
   return r;
}

static VkResult
create_graphics_pipe(VkDevice dev,
                     PFN_vkCreateShaderModule p_CreateShaderModule,
                     PFN_vkDestroyShaderModule p_DestroyShaderModule,
                     PFN_vkCreateGraphicsPipelines p_CreateGraphicsPipelines,
                     const uint32_t *vert_spv,
                     size_t vert_bytes,
                     const uint32_t *frag_spv,
                     size_t frag_bytes,
                     VkRenderPass render_pass,
                     VkPipelineLayout layout,
                     uint32_t width,
                     uint32_t height,
                     VkPipeline *pipeline)
{
   VkShaderModuleCreateInfo v_smci = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = vert_bytes,
      .pCode = vert_spv,
   };
   VkShaderModule v_sm;
   VkResult r = p_CreateShaderModule(dev, &v_smci, NULL, &v_sm);
   if (r != VK_SUCCESS)
      return r;

   VkShaderModuleCreateInfo f_smci = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = frag_bytes,
      .pCode = frag_spv,
   };
   VkShaderModule f_sm;
   r = p_CreateShaderModule(dev, &f_smci, NULL, &f_sm);
   if (r != VK_SUCCESS) {
      p_DestroyShaderModule(dev, v_sm, NULL);
      return r;
   }

   VkPipelineShaderStageCreateInfo stages[2] = {
      {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = v_sm,
         .pName = "main",
      },
      {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = f_sm,
         .pName = "main",
      },
   };

   VkPipelineVertexInputStateCreateInfo vi = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
   };
   VkPipelineInputAssemblyStateCreateInfo ia = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
   };

   VkViewport vp = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
   VkRect2D sc = { {0, 0}, {width, height} };
   VkPipelineViewportStateCreateInfo vsci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .pViewports = &vp,
      .scissorCount = 1,
      .pScissors = &sc,
   };

   VkPipelineRasterizationStateCreateInfo rs = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_NONE,
      .frontFace = VK_FRONT_FACE_CLOCKWISE,
      .lineWidth = 1.0f,
   };

   VkPipelineMultisampleStateCreateInfo ms = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
   };

   VkPipelineColorBlendAttachmentState cba = {
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
      .blendEnable = VK_FALSE,
   };
   VkPipelineColorBlendStateCreateInfo cb = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &cba,
   };

   VkGraphicsPipelineCreateInfo gpci = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount = 2,
      .pStages = stages,
      .pVertexInputState = &vi,
      .pInputAssemblyState = &ia,
      .pViewportState = &vsci,
      .pRasterizationState = &rs,
      .pMultisampleState = &ms,
      .pColorBlendState = &cb,
      .layout = layout,
      .renderPass = render_pass,
      .subpass = 0,
   };

   r = p_CreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, pipeline);

   p_DestroyShaderModule(dev, f_sm, NULL);
   p_DestroyShaderModule(dev, v_sm, NULL);
   return r;
}

static VkResult
create_ssbo_dsl(VkDevice dev,
                PFN_vkCreateDescriptorSetLayout p_CreateDescriptorSetLayout,
                uint32_t count,
                VkDescriptorSetLayout *dsl)
{
   VkDescriptorSetLayoutBinding bindings[4];
   for (uint32_t i = 0; i < count; i++) {
      bindings[i].binding = i;
      bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bindings[i].descriptorCount = 1;
      bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
      bindings[i].pImmutableSamplers = NULL;
   }
   VkDescriptorSetLayoutCreateInfo dslci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = count,
      .pBindings = bindings,
   };
   return p_CreateDescriptorSetLayout(dev, &dslci, NULL, dsl);
}

static VkResult
create_ssbo_descriptors(VkDevice dev,
                        PFN_vkCreateDescriptorPool p_CreateDescriptorPool,
                        PFN_vkAllocateDescriptorSets p_AllocateDescriptorSets,
                        PFN_vkUpdateDescriptorSets p_UpdateDescriptorSets,
                        VkDescriptorSetLayout dsl,
                        uint32_t count,
                        const struct exec_buf *bufs,
                        VkDescriptorPool *pool,
                        VkDescriptorSet *set)
{
   VkDescriptorPoolSize ps = {
      .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount = count,
   };
   VkDescriptorPoolCreateInfo dpci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1,
      .poolSizeCount = 1,
      .pPoolSizes = &ps,
   };
   VkResult r = p_CreateDescriptorPool(dev, &dpci, NULL, pool);
   if (r != VK_SUCCESS)
      return r;

   VkDescriptorSetAllocateInfo dsai = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = *pool,
      .descriptorSetCount = 1,
      .pSetLayouts = &dsl,
   };
   r = p_AllocateDescriptorSets(dev, &dsai, set);
   if (r != VK_SUCCESS)
      return r;

   VkDescriptorBufferInfo bis[4];
   VkWriteDescriptorSet wds[4];
   for (uint32_t i = 0; i < count; i++) {
      bis[i].buffer = bufs[i].buffer;
      bis[i].offset = 0;
      bis[i].range = VK_WHOLE_SIZE;

      wds[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      wds[i].pNext = NULL;
      wds[i].dstSet = *set;
      wds[i].dstBinding = i;
      wds[i].dstArrayElement = 0;
      wds[i].descriptorCount = 1;
      wds[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      wds[i].pImageInfo = NULL;
      wds[i].pBufferInfo = &bis[i];
      wds[i].pTexelBufferView = NULL;
   }
   p_UpdateDescriptorSets(dev, count, wds, 0, NULL);
   return VK_SUCCESS;
}

static VkResult
create_simple_pipeline_layout(VkDevice dev,
                              PFN_vkCreatePipelineLayout p_CreatePipelineLayout,
                              VkDescriptorSetLayout dsl,
                              VkPipelineLayout *playout)
{
   VkPipelineLayoutCreateInfo plci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = dsl ? 1 : 0,
      .pSetLayouts = dsl ? &dsl : NULL,
   };
   return p_CreatePipelineLayout(dev, &plci, NULL, playout);
}

static int
check_wait_idle(VkQueue q, PFN_vkQueueWaitIdle p_QueueWaitIdle, const char *case_name)
{
   VkResult r = p_QueueWaitIdle(q);
   if (r == VK_ERROR_DEVICE_LOST) {
      printf("FAIL case %s DEVICE_LOST\n", case_name);
      printf("RESULT FAIL\n");
      exit(1);
   }
   if (r != VK_SUCCESS) {
      printf("FAIL case %s QueueWaitIdle r=%d\n", case_name, (int)r);
      return 0;
   }
   return 1;
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
      return 1;
   }

   PFN_vkCreateInstance CreateInstance =
      (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
   if (!CreateInstance) {
      printf("FAIL missing vkCreateInstance\n");
      return 1;
   }

   const char *iexts[] = {"VK_KHR_get_physical_device_properties2"};
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

   PFN_vkGetPhysicalDeviceProperties2 p_GetPhysicalDeviceProperties2 =
      (PFN_vkGetPhysicalDeviceProperties2)gipa(inst, "vkGetPhysicalDeviceProperties2");
   if (!p_GetPhysicalDeviceProperties2)
      p_GetPhysicalDeviceProperties2 =
         (PFN_vkGetPhysicalDeviceProperties2)gipa(inst, "vkGetPhysicalDeviceProperties2KHR");

   uint32_t nd = 0;
   if (p_EnumeratePhysicalDevices(inst, &nd, NULL) != VK_SUCCESS || nd == 0) {
      printf("FAIL EnumeratePhysicalDevices count=0\n");
      return 1;
   }
   VkPhysicalDevice *devs = calloc(nd, sizeof(*devs));
   if (!devs)
      return 1;
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
      return 1;
   }

   /* Enumerate device extensions */
   uint32_t ext_count = 0;
   p_EnumerateDeviceExtensionProperties(phys, NULL, &ext_count, NULL);
   VkExtensionProperties *exts = calloc(ext_count, sizeof(*exts));
   if (ext_count > 0 && !exts)
      return 1;
   p_EnumerateDeviceExtensionProperties(phys, NULL, &ext_count, exts);

   int has_demote_ext = has_extension(exts, ext_count, "VK_EXT_shader_demote_to_helper_invocation");
   int has_subgroup_size_ext = has_extension(exts, ext_count, "VK_EXT_subgroup_size_control");
   int has_zero_init_ext = has_extension(exts, ext_count, "VK_KHR_zero_initialize_workgroup_memory");
   free(exts);

   VkPhysicalDeviceProperties dev_props;
   p_GetPhysicalDeviceProperties(phys, &dev_props);

   int can_chain_demote = (dev_props.apiVersion >= VK_API_VERSION_1_3) || has_demote_ext;
   int can_chain_subgroup_size = (dev_props.apiVersion >= VK_API_VERSION_1_3) || has_subgroup_size_ext;
   int can_chain_zero_init = (dev_props.apiVersion >= VK_API_VERSION_1_3) || has_zero_init_ext;

   /* Query Features:
    * VkPhysicalDeviceFeatures2 (shaderInt16, shaderInt64, fragmentStoresAndAtomics)
    * -> VkPhysicalDeviceVulkan11Features (storageBuffer16BitAccess)
    * -> VkPhysicalDeviceVulkan12Features (shaderInt8, storageBuffer8BitAccess)
    * -> plus promoted structs chained only if dev apiVersion >= 1.3 or extension listed.
    */
   VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures supported_demote = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES,
   };
   VkPhysicalDeviceSubgroupSizeControlFeatures supported_subgroup_size = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES,
   };
   VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeatures supported_zero_init = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ZERO_INITIALIZE_WORKGROUP_MEMORY_FEATURES,
   };
   VkPhysicalDeviceVulkan12Features supported_f12 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
   };
   VkPhysicalDeviceVulkan11Features supported_f11 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
      .pNext = &supported_f12,
   };
   VkPhysicalDeviceFeatures2 supported_f2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &supported_f11,
   };

   void **feat_tail = &supported_f12.pNext;
   if (can_chain_demote) {
      *feat_tail = &supported_demote;
      feat_tail = &supported_demote.pNext;
   }
   if (can_chain_subgroup_size) {
      *feat_tail = &supported_subgroup_size;
      feat_tail = &supported_subgroup_size.pNext;
   }
   if (can_chain_zero_init) {
      *feat_tail = &supported_zero_init;
      feat_tail = &supported_zero_init.pNext;
   }
   *feat_tail = NULL;

   p_GetPhysicalDeviceFeatures2(phys, &supported_f2);

   /* Query Properties:
    * VkPhysicalDeviceProperties2 -> VkPhysicalDeviceSubgroupProperties -> VkPhysicalDeviceSubgroupSizeControlProperties
    */
   VkPhysicalDeviceSubgroupSizeControlProperties subgroup_size_props = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES,
   };
   VkPhysicalDeviceSubgroupProperties subgroup_props = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES,
   };
   VkPhysicalDeviceProperties2 props2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
      .pNext = &subgroup_props,
   };
   if (can_chain_subgroup_size) {
      subgroup_props.pNext = &subgroup_size_props;
   }
   if (p_GetPhysicalDeviceProperties2) {
      p_GetPhysicalDeviceProperties2(phys, &props2);
   }

   /* Print one INFO line with the feature bits and subgroup min/max sizes */
   printf("INFO features: int8=%d int16=%d int64=%d storage8=%d storage16=%d demote=%d subgroup_size_control=%d compute_full_subgroups=%d zero_init_wg=%d frag_stores=%d subgroupSize=%u minSubgroupSize=%u maxSubgroupSize=%u\n",
          (int)supported_f12.shaderInt8,
          (int)supported_f2.features.shaderInt16,
          (int)supported_f2.features.shaderInt64,
          (int)supported_f12.storageBuffer8BitAccess,
          (int)supported_f11.storageBuffer16BitAccess,
          (int)(can_chain_demote ? supported_demote.shaderDemoteToHelperInvocation : 0),
          (int)(can_chain_subgroup_size ? supported_subgroup_size.subgroupSizeControl : 0),
          (int)(can_chain_subgroup_size ? supported_subgroup_size.computeFullSubgroups : 0),
          (int)(can_chain_zero_init ? supported_zero_init.shaderZeroInitializeWorkgroupMemory : 0),
          (int)supported_f2.features.fragmentStoresAndAtomics,
          subgroup_props.subgroupSize,
          can_chain_subgroup_size ? subgroup_size_props.minSubgroupSize : 0,
          can_chain_subgroup_size ? subgroup_size_props.maxSubgroupSize : 0);

   /* Find Queue Family: prefer one that supports both compute and graphics */
   uint32_t qn = 0;
   p_GetPhysicalDeviceQueueFamilyProperties(phys, &qn, NULL);
   VkQueueFamilyProperties *qp = calloc(qn, sizeof(*qp));
   if (qn > 0 && !qp)
      return 1;
   p_GetPhysicalDeviceQueueFamilyProperties(phys, &qn, qp);
   uint32_t qi = ~0u;
   for (uint32_t i = 0; i < qn; i++) {
      if ((qp[i].queueFlags & (VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT)) ==
          (VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT)) {
         qi = i;
         break;
      }
   }
   if (qi == ~0u) {
      for (uint32_t i = 0; i < qn; i++) {
         if (qp[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            qi = i;
            break;
         }
      }
   }
   if (qi == ~0u) {
      for (uint32_t i = 0; i < qn; i++) {
         if (qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            qi = i;
            break;
         }
      }
   }
   free(qp);

   if (qi == ~0u) {
      printf("FAIL no compute or graphics queue\n");
      return 1;
   }

   VkPhysicalDeviceMemoryProperties mp;
   p_GetPhysicalDeviceMemoryProperties(phys, &mp);

   /* Device Creation: enable only supported features and extensions */
   const char *dev_exts[4];
   uint32_t dev_ext_count = 0;
   if (dev_props.apiVersion < VK_API_VERSION_1_3) {
      if (has_demote_ext && can_chain_demote && supported_demote.shaderDemoteToHelperInvocation)
         dev_exts[dev_ext_count++] = "VK_EXT_shader_demote_to_helper_invocation";
      if (has_subgroup_size_ext && can_chain_subgroup_size && (supported_subgroup_size.subgroupSizeControl || supported_subgroup_size.computeFullSubgroups))
         dev_exts[dev_ext_count++] = "VK_EXT_subgroup_size_control";
      if (has_zero_init_ext && can_chain_zero_init && supported_zero_init.shaderZeroInitializeWorkgroupMemory)
         dev_exts[dev_ext_count++] = "VK_KHR_zero_initialize_workgroup_memory";
   }

   VkPhysicalDeviceFeatures2 dev_feat2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
   };
   dev_feat2.features.shaderInt16 = supported_f2.features.shaderInt16;
   dev_feat2.features.shaderInt64 = supported_f2.features.shaderInt64;
   dev_feat2.features.fragmentStoresAndAtomics = supported_f2.features.fragmentStoresAndAtomics;

   VkPhysicalDeviceVulkan11Features dev_f11 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
      .storageBuffer16BitAccess = supported_f11.storageBuffer16BitAccess,
   };
   VkPhysicalDeviceVulkan12Features dev_f12 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .shaderInt8 = supported_f12.shaderInt8,
      .storageBuffer8BitAccess = supported_f12.storageBuffer8BitAccess,
   };
   dev_feat2.pNext = &dev_f11;
   dev_f11.pNext = &dev_f12;

   void **dev_tail = &dev_f12.pNext;
   VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures dev_demote = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES,
   };
   if (can_chain_demote && supported_demote.shaderDemoteToHelperInvocation) {
      dev_demote.shaderDemoteToHelperInvocation = VK_TRUE;
      *dev_tail = &dev_demote;
      dev_tail = &dev_demote.pNext;
   }
   VkPhysicalDeviceSubgroupSizeControlFeatures dev_subgroup_size = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES,
   };
   if (can_chain_subgroup_size && (supported_subgroup_size.subgroupSizeControl || supported_subgroup_size.computeFullSubgroups)) {
      dev_subgroup_size.subgroupSizeControl = supported_subgroup_size.subgroupSizeControl;
      dev_subgroup_size.computeFullSubgroups = supported_subgroup_size.computeFullSubgroups;
      *dev_tail = &dev_subgroup_size;
      dev_tail = &dev_subgroup_size.pNext;
   }
   VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeatures dev_zero_init = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ZERO_INITIALIZE_WORKGROUP_MEMORY_FEATURES,
   };
   if (can_chain_zero_init && supported_zero_init.shaderZeroInitializeWorkgroupMemory) {
      dev_zero_init.shaderZeroInitializeWorkgroupMemory = VK_TRUE;
      *dev_tail = &dev_zero_init;
      dev_tail = &dev_zero_init.pNext;
   }
   *dev_tail = NULL;

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
      return 1;
   }

#define GD(n)                                                                  \
   PFN_vk##n p_##n = (PFN_vk##n)p_GetDeviceProcAddr(dev, "vk" #n);            \
   if (!p_##n)                                                                 \
      p_##n = (PFN_vk##n)gipa(inst, "vk" #n);                                  \
   if (!p_##n) {                                                               \
      printf("FAIL missing device vk" #n "\n");                                \
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
   GD(CreateShaderModule)
   GD(DestroyShaderModule)
   GD(CreateDescriptorSetLayout)
   GD(DestroyDescriptorSetLayout)
   GD(CreatePipelineLayout)
   GD(DestroyPipelineLayout)
   GD(CreateComputePipelines)
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
   GD(CmdBindPipeline)
   GD(CmdBindDescriptorSets)
   GD(CmdDispatch)
   GD(CmdPipelineBarrier)
   GD(QueueSubmit)
   GD(QueueWaitIdle)

   /* Graphics functions for case 6 demote */
   GD(CreateRenderPass)
   GD(DestroyRenderPass)
   GD(CreateImage)
   GD(DestroyImage)
   GD(GetImageMemoryRequirements)
   GD(BindImageMemory)
   GD(CreateImageView)
   GD(DestroyImageView)
   GD(CreateFramebuffer)
   GD(DestroyFramebuffer)
   GD(CreateGraphicsPipelines)
   GD(CmdBeginRenderPass)
   GD(CmdEndRenderPass)
   GD(CmdDraw)
   GD(CmdCopyImageToBuffer)
#undef GD

   PFN_vkGetBufferDeviceAddress g_bda =
      (PFN_vkGetBufferDeviceAddress)p_GetDeviceProcAddr(dev, "vkGetBufferDeviceAddress");
   if (!g_bda)
      g_bda = (PFN_vkGetBufferDeviceAddress)gipa(inst, "vkGetBufferDeviceAddress");

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
      return 1;
   }

   int passes = 0;
   int fails = 0;
   int skips = 0;

   /* ──────────────────────────────────────────────────────────────────────────
    * Case 1: int8
    * ────────────────────────────────────────────────────────────────────────── */
   if (!supported_f12.shaderInt8) {
      printf("SKIP case int8 shaderInt8 missing\n");
      skips++;
   } else {
      struct exec_buf buf = {0};
      VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
      VkPipelineLayout playout = VK_NULL_HANDLE;
      VkDescriptorPool dpool = VK_NULL_HANDLE;
      VkDescriptorSet dset = VK_NULL_HANDLE;
      VkPipeline pipe = VK_NULL_HANDLE;

      if (create_buffer(dev, &mp, 64 * 4 * sizeof(uint32_t),
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        g_bda, &buf)) {
         printf("FAIL case int8 create_buffer\n");
         fails++;
         goto int8_cleanup;
      }
      uint32_t *map = (uint32_t *)buf.map;
      for (uint32_t i = 0; i < 64 * 4; i++)
         map[i] = 0xDEADBEEFu;

      if (create_ssbo_dsl(dev, p_CreateDescriptorSetLayout, 1, &dsl) != VK_SUCCESS) {
         printf("FAIL case int8 CreateDescriptorSetLayout\n");
         fails++;
         goto int8_cleanup;
      }
      if (create_simple_pipeline_layout(dev, p_CreatePipelineLayout, dsl, &playout) != VK_SUCCESS) {
         printf("FAIL case int8 CreatePipelineLayout\n");
         fails++;
         goto int8_cleanup;
      }
      if (create_ssbo_descriptors(dev, p_CreateDescriptorPool, p_AllocateDescriptorSets,
                                  p_UpdateDescriptorSets, dsl, 1, &buf, &dpool, &dset) != VK_SUCCESS) {
         printf("FAIL case int8 allocate descriptors\n");
         fails++;
         goto int8_cleanup;
      }
      VkResult vr = create_compute_pipe(dev, p_CreateShaderModule, p_DestroyShaderModule,
                                        p_CreateComputePipelines, int8_spv, sizeof(int8_spv),
                                        playout, &pipe);
      if (vr != VK_SUCCESS) {
         printf("FAIL case int8 CreateComputePipelines r=%d\n", (int)vr);
         fails++;
         goto int8_cleanup;
      }

      VkCommandBufferBeginInfo bi = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      };
      p_BeginCommandBuffer(cmd, &bi);
      p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
      p_CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &dset, 0, NULL);
      p_CmdDispatch(cmd, 1, 1, 1);
      VkMemoryBarrier host_mb = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
      };
      p_CmdPipelineBarrier(cmd,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_HOST_BIT,
                           0, 1, &host_mb, 0, NULL, 0, NULL);
      p_EndCommandBuffer(cmd);

      VkSubmitInfo si = {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .commandBufferCount = 1,
         .pCommandBuffers = &cmd,
      };
      vr = p_QueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
      if (vr != VK_SUCCESS) {
         printf("FAIL case int8 QueueSubmit r=%d\n", (int)vr);
         fails++;
         goto int8_cleanup;
      }
      if (!check_wait_idle(queue, p_QueueWaitIdle, "int8")) {
         fails++;
         goto int8_cleanup;
      }

      int match = 1;
      for (uint32_t i = 0; i < 64; i++) {
         int8_t a = (int8_t)((int32_t)i * 37 - 100);
         int8_t b = (int8_t)((int32_t)i * 11 + 3);
         int8_t r1 = (int8_t)((uint32_t)(uint8_t)a * (uint32_t)(uint8_t)b);
         int8_t r2 = (int8_t)((uint8_t)a + (uint8_t)b);
         int8_t r3 = (int8_t)((int32_t)a >> 2);
         uint8_t u = (uint8_t)((uint32_t)(uint8_t)a * (uint32_t)(uint8_t)7);
         uint8_t u2 = (uint8_t)(u / (uint8_t)3);

         uint32_t exp[4];
         exp[0] = (uint32_t)(int32_t)r1;
         exp[1] = (uint32_t)(int32_t)r2;
         exp[2] = (uint32_t)(int32_t)r3;
         exp[3] = (uint32_t)u2;

         for (uint32_t c = 0; c < 4; c++) {
            uint32_t idx = i * 4u + c;
            if (map[idx] != exp[c]) {
               printf("FAIL case int8 mismatch at [%u]: got 0x%08x exp 0x%08x\n",
                      idx, map[idx], exp[c]);
               fails++;
               match = 0;
               break;
            }
         }
         if (!match)
            break;
      }
      if (match) {
         printf("PASS case int8\n");
         passes++;
      }

int8_cleanup:
      if (pipe)
         p_DestroyPipeline(dev, pipe, NULL);
      if (dpool)
         p_DestroyDescriptorPool(dev, dpool, NULL);
      if (playout)
         p_DestroyPipelineLayout(dev, playout, NULL);
      if (dsl)
         p_DestroyDescriptorSetLayout(dev, dsl, NULL);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &buf);
   }

   /* ──────────────────────────────────────────────────────────────────────────
    * Case 2: int16
    * ────────────────────────────────────────────────────────────────────────── */
   if (!supported_f2.features.shaderInt16) {
      printf("SKIP case int16 shaderInt16 missing\n");
      skips++;
   } else {
      struct exec_buf buf = {0};
      VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
      VkPipelineLayout playout = VK_NULL_HANDLE;
      VkDescriptorPool dpool = VK_NULL_HANDLE;
      VkDescriptorSet dset = VK_NULL_HANDLE;
      VkPipeline pipe = VK_NULL_HANDLE;

      if (create_buffer(dev, &mp, 64 * 4 * sizeof(uint32_t),
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        g_bda, &buf)) {
         printf("FAIL case int16 create_buffer\n");
         fails++;
         goto int16_cleanup;
      }
      uint32_t *map = (uint32_t *)buf.map;
      for (uint32_t i = 0; i < 64 * 4; i++)
         map[i] = 0xDEADBEEFu;

      if (create_ssbo_dsl(dev, p_CreateDescriptorSetLayout, 1, &dsl) != VK_SUCCESS) {
         printf("FAIL case int16 CreateDescriptorSetLayout\n");
         fails++;
         goto int16_cleanup;
      }
      if (create_simple_pipeline_layout(dev, p_CreatePipelineLayout, dsl, &playout) != VK_SUCCESS) {
         printf("FAIL case int16 CreatePipelineLayout\n");
         fails++;
         goto int16_cleanup;
      }
      if (create_ssbo_descriptors(dev, p_CreateDescriptorPool, p_AllocateDescriptorSets,
                                  p_UpdateDescriptorSets, dsl, 1, &buf, &dpool, &dset) != VK_SUCCESS) {
         printf("FAIL case int16 allocate descriptors\n");
         fails++;
         goto int16_cleanup;
      }
      VkResult vr = create_compute_pipe(dev, p_CreateShaderModule, p_DestroyShaderModule,
                                        p_CreateComputePipelines, int16_spv, sizeof(int16_spv),
                                        playout, &pipe);
      if (vr != VK_SUCCESS) {
         printf("FAIL case int16 CreateComputePipelines r=%d\n", (int)vr);
         fails++;
         goto int16_cleanup;
      }

      VkCommandBufferBeginInfo bi = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      };
      p_BeginCommandBuffer(cmd, &bi);
      p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
      p_CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &dset, 0, NULL);
      p_CmdDispatch(cmd, 1, 1, 1);
      VkMemoryBarrier host_mb = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
      };
      p_CmdPipelineBarrier(cmd,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_HOST_BIT,
                           0, 1, &host_mb, 0, NULL, 0, NULL);
      p_EndCommandBuffer(cmd);

      VkSubmitInfo si = {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .commandBufferCount = 1,
         .pCommandBuffers = &cmd,
      };
      vr = p_QueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
      if (vr != VK_SUCCESS) {
         printf("FAIL case int16 QueueSubmit r=%d\n", (int)vr);
         fails++;
         goto int16_cleanup;
      }
      if (!check_wait_idle(queue, p_QueueWaitIdle, "int16")) {
         fails++;
         goto int16_cleanup;
      }

      int match = 1;
      for (uint32_t i = 0; i < 64; i++) {
         int16_t a = (int16_t)((int32_t)i * 1237 - 30000);
         int16_t b = (int16_t)((int32_t)i * 911 + 17);
         int16_t r1 = (int16_t)((uint32_t)(uint16_t)a * (uint32_t)(uint16_t)b);
         int16_t r2 = (int16_t)((uint16_t)a - (uint16_t)b);
         int16_t r3 = (int16_t)((int32_t)a >> 3);
         uint16_t r4 = (uint16_t)((uint32_t)(uint16_t)a * (uint32_t)(uint16_t)0x9E37);

         uint32_t exp[4];
         exp[0] = (uint32_t)(int32_t)r1;
         exp[1] = (uint32_t)(int32_t)r2;
         exp[2] = (uint32_t)(int32_t)r3;
         exp[3] = (uint32_t)r4;

         for (uint32_t c = 0; c < 4; c++) {
            uint32_t idx = i * 4u + c;
            if (map[idx] != exp[c]) {
               printf("FAIL case int16 mismatch at [%u]: got 0x%08x exp 0x%08x\n",
                      idx, map[idx], exp[c]);
               fails++;
               match = 0;
               break;
            }
         }
         if (!match)
            break;
      }
      if (match) {
         printf("PASS case int16\n");
         passes++;
      }

int16_cleanup:
      if (pipe)
         p_DestroyPipeline(dev, pipe, NULL);
      if (dpool)
         p_DestroyDescriptorPool(dev, dpool, NULL);
      if (playout)
         p_DestroyPipelineLayout(dev, playout, NULL);
      if (dsl)
         p_DestroyDescriptorSetLayout(dev, dsl, NULL);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &buf);
   }

   /* ──────────────────────────────────────────────────────────────────────────
    * Case 3: int64
    * ────────────────────────────────────────────────────────────────────────── */
   if (!supported_f2.features.shaderInt64) {
      printf("SKIP case int64 shaderInt64 missing\n");
      skips++;
   } else {
      struct exec_buf buf = {0};
      VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
      VkPipelineLayout playout = VK_NULL_HANDLE;
      VkDescriptorPool dpool = VK_NULL_HANDLE;
      VkDescriptorSet dset = VK_NULL_HANDLE;
      VkPipeline pipe = VK_NULL_HANDLE;

      if (create_buffer(dev, &mp, 64 * 8 * sizeof(uint64_t),
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        g_bda, &buf)) {
         printf("FAIL case int64 create_buffer\n");
         fails++;
         goto int64_cleanup;
      }
      uint64_t *map = (uint64_t *)buf.map;
      for (uint32_t i = 0; i < 64 * 8; i++)
         map[i] = 0xDEADBEEFDEADBEEFull;

      if (create_ssbo_dsl(dev, p_CreateDescriptorSetLayout, 1, &dsl) != VK_SUCCESS) {
         printf("FAIL case int64 CreateDescriptorSetLayout\n");
         fails++;
         goto int64_cleanup;
      }
      if (create_simple_pipeline_layout(dev, p_CreatePipelineLayout, dsl, &playout) != VK_SUCCESS) {
         printf("FAIL case int64 CreatePipelineLayout\n");
         fails++;
         goto int64_cleanup;
      }
      if (create_ssbo_descriptors(dev, p_CreateDescriptorPool, p_AllocateDescriptorSets,
                                  p_UpdateDescriptorSets, dsl, 1, &buf, &dpool, &dset) != VK_SUCCESS) {
         printf("FAIL case int64 allocate descriptors\n");
         fails++;
         goto int64_cleanup;
      }
      VkResult vr = create_compute_pipe(dev, p_CreateShaderModule, p_DestroyShaderModule,
                                        p_CreateComputePipelines, int64_spv, sizeof(int64_spv),
                                        playout, &pipe);
      if (vr != VK_SUCCESS) {
         printf("FAIL case int64 CreateComputePipelines r=%d\n", (int)vr);
         fails++;
         goto int64_cleanup;
      }

      VkCommandBufferBeginInfo bi = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      };
      p_BeginCommandBuffer(cmd, &bi);
      p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
      p_CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &dset, 0, NULL);
      p_CmdDispatch(cmd, 1, 1, 1);
      VkMemoryBarrier host_mb = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
      };
      p_CmdPipelineBarrier(cmd,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_HOST_BIT,
                           0, 1, &host_mb, 0, NULL, 0, NULL);
      p_EndCommandBuffer(cmd);

      VkSubmitInfo si = {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .commandBufferCount = 1,
         .pCommandBuffers = &cmd,
      };
      vr = p_QueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
      if (vr != VK_SUCCESS) {
         printf("FAIL case int64 QueueSubmit r=%d\n", (int)vr);
         fails++;
         goto int64_cleanup;
      }
      if (!check_wait_idle(queue, p_QueueWaitIdle, "int64")) {
         fails++;
         goto int64_cleanup;
      }

      int match = 1;
      for (uint32_t i = 0; i < 64; i++) {
         uint64_t x = (uint64_t)i * 0x9E3779B97F4A7C15ull + 0x0123456789ABCDEFull;
         uint64_t exp[8];
         exp[0] = x * x;
         exp[1] = x >> 17;
         exp[2] = x << 23;
         exp[3] = x ^ (x >> 31);
         exp[4] = (uint64_t)((int64_t)x >> 40);
         exp[5] = ((int64_t)x < 0) ? 1ull : 0ull;
         exp[6] = x / 7ull;
         exp[7] = x % 1000ull;

         uint32_t base = i * 8u;
         for (uint32_t c = 0; c < 8; c++) {
            uint32_t idx = base + c;
            if (map[idx] != exp[c]) {
               printf("FAIL case int64 mismatch at [%u]: got 0x%016" PRIx64 " exp 0x%016" PRIx64 "\n",
                      idx, map[idx], exp[c]);
               fails++;
               match = 0;
               break;
            }
         }
         if (!match)
            break;
      }
      if (match) {
         printf("PASS case int64\n");
         passes++;
      }

int64_cleanup:
      if (pipe)
         p_DestroyPipeline(dev, pipe, NULL);
      if (dpool)
         p_DestroyDescriptorPool(dev, dpool, NULL);
      if (playout)
         p_DestroyPipelineLayout(dev, playout, NULL);
      if (dsl)
         p_DestroyDescriptorSetLayout(dev, dsl, NULL);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &buf);
   }

   /* ──────────────────────────────────────────────────────────────────────────
    * Case 4: storage8
    * ────────────────────────────────────────────────────────────────────────── */
   if (!supported_f12.storageBuffer8BitAccess) {
      printf("SKIP case storage8 storageBuffer8BitAccess missing\n");
      skips++;
   } else {
      struct exec_buf in_buf = {0}, out8_buf = {0}, out32_buf = {0};
      VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
      VkPipelineLayout playout = VK_NULL_HANDLE;
      VkDescriptorPool dpool = VK_NULL_HANDLE;
      VkDescriptorSet dset = VK_NULL_HANDLE;
      VkPipeline pipe = VK_NULL_HANDLE;

      if (create_buffer(dev, &mp, 256,
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        g_bda, &in_buf) ||
          create_buffer(dev, &mp, 256 + 16,
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        g_bda, &out8_buf) ||
          create_buffer(dev, &mp, 256 * sizeof(uint32_t),
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        g_bda, &out32_buf)) {
         printf("FAIL case storage8 create_buffer\n");
         fails++;
         goto storage8_cleanup;
      }

      uint8_t *in_p = (uint8_t *)in_buf.map;
      for (uint32_t j = 0; j < 256; j++)
         in_p[j] = (uint8_t)(j * 13u + 5u);

      uint8_t *out8_p = (uint8_t *)out8_buf.map;
      memset(out8_p, 0xEF, 256 + 16);

      uint32_t *out32_p = (uint32_t *)out32_buf.map;
      for (uint32_t j = 0; j < 256; j++) {
         uint32_t exp32 = (uint8_t)(j * 13u + 5u);
         uint8_t exp8 = (uint8_t)(exp32 * 3u + 1u);
         out8_p[j] = (uint8_t)~exp8;
         out32_p[j] = ~exp32;
      }

      if (create_ssbo_dsl(dev, p_CreateDescriptorSetLayout, 3, &dsl) != VK_SUCCESS) {
         printf("FAIL case storage8 CreateDescriptorSetLayout\n");
         fails++;
         goto storage8_cleanup;
      }
      if (create_simple_pipeline_layout(dev, p_CreatePipelineLayout, dsl, &playout) != VK_SUCCESS) {
         printf("FAIL case storage8 CreatePipelineLayout\n");
         fails++;
         goto storage8_cleanup;
      }
      struct exec_buf bufs[3] = { in_buf, out8_buf, out32_buf };
      if (create_ssbo_descriptors(dev, p_CreateDescriptorPool, p_AllocateDescriptorSets,
                                  p_UpdateDescriptorSets, dsl, 3, bufs, &dpool, &dset) != VK_SUCCESS) {
         printf("FAIL case storage8 allocate descriptors\n");
         fails++;
         goto storage8_cleanup;
      }
      VkResult vr = create_compute_pipe(dev, p_CreateShaderModule, p_DestroyShaderModule,
                                        p_CreateComputePipelines, storage8_spv, sizeof(storage8_spv),
                                        playout, &pipe);
      if (vr != VK_SUCCESS) {
         printf("FAIL case storage8 CreateComputePipelines r=%d\n", (int)vr);
         fails++;
         goto storage8_cleanup;
      }

      VkCommandBufferBeginInfo bi = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      };
      p_BeginCommandBuffer(cmd, &bi);
      p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
      p_CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &dset, 0, NULL);
      p_CmdDispatch(cmd, 4, 1, 1);
      VkMemoryBarrier host_mb = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
      };
      p_CmdPipelineBarrier(cmd,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_HOST_BIT,
                           0, 1, &host_mb, 0, NULL, 0, NULL);
      p_EndCommandBuffer(cmd);

      VkSubmitInfo si = {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .commandBufferCount = 1,
         .pCommandBuffers = &cmd,
      };
      vr = p_QueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
      if (vr != VK_SUCCESS) {
         printf("FAIL case storage8 QueueSubmit r=%d\n", (int)vr);
         fails++;
         goto storage8_cleanup;
      }
      if (!check_wait_idle(queue, p_QueueWaitIdle, "storage8")) {
         fails++;
         goto storage8_cleanup;
      }

      int match = 1;
      for (uint32_t j = 0; j < 256; j++) {
         uint32_t exp32 = (uint8_t)(j * 13u + 5u);
         uint8_t exp8 = (uint8_t)(exp32 * 3u + 1u);
         if (out8_p[j] != exp8) {
            printf("FAIL case storage8 mismatch at [out8 %u]: got 0x%02x exp 0x%02x\n",
                   j, out8_p[j], exp8);
            fails++;
            match = 0;
            break;
         }
         if (out32_p[j] != exp32) {
            printf("FAIL case storage8 mismatch at [out32 %u]: got 0x%08x exp 0x%08x\n",
                   j, out32_p[j], exp32);
            fails++;
            match = 0;
            break;
         }
      }
      if (match) {
         for (uint32_t k = 0; k < 16; k++) {
            uint32_t idx = 256 + k;
            if (out8_p[idx] != 0xEF) {
               printf("FAIL case storage8 guard byte corrupted at [%u]: got 0x%02x exp 0xef\n",
                      idx, out8_p[idx]);
               fails++;
               match = 0;
               break;
            }
         }
      }
      if (match) {
         printf("PASS case storage8\n");
         passes++;
      }

storage8_cleanup:
      if (pipe)
         p_DestroyPipeline(dev, pipe, NULL);
      if (dpool)
         p_DestroyDescriptorPool(dev, dpool, NULL);
      if (playout)
         p_DestroyPipelineLayout(dev, playout, NULL);
      if (dsl)
         p_DestroyDescriptorSetLayout(dev, dsl, NULL);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &in_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &out8_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &out32_buf);
   }

   /* ──────────────────────────────────────────────────────────────────────────
    * Case 5: storage16
    * ────────────────────────────────────────────────────────────────────────── */
   if (!supported_f11.storageBuffer16BitAccess) {
      printf("SKIP case storage16 storageBuffer16BitAccess missing\n");
      skips++;
   } else {
      struct exec_buf in_buf = {0}, out16_buf = {0}, out32_buf = {0};
      VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
      VkPipelineLayout playout = VK_NULL_HANDLE;
      VkDescriptorPool dpool = VK_NULL_HANDLE;
      VkDescriptorSet dset = VK_NULL_HANDLE;
      VkPipeline pipe = VK_NULL_HANDLE;

      if (create_buffer(dev, &mp, 128 * sizeof(uint16_t),
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        g_bda, &in_buf) ||
          create_buffer(dev, &mp, 128 * sizeof(uint16_t) + 16,
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        g_bda, &out16_buf) ||
          create_buffer(dev, &mp, 128 * sizeof(uint32_t),
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        g_bda, &out32_buf)) {
         printf("FAIL case storage16 create_buffer\n");
         fails++;
         goto storage16_cleanup;
      }

      uint16_t *in_p = (uint16_t *)in_buf.map;
      for (uint32_t j = 0; j < 128; j++)
         in_p[j] = (uint16_t)(j * 4099u + 11u);

      uint8_t *out16_bytes = (uint8_t *)out16_buf.map;
      memset(out16_bytes, 0xEF, 128 * sizeof(uint16_t) + 16);
      uint16_t *out16_p = (uint16_t *)out16_buf.map;

      uint32_t *out32_p = (uint32_t *)out32_buf.map;
      for (uint32_t j = 0; j < 128; j++) {
         uint32_t exp32 = (uint16_t)(j * 4099u + 11u);
         uint16_t exp16 = (uint16_t)(exp32 * 5u + 3u);
         out16_p[j] = (uint16_t)~exp16;
         out32_p[j] = ~exp32;
      }

      if (create_ssbo_dsl(dev, p_CreateDescriptorSetLayout, 3, &dsl) != VK_SUCCESS) {
         printf("FAIL case storage16 CreateDescriptorSetLayout\n");
         fails++;
         goto storage16_cleanup;
      }
      if (create_simple_pipeline_layout(dev, p_CreatePipelineLayout, dsl, &playout) != VK_SUCCESS) {
         printf("FAIL case storage16 CreatePipelineLayout\n");
         fails++;
         goto storage16_cleanup;
      }
      struct exec_buf bufs[3] = { in_buf, out16_buf, out32_buf };
      if (create_ssbo_descriptors(dev, p_CreateDescriptorPool, p_AllocateDescriptorSets,
                                  p_UpdateDescriptorSets, dsl, 3, bufs, &dpool, &dset) != VK_SUCCESS) {
         printf("FAIL case storage16 allocate descriptors\n");
         fails++;
         goto storage16_cleanup;
      }
      VkResult vr = create_compute_pipe(dev, p_CreateShaderModule, p_DestroyShaderModule,
                                        p_CreateComputePipelines, storage16_spv, sizeof(storage16_spv),
                                        playout, &pipe);
      if (vr != VK_SUCCESS) {
         printf("FAIL case storage16 CreateComputePipelines r=%d\n", (int)vr);
         fails++;
         goto storage16_cleanup;
      }

      VkCommandBufferBeginInfo bi = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      };
      p_BeginCommandBuffer(cmd, &bi);
      p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
      p_CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &dset, 0, NULL);
      p_CmdDispatch(cmd, 2, 1, 1);
      VkMemoryBarrier host_mb = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
      };
      p_CmdPipelineBarrier(cmd,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_HOST_BIT,
                           0, 1, &host_mb, 0, NULL, 0, NULL);
      p_EndCommandBuffer(cmd);

      VkSubmitInfo si = {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .commandBufferCount = 1,
         .pCommandBuffers = &cmd,
      };
      vr = p_QueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
      if (vr != VK_SUCCESS) {
         printf("FAIL case storage16 QueueSubmit r=%d\n", (int)vr);
         fails++;
         goto storage16_cleanup;
      }
      if (!check_wait_idle(queue, p_QueueWaitIdle, "storage16")) {
         fails++;
         goto storage16_cleanup;
      }

      int match = 1;
      for (uint32_t j = 0; j < 128; j++) {
         uint32_t exp32 = (uint16_t)(j * 4099u + 11u);
         uint16_t exp16 = (uint16_t)(exp32 * 5u + 3u);
         if (out16_p[j] != exp16) {
            printf("FAIL case storage16 mismatch at [out16 %u]: got 0x%04x exp 0x%04x\n",
                   j, out16_p[j], exp16);
            fails++;
            match = 0;
            break;
         }
         if (out32_p[j] != exp32) {
            printf("FAIL case storage16 mismatch at [out32 %u]: got 0x%08x exp 0x%08x\n",
                   j, out32_p[j], exp32);
            fails++;
            match = 0;
            break;
         }
      }
      if (match) {
         for (uint32_t k = 0; k < 16; k++) {
            uint32_t idx = 128 * sizeof(uint16_t) + k;
            if (out16_bytes[idx] != 0xEF) {
               printf("FAIL case storage16 guard byte corrupted at [%u]: got 0x%02x exp 0xef\n",
                      idx, out16_bytes[idx]);
               fails++;
               match = 0;
               break;
            }
         }
      }
      if (match) {
         printf("PASS case storage16\n");
         passes++;
      }

storage16_cleanup:
      if (pipe)
         p_DestroyPipeline(dev, pipe, NULL);
      if (dpool)
         p_DestroyDescriptorPool(dev, dpool, NULL);
      if (playout)
         p_DestroyPipelineLayout(dev, playout, NULL);
      if (dsl)
         p_DestroyDescriptorSetLayout(dev, dsl, NULL);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &in_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &out16_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &out32_buf);
   }

   /* ──────────────────────────────────────────────────────────────────────────
    * Case 6: demote
    * ────────────────────────────────────────────────────────────────────────── */
   if (!can_chain_demote || !supported_demote.shaderDemoteToHelperInvocation) {
      printf("SKIP case demote shaderDemoteToHelperInvocation missing\n");
      skips++;
   } else {
      struct exec_image img = {0};
      struct exec_buf read_buf = {0};
      VkRenderPass rpass = VK_NULL_HANDLE;
      VkFramebuffer fbuf = VK_NULL_HANDLE;
      VkPipelineLayout playout = VK_NULL_HANDLE;
      VkPipeline pipe = VK_NULL_HANDLE;

      if (create_image_2d(dev, &mp, 8, 8, VK_FORMAT_R32_UINT,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                          p_CreateImage, p_GetImageMemoryRequirements,
                          p_AllocateMemory, p_BindImageMemory, p_CreateImageView,
                          &img)) {
         printf("FAIL case demote create_image_2d\n");
         fails++;
         goto demote_cleanup;
      }

      if (create_buffer(dev, &mp, 8 * 8 * sizeof(uint32_t),
                        VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        g_bda, &read_buf)) {
         printf("FAIL case demote create_buffer\n");
         fails++;
         goto demote_cleanup;
      }
      uint32_t *read_map = (uint32_t *)read_buf.map;
      for (uint32_t i = 0; i < 64; i++)
         read_map[i] = 0xDEADBEEFu;

      VkAttachmentDescription attach = {
         .format = VK_FORMAT_R32_UINT,
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
         .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
         .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
         .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      };
      VkAttachmentReference col_ref = {
         .attachment = 0,
         .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      };
      VkSubpassDescription subpass = {
         .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
         .colorAttachmentCount = 1,
         .pColorAttachments = &col_ref,
      };
      VkSubpassDependency dep = {
         .srcSubpass = VK_SUBPASS_EXTERNAL,
         .dstSubpass = 0,
         .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
         .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
         .srcAccessMask = 0,
         .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      };
      VkRenderPassCreateInfo rpci = {
         .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
         .attachmentCount = 1,
         .pAttachments = &attach,
         .subpassCount = 1,
         .pSubpasses = &subpass,
         .dependencyCount = 1,
         .pDependencies = &dep,
      };
      if (p_CreateRenderPass(dev, &rpci, NULL, &rpass) != VK_SUCCESS) {
         printf("FAIL case demote CreateRenderPass\n");
         fails++;
         goto demote_cleanup;
      }

      VkFramebufferCreateInfo fbci = {
         .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
         .renderPass = rpass,
         .attachmentCount = 1,
         .pAttachments = &img.view,
         .width = 8,
         .height = 8,
         .layers = 1,
      };
      if (p_CreateFramebuffer(dev, &fbci, NULL, &fbuf) != VK_SUCCESS) {
         printf("FAIL case demote CreateFramebuffer\n");
         fails++;
         goto demote_cleanup;
      }

      if (create_simple_pipeline_layout(dev, p_CreatePipelineLayout, VK_NULL_HANDLE, &playout) != VK_SUCCESS) {
         printf("FAIL case demote CreatePipelineLayout\n");
         fails++;
         goto demote_cleanup;
      }

      VkResult vr = create_graphics_pipe(dev, p_CreateShaderModule, p_DestroyShaderModule,
                                         p_CreateGraphicsPipelines,
                                         demote_vert_spv, sizeof(demote_vert_spv),
                                         demote_frag_spv, sizeof(demote_frag_spv),
                                         rpass, playout, 8, 8, &pipe);
      if (vr != VK_SUCCESS) {
         printf("FAIL case demote CreateGraphicsPipelines r=%d\n", (int)vr);
         fails++;
         goto demote_cleanup;
      }

      VkCommandBufferBeginInfo bi = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      };
      p_BeginCommandBuffer(cmd, &bi);

      VkClearValue cv;
      cv.color.uint32[0] = 0xCCCCCCCCu;
      cv.color.uint32[1] = 0;
      cv.color.uint32[2] = 0;
      cv.color.uint32[3] = 0;

      VkRenderPassBeginInfo rpbi = {
         .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
         .renderPass = rpass,
         .framebuffer = fbuf,
         .renderArea = { {0, 0}, {8, 8} },
         .clearValueCount = 1,
         .pClearValues = &cv,
      };
      p_CmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
      p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
      p_CmdDraw(cmd, 3, 1, 0, 0);
      p_CmdEndRenderPass(cmd);

      VkImageMemoryBarrier imb = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
         .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
         .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .image = img.image,
         .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
      };
      p_CmdPipelineBarrier(cmd,
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0, 0, NULL, 0, NULL, 1, &imb);

      VkBufferImageCopy bic = {
         .bufferOffset = 0,
         .bufferRowLength = 0,
         .bufferImageHeight = 0,
         .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
         .imageOffset = {0, 0, 0},
         .imageExtent = {8, 8, 1},
      };
      p_CmdCopyImageToBuffer(cmd, img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, read_buf.buffer, 1, &bic);

      VkBufferMemoryBarrier bmb = {
         .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .buffer = read_buf.buffer,
         .offset = 0,
         .size = VK_WHOLE_SIZE,
      };
      p_CmdPipelineBarrier(cmd,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_HOST_BIT,
                           0, 0, NULL, 1, &bmb, 0, NULL);
      p_EndCommandBuffer(cmd);

      VkSubmitInfo si = {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .commandBufferCount = 1,
         .pCommandBuffers = &cmd,
      };
      vr = p_QueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
      if (vr != VK_SUCCESS) {
         printf("FAIL case demote QueueSubmit r=%d\n", (int)vr);
         fails++;
         goto demote_cleanup;
      }
      if (!check_wait_idle(queue, p_QueueWaitIdle, "demote")) {
         fails++;
         goto demote_cleanup;
      }

      int match = 1;
      for (uint32_t y = 0; y < 8; y++) {
         for (uint32_t x = 0; x < 8; x++) {
            uint32_t idx = y * 8u + x;
            uint32_t got = read_map[idx];
            uint32_t exp = ((x ^ y) & 1) ? 0xCCCCCCCCu : (0x11600Du + x + 8u * y);
            if (got != exp) {
               printf("FAIL case demote mismatch at [%u,%u]: got 0x%08x exp 0x%08x\n",
                      x, y, got, exp);
               fails++;
               match = 0;
               break;
            }
         }
         if (!match)
            break;
      }
      if (match) {
         printf("PASS case demote\n");
         passes++;
      }

demote_cleanup:
      if (pipe)
         p_DestroyPipeline(dev, pipe, NULL);
      if (playout)
         p_DestroyPipelineLayout(dev, playout, NULL);
      if (fbuf)
         p_DestroyFramebuffer(dev, fbuf, NULL);
      if (rpass)
         p_DestroyRenderPass(dev, rpass, NULL);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &read_buf);
      destroy_image_2d(dev, p_DestroyImageView, p_DestroyImage, p_FreeMemory, &img);
   }

   /* ──────────────────────────────────────────────────────────────────────────
    * Case 7: subgroup_size_control
    * ────────────────────────────────────────────────────────────────────────── */
   int subgroup_caps_ok =
      can_chain_subgroup_size &&
      supported_subgroup_size.subgroupSizeControl &&
      supported_subgroup_size.computeFullSubgroups &&
      (subgroup_size_props.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT) &&
      (subgroup_props.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) &&
      ((subgroup_props.supportedOperations & (VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_ARITHMETIC_BIT)) ==
       (VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_ARITHMETIC_BIT));

   if (!subgroup_caps_ok) {
      printf("SKIP case subgroup_size_control subgroupSizeControl features missing\n");
      skips++;
   } else {
      uint32_t min_s = subgroup_size_props.minSubgroupSize;
      uint32_t max_s = subgroup_size_props.maxSubgroupSize;

      if (min_s == 0 || max_s == 0 || min_s > max_s) {
         printf("SKIP case subgroup_size_control invalid min/max subgroup size\n");
         skips++;
      } else {
         uint32_t wg_size = max_s > 128 ? max_s : 128;
         uint32_t eligible_sizes = 0;
         for (uint32_t s = min_s; s <= max_s; s *= 2) {
            if (wg_size > dev_props.limits.maxComputeWorkGroupInvocations ||
                wg_size > dev_props.limits.maxComputeWorkGroupSize[0] ||
                wg_size / s > subgroup_size_props.maxComputeWorkgroupSubgroups) {
               printf("INFO case subgroup_size_control size %u skipped (limits)\n", s);
               continue;
            }
            eligible_sizes++;
         }
         if (!eligible_sizes) {
            printf("SKIP case subgroup_size_control all sizes skipped (limits)\n");
            skips++;
            goto skip_subgroup_test;
         }

         uint32_t total_invocations = 4u * wg_size;
         struct exec_buf buf = {0};
         VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
         VkPipelineLayout playout = VK_NULL_HANDLE;
         VkDescriptorPool dpool = VK_NULL_HANDLE;
         VkDescriptorSet dset = VK_NULL_HANDLE;

         if (create_buffer(dev, &mp, total_invocations * sizeof(uint32_t),
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0,
                           p_CreateBuffer, p_GetBufferMemoryRequirements,
                           p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                           g_bda, &buf)) {
            printf("FAIL case subgroup_size_control create_buffer\n");
            fails++;
            goto subgroup_cleanup;
         }

         if (create_ssbo_dsl(dev, p_CreateDescriptorSetLayout, 1, &dsl) != VK_SUCCESS) {
            printf("FAIL case subgroup_size_control CreateDescriptorSetLayout\n");
            fails++;
            goto subgroup_cleanup;
         }
         if (create_simple_pipeline_layout(dev, p_CreatePipelineLayout, dsl, &playout) != VK_SUCCESS) {
            printf("FAIL case subgroup_size_control CreatePipelineLayout\n");
            fails++;
            goto subgroup_cleanup;
         }
         if (create_ssbo_descriptors(dev, p_CreateDescriptorPool, p_AllocateDescriptorSets,
                                     p_UpdateDescriptorSets, dsl, 1, &buf, &dpool, &dset) != VK_SUCCESS) {
            printf("FAIL case subgroup_size_control allocate descriptors\n");
            fails++;
            goto subgroup_cleanup;
         }

         VkSpecializationMapEntry spec_entry = {
            .constantID = 0,
            .offset = 0,
            .size = sizeof(uint32_t),
         };
         VkSpecializationInfo spec_info = {
            .mapEntryCount = 1,
            .pMapEntries = &spec_entry,
            .dataSize = sizeof(wg_size),
            .pData = &wg_size,
         };

         int all_passed = 1;
         uint32_t *map = (uint32_t *)buf.map;

         for (uint32_t s = min_s; s <= max_s; s *= 2) {
            if (wg_size > dev_props.limits.maxComputeWorkGroupInvocations ||
                wg_size > dev_props.limits.maxComputeWorkGroupSize[0] ||
                wg_size / s > subgroup_size_props.maxComputeWorkgroupSubgroups)
               continue;

            VkPipeline pipe = VK_NULL_HANDLE;
            VkPipelineShaderStageRequiredSubgroupSizeCreateInfo req_subgroup_size = {
               .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO,
               .requiredSubgroupSize = s,
            };

            VkResult vr = create_compute_pipe_subgroup(dev, p_CreateShaderModule, p_DestroyShaderModule,
                                                       p_CreateComputePipelines,
                                                       subgroup_size_control_spv,
                                                       sizeof(subgroup_size_control_spv),
                                                       playout,
                                                       VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT,
                                                       &req_subgroup_size,
                                                       &spec_info,
                                                       &pipe);
            if (vr != VK_SUCCESS) {
               printf("FAIL case subgroup_size_control size %u CreateComputePipelines r=%d\n", s, (int)vr);
               all_passed = 0;
               fails++;
               continue;
            }

            for (uint32_t i = 0; i < total_invocations; i++)
               map[i] = 0xDEADBEEFu;

            VkCommandBufferBeginInfo bi = {
               .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            };
            p_BeginCommandBuffer(cmd, &bi);
            p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
            p_CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &dset, 0, NULL);
            p_CmdDispatch(cmd, 4, 1, 1);
            VkMemoryBarrier host_mb = {
               .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
               .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
               .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
            };
            p_CmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_HOST_BIT,
                                 0, 1, &host_mb, 0, NULL, 0, NULL);
            p_EndCommandBuffer(cmd);

            VkSubmitInfo si = {
               .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
               .commandBufferCount = 1,
               .pCommandBuffers = &cmd,
            };
            vr = p_QueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
            if (vr != VK_SUCCESS) {
               printf("FAIL case subgroup_size_control size %u QueueSubmit r=%d\n", s, (int)vr);
               all_passed = 0;
               fails++;
               p_DestroyPipeline(dev, pipe, NULL);
               break;
            }
            if (!check_wait_idle(queue, p_QueueWaitIdle, "subgroup_size_control")) {
               all_passed = 0;
               fails++;
               p_DestroyPipeline(dev, pipe, NULL);
               break;
            }

            uint32_t exp_val = s | (s << 16);
            for (uint32_t idx = 0; idx < total_invocations; idx++) {
               if (map[idx] != exp_val) {
                  printf("FAIL case subgroup_size_control size %u mismatch at [%u]: got 0x%08x exp 0x%08x\n",
                         s, idx, map[idx], exp_val);
                  all_passed = 0;
                  fails++;
                  break;
               }
            }
            p_DestroyPipeline(dev, pipe, NULL);
         }

         if (all_passed) {
            printf("PASS case subgroup_size_control\n");
            passes++;
         }

subgroup_cleanup:
         if (dpool)
            p_DestroyDescriptorPool(dev, dpool, NULL);
         if (playout)
            p_DestroyPipelineLayout(dev, playout, NULL);
         if (dsl)
            p_DestroyDescriptorSetLayout(dev, dsl, NULL);
         destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &buf);
skip_subgroup_test:;
      }
   }

   /* ──────────────────────────────────────────────────────────────────────────
    * Case 8: zero_init_wg
    * ────────────────────────────────────────────────────────────────────────── */
   if (!can_chain_zero_init || !supported_zero_init.shaderZeroInitializeWorkgroupMemory) {
      printf("SKIP case zero_init_wg shaderZeroInitializeWorkgroupMemory missing\n");
      skips++;
   } else {
      struct exec_buf scratch_buf = {0}, out_buf = {0}, control_buf = {0};
      VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
      VkPipelineLayout playout = VK_NULL_HANDLE;
      VkDescriptorPool dpool = VK_NULL_HANDLE;
      VkDescriptorSet set_a = VK_NULL_HANDLE, set_b = VK_NULL_HANDLE, set_c = VK_NULL_HANDLE;
      VkPipeline pipe_a = VK_NULL_HANDLE, pipe_b = VK_NULL_HANDLE, pipe_c = VK_NULL_HANDLE;

      if (create_buffer(dev, &mp, 16 * 128 * sizeof(uint32_t),
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        g_bda, &scratch_buf) ||
          create_buffer(dev, &mp, 16 * 128 * 4 * sizeof(uint32_t),
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        g_bda, &out_buf) ||
          create_buffer(dev, &mp, 16 * 128 * 4 * sizeof(uint32_t),
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        g_bda, &control_buf)) {
         printf("FAIL case zero_init_wg create_buffer\n");
         fails++;
         goto zero_init_cleanup;
      }

      uint32_t *out_p = (uint32_t *)out_buf.map;
      uint32_t *control_p = (uint32_t *)control_buf.map;
      uint32_t *scratch_p = (uint32_t *)scratch_buf.map;
      for (uint32_t i = 0; i < 16 * 128; i++)
         scratch_p[i] = ~(0xDEAD0000u + ((i % 128u) * 4u + 5u) % 512u);
      for (uint32_t i = 0; i < 16 * 128 * 4; i++) {
         out_p[i] = 0xDEADBEEFu;
         control_p[i] = 0xDEADBEEFu;
      }

      if (create_ssbo_dsl(dev, p_CreateDescriptorSetLayout, 1, &dsl) != VK_SUCCESS) {
         printf("FAIL case zero_init_wg CreateDescriptorSetLayout\n");
         fails++;
         goto zero_init_cleanup;
      }
      if (create_simple_pipeline_layout(dev, p_CreatePipelineLayout, dsl, &playout) != VK_SUCCESS) {
         printf("FAIL case zero_init_wg CreatePipelineLayout\n");
         fails++;
         goto zero_init_cleanup;
      }

      VkDescriptorPoolSize ps = {
         .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 3,
      };
      VkDescriptorPoolCreateInfo dpci = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
         .maxSets = 3,
         .poolSizeCount = 1,
         .pPoolSizes = &ps,
      };
      dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
      if (p_CreateDescriptorPool(dev, &dpci, NULL, &dpool) != VK_SUCCESS) {
         printf("FAIL case zero_init_wg CreateDescriptorPool\n");
         fails++;
         goto zero_init_cleanup;
      }

      VkDescriptorSetLayout layouts[3] = { dsl, dsl, dsl };
      VkDescriptorSetAllocateInfo dsai = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
         .descriptorPool = dpool,
         .descriptorSetCount = 3,
         .pSetLayouts = layouts,
      };
      VkDescriptorSet sets[3];
      if (p_AllocateDescriptorSets(dev, &dsai, sets) != VK_SUCCESS) {
         printf("FAIL case zero_init_wg AllocateDescriptorSets\n");
         fails++;
         goto zero_init_cleanup;
      }
      set_a = sets[0];
      set_b = sets[1];
      set_c = sets[2];

      VkDescriptorBufferInfo dbi_a = {
         .buffer = scratch_buf.buffer,
         .offset = 0,
         .range = VK_WHOLE_SIZE,
      };
      VkDescriptorBufferInfo dbi_b = {
         .buffer = out_buf.buffer,
         .offset = 0,
         .range = VK_WHOLE_SIZE,
      };
      VkDescriptorBufferInfo dbi_c = {
         .buffer = control_buf.buffer,
         .offset = 0,
         .range = VK_WHOLE_SIZE,
      };
      VkWriteDescriptorSet wds[3] = {
         {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set_a,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &dbi_a,
         },
         {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set_b,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &dbi_b,
         },
         {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set_c,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &dbi_c,
         },
      };
      p_UpdateDescriptorSets(dev, 3, wds, 0, NULL);

      VkResult vr = create_compute_pipe(dev, p_CreateShaderModule, p_DestroyShaderModule,
                                        p_CreateComputePipelines, zero_init_a_spv, sizeof(zero_init_a_spv),
                                        playout, &pipe_a);
      if (vr != VK_SUCCESS) {
         printf("FAIL case zero_init_wg CreateComputePipelines pipe_a r=%d\n", (int)vr);
         fails++;
         goto zero_init_cleanup;
      }

      vr = create_compute_pipe(dev, p_CreateShaderModule, p_DestroyShaderModule,
                               p_CreateComputePipelines, zero_init_b_spv, sizeof(zero_init_b_spv),
                               playout, &pipe_b);
      if (vr != VK_SUCCESS) {
         printf("FAIL case zero_init_wg CreateComputePipelines pipe_b r=%d\n", (int)vr);
         fails++;
         goto zero_init_cleanup;
      }

      vr = create_compute_pipe(dev, p_CreateShaderModule, p_DestroyShaderModule,
                               p_CreateComputePipelines, zero_init_c_spv, sizeof(zero_init_c_spv),
                               playout, &pipe_c);
      if (vr != VK_SUCCESS) {
         printf("FAIL case zero_init_wg CreateComputePipelines pipe_c r=%d\n", (int)vr);
         fails++;
         goto zero_init_cleanup;
      }

      VkCommandBufferBeginInfo bi = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      };
      p_BeginCommandBuffer(cmd, &bi);
      p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe_a);
      p_CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &set_a, 0, NULL);
      p_CmdDispatch(cmd, 16, 1, 1);

      VkMemoryBarrier mb = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
      };
      p_CmdPipelineBarrier(cmd,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           0, 1, &mb, 0, NULL, 0, NULL);

      p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe_c);
      p_CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &set_c, 0, NULL);
      p_CmdDispatch(cmd, 16, 1, 1);
      p_CmdPipelineBarrier(cmd,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           0, 1, &mb, 0, NULL, 0, NULL);

      p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe_a);
      p_CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &set_a, 0, NULL);
      p_CmdDispatch(cmd, 16, 1, 1);
      p_CmdPipelineBarrier(cmd,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           0, 1, &mb, 0, NULL, 0, NULL);

      p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe_b);
      p_CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &set_b, 0, NULL);
      p_CmdDispatch(cmd, 16, 1, 1);
      VkMemoryBarrier host_mb = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
      };
      p_CmdPipelineBarrier(cmd,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_HOST_BIT,
                           0, 1, &host_mb, 0, NULL, 0, NULL);
      p_EndCommandBuffer(cmd);

      VkSubmitInfo si = {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .commandBufferCount = 1,
         .pCommandBuffers = &cmd,
      };
      vr = p_QueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
      if (vr != VK_SUCCESS) {
         printf("FAIL case zero_init_wg QueueSubmit r=%d\n", (int)vr);
         fails++;
         goto zero_init_cleanup;
      }
      if (!check_wait_idle(queue, p_QueueWaitIdle, "zero_init_wg")) {
         fails++;
         goto zero_init_cleanup;
      }

      int match = 1;
      for (uint32_t i = 0; i < 16 * 128; i++) {
         uint32_t lid = i % 128u;
         uint32_t exp = 0xDEAD0000u + (lid * 4u + 5u) % 512u;
         if (scratch_p[i] != exp) {
            printf("FAIL case zero_init_wg scratch mismatch at [%u]: got 0x%08x exp 0x%08x\n",
                   i, scratch_p[i], exp);
            fails++;
            match = 0;
            break;
         }
      }
      for (uint32_t i = 0; match && i < 16 * 128 * 4; i++) {
         if (out_p[i] != 0u) {
            printf("FAIL case zero_init_wg mismatch at [%u]: got 0x%08x exp 0x00000000\n",
                   i, out_p[i]);
            fails++;
            match = 0;
            break;
         }
      }
      if (match) {
         int control_zero = 1;
         for (uint32_t i = 0; i < 16 * 128 * 4; i++) {
            if (control_p[i] != 0u) {
               control_zero = 0;
               break;
            }
         }
         if (control_zero)
            printf("INFO case zero_init_wg control read zero, initializer not proven\n");
         printf("PASS case zero_init_wg\n");
         passes++;
      }

zero_init_cleanup:
      if (pipe_c)
         p_DestroyPipeline(dev, pipe_c, NULL);
      if (pipe_b)
         p_DestroyPipeline(dev, pipe_b, NULL);
      if (pipe_a)
         p_DestroyPipeline(dev, pipe_a, NULL);
      if (dpool)
         p_DestroyDescriptorPool(dev, dpool, NULL);
      if (playout)
         p_DestroyPipelineLayout(dev, playout, NULL);
      if (dsl)
         p_DestroyDescriptorSetLayout(dev, dsl, NULL);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &scratch_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &out_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &control_buf);
   }

   (void)passes;
   p_DestroyCommandPool(dev, pool, NULL);
   p_DestroyDevice(dev, NULL);
   p_DestroyInstance(inst, NULL);
   dlclose(h);

   if (fails > 0) {
      printf("RESULT FAIL\n");
      return 1;
   }
   if (skips == 8) {
      printf("RESULT SKIP\n");
      return 0;
   }
   printf("RESULT PASS\n");
   return 0;
}

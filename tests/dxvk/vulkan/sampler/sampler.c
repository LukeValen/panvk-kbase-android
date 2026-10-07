/* DXVK sampler coverage (PanProbe plan 2.6), all sampled from compute.
 *
 *  anisotropy             8:1 footprint via textureGrad: aniso LOD below iso
 *  mirror_clamp_to_edge   VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE
 *  border_opaque_white    core CLAMP_TO_BORDER
 *  border_custom          VK_EXT_custom_border_color (with format)
 *  border_custom_no_format  customBorderColorWithoutFormat
 *  cube_array             samplerCubeArray, 2 cubes x 6 faces
 *  cube_seamless          LINEAR filter across a cube edge must blend faces
 *  cube_non_seamless      VK_EXT_non_seamless_cube_map: clamp within the face
 *  gather                 textureGather + const textureGatherOffset (core)
 *  gather_offsets         textureGatherOffsets + dynamic offset (extended)
 *
 * Output: PASS/FAIL/SKIP per case, then RESULT PASS|FAIL|SKIP.
 * usage: sampler <libvulkan_panfrost.so>
 * SPIR-V regen: ./build_spv.sh
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

#include "sampler_spv.h"

#define CK(expr, what)                                                         \
   do {                                                                        \
      VkResult _r = (expr);                                                    \
      if (_r != VK_SUCCESS) {                                                  \
         printf("FAIL %s r=%d line=%d\n", what, (int)_r, __LINE__);            \
         return 1;                                                             \
      }                                                                        \
   } while (0)

typedef PFN_vkVoidFunction (*icd_gipa_fn)(VkInstance, const char *);

/* Global loader function pointers */
#define GI_LIST(X)                                                             \
   X(EnumeratePhysicalDevices)                                                 \
   X(GetPhysicalDeviceProperties)                                              \
   X(GetPhysicalDeviceFeatures2)                                               \
   X(GetPhysicalDeviceQueueFamilyProperties)                                   \
   X(GetPhysicalDeviceMemoryProperties)                                        \
   X(EnumerateDeviceExtensionProperties)                                       \
   X(CreateDevice)                                                             \
   X(DestroyDevice)                                                            \
   X(GetDeviceProcAddr)                                                        \
   X(DestroyInstance)

#define GD_LIST(X)                                                             \
   X(GetDeviceQueue)                                                           \
   X(CreateBuffer)                                                             \
   X(DestroyBuffer)                                                            \
   X(GetBufferMemoryRequirements)                                              \
   X(AllocateMemory)                                                           \
   X(FreeMemory)                                                               \
   X(BindBufferMemory)                                                         \
   X(MapMemory)                                                                \
   X(UnmapMemory)                                                              \
   X(CreateImage)                                                              \
   X(DestroyImage)                                                             \
   X(GetImageMemoryRequirements)                                               \
   X(BindImageMemory)                                                          \
   X(CreateImageView)                                                          \
   X(DestroyImageView)                                                         \
   X(CreateSampler)                                                            \
   X(DestroySampler)                                                           \
   X(CreateShaderModule)                                                       \
   X(DestroyShaderModule)                                                      \
   X(CreateDescriptorSetLayout)                                                \
   X(DestroyDescriptorSetLayout)                                               \
   X(CreatePipelineLayout)                                                     \
   X(DestroyPipelineLayout)                                                    \
   X(CreateComputePipelines)                                                   \
   X(DestroyPipeline)                                                          \
   X(CreateDescriptorPool)                                                     \
   X(DestroyDescriptorPool)                                                    \
   X(AllocateDescriptorSets)                                                   \
   X(UpdateDescriptorSets)                                                     \
   X(CreateCommandPool)                                                        \
   X(DestroyCommandPool)                                                       \
   X(AllocateCommandBuffers)                                                   \
   X(FreeCommandBuffers)                                                       \
   X(BeginCommandBuffer)                                                       \
   X(EndCommandBuffer)                                                         \
   X(ResetCommandBuffer)                                                       \
   X(CmdBindPipeline)                                                          \
   X(CmdBindDescriptorSets)                                                    \
   X(CmdPushConstants)                                                         \
   X(CmdDispatch)                                                              \
   X(CmdPipelineBarrier)                                                       \
   X(CmdCopyBufferToImage)                                                     \
   X(CreateFence)                                                              \
   X(DestroyFence)                                                             \
   X(ResetFences)                                                              \
   X(WaitForFences)                                                            \
   X(QueueSubmit)                                                              \
   X(QueueWaitIdle)

#define DECL_FN(n) static PFN_vk##n p_##n;
GI_LIST(DECL_FN)
GD_LIST(DECL_FN)
#undef DECL_FN

static VkDevice dev;
static VkQueue queue;
static VkCommandPool cmd_pool;
static VkPhysicalDeviceMemoryProperties mem_props;
static VkPhysicalDeviceLimits dev_limits;

struct exec_buf {
   VkBuffer buffer;
   VkDeviceMemory memory;
   VkDeviceSize size;
   void *map;
};

struct texture {
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
pick_type(uint32_t bits, VkMemoryPropertyFlags want, uint32_t *out)
{
   for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
      if ((bits & (1u << i)) &&
          (mem_props.memoryTypes[i].propertyFlags & want) == want) {
         *out = i;
         return 0;
      }
   }
   for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
      if (bits & (1u << i)) {
         *out = i;
         return 0;
      }
   }
   return 1;
}

static int
create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
              VkMemoryPropertyFlags mem_flags, struct exec_buf *b)
{
   memset(b, 0, sizeof(*b));
   b->size = size;

   VkBufferCreateInfo bci = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = size,
      .usage = usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   };
   CK(p_CreateBuffer(dev, &bci, NULL, &b->buffer), "CreateBuffer");

   VkMemoryRequirements mr;
   p_GetBufferMemoryRequirements(dev, b->buffer, &mr);

   uint32_t type_idx = ~0u;
   if (pick_type(mr.memoryTypeBits, mem_flags, &type_idx))
      return 1;

   VkMemoryAllocateInfo mai = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mr.size,
      .memoryTypeIndex = type_idx,
   };
   CK(p_AllocateMemory(dev, &mai, NULL, &b->memory), "AllocateMemory");
   CK(p_BindBufferMemory(dev, b->buffer, b->memory, 0), "BindBufferMemory");

   if (mem_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
      CK(p_MapMemory(dev, b->memory, 0, size, 0, &b->map), "MapMemory");
   }

   return 0;
}

static void
destroy_buffer(struct exec_buf *b)
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

static void
destroy_texture(struct texture *tex)
{
   if (tex->view) {
      p_DestroyImageView(dev, tex->view, NULL);
      tex->view = VK_NULL_HANDLE;
   }
   if (tex->image) {
      p_DestroyImage(dev, tex->image, NULL);
      tex->image = VK_NULL_HANDLE;
   }
   if (tex->memory) {
      p_FreeMemory(dev, tex->memory, NULL);
      tex->memory = VK_NULL_HANDLE;
   }
}

typedef void (*texture_fill_fn)(uint32_t mip, uint32_t layer, uint32_t w,
                               uint32_t h, void *dst);

static int
make_texture(VkImageType type, VkFormat fmt, uint32_t w, uint32_t h,
             uint32_t mips, uint32_t layers, bool cube_compatible,
             VkImageViewType view_type, texture_fill_fn fill_cb,
             struct texture *out_tex)
{
   memset(out_tex, 0, sizeof(*out_tex));

   uint32_t bpp = (fmt == VK_FORMAT_R8G8B8A8_UNORM) ? 4 : 1;

   VkImageCreateInfo ici = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .flags = cube_compatible ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0,
      .imageType = type,
      .format = fmt,
      .extent = { .width = w, .height = h, .depth = 1 },
      .mipLevels = mips,
      .arrayLayers = layers,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };
   CK(p_CreateImage(dev, &ici, NULL, &out_tex->image), "CreateImage");

   VkMemoryRequirements mr;
   p_GetImageMemoryRequirements(dev, out_tex->image, &mr);

   uint32_t mem_type = ~0u;
   if (pick_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &mem_type))
      return 1;

   VkMemoryAllocateInfo mai = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mr.size,
      .memoryTypeIndex = mem_type,
   };
   CK(p_AllocateMemory(dev, &mai, NULL, &out_tex->memory), "AllocateMemory");
   CK(p_BindImageMemory(dev, out_tex->image, out_tex->memory, 0), "BindImageMemory");

   /* Calculate staging size and copy regions */
   VkDeviceSize total_staging_size = 0;
   uint32_t region_count = mips * layers;
   VkBufferImageCopy *regions = calloc(region_count, sizeof(*regions));
   if (!regions)
      return 1;

   uint32_t r_idx = 0;
   for (uint32_t l = 0; l < layers; l++) {
      uint32_t mw = w;
      uint32_t mh = h;
      for (uint32_t m = 0; m < mips; m++) {
         regions[r_idx].bufferOffset = total_staging_size;
         regions[r_idx].bufferRowLength = 0;
         regions[r_idx].bufferImageHeight = 0;
         regions[r_idx].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
         regions[r_idx].imageSubresource.mipLevel = m;
         regions[r_idx].imageSubresource.baseArrayLayer = l;
         regions[r_idx].imageSubresource.layerCount = 1;
         regions[r_idx].imageOffset = (VkOffset3D){ 0, 0, 0 };
         regions[r_idx].imageExtent = (VkExtent3D){ mw, mh, 1 };

         VkDeviceSize layer_mip_size = (VkDeviceSize)mw * mh * bpp;
         /* 4-byte align each slice offset for safety */
         total_staging_size += (layer_mip_size + 3u) & ~3u;

         if (mw > 1) mw /= 2;
         if (mh > 1) mh /= 2;
         r_idx++;
      }
   }

   struct exec_buf staging;
   if (create_buffer(total_staging_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     &staging)) {
      free(regions);
      return 1;
   }

   /* Fill staging buffer */
   r_idx = 0;
   for (uint32_t l = 0; l < layers; l++) {
      uint32_t mw = w;
      uint32_t mh = h;
      for (uint32_t m = 0; m < mips; m++) {
         uint8_t *dst = (uint8_t *)staging.map + regions[r_idx].bufferOffset;
         if (fill_cb)
            fill_cb(m, l, mw, mh, dst);
         else
            memset(dst, 0, (size_t)mw * mh * bpp);

         if (mw > 1) mw /= 2;
         if (mh > 1) mh /= 2;
         r_idx++;
      }
   }

   /* Record upload commands */
   VkCommandBufferAllocateInfo cbai = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = cmd_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
   };
   VkCommandBuffer cmd = VK_NULL_HANDLE;
   CK(p_AllocateCommandBuffers(dev, &cbai, &cmd), "AllocateCommandBuffers");

   VkCommandBufferBeginInfo bi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
   };
   CK(p_BeginCommandBuffer(cmd, &bi), "BeginCommandBuffer");

   VkImageMemoryBarrier imb1 = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = 0,
      .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = out_tex->image,
      .subresourceRange = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .baseMipLevel = 0,
         .levelCount = mips,
         .baseArrayLayer = 0,
         .layerCount = layers,
      },
   };
   p_CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL,
                        1, &imb1);

   p_CmdCopyBufferToImage(cmd, staging.buffer, out_tex->image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          region_count, regions);

   VkImageMemoryBarrier imb2 = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = out_tex->image,
      .subresourceRange = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .baseMipLevel = 0,
         .levelCount = mips,
         .baseArrayLayer = 0,
         .layerCount = layers,
      },
   };
   p_CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL,
                        1, &imb2);

   CK(p_EndCommandBuffer(cmd), "EndCommandBuffer");

   VkFence fence;
   VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
   CK(p_CreateFence(dev, &fci, NULL, &fence), "CreateFence");

   VkSubmitInfo si = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers = &cmd,
   };
   CK(p_QueueSubmit(queue, 1, &si, fence), "QueueSubmit");

   VkResult wr = VK_TIMEOUT;
   for (int k = 0; k < 40 && wr == VK_TIMEOUT; k++)
      wr = p_WaitForFences(dev, 1, &fence, VK_TRUE, 500000000ull);
   if (wr != VK_SUCCESS) {
      printf("FAIL texture upload fence wait r=%d\n", (int)wr);
      return 1;
   }

   p_DestroyFence(dev, fence, NULL);
   p_FreeCommandBuffers(dev, cmd_pool, 1, &cmd);
   destroy_buffer(&staging);
   free(regions);

   /* Create image view */
   VkImageViewCreateInfo vci = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = out_tex->image,
      .viewType = view_type,
      .format = fmt,
      .subresourceRange = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .baseMipLevel = 0,
         .levelCount = mips,
         .baseArrayLayer = 0,
         .layerCount = layers,
      },
   };
   CK(p_CreateImageView(dev, &vci, NULL, &out_tex->view), "CreateImageView");
   return 0;
}

static int
run_compute(const uint32_t *spv, size_t spv_size, VkImageView view,
            VkSampler sampler, const float *in_vec4s, uint32_t n,
            uint32_t out_vec4_count, float *out)
{
   /* Callers do not check the return value: on any failure below the
    * output keeps this -1 pattern, which never matches a reference. */
   for (uint32_t i = 0; i < out_vec4_count * 4; i++)
      out[i] = -1.0f;

   /* Create Descriptor Set Layout */
   VkDescriptorSetLayoutBinding bindings[3] = {
      {
         .binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      },
      {
         .binding = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      },
      {
         .binding = 2,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      },
   };
   VkDescriptorSetLayoutCreateInfo dslci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 3,
      .pBindings = bindings,
   };
   VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
   CK(p_CreateDescriptorSetLayout(dev, &dslci, NULL, &set_layout), "CreateDescriptorSetLayout");

   /* Create Pipeline Layout */
   VkPushConstantRange pcr = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .offset = 0,
      .size = sizeof(uint32_t),
   };
   VkPipelineLayoutCreateInfo plci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &set_layout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &pcr,
   };
   VkPipelineLayout playout = VK_NULL_HANDLE;
   CK(p_CreatePipelineLayout(dev, &plci, NULL, &playout), "CreatePipelineLayout");

   /* Create Compute Pipeline */
   VkShaderModuleCreateInfo smci = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = spv_size,
      .pCode = spv,
   };
   VkShaderModule sm = VK_NULL_HANDLE;
   CK(p_CreateShaderModule(dev, &smci, NULL, &sm), "CreateShaderModule");

   VkComputePipelineCreateInfo cpci = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_COMPUTE_BIT,
         .module = sm,
         .pName = "main",
      },
      .layout = playout,
   };
   VkPipeline pipeline = VK_NULL_HANDLE;
   CK(p_CreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, &pipeline), "CreateComputePipelines");
   p_DestroyShaderModule(dev, sm, NULL);

   /* Create Descriptor Pool */
   VkDescriptorPoolSize pool_sizes[2] = {
      { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1 },
      { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 2 },
   };
   VkDescriptorPoolCreateInfo dpci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1,
      .poolSizeCount = 2,
      .pPoolSizes = pool_sizes,
   };
   VkDescriptorPool desc_pool = VK_NULL_HANDLE;
   CK(p_CreateDescriptorPool(dev, &dpci, NULL, &desc_pool), "CreateDescriptorPool");

   VkDescriptorSetAllocateInfo dsai = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = desc_pool,
      .descriptorSetCount = 1,
      .pSetLayouts = &set_layout,
   };
   VkDescriptorSet desc_set = VK_NULL_HANDLE;
   CK(p_AllocateDescriptorSets(dev, &dsai, &desc_set), "AllocateDescriptorSets");

   /* Buffers */
   struct exec_buf in_buf, out_buf;
   VkDeviceSize in_size = (VkDeviceSize)n * 4 * sizeof(float);
   if (in_size < 16) in_size = 16;
   VkDeviceSize out_size = (VkDeviceSize)out_vec4_count * 4 * sizeof(float);
   if (out_size < 16) out_size = 16;

   if (create_buffer(in_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     &in_buf))
      return 1;
   if (create_buffer(out_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     &out_buf)) {
      destroy_buffer(&in_buf);
      return 1;
   }

   memcpy(in_buf.map, in_vec4s, n * 4 * sizeof(float));
   memset(out_buf.map, 0, out_vec4_count * 4 * sizeof(float));

   /* Update Descriptors */
   VkDescriptorImageInfo dii = {
      .sampler = sampler,
      .imageView = view,
      .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
   };
   VkDescriptorBufferInfo dbi_in = {
      .buffer = in_buf.buffer,
      .offset = 0,
      .range = in_size,
   };
   VkDescriptorBufferInfo dbi_out = {
      .buffer = out_buf.buffer,
      .offset = 0,
      .range = out_size,
   };

   VkWriteDescriptorSet writes[3] = {
      {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = desc_set,
         .dstBinding = 0,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .pImageInfo = &dii,
      },
      {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = desc_set,
         .dstBinding = 1,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .pBufferInfo = &dbi_in,
      },
      {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = desc_set,
         .dstBinding = 2,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .pBufferInfo = &dbi_out,
      },
   };
   p_UpdateDescriptorSets(dev, 3, writes, 0, NULL);

   /* Allocate Command Buffer */
   VkCommandBufferAllocateInfo cbai = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = cmd_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
   };
   VkCommandBuffer cmd = VK_NULL_HANDLE;
   CK(p_AllocateCommandBuffers(dev, &cbai, &cmd), "AllocateCommandBuffers");

   VkCommandBufferBeginInfo bi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
   };
   CK(p_BeginCommandBuffer(cmd, &bi), "BeginCommandBuffer");

   p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
   p_CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1,
                           &desc_set, 0, NULL);
   p_CmdPushConstants(cmd, playout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &n);

   uint32_t groups = (n + 63) / 64;
   p_CmdDispatch(cmd, groups, 1, 1);

   VkMemoryBarrier mb = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
   };
   p_CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);

   CK(p_EndCommandBuffer(cmd), "EndCommandBuffer");

   /* Submit and wait */
   VkFence fence;
   VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
   CK(p_CreateFence(dev, &fci, NULL, &fence), "CreateFence");

   VkSubmitInfo si = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers = &cmd,
   };
   CK(p_QueueSubmit(queue, 1, &si, fence), "QueueSubmit");

   VkResult wr = VK_TIMEOUT;
   for (int k = 0; k < 40 && wr == VK_TIMEOUT; k++)
      wr = p_WaitForFences(dev, 1, &fence, VK_TRUE, 500000000ull);
   if (wr != VK_SUCCESS) {
      printf("FAIL compute wait r=%d\n", (int)wr);
      return 1;
   }

   memcpy(out, out_buf.map, out_vec4_count * 4 * sizeof(float));

   /* Clean up */
   p_DestroyFence(dev, fence, NULL);
   p_FreeCommandBuffers(dev, cmd_pool, 1, &cmd);
   destroy_buffer(&in_buf);
   destroy_buffer(&out_buf);
   p_DestroyDescriptorPool(dev, desc_pool, NULL);
   p_DestroyPipeline(dev, pipeline, NULL);
   p_DestroyPipelineLayout(dev, playout, NULL);
   p_DestroyDescriptorSetLayout(dev, set_layout, NULL);

   return 0;
}

/* Texture fill callbacks */
static void
fill_aniso(uint32_t mip, uint32_t layer, uint32_t w, uint32_t h, void *dst)
{
   (void)layer;
   uint8_t *pix = (uint8_t *)dst;
   uint8_t r = (uint8_t)(mip * 32);
   for (uint32_t i = 0; i < w * h; i++) {
      pix[4 * i + 0] = r;
      pix[4 * i + 1] = 0;
      pix[4 * i + 2] = 0;
      pix[4 * i + 3] = 255;
   }
}

static void
fill_4x1(uint32_t mip, uint32_t layer, uint32_t w, uint32_t h, void *dst)
{
   (void)mip;
   (void)layer;
   (void)h;
   uint8_t *pix = (uint8_t *)dst;
   for (uint32_t i = 0; i < w; i++) {
      pix[4 * i + 0] = (uint8_t)(10 * (i + 1));
      pix[4 * i + 1] = 0;
      pix[4 * i + 2] = 0;
      pix[4 * i + 3] = 255;
   }
}

static void
fill_cubearr(uint32_t mip, uint32_t layer, uint32_t w, uint32_t h, void *dst)
{
   (void)mip;
   uint8_t *pix = (uint8_t *)dst;
   uint8_t r = (uint8_t)(20 * layer + 5);
   for (uint32_t i = 0; i < w * h; i++) {
      pix[4 * i + 0] = r;
      pix[4 * i + 1] = 0;
      pix[4 * i + 2] = 0;
      pix[4 * i + 3] = 255;
   }
}

static void
fill_cube_seamless(uint32_t mip, uint32_t layer, uint32_t w, uint32_t h, void *dst)
{
   (void)mip;
   uint8_t *pix = (uint8_t *)dst;
   uint8_t r = (uint8_t)(30 * layer + 10);
   for (uint32_t i = 0; i < w * h; i++) {
      pix[4 * i + 0] = r;
      pix[4 * i + 1] = 0;
      pix[4 * i + 2] = 0;
      pix[4 * i + 3] = 255;
   }
}

static void
fill_gather_8x8(uint32_t mip, uint32_t layer, uint32_t w, uint32_t h, void *dst)
{
   (void)mip;
   (void)layer;
   uint8_t *pix = (uint8_t *)dst;
   for (uint32_t y = 0; y < h; y++) {
      for (uint32_t x = 0; x < w; x++) {
         pix[y * w + x] = (uint8_t)(y * 8 + x);
      }
   }
}

static inline int
clamp_coord(int v, int lo, int hi)
{
   if (v < lo) return lo;
   if (v > hi) return hi;
   return v;
}

static inline int
texel_8x8(int x, int y)
{
   return clamp_coord(y, 0, 7) * 8 + clamp_coord(x, 0, 7);
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
      return 1;
   }

#define LOAD_GI(n)                                                             \
   p_##n = (PFN_vk##n)gipa(inst, "vk" #n);                                     \
   if (!p_##n) {                                                               \
      printf("FAIL missing vk" #n "\n");                                       \
      return 1;                                                                \
   }
   GI_LIST(LOAD_GI)
#undef LOAD_GI

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

   /* Device properties and limits */
   VkPhysicalDeviceProperties props;
   p_GetPhysicalDeviceProperties(phys, &props);
   dev_limits = props.limits;

   /* Enumerate device extensions */
   uint32_t ext_count = 0;
   p_EnumerateDeviceExtensionProperties(phys, NULL, &ext_count, NULL);
   VkExtensionProperties *exts = calloc(ext_count, sizeof(*exts));
   if (ext_count > 0 && !exts)
      return 1;
   p_EnumerateDeviceExtensionProperties(phys, NULL, &ext_count, exts);

   int has_mirror_clamp_ext =
      has_extension(exts, ext_count, "VK_KHR_sampler_mirror_clamp_to_edge");
   int has_custom_border_ext =
      has_extension(exts, ext_count, "VK_EXT_custom_border_color");
   int has_non_seamless_ext =
      has_extension(exts, ext_count, "VK_EXT_non_seamless_cube_map");
   free(exts);

   /* Query features */
   VkPhysicalDeviceVulkan12Features supported_f12 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
   };
   VkPhysicalDeviceCustomBorderColorFeaturesEXT supported_cbc = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT,
   };
   VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT supported_nscm = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_NON_SEAMLESS_CUBE_MAP_FEATURES_EXT,
   };

   void **tail = &supported_f12.pNext;
   if (has_custom_border_ext) {
      *tail = &supported_cbc;
      tail = &supported_cbc.pNext;
   }
   if (has_non_seamless_ext) {
      *tail = &supported_nscm;
      tail = &supported_nscm.pNext;
   }

   /* VkPhysicalDeviceVulkan12Features is only valid on a 1.2+ device. */
   const bool api12 = props.apiVersion >= VK_API_VERSION_1_2;
   VkPhysicalDeviceFeatures2 supported_f2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = api12 ? (void *)&supported_f12 : supported_f12.pNext,
   };
   p_GetPhysicalDeviceFeatures2(phys, &supported_f2);

   bool feat_aniso = supported_f2.features.samplerAnisotropy != VK_FALSE;
   bool feat_mirror_clamp = supported_f12.samplerMirrorClampToEdge != VK_FALSE ||
                            has_mirror_clamp_ext;
   bool feat_custom_border = has_custom_border_ext &&
                             supported_cbc.customBorderColors != VK_FALSE;
   bool feat_custom_border_no_fmt = has_custom_border_ext &&
                                    supported_cbc.customBorderColorWithoutFormat != VK_FALSE;
   bool feat_cubearr = supported_f2.features.imageCubeArray != VK_FALSE;
   bool feat_non_seamless = has_non_seamless_ext &&
                            supported_nscm.nonSeamlessCubeMap != VK_FALSE;
   bool feat_gather_ext = supported_f2.features.shaderImageGatherExtended != VK_FALSE;

   printf("FEATURE samplerAnisotropy=%d\n", feat_aniso ? 1 : 0);
   printf("FEATURE samplerMirrorClampToEdge=%d\n", feat_mirror_clamp ? 1 : 0);
   printf("FEATURE customBorderColors=%d\n", feat_custom_border ? 1 : 0);
   printf("FEATURE customBorderColorWithoutFormat=%d\n", feat_custom_border_no_fmt ? 1 : 0);
   printf("FEATURE imageCubeArray=%d\n", feat_cubearr ? 1 : 0);
   printf("FEATURE nonSeamlessCubeMap=%d\n", feat_non_seamless ? 1 : 0);
   printf("FEATURE shaderImageGatherExtended=%d\n", feat_gather_ext ? 1 : 0);

   /* Queue family */
   uint32_t qn = 0;
   p_GetPhysicalDeviceQueueFamilyProperties(phys, &qn, NULL);
   VkQueueFamilyProperties *qp = calloc(qn, sizeof(*qp));
   if (qn > 0 && !qp)
      return 1;
   p_GetPhysicalDeviceQueueFamilyProperties(phys, &qn, qp);
   uint32_t qi = ~0u;
   for (uint32_t i = 0; i < qn; i++) {
      if (qp[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
         qi = i;
         break;
      }
   }
   free(qp);
   if (qi == ~0u) {
      printf("FAIL no compute queue\n");
      return 1;
   }

   p_GetPhysicalDeviceMemoryProperties(phys, &mem_props);

   /* Prepare device creation */
   const char *dev_exts[4];
   uint32_t dev_ext_count = 0;
   if (has_mirror_clamp_ext)
      dev_exts[dev_ext_count++] = "VK_KHR_sampler_mirror_clamp_to_edge";
   if (has_custom_border_ext)
      dev_exts[dev_ext_count++] = "VK_EXT_custom_border_color";
   if (has_non_seamless_ext)
      dev_exts[dev_ext_count++] = "VK_EXT_non_seamless_cube_map";

   VkPhysicalDeviceFeatures2 dev_feat2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
   };
   dev_feat2.features.samplerAnisotropy = supported_f2.features.samplerAnisotropy;
   dev_feat2.features.imageCubeArray = supported_f2.features.imageCubeArray;
   dev_feat2.features.shaderImageGatherExtended = supported_f2.features.shaderImageGatherExtended;

   VkPhysicalDeviceVulkan12Features dev_f12 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .samplerMirrorClampToEdge = supported_f12.samplerMirrorClampToEdge,
   };
   void **dev_tail = &dev_feat2.pNext;
   if (api12) {
      *dev_tail = &dev_f12;
      dev_tail = &dev_f12.pNext;
   }
   VkPhysicalDeviceCustomBorderColorFeaturesEXT dev_cbc = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT,
   };
   if (has_custom_border_ext) {
      dev_cbc.customBorderColors = supported_cbc.customBorderColors;
      dev_cbc.customBorderColorWithoutFormat = supported_cbc.customBorderColorWithoutFormat;
      *dev_tail = &dev_cbc;
      dev_tail = &dev_cbc.pNext;
   }

   VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT dev_nscm = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_NON_SEAMLESS_CUBE_MAP_FEATURES_EXT,
   };
   if (has_non_seamless_ext) {
      dev_nscm.nonSeamlessCubeMap = supported_nscm.nonSeamlessCubeMap;
      *dev_tail = &dev_nscm;
      dev_tail = &dev_nscm.pNext;
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

   if (p_CreateDevice(phys, &dci, NULL, &dev) != VK_SUCCESS) {
      printf("FAIL CreateDevice\n");
      printf("RESULT FAIL\n");
      return 1;
   }

#define LOAD_GD(n)                                                             \
   p_##n = (PFN_vk##n)p_GetDeviceProcAddr(dev, "vk" #n);                       \
   if (!p_##n)                                                                 \
      p_##n = (PFN_vk##n)gipa(inst, "vk" #n);                                  \
   if (!p_##n) {                                                               \
      printf("FAIL missing device vk" #n "\n");                                \
      return 1;                                                                \
   }
   GD_LIST(LOAD_GD)
#undef LOAD_GD

   p_GetDeviceQueue(dev, qi, 0, &queue);

   VkCommandPoolCreateInfo cpci = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = qi,
   };
   CK(p_CreateCommandPool(dev, &cpci, NULL, &cmd_pool), "CreateCommandPool");

   int passes = 0, fails = 0, skips = 0;

   /* Case 1: anisotropy */
   if (!feat_aniso) {
      printf("SKIP case anisotropy samplerAnisotropy not supported\n");
      skips++;
   } else {
      struct texture tex_aniso;
      if (make_texture(VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, 64, 64, 7, 1,
                       false, VK_IMAGE_VIEW_TYPE_2D, fill_aniso, &tex_aniso)) {
         printf("FAIL case anisotropy texture creation\n");
         fails++;
      } else {
         VkSamplerCreateInfo sci = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .minLod = 0.0f,
            .maxLod = 6.0f,
            .anisotropyEnable = VK_FALSE,
            .maxAnisotropy = 1.0f,
         };
         VkSampler s_iso = VK_NULL_HANDLE;
         CK(p_CreateSampler(dev, &sci, NULL, &s_iso), "CreateSampler");

         sci.anisotropyEnable = VK_TRUE;
         sci.maxAnisotropy = 2.0f;
         VkSampler s_aniso2 = VK_NULL_HANDLE;
         CK(p_CreateSampler(dev, &sci, NULL, &s_aniso2), "CreateSampler");

         sci.maxAnisotropy = fminf(16.0f, dev_limits.maxSamplerAnisotropy);
         VkSampler s_aniso16 = VK_NULL_HANDLE;
         CK(p_CreateSampler(dev, &sci, NULL, &s_aniso16), "CreateSampler");

         float in_sample[4] = { 0.5f, 0.5f, 8.0f / 64.0f, 1.0f / 64.0f };
         float out_iso[4], out_aniso2[4], out_aniso16[4];

         run_compute(s2d_spv, sizeof(s2d_spv), tex_aniso.view, s_iso,
                     in_sample, 1, 1, out_iso);
         run_compute(s2d_spv, sizeof(s2d_spv), tex_aniso.view, s_aniso2,
                     in_sample, 1, 1, out_aniso2);
         run_compute(s2d_spv, sizeof(s2d_spv), tex_aniso.view, s_aniso16,
                     in_sample, 1, 1, out_aniso16);

         float lod_iso = out_iso[0] * 255.0f / 32.0f;
         float lod_aniso2 = out_aniso2[0] * 255.0f / 32.0f;
         float lod_aniso16 = out_aniso16[0] * 255.0f / 32.0f;

         bool ok = true;
         if (fabsf(lod_iso - 3.0f) > 0.5f) ok = false;
         if (lod_aniso16 > lod_iso - 1.0f) ok = false;
         if (lod_aniso2 < lod_iso - 1.5f || lod_aniso2 > lod_iso - 0.5f) ok = false;

         if (ok) {
            printf("PASS case anisotropy lod_iso=%.2f lod_aniso2=%.2f lod_aniso16=%.2f\n",
                   lod_iso, lod_aniso2, lod_aniso16);
            passes++;
         } else {
            printf("FAIL case anisotropy lod_iso=%.2f lod_aniso2=%.2f lod_aniso16=%.2f\n",
                   lod_iso, lod_aniso2, lod_aniso16);
            fails++;
         }

         p_DestroySampler(dev, s_iso, NULL);
         p_DestroySampler(dev, s_aniso2, NULL);
         p_DestroySampler(dev, s_aniso16, NULL);
         destroy_texture(&tex_aniso);
      }
   }

   /* Case 2 & 3 share 4x1 texture */
   struct texture tex_4x1;
   bool has_tex_4x1 = (make_texture(VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
                                    4, 1, 1, 1, false, VK_IMAGE_VIEW_TYPE_2D,
                                    fill_4x1, &tex_4x1) == 0);

   /* Case 2: mirror_clamp_to_edge */
   if (!feat_mirror_clamp) {
      printf("SKIP case mirror_clamp_to_edge samplerMirrorClampToEdge not supported\n");
      skips++;
   } else if (!has_tex_4x1) {
      printf("FAIL case mirror_clamp_to_edge texture creation\n");
      fails++;
   } else {
      VkSamplerCreateInfo sci = {
         .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
         .magFilter = VK_FILTER_NEAREST,
         .minFilter = VK_FILTER_NEAREST,
         .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
         .addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE,
         .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         .minLod = 0.0f,
         .maxLod = 0.0f,
      };
      VkSampler sampler = VK_NULL_HANDLE;
      CK(p_CreateSampler(dev, &sci, NULL, &sampler), "CreateSampler");

      const float u_vals[9] = { -0.125f, -0.375f, -0.625f, -0.875f, -1.375f,
                                 0.125f,  0.625f,  1.375f,  2.125f };
      float inp[9 * 4];
      for (int i = 0; i < 9; i++) {
         inp[4 * i + 0] = u_vals[i];
         inp[4 * i + 1] = 0.5f;
         inp[4 * i + 2] = 0.0f;
         inp[4 * i + 3] = 0.0f;
      }
      float out[9 * 4];
      run_compute(s2d_spv, sizeof(s2d_spv), tex_4x1.view, sampler, inp, 9, 9, out);

      int match = 1;
      int mismatch_u = 0, got_r = 0, exp_r = 0;
      for (int k = 0; k < 9; k++) {
         int idx = (int)floorf(u_vals[k] * 4.0f);
         if (idx < 0) idx = -(1 + idx);
         if (idx < 0) idx = 0;
         if (idx > 3) idx = 3;
         int expected = 10 * (idx + 1);
         int got = (int)roundf(out[4 * k + 0] * 255.0f);
         if (got != expected) {
            if (match) {
               mismatch_u = k;
               got_r = got;
               exp_r = expected;
            }
            match = 0;
         }
      }

      if (match) {
         printf("PASS case mirror_clamp_to_edge all 9 samples match\n");
         passes++;
      } else {
         printf("FAIL case mirror_clamp_to_edge sample %d (u=%.3f) got %d expected %d\n",
                mismatch_u, u_vals[mismatch_u], got_r, exp_r);
         fails++;
      }
      p_DestroySampler(dev, sampler, NULL);
   }

   /* Case 3: border_color */
   const float u_border[3] = { -0.5f, 1.5f, 0.375f };
   float inp_border[3 * 4];
   for (int i = 0; i < 3; i++) {
      inp_border[4 * i + 0] = u_border[i];
      inp_border[4 * i + 1] = 0.5f;
      inp_border[4 * i + 2] = 0.0f;
      inp_border[4 * i + 3] = 0.0f;
   }

   /* 3a: border_opaque_white */
   if (!has_tex_4x1) {
      printf("FAIL case border_opaque_white texture creation\n");
      fails++;
   } else {
      VkSamplerCreateInfo sci = {
         .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
         .magFilter = VK_FILTER_NEAREST,
         .minFilter = VK_FILTER_NEAREST,
         .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
         .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
         .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
         .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
         .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
         .minLod = 0.0f,
         .maxLod = 0.0f,
      };
      VkSampler sampler = VK_NULL_HANDLE;
      CK(p_CreateSampler(dev, &sci, NULL, &sampler), "CreateSampler");

      float out[3 * 4];
      run_compute(s2d_spv, sizeof(s2d_spv), tex_4x1.view, sampler, inp_border, 3, 3, out);

      bool ok = true;
      /* Border samples 0 and 1: expect (1, 1, 1, 1) */
      for (int s = 0; s < 2; s++) {
         for (int c = 0; c < 4; c++) {
            if ((int)roundf(out[4 * s + c] * 255.0f) != 255)
               ok = false;
         }
      }
      /* Inside sample 2: expect R=20, G=0, B=0, A=255 */
      if ((int)roundf(out[8 + 0] * 255.0f) != 20 ||
          (int)roundf(out[8 + 1] * 255.0f) != 0 ||
          (int)roundf(out[8 + 2] * 255.0f) != 0 ||
          (int)roundf(out[8 + 3] * 255.0f) != 255)
         ok = false;

      if (ok) {
         printf("PASS case border_opaque_white border and inside correct\n");
         passes++;
      } else {
         printf("FAIL case border_opaque_white s0=(%d,%d,%d,%d) s2=(%d,%d,%d,%d)\n",
                (int)roundf(out[0] * 255.0f), (int)roundf(out[1] * 255.0f),
                (int)roundf(out[2] * 255.0f), (int)roundf(out[3] * 255.0f),
                (int)roundf(out[8] * 255.0f), (int)roundf(out[9] * 255.0f),
                (int)roundf(out[10] * 255.0f), (int)roundf(out[11] * 255.0f));
         fails++;
      }
      p_DestroySampler(dev, sampler, NULL);
   }

   /* 3b: border_custom */
   if (!feat_custom_border) {
      printf("SKIP case border_custom customBorderColors not supported\n");
      skips++;
   } else if (!has_tex_4x1) {
      printf("FAIL case border_custom texture creation\n");
      fails++;
   } else {
      VkSamplerCustomBorderColorCreateInfoEXT cbc = {
         .sType = VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT,
         .customBorderColor = { .float32 = { 0.25f, 0.5f, 0.75f, 0.125f } },
         .format = VK_FORMAT_R8G8B8A8_UNORM,
      };
      VkSamplerCreateInfo sci = {
         .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
         .pNext = &cbc,
         .magFilter = VK_FILTER_NEAREST,
         .minFilter = VK_FILTER_NEAREST,
         .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
         .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
         .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
         .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
         .borderColor = VK_BORDER_COLOR_FLOAT_CUSTOM_EXT,
         .minLod = 0.0f,
         .maxLod = 0.0f,
      };
      VkSampler sampler = VK_NULL_HANDLE;
      CK(p_CreateSampler(dev, &sci, NULL, &sampler), "CreateSampler");

      float out[3 * 4];
      run_compute(s2d_spv, sizeof(s2d_spv), tex_4x1.view, sampler, inp_border, 3, 3, out);

      bool ok = true;
      const float exp_cbc[4] = { 0.25f, 0.5f, 0.75f, 0.125f };
      for (int s = 0; s < 2; s++) {
         for (int c = 0; c < 4; c++) {
            if (fabsf(out[4 * s + c] - exp_cbc[c]) > 1.5f / 255.0f)
               ok = false;
         }
      }
      if ((int)roundf(out[8 + 0] * 255.0f) != 20 ||
          (int)roundf(out[8 + 1] * 255.0f) != 0 ||
          (int)roundf(out[8 + 2] * 255.0f) != 0 ||
          (int)roundf(out[8 + 3] * 255.0f) != 255)
         ok = false;

      if (ok) {
         printf("PASS case border_custom custom border match\n");
         passes++;
      } else {
         printf("FAIL case border_custom s0=(%.3f,%.3f,%.3f,%.3f) exp=(0.25,0.50,0.75,0.125)\n",
                out[0], out[1], out[2], out[3]);
         fails++;
      }
      p_DestroySampler(dev, sampler, NULL);
   }

   /* 3c: border_custom_no_format */
   if (!feat_custom_border_no_fmt) {
      printf("SKIP case border_custom_no_format customBorderColorWithoutFormat not supported\n");
      skips++;
   } else if (!has_tex_4x1) {
      printf("FAIL case border_custom_no_format texture creation\n");
      fails++;
   } else {
      VkSamplerCustomBorderColorCreateInfoEXT cbc = {
         .sType = VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT,
         .customBorderColor = { .float32 = { 0.25f, 0.5f, 0.75f, 0.125f } },
         .format = VK_FORMAT_UNDEFINED,
      };
      VkSamplerCreateInfo sci = {
         .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
         .pNext = &cbc,
         .magFilter = VK_FILTER_NEAREST,
         .minFilter = VK_FILTER_NEAREST,
         .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
         .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
         .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
         .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
         .borderColor = VK_BORDER_COLOR_FLOAT_CUSTOM_EXT,
         .minLod = 0.0f,
         .maxLod = 0.0f,
      };
      VkSampler sampler = VK_NULL_HANDLE;
      CK(p_CreateSampler(dev, &sci, NULL, &sampler), "CreateSampler");

      float out[3 * 4];
      run_compute(s2d_spv, sizeof(s2d_spv), tex_4x1.view, sampler, inp_border, 3, 3, out);

      bool ok = true;
      const float exp_cbc[4] = { 0.25f, 0.5f, 0.75f, 0.125f };
      for (int s = 0; s < 2; s++) {
         for (int c = 0; c < 4; c++) {
            if (fabsf(out[4 * s + c] - exp_cbc[c]) > 1.5f / 255.0f)
               ok = false;
         }
      }
      if ((int)roundf(out[8 + 0] * 255.0f) != 20 ||
          (int)roundf(out[8 + 1] * 255.0f) != 0 ||
          (int)roundf(out[8 + 2] * 255.0f) != 0 ||
          (int)roundf(out[8 + 3] * 255.0f) != 255)
         ok = false;

      if (ok) {
         printf("PASS case border_custom_no_format format=UNDEFINED match\n");
         passes++;
      } else {
         printf("FAIL case border_custom_no_format s0=(%.3f,%.3f,%.3f,%.3f) exp=(0.25,0.50,0.75,0.125)\n",
                out[0], out[1], out[2], out[3]);
         fails++;
      }
      p_DestroySampler(dev, sampler, NULL);
   }

   if (has_tex_4x1)
      destroy_texture(&tex_4x1);

   /* Case 4: cube_array */
   if (!feat_cubearr) {
      printf("SKIP case cube_array imageCubeArray not supported\n");
      skips++;
   } else {
      struct texture tex_cubearr;
      if (make_texture(VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, 2, 2, 1, 12,
                       true, VK_IMAGE_VIEW_TYPE_CUBE_ARRAY, fill_cubearr, &tex_cubearr)) {
         printf("FAIL case cube_array texture creation\n");
         fails++;
      } else {
         VkSamplerCreateInfo sci = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_NEAREST,
            .minFilter = VK_FILTER_NEAREST,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .minLod = 0.0f,
            .maxLod = 0.0f,
         };
         VkSampler sampler = VK_NULL_HANDLE;
         CK(p_CreateSampler(dev, &sci, NULL, &sampler), "CreateSampler");

         const float face_dirs[6][3] = {
            {  1.0f,  0.0f,  0.0f }, /* +X */
            { -1.0f,  0.0f,  0.0f }, /* -X */
            {  0.0f,  1.0f,  0.0f }, /* +Y */
            {  0.0f, -1.0f,  0.0f }, /* -Y */
            {  0.0f,  0.0f,  1.0f }, /* +Z */
            {  0.0f,  0.0f, -1.0f }, /* -Z */
         };
         float inp[12 * 4];
         for (int c = 0; c < 2; c++) {
            for (int f = 0; f < 6; f++) {
               int idx = c * 6 + f;
               inp[4 * idx + 0] = face_dirs[f][0];
               inp[4 * idx + 1] = face_dirs[f][1];
               inp[4 * idx + 2] = face_dirs[f][2];
               inp[4 * idx + 3] = (float)c;
            }
         }

         float out[12 * 4];
         run_compute(cubearr_spv, sizeof(cubearr_spv), tex_cubearr.view, sampler,
                     inp, 12, 12, out);

         int match = 1;
         int mismatch_idx = 0, got_r = 0, exp_r = 0;
         for (int c = 0; c < 2; c++) {
            for (int f = 0; f < 6; f++) {
               int idx = c * 6 + f;
               int expected = 20 * (6 * c + f) + 5;
               int got = (int)roundf(out[4 * idx + 0] * 255.0f);
               if (got != expected) {
                  if (match) {
                     mismatch_idx = idx;
                     got_r = got;
                     exp_r = expected;
                  }
                  match = 0;
               }
            }
         }

         if (match) {
            printf("PASS case cube_array all 12 cube layer faces match\n");
            passes++;
         } else {
            printf("FAIL case cube_array sample %d got %d expected %d\n",
                   mismatch_idx, got_r, exp_r);
            fails++;
         }

         p_DestroySampler(dev, sampler, NULL);
         destroy_texture(&tex_cubearr);
      }
   }

   /* Case 5 & 6 share cube texture */
   struct texture tex_cube;
   bool has_tex_cube = (make_texture(VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
                                     2, 2, 1, 6, true, VK_IMAGE_VIEW_TYPE_CUBE,
                                     fill_cube_seamless, &tex_cube) == 0);

   /* Case 5: cube_seamless (core) */
   if (!has_tex_cube) {
      printf("FAIL case cube_seamless texture creation\n");
      fails++;
   } else {
      VkSamplerCreateInfo sci = {
         .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
         .magFilter = VK_FILTER_LINEAR,
         .minFilter = VK_FILTER_LINEAR,
         .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
         .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         .minLod = 0.0f,
         .maxLod = 0.0f,
      };
      VkSampler sampler = VK_NULL_HANDLE;
      CK(p_CreateSampler(dev, &sci, NULL, &sampler), "CreateSampler");

      float inp[2 * 4] = {
         1.0f, 0.0f, 0.0f, 0.0f,
         1.0f, 0.99f, 0.0f, 0.0f,
      };
      float out[2 * 4];
      run_compute(cube_spv, sizeof(cube_spv), tex_cube.view, sampler, inp, 2, 2, out);

      int r0 = (int)roundf(out[0] * 255.0f);
      int r1 = (int)roundf(out[4] * 255.0f);

      bool ok = (abs(r0 - 10) <= 1) && (r1 >= 25 && r1 <= 55);
      if (ok) {
         printf("PASS case cube_seamless r0=%d r1=%d (seamless blend)\n", r0, r1);
         passes++;
      } else {
         printf("FAIL case cube_seamless got r0=%d (exp ~10) r1=%d (exp 25..55)\n",
                r0, r1);
         fails++;
      }
      p_DestroySampler(dev, sampler, NULL);
   }

   /* Case 6: cube_non_seamless */
   if (!feat_non_seamless) {
      printf("SKIP case cube_non_seamless nonSeamlessCubeMap not supported\n");
      skips++;
   } else if (!has_tex_cube) {
      printf("FAIL case cube_non_seamless texture creation\n");
      fails++;
   } else {
      VkSamplerCreateInfo sci = {
         .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
         .flags = VK_SAMPLER_CREATE_NON_SEAMLESS_CUBE_MAP_BIT_EXT,
         .magFilter = VK_FILTER_LINEAR,
         .minFilter = VK_FILTER_LINEAR,
         .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
         .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         .minLod = 0.0f,
         .maxLod = 0.0f,
      };
      VkSampler sampler = VK_NULL_HANDLE;
      CK(p_CreateSampler(dev, &sci, NULL, &sampler), "CreateSampler");

      float inp[2 * 4] = {
         1.0f, 0.0f, 0.0f, 0.0f,
         1.0f, 0.99f, 0.0f, 0.0f,
      };
      float out[2 * 4];
      run_compute(cube_spv, sizeof(cube_spv), tex_cube.view, sampler, inp, 2, 2, out);

      int r0 = (int)roundf(out[0] * 255.0f);
      int r1 = (int)roundf(out[4] * 255.0f);

      bool ok = (abs(r0 - 10) <= 2) && (abs(r1 - 10) <= 2);
      if (ok) {
         printf("PASS case cube_non_seamless r0=%d r1=%d\n", r0, r1);
         passes++;
      } else {
         printf("FAIL case cube_non_seamless got r0=%d r1=%d (both exp ~10)\n",
                r0, r1);
         fails++;
      }
      p_DestroySampler(dev, sampler, NULL);
   }

   if (has_tex_cube)
      destroy_texture(&tex_cube);

   /* Case 7 & 8 share 8x8 R8_UNORM texture */
   struct texture tex_8x8;
   bool has_tex_8x8 = (make_texture(VK_IMAGE_TYPE_2D, VK_FORMAT_R8_UNORM,
                                    8, 8, 1, 1, false, VK_IMAGE_VIEW_TYPE_2D,
                                    fill_gather_8x8, &tex_8x8) == 0);

   /* Case 7: gather (core) */
   if (!has_tex_8x8) {
      printf("FAIL case gather texture creation\n");
      fails++;
   } else {
      VkSamplerCreateInfo sci = {
         .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
         .magFilter = VK_FILTER_NEAREST,
         .minFilter = VK_FILTER_NEAREST,
         .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
         .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         .minLod = 0.0f,
         .maxLod = 0.0f,
      };
      VkSampler sampler = VK_NULL_HANDLE;
      CK(p_CreateSampler(dev, &sci, NULL, &sampler), "CreateSampler");

      float inp[2 * 4] = {
         2.75f / 8.0f, 2.75f / 8.0f, 0.0f, 0.0f,
         4.75f / 8.0f, 5.75f / 8.0f, 0.0f, 0.0f,
      };
      float out[4 * 4];
      run_compute(gather_spv, sizeof(gather_spv), tex_8x8.view, sampler, inp, 2, 4, out);

      bool match = true;
      for (int i = 0; i < 2; i++) {
         float u = inp[4 * i + 0];
         float v = inp[4 * i + 1];
         int i0 = (int)floorf(u * 8.0f - 0.5f);
         int j0 = (int)floorf(v * 8.0f - 0.5f);

         int exp_g0[4] = {
            texel_8x8(i0, j0 + 1),
            texel_8x8(i0 + 1, j0 + 1),
            texel_8x8(i0 + 1, j0),
            texel_8x8(i0, j0)
         };
         int exp_g1[4] = {
            texel_8x8(i0 + 1, j0),
            texel_8x8(i0 + 2, j0),
            texel_8x8(i0 + 2, j0 - 1),
            texel_8x8(i0 + 1, j0 - 1)
         };

         for (int c = 0; c < 4; c++) {
            int got0 = (int)roundf(out[(2 * i + 0) * 4 + c] * 255.0f);
            int got1 = (int)roundf(out[(2 * i + 1) * 4 + c] * 255.0f);
            if (got0 != exp_g0[c] || got1 != exp_g1[c]) {
               match = false;
               printf("FAIL case gather mismatch sample %d comp %d got (%d,%d) exp (%d,%d)\n",
                      i, c, got0, got1, exp_g0[c], exp_g1[c]);
               break;
            }
         }
         if (!match) break;
      }

      if (match) {
         printf("PASS case gather all 4 gather results exact\n");
         passes++;
      } else {
         fails++;
      }
      p_DestroySampler(dev, sampler, NULL);
   }

   /* Case 8: gather_offsets */
   if (!feat_gather_ext) {
      printf("SKIP case gather_offsets shaderImageGatherExtended not supported\n");
      skips++;
   } else if (!has_tex_8x8) {
      printf("FAIL case gather_offsets texture creation\n");
      fails++;
   } else {
      VkSamplerCreateInfo sci = {
         .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
         .magFilter = VK_FILTER_NEAREST,
         .minFilter = VK_FILTER_NEAREST,
         .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
         .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         .minLod = 0.0f,
         .maxLod = 0.0f,
      };
      VkSampler sampler = VK_NULL_HANDLE;
      CK(p_CreateSampler(dev, &sci, NULL, &sampler), "CreateSampler");

      float inp[2 * 4] = {
         2.75f / 8.0f, 2.75f / 8.0f, -1.0f, 2.0f,
         4.75f / 8.0f, 5.75f / 8.0f, -1.0f, 2.0f,
      };
      float out[4 * 4];
      run_compute(gather_ext_spv, sizeof(gather_ext_spv), tex_8x8.view, sampler,
                  inp, 2, 4, out);

      const int off_x[4] = { 0, 1, -2, -1 };
      const int off_y[4] = { 0, 0,  1, -2 };

      bool match = true;
      for (int i = 0; i < 2; i++) {
         float u = inp[4 * i + 0];
         float v = inp[4 * i + 1];
         int i0 = (int)floorf(u * 8.0f - 0.5f);
         int j0 = (int)floorf(v * 8.0f - 0.5f);

         int exp_offsets[4];
         for (int k = 0; k < 4; k++)
            exp_offsets[k] = texel_8x8(i0 + off_x[k], j0 + off_y[k]);

         int exp_dyn[4] = {
            texel_8x8(i0 - 1, j0 + 3),
            texel_8x8(i0,     j0 + 3),
            texel_8x8(i0,     j0 + 2),
            texel_8x8(i0 - 1, j0 + 2)
         };

         for (int c = 0; c < 4; c++) {
            int got0 = (int)roundf(out[(2 * i + 0) * 4 + c] * 255.0f);
            int got1 = (int)roundf(out[(2 * i + 1) * 4 + c] * 255.0f);
            if (got0 != exp_offsets[c] || got1 != exp_dyn[c]) {
               match = false;
               printf("FAIL case gather_offsets mismatch sample %d comp %d got (%d,%d) exp (%d,%d)\n",
                      i, c, got0, got1, exp_offsets[c], exp_dyn[c]);
               break;
            }
         }
         if (!match) break;
      }

      if (match) {
         printf("PASS case gather_offsets all components exact\n");
         passes++;
      } else {
         fails++;
      }
      p_DestroySampler(dev, sampler, NULL);
   }

   if (has_tex_8x8)
      destroy_texture(&tex_8x8);

   (void)passes;
   p_DestroyCommandPool(dev, cmd_pool, NULL);
   p_DestroyDevice(dev, NULL);
   p_DestroyInstance(inst, NULL);
   dlclose(h);

   if (fails > 0) {
      printf("RESULT FAIL\n");
      return 1;
   }
   if (passes == 0 && skips > 0) {
      printf("RESULT SKIP\n");
      return 0;
   }
   printf("RESULT PASS\n");
   return 0;
}

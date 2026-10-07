/* Standalone Vulkan robustness2 test program.
 * Tests VK_EXT_robustness2 / VK_KHR_robustness2 features:
 *   - nullDescriptor (compute descriptor set and vertex buffer)
 *   - robustBufferAccess2 (SSBO/UBO OOB loads, SSBO OOB stores and atomicAdd)
 *
 * Usage: robustness2 <path-to-libvulkan_panfrost.so>
 */
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if __has_include("../dx7_harness.h")
#include "../dx7_harness.h"
#else
#include "dx7_harness.h"
#endif

#include "robustness2_spv.h"

#define EXTRA_FUNCS(X) \
   X(CreateComputePipelines) \
   X(CmdDispatch) \
   X(CreateDescriptorSetLayout) \
   X(DestroyDescriptorSetLayout) \
   X(DestroyPipelineLayout) \
   X(CreateDescriptorPool) \
   X(DestroyDescriptorPool) \
   X(AllocateDescriptorSets) \
   X(UpdateDescriptorSets) \
   X(CmdBindDescriptorSets) \
   X(CreateSampler) \
   X(DestroySampler) \
   X(DestroyBuffer) \
   X(FreeMemory) \
   X(QueueWaitIdle)

EXTRA_FUNCS(DX7_DECL)

static PFN_vkGetInstanceProcAddr gipa;
static VkPhysicalDeviceRobustness2FeaturesEXT supported_rob2;
static VkPhysicalDeviceRobustness2PropertiesEXT props_rob2;
static const char *s_ext_name = NULL;

static void
device_hook(struct dx7 *t, VkDeviceCreateInfo *dci)
{
   PFN_vkEnumerateDeviceExtensionProperties p_EnumerateDeviceExtensionProperties =
      (PFN_vkEnumerateDeviceExtensionProperties)gipa(t->inst, "vkEnumerateDeviceExtensionProperties");
   PFN_vkGetPhysicalDeviceFeatures2 p_GetPhysicalDeviceFeatures2 =
      (PFN_vkGetPhysicalDeviceFeatures2)gipa(t->inst, "vkGetPhysicalDeviceFeatures2");
   if (!p_GetPhysicalDeviceFeatures2)
      p_GetPhysicalDeviceFeatures2 =
         (PFN_vkGetPhysicalDeviceFeatures2)gipa(t->inst, "vkGetPhysicalDeviceFeatures2KHR");
   PFN_vkGetPhysicalDeviceProperties2 p_GetPhysicalDeviceProperties2 =
      (PFN_vkGetPhysicalDeviceProperties2)gipa(t->inst, "vkGetPhysicalDeviceProperties2");
   if (!p_GetPhysicalDeviceProperties2)
      p_GetPhysicalDeviceProperties2 =
         (PFN_vkGetPhysicalDeviceProperties2)gipa(t->inst, "vkGetPhysicalDeviceProperties2KHR");

   if (!p_EnumerateDeviceExtensionProperties || !p_GetPhysicalDeviceFeatures2 || !p_GetPhysicalDeviceProperties2) {
      printf("FAIL missing instance entry points\n");
      exit(1);
   }

   uint32_t ext_count = 0;
   CK(p_EnumerateDeviceExtensionProperties(t->phys, NULL, &ext_count, NULL), "enum exts count");
   VkExtensionProperties *exts = calloc(ext_count, sizeof(*exts));
   if (ext_count > 0 && !exts) {
      printf("FAIL out of memory allocating exts\n");
      exit(1);
   }
   CK(p_EnumerateDeviceExtensionProperties(t->phys, NULL, &ext_count, exts), "enum exts");

   int has_ext = 0, has_khr = 0;
   for (uint32_t i = 0; i < ext_count; i++) {
      if (!strcmp(exts[i].extensionName, "VK_EXT_robustness2"))
         has_ext = 1;
      if (!strcmp(exts[i].extensionName, "VK_KHR_robustness2"))
         has_khr = 1;
   }
   free(exts);

   if (has_ext) {
      s_ext_name = "VK_EXT_robustness2";
   } else if (has_khr) {
      s_ext_name = "VK_KHR_robustness2";
   } else {
      printf("SKIP robustness2 extension missing\n");
      printf("RESULT SKIP\n");
      exit(0);
   }

   supported_rob2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;
   VkPhysicalDeviceFeatures2 feat2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &supported_rob2,
   };
   p_GetPhysicalDeviceFeatures2(t->phys, &feat2);

   props_rob2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_PROPERTIES_EXT;
   VkPhysicalDeviceProperties2 prop2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
      .pNext = &props_rob2,
   };
   p_GetPhysicalDeviceProperties2(t->phys, &prop2);

   printf("INFO ext=%s robustBufferAccess2=%d robustImageAccess2=%d nullDescriptor=%d robustStorageBufferAccessSizeAlignment=%llu robustUniformBufferAccessSizeAlignment=%llu\n",
          s_ext_name,
          supported_rob2.robustBufferAccess2,
          supported_rob2.robustImageAccess2,
          supported_rob2.nullDescriptor,
          (unsigned long long)props_rob2.robustStorageBufferAccessSizeAlignment,
          (unsigned long long)props_rob2.robustUniformBufferAccessSizeAlignment);

   static VkPhysicalDeviceFeatures2 dev_feat2;
   static VkPhysicalDeviceRobustness2FeaturesEXT dev_rob2;
   static const char *enabled_exts[1];

   dev_rob2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;
   dev_rob2.robustBufferAccess2 = supported_rob2.robustBufferAccess2;
   dev_rob2.robustImageAccess2 = supported_rob2.robustImageAccess2;
   dev_rob2.nullDescriptor = supported_rob2.nullDescriptor;
   dev_rob2.pNext = NULL;

   dev_feat2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
   dev_feat2.features.robustBufferAccess = VK_TRUE;
   dev_feat2.pNext = &dev_rob2;

   enabled_exts[0] = s_ext_name;

   dci->pNext = &dev_feat2;
   dci->pEnabledFeatures = NULL;
   dci->enabledExtensionCount = 1;
   dci->ppEnabledExtensionNames = enabled_exts;
}

static void
load_extra_funcs(struct dx7 *t, const char *icd)
{
   void *h = dlopen(icd, RTLD_NOW | RTLD_LOCAL);
   if (!h) {
      printf("FAIL dlopen %s\n", dlerror());
      exit(1);
   }
   PFN_vkGetInstanceProcAddr p_gipa =
      (PFN_vkGetInstanceProcAddr)dlsym(h, "vk_icdGetInstanceProcAddr");
   if (!p_gipa)
      p_gipa = (PFN_vkGetInstanceProcAddr)dlsym(h, "vkGetInstanceProcAddr");
   if (!p_gipa) {
      printf("FAIL missing gipa\n");
      exit(1);
   }

   PFN_vkGetDeviceProcAddr gdpa =
      (PFN_vkGetDeviceProcAddr)p_gipa(t->inst, "vkGetDeviceProcAddr");

#define LOAD_EXTRA(n) \
   if (gdpa) \
      vk##n = (PFN_vk##n)gdpa(t->dev, "vk" #n); \
   if (!vk##n) \
      vk##n = (PFN_vk##n)p_gipa(t->inst, "vk" #n); \
   if (!vk##n) { \
      printf("FAIL missing vk" #n "\n"); \
      exit(1); \
   }
   EXTRA_FUNCS(LOAD_EXTRA)
#undef LOAD_EXTRA
}

static VkPipeline
create_compute_pipe(struct dx7 *t, const uint32_t *spv, size_t spv_bytes, VkPipelineLayout layout)
{
   VkShaderModule sm = dx7_module(t, spv, spv_bytes);
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
   VkPipeline pipe = VK_NULL_HANDLE;
   CK(vkCreateComputePipelines(t->dev, VK_NULL_HANDLE, 1, &cpci, NULL, &pipe), "CreateComputePipelines");
   vkDestroyShaderModule(t->dev, sm, NULL);
   return pipe;
}

static void
host_read_barrier(struct dx7 *t)
{
   VkMemoryBarrier barrier = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
   };
   vkCmdPipelineBarrier(t->cmd,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
}

static void
test_null_descriptors(struct dx7 *t, int *passes, int *fails, int *skips)
{
   if (!supported_rob2.nullDescriptor) {
      printf("SKIP case null_descriptors nullDescriptor missing\n");
      (*skips)++;
      return;
   }

   VkSamplerCreateInfo sci = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = VK_FILTER_NEAREST,
      .minFilter = VK_FILTER_NEAREST,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
   };
   VkSampler sampler = VK_NULL_HANDLE;
   CK(vkCreateSampler(t->dev, &sci, NULL, &sampler), "CreateSampler");

   VkBuffer out_buf = VK_NULL_HANDLE;
   VkDeviceMemory out_mem = VK_NULL_HANDLE;
   dx7_buffer(t, 256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, NULL, &out_buf, &out_mem);

   uint32_t *map = NULL;
   CK(vkMapMemory(t->dev, out_mem, 0, 256, 0, (void **)&map), "MapOut");
   for (int i = 0; i < 64; i++)
      map[i] = 0xDEADBEEFu;
   vkUnmapMemory(t->dev, out_mem);

   VkDescriptorSetLayoutBinding bindings[6] = {
      { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
      { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
      { .binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
      { .binding = 3, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
      { .binding = 4, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
      { .binding = 5, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
   };
   VkDescriptorSetLayoutCreateInfo dslci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 6,
      .pBindings = bindings,
   };
   VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
   CK(vkCreateDescriptorSetLayout(t->dev, &dslci, NULL, &dsl), "CreateDSL");

   VkPipelineLayoutCreateInfo plci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &dsl,
   };
   VkPipelineLayout playout = VK_NULL_HANDLE;
   CK(vkCreatePipelineLayout(t->dev, &plci, NULL, &playout), "CreatePipelineLayout");

   VkDescriptorPoolSize pool_sizes[5] = {
      { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1 },
      { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 2 },
      { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1 },
      { .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 1 },
      { .type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, .descriptorCount = 1 },
   };
   VkDescriptorPoolCreateInfo dpci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1,
      .poolSizeCount = 5,
      .pPoolSizes = pool_sizes,
   };
   VkDescriptorPool dpool = VK_NULL_HANDLE;
   CK(vkCreateDescriptorPool(t->dev, &dpci, NULL, &dpool), "CreateDP");

   VkDescriptorSetAllocateInfo dsai = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = dpool,
      .descriptorSetCount = 1,
      .pSetLayouts = &dsl,
   };
   VkDescriptorSet dset = VK_NULL_HANDLE;
   CK(vkAllocateDescriptorSets(t->dev, &dsai, &dset), "AllocDS");

   VkDescriptorBufferInfo dbi0 = {
      .buffer = VK_NULL_HANDLE,
      .offset = 0,
      .range = VK_WHOLE_SIZE,
   };
   VkDescriptorBufferInfo dbi1 = {
      .buffer = VK_NULL_HANDLE,
      .offset = 0,
      .range = VK_WHOLE_SIZE,
   };
   VkDescriptorImageInfo dii2 = {
      .sampler = sampler,
      .imageView = VK_NULL_HANDLE,
      .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
   };
   VkDescriptorImageInfo dii3 = {
      .sampler = VK_NULL_HANDLE,
      .imageView = VK_NULL_HANDLE,
      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
   };
   VkBufferView null_bview = VK_NULL_HANDLE;
   VkDescriptorBufferInfo dbi5 = {
      .buffer = out_buf,
      .offset = 0,
      .range = 256,
   };

   VkWriteDescriptorSet writes[6] = {
      { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = dset, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &dbi0 },
      { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = dset, .dstBinding = 1, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &dbi1 },
      { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = dset, .dstBinding = 2, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &dii2 },
      { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = dset, .dstBinding = 3, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo = &dii3 },
      { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = dset, .dstBinding = 4, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, .pTexelBufferView = &null_bview },
      { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = dset, .dstBinding = 5, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &dbi5 },
   };
   vkUpdateDescriptorSets(t->dev, 6, writes, 0, NULL);

   VkPipeline pipe = create_compute_pipe(t, null_desc_spv, sizeof(null_desc_spv), playout);

   CK(vkResetFences(t->dev, 1, &t->fence), "ResetFence");
   CK(vkResetCommandBuffer(t->cmd, 0), "ResetCmd");
   VkCommandBufferBeginInfo bbi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
   };
   CK(vkBeginCommandBuffer(t->cmd, &bbi), "BeginCmd");
   vkCmdBindPipeline(t->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
   vkCmdBindDescriptorSets(t->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &dset, 0, NULL);
   vkCmdDispatch(t->cmd, 1, 1, 1);
   host_read_barrier(t);
   CK(vkEndCommandBuffer(t->cmd), "EndCmd");

   VkSubmitInfo si = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers = &t->cmd,
   };
   CK(vkQueueSubmit(t->queue, 1, &si, t->fence), "QueueSubmit");
   CK(vkWaitForFences(t->dev, 1, &t->fence, VK_TRUE, 30ull * 1000000000ull), "WaitForFences");

   uint32_t res[64];
   CK(vkMapMemory(t->dev, out_mem, 0, 256, 0, (void **)&map), "MapOutRead");
   memcpy(res, map, sizeof(res));
   vkUnmapMemory(t->dev, out_mem);

   int not_written = 0;
   for (int i = 0; i < 17; i++) {
      if (res[i] == 0xDEADBEEFu)
         not_written = 1;
   }

   float a_samp = 0.0f, a_img = 0.0f, a_txb = 0.0f;
   memcpy(&a_samp, &res[8], sizeof(float));
   memcpy(&a_img, &res[12], sizeof(float));
   memcpy(&a_txb, &res[16], sizeof(float));

   printf("INFO null_descriptors alpha: sampler=%.1f image=%.1f texel_buf=%.1f\n",
          a_samp, a_img, a_txb);

   if (not_written) {
      printf("FAIL case null_descriptors output not written (sentinel 0xDEADBEEF present)\n");
      (*fails)++;
   } else if (res[0] != 0 || res[1] != 0 || res[2] != 0 || res[3] != 0 || res[4] != 0) {
      printf("FAIL case null_descriptors buffer read non-zero: ubo=[0x%08x,0x%08x,0x%08x,0x%08x] ssbo=0x%08x\n",
             res[0], res[1], res[2], res[3], res[4]);
      (*fails)++;
   } else if (res[5] != 0 || res[6] != 0 || res[7] != 0 || res[8] != 0 ||
              res[9] != 0 || res[10] != 0 || res[11] != 0 || res[12] != 0 ||
              res[13] != 0 || res[14] != 0 || res[15] != 0 || res[16] != 0) {
      printf("FAIL case null_descriptors bad image read: samp_rgb=[0x%x,0x%x,0x%x] a_samp=%.1f img_rgb=[0x%x,0x%x,0x%x] a_img=%.1f txb_rgb=[0x%x,0x%x,0x%x] a_txb=%.1f\n",
             res[5], res[6], res[7], a_samp,
             res[9], res[10], res[11], a_img,
             res[13], res[14], res[15], a_txb);
      (*fails)++;
   } else {
      printf("PASS case null_descriptors\n");
      (*passes)++;
   }

   vkDestroyPipeline(t->dev, pipe, NULL);
   vkDestroyPipelineLayout(t->dev, playout, NULL);
   vkDestroyDescriptorPool(t->dev, dpool, NULL);
   vkDestroyDescriptorSetLayout(t->dev, dsl, NULL);
   vkDestroySampler(t->dev, sampler, NULL);
   vkDestroyBuffer(t->dev, out_buf, NULL);
   vkFreeMemory(t->dev, out_mem, NULL);
}

static void
test_oob_cases(struct dx7 *t, int *passes, int *fails, int *skips)
{
   if (!supported_rob2.robustBufferAccess2) {
      printf("SKIP case oob_ssbo_load robustBufferAccess2 missing\n");
      printf("SKIP case oob_ubo_load robustBufferAccess2 missing\n");
      printf("SKIP case oob_ssbo_store robustBufferAccess2 missing\n");
      *skips += 3;
      return;
   }

   VkDescriptorSetLayoutBinding bindings[3] = {
      { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
      { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
      { .binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
   };
   VkDescriptorSetLayoutCreateInfo dslci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 3,
      .pBindings = bindings,
   };
   VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
   CK(vkCreateDescriptorSetLayout(t->dev, &dslci, NULL, &dsl), "CreateDSLOOB");

   VkPushConstantRange pcr = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .offset = 0,
      .size = 8,
   };
   VkPipelineLayoutCreateInfo plci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &dsl,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &pcr,
   };
   VkPipelineLayout playout = VK_NULL_HANDLE;
   CK(vkCreatePipelineLayout(t->dev, &plci, NULL, &playout), "CreatePipelineLayoutOOB");

   VkDescriptorPoolSize pool_sizes[2] = {
      { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1 },
      { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 2 },
   };
   VkDescriptorPoolCreateInfo dpci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1,
      .poolSizeCount = 2,
      .pPoolSizes = pool_sizes,
   };
   VkDescriptorPool dpool = VK_NULL_HANDLE;
   CK(vkCreateDescriptorPool(t->dev, &dpci, NULL, &dpool), "CreateDPOOB");

   VkDescriptorSetAllocateInfo dsai = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = dpool,
      .descriptorSetCount = 1,
      .pSetLayouts = &dsl,
   };
   VkDescriptorSet dset = VK_NULL_HANDLE;
   CK(vkAllocateDescriptorSets(t->dev, &dsai, &dset), "AllocDSOOB");

   VkPipeline pipe = create_compute_pipe(t, oob_spv, sizeof(oob_spv), playout);

   VkBuffer test_buf = VK_NULL_HANDLE;
   VkDeviceMemory test_mem = VK_NULL_HANDLE;
   dx7_buffer(t, 4096,
              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
              NULL, &test_buf, &test_mem);

   VkBuffer out_buf = VK_NULL_HANDLE;
   VkDeviceMemory out_mem = VK_NULL_HANDLE;
   const VkDeviceSize out_size = 257 * sizeof(uint32_t);
   dx7_buffer(t, out_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, NULL, &out_buf, &out_mem);

   /* ──────────────────────────────────────────────────────────────────────────
    * Case 2a: oob_ssbo_load
    * ────────────────────────────────────────────────────────────────────────── */
   {
      VkDeviceSize s_align = props_rob2.robustStorageBufferAccessSizeAlignment;
      VkDeviceSize ssbo_range = 16;
      if (s_align > ssbo_range)
         ssbo_range = s_align;
      if (s_align > 0 && (ssbo_range % s_align) != 0)
         ssbo_range = ((ssbo_range + s_align - 1) / s_align) * s_align;

      uint32_t *map = NULL;
      CK(vkMapMemory(t->dev, test_mem, 0, 4096, 0, (void **)&map), "MapTestBufSSBOLoad");
      for (int i = 0; i < 4096 / 4; i++)
         map[i] = 0xABABABABu;
      vkUnmapMemory(t->dev, test_mem);

      CK(vkMapMemory(t->dev, out_mem, 0, out_size, 0, (void **)&map), "MapOutSSBOLoad");
      for (int i = 0; i < 257; i++)
         map[i] = 0xDEADBEEFu;
      vkUnmapMemory(t->dev, out_mem);

      VkDescriptorBufferInfo dbi0 = { .buffer = test_buf, .offset = 0, .range = 256 };
      VkDescriptorBufferInfo dbi1 = { .buffer = test_buf, .offset = 0, .range = ssbo_range };
      VkDescriptorBufferInfo dbi2 = { .buffer = out_buf, .offset = 0, .range = out_size };
      VkWriteDescriptorSet writes[3] = {
         { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = dset, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &dbi0 },
         { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = dset, .dstBinding = 1, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &dbi1 },
         { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = dset, .dstBinding = 2, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &dbi2 },
      };
      vkUpdateDescriptorSets(t->dev, 3, writes, 0, NULL);

      CK(vkResetFences(t->dev, 1, &t->fence), "ResetFence");
      CK(vkResetCommandBuffer(t->cmd, 0), "ResetCmd");
      VkCommandBufferBeginInfo bbi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
      CK(vkBeginCommandBuffer(t->cmd, &bbi), "BeginCmd");
      vkCmdBindPipeline(t->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
      vkCmdBindDescriptorSets(t->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &dset, 0, NULL);
      struct { uint32_t mode; uint32_t atomic_idx; } pc = { 0, 0 };
      vkCmdPushConstants(t->cmd, playout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
      vkCmdDispatch(t->cmd, 1, 1, 1);
      host_read_barrier(t);
      CK(vkEndCommandBuffer(t->cmd), "EndCmd");

      VkSubmitInfo si = {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .commandBufferCount = 1,
         .pCommandBuffers = &t->cmd,
      };
      CK(vkQueueSubmit(t->queue, 1, &si, t->fence), "QueueSubmit");
      CK(vkWaitForFences(t->dev, 1, &t->fence, VK_TRUE, 30ull * 1000000000ull), "WaitForFences");

      uint32_t res[64];
      CK(vkMapMemory(t->dev, out_mem, 0, out_size, 0, (void **)&map), "MapOutSSBOLoadRead");
      memcpy(res, map, sizeof(res));
      vkUnmapMemory(t->dev, out_mem);

      uint32_t in_range_words = (uint32_t)(ssbo_range / 4);
      int ok = 1;
      for (uint32_t i = 0; i < 64; i++) {
         uint32_t exp = (i < in_range_words) ? 0xABABABABu : 0u;
         if (res[i] != exp) {
            printf("FAIL case oob_ssbo_load index %u got 0x%08x exp 0x%08x (range=%llu in_range_words=%u)\n",
                   i, res[i], exp, (unsigned long long)ssbo_range, in_range_words);
            ok = 0;
            break;
         }
      }
      if (ok) {
         printf("PASS case oob_ssbo_load\n");
         (*passes)++;
      } else {
         (*fails)++;
      }
   }

   /* ──────────────────────────────────────────────────────────────────────────
    * Case 2b: oob_ubo_load
    * ────────────────────────────────────────────────────────────────────────── */
   {
      VkDeviceSize u_align = props_rob2.robustUniformBufferAccessSizeAlignment;
      VkDeviceSize ubo_range = 256;
      if (u_align > ubo_range)
         ubo_range = u_align;
      if (u_align > 0 && (ubo_range % u_align) != 0)
         ubo_range = ((ubo_range + u_align - 1) / u_align) * u_align;

      if (ubo_range > t->props.limits.maxUniformBufferRange)
         ubo_range = t->props.limits.maxUniformBufferRange;

      VkDeviceSize rounded_ubo_range = ubo_range;
      if (u_align > 0)
         rounded_ubo_range = ((ubo_range + u_align - 1) / u_align) * u_align;

      uint32_t *map = NULL;
      CK(vkMapMemory(t->dev, test_mem, 0, 4096, 0, (void **)&map), "MapTestBufUBOLoad");
      for (int i = 0; i < 4096 / 4; i++)
         map[i] = 0xABABABABu;
      vkUnmapMemory(t->dev, test_mem);

      CK(vkMapMemory(t->dev, out_mem, 0, out_size, 0, (void **)&map), "MapOutUBOLoad");
      for (int i = 0; i < 257; i++)
         map[i] = 0xDEADBEEFu;
      vkUnmapMemory(t->dev, out_mem);

      VkDescriptorBufferInfo dbi0 = { .buffer = test_buf, .offset = 0, .range = ubo_range };
      VkDescriptorBufferInfo dbi1 = { .buffer = test_buf, .offset = 0, .range = 256 };
      VkDescriptorBufferInfo dbi2 = { .buffer = out_buf, .offset = 0, .range = out_size };
      VkWriteDescriptorSet writes[3] = {
         { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = dset, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &dbi0 },
         { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = dset, .dstBinding = 1, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &dbi1 },
         { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = dset, .dstBinding = 2, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &dbi2 },
      };
      vkUpdateDescriptorSets(t->dev, 3, writes, 0, NULL);

      CK(vkResetFences(t->dev, 1, &t->fence), "ResetFence");
      CK(vkResetCommandBuffer(t->cmd, 0), "ResetCmd");
      VkCommandBufferBeginInfo bbi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
      CK(vkBeginCommandBuffer(t->cmd, &bbi), "BeginCmd");
      vkCmdBindPipeline(t->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
      vkCmdBindDescriptorSets(t->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &dset, 0, NULL);
      struct { uint32_t mode; uint32_t atomic_idx; } pc = { 1, 0 };
      vkCmdPushConstants(t->cmd, playout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
      vkCmdDispatch(t->cmd, 1, 1, 1);
      host_read_barrier(t);
      CK(vkEndCommandBuffer(t->cmd), "EndCmd");

      VkSubmitInfo si = {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .commandBufferCount = 1,
         .pCommandBuffers = &t->cmd,
      };
      CK(vkQueueSubmit(t->queue, 1, &si, t->fence), "QueueSubmit");
      CK(vkWaitForFences(t->dev, 1, &t->fence, VK_TRUE, 30ull * 1000000000ull), "WaitForFences");

      uint32_t res[256];
      CK(vkMapMemory(t->dev, out_mem, 0, out_size, 0, (void **)&map), "MapOutUBOLoadRead");
      memcpy(res, map, sizeof(res));
      vkUnmapMemory(t->dev, out_mem);

      int ok = 1;
      for (uint32_t i = 0; i < 256; i++) {
         uint32_t byte_off = i * sizeof(uint32_t);
         if (byte_off + sizeof(uint32_t) <= ubo_range) {
            uint32_t exp = 0xABABABABu;
            if (res[i] != exp) {
               printf("FAIL case oob_ubo_load in-range index %u (byte_off %u) got 0x%08x exp 0x%08x (range=%llu)\n",
                      i, byte_off, res[i], exp, (unsigned long long)ubo_range);
               ok = 0;
               break;
            }
         } else if (byte_off >= rounded_ubo_range) {
            uint32_t exp = 0u;
            if (res[i] != exp) {
               printf("FAIL case oob_ubo_load oob index %u (byte_off %u) got 0x%08x exp 0x%08x (range=%llu)\n",
                      i, byte_off, res[i], exp, (unsigned long long)ubo_range);
               ok = 0;
               break;
            }
         }
      }
      if (ok) {
         printf("PASS case oob_ubo_load\n");
         (*passes)++;
      } else {
         (*fails)++;
      }
   }

   /* ──────────────────────────────────────────────────────────────────────────
    * Case 3: oob_ssbo_store
    * ────────────────────────────────────────────────────────────────────────── */
   {
      VkDeviceSize min_offset_align = t->props.limits.minStorageBufferOffsetAlignment;
      VkDeviceSize ssbo_offset = 256;
      if (min_offset_align > ssbo_offset)
         ssbo_offset = min_offset_align;
      if (min_offset_align > 0 && (ssbo_offset % min_offset_align) != 0)
         ssbo_offset = ((ssbo_offset + min_offset_align - 1) / min_offset_align) * min_offset_align;

      VkDeviceSize s_align = props_rob2.robustStorageBufferAccessSizeAlignment;
      VkDeviceSize ssbo_range = 16;
      if (s_align > ssbo_range)
         ssbo_range = s_align;
      if (s_align > 0 && (ssbo_range % s_align) != 0)
         ssbo_range = ((ssbo_range + s_align - 1) / s_align) * s_align;

      uint8_t *bmap = NULL;
      CK(vkMapMemory(t->dev, test_mem, 0, 4096, 0, (void **)&bmap), "MapTestBufStore");
      memset(bmap, 0xCD, 4096);
      vkUnmapMemory(t->dev, test_mem);

      uint32_t *map = NULL;
      CK(vkMapMemory(t->dev, out_mem, 0, out_size, 0, (void **)&map), "MapOutStore");
      for (int i = 0; i < 257; i++)
         map[i] = 0xDEADBEEFu;
      vkUnmapMemory(t->dev, out_mem);

      VkDescriptorBufferInfo dbi0 = { .buffer = test_buf, .offset = 0, .range = 256 };
      VkDescriptorBufferInfo dbi1 = { .buffer = test_buf, .offset = ssbo_offset, .range = ssbo_range };
      VkDescriptorBufferInfo dbi2 = { .buffer = out_buf, .offset = 0, .range = out_size };
      VkWriteDescriptorSet writes[3] = {
         { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = dset, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &dbi0 },
         { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = dset, .dstBinding = 1, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &dbi1 },
         { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = dset, .dstBinding = 2, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &dbi2 },
      };
      vkUpdateDescriptorSets(t->dev, 3, writes, 0, NULL);

      CK(vkResetFences(t->dev, 1, &t->fence), "ResetFence");
      CK(vkResetCommandBuffer(t->cmd, 0), "ResetCmd");
      VkCommandBufferBeginInfo bbi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
      CK(vkBeginCommandBuffer(t->cmd, &bbi), "BeginCmd");
      vkCmdBindPipeline(t->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
      vkCmdBindDescriptorSets(t->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &dset, 0, NULL);
      uint32_t oob_atomic_idx = 60;
      struct { uint32_t mode; uint32_t atomic_idx; } pc = { 2, oob_atomic_idx };
      vkCmdPushConstants(t->cmd, playout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
      vkCmdDispatch(t->cmd, 1, 1, 1);
      host_read_barrier(t);
      CK(vkEndCommandBuffer(t->cmd), "EndCmd");

      VkSubmitInfo si = {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .commandBufferCount = 1,
         .pCommandBuffers = &t->cmd,
      };
      CK(vkQueueSubmit(t->queue, 1, &si, t->fence), "QueueSubmit");
      CK(vkWaitForFences(t->dev, 1, &t->fence, VK_TRUE, 30ull * 1000000000ull), "WaitForFences");

      uint32_t out_res[257];
      CK(vkMapMemory(t->dev, out_mem, 0, out_size, 0, (void **)&map), "MapOutStoreRead");
      memcpy(out_res, map, sizeof(out_res));
      vkUnmapMemory(t->dev, out_mem);

      uint8_t test_res[4096];
      CK(vkMapMemory(t->dev, test_mem, 0, 4096, 0, (void **)&bmap), "MapTestBufStoreRead");
      memcpy(test_res, bmap, 4096);
      vkUnmapMemory(t->dev, test_mem);

      uint32_t atomic_ret = out_res[256];
      printf("INFO oob atomicAdd ret=0x%08x\n", atomic_ret);
      int ok = 1;

      if (ok) {
         for (uint32_t k = 0; k < 4096; k++) {
            if (k >= ssbo_offset && k < ssbo_offset + ssbo_range) {
               uint32_t word_idx = (uint32_t)((k - ssbo_offset) / 4);
               uint32_t byte_in_word = (uint32_t)((k - ssbo_offset) % 4);
               uint32_t exp_word = 0x1000u + word_idx;
               uint8_t exp_byte = (uint8_t)((exp_word >> (byte_in_word * 8)) & 0xFF);
               if (test_res[k] != exp_byte) {
                  printf("FAIL case oob_ssbo_store in-range byte %u (word %u) got 0x%02x exp 0x%02x\n",
                         k, word_idx, test_res[k], exp_byte);
                  ok = 0;
                  break;
               }
            } else {
               if (test_res[k] != 0xCD) {
                  printf("FAIL case oob_ssbo_store guard corrupted at byte %u got 0x%02x exp 0xCD\n",
                         k, test_res[k]);
                  ok = 0;
                  break;
               }
            }
         }
      }

      if (ok) {
         printf("PASS case oob_ssbo_store\n");
         (*passes)++;
      } else {
         (*fails)++;
      }
   }

   vkDestroyPipeline(t->dev, pipe, NULL);
   vkDestroyPipelineLayout(t->dev, playout, NULL);
   vkDestroyDescriptorPool(t->dev, dpool, NULL);
   vkDestroyDescriptorSetLayout(t->dev, dsl, NULL);
   vkDestroyBuffer(t->dev, test_buf, NULL);
   vkFreeMemory(t->dev, test_mem, NULL);
   vkDestroyBuffer(t->dev, out_buf, NULL);
   vkFreeMemory(t->dev, out_mem, NULL);
}

static void
test_null_vertex_buffer(struct dx7 *t, int *passes, int *fails, int *skips)
{
   if (!supported_rob2.nullDescriptor) {
      printf("SKIP case null_vertex_buffer nullDescriptor missing\n");
      (*skips)++;
      return;
   }

   float quad_pos[6][4] = {
      { -0.5f, -0.5f, 0.0f, 1.0f },
      {  0.5f, -0.5f, 0.0f, 1.0f },
      { -0.5f,  0.5f, 0.0f, 1.0f },
      { -0.5f,  0.5f, 0.0f, 1.0f },
      {  0.5f, -0.5f, 0.0f, 1.0f },
      {  0.5f,  0.5f, 0.0f, 1.0f },
   };

   VkBuffer vbuf = VK_NULL_HANDLE;
   VkDeviceMemory vmem = VK_NULL_HANDLE;
   dx7_buffer(t, sizeof(quad_pos), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, quad_pos, &vbuf, &vmem);

   VkVertexInputBindingDescription vbs[2] = {
      { .binding = 0, .stride = 16, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX },
      { .binding = 1, .stride = 16, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX },
   };
   VkVertexInputAttributeDescription vas[2] = {
      { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 0 },
      { .location = 1, .binding = 1, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 0 },
   };
   VkPipelineVertexInputStateCreateInfo vi = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 2,
      .pVertexBindingDescriptions = vbs,
      .vertexAttributeDescriptionCount = 2,
      .pVertexAttributeDescriptions = vas,
   };

   VkShaderModule vs = dx7_module(t, vbuf_vert_spv, sizeof(vbuf_vert_spv));
   VkShaderModule fs = dx7_module(t, vbuf_frag_spv, sizeof(vbuf_frag_spv));

   VkPipelineShaderStageCreateInfo stages[2] = {
      { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main" },
      { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = "main" },
   };
   VkPipelineInputAssemblyStateCreateInfo ia = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .primitiveRestartEnable = VK_FALSE,
   };
   VkViewport vp = { 0, 0, RT_W, RT_H, 0, 1 };
   VkRect2D sc = { { 0, 0 }, { RT_W, RT_H } };
   VkPipelineViewportStateCreateInfo vps = {
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
      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .lineWidth = 1.0f,
   };
   VkPipelineMultisampleStateCreateInfo ms = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
   };
   VkPipelineColorBlendAttachmentState ba = {
      .colorWriteMask = 0xf,
      .blendEnable = VK_FALSE,
   };
   VkPipelineColorBlendStateCreateInfo cb = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &ba,
   };
   VkGraphicsPipelineCreateInfo gpci = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount = 2,
      .pStages = stages,
      .pVertexInputState = &vi,
      .pInputAssemblyState = &ia,
      .pViewportState = &vps,
      .pRasterizationState = &rs,
      .pMultisampleState = &ms,
      .pColorBlendState = &cb,
      .layout = t->layout,
      .renderPass = t->rp,
   };
   VkPipeline pipe = VK_NULL_HANDLE;
   CK(vkCreateGraphicsPipelines(t->dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipe), "CreateGraphicsPipelines");
   vkDestroyShaderModule(t->dev, vs, NULL);
   vkDestroyShaderModule(t->dev, fs, NULL);

   CK(vkResetFences(t->dev, 1, &t->fence), "ResetFence");
   CK(vkResetCommandBuffer(t->cmd, 0), "ResetCmd");
   VkCommandBufferBeginInfo bbi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
   };
   CK(vkBeginCommandBuffer(t->cmd, &bbi), "BeginCmd");

   VkClearValue clear = { .color = { { 0.0f, 0.0f, 1.0f, 1.0f } } };
   VkRenderPassBeginInfo rpbi = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = t->rp,
      .framebuffer = t->fb,
      .renderArea = { { 0, 0 }, { RT_W, RT_H } },
      .clearValueCount = 1,
      .pClearValues = &clear,
   };
   vkCmdBeginRenderPass(t->cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
   vkCmdBindPipeline(t->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);

   VkBuffer bufs[2] = { vbuf, VK_NULL_HANDLE };
   VkDeviceSize offsets[2] = { 0, 0 };
   vkCmdBindVertexBuffers(t->cmd, 0, 2, bufs, offsets);
   vkCmdDraw(t->cmd, 6, 1, 0, 0);
   vkCmdEndRenderPass(t->cmd);

   VkImageMemoryBarrier bar = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = t->img,
      .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
   };
   vkCmdPipelineBarrier(t->cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &bar);
   VkBufferImageCopy region = {
      .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
      .imageExtent = { RT_W, RT_H, 1 },
   };
   vkCmdCopyImageToBuffer(t->cmd, t->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          t->rbuf, 1, &region);
   host_read_barrier(t);
   CK(vkEndCommandBuffer(t->cmd), "EndCmd");

   VkSubmitInfo si = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers = &t->cmd,
   };
   CK(vkQueueSubmit(t->queue, 1, &si, t->fence), "QueueSubmit");
   CK(vkWaitForFences(t->dev, 1, &t->fence, VK_TRUE, 30ull * 1000000000ull), "WaitForFences");

   int covered = 0, bad_covered = 0, bad_uncovered = 0;
   int fx = -1, fy = -1;
   uint8_t f_rgba[4] = { 0 };

   for (int y = 0; y < RT_H; y++) {
      for (int x = 0; x < RT_W; x++) {
         const uint8_t *p = dx7_px(t, x, y);
         if (p[2] == 0) {
            /* The fragment shader marks covered pixels with B == 0. */
            if (covered++ == 0 && (p[1] == 0 || p[1] == 255))
               printf("INFO null_vertex_buffer w=%u\n", p[1] == 255 ? 1u : 0u);
            if (p[0] != 0 || (p[1] != 0 && p[1] != 255)) {
               if (!bad_covered) {
                  fx = x;
                  fy = y;
                  memcpy(f_rgba, p, 4);
               }
               bad_covered++;
            }
         } else {
            /* Uncovered pixels must retain the exact clear color. */
            if (p[0] != 0 || p[1] != 0 || p[2] != 255 || p[3] != 255) {
               if (!bad_covered && !bad_uncovered) {
                  fx = x;
                  fy = y;
                  memcpy(f_rgba, p, 4);
               }
               bad_uncovered++;
            }
         }
      }
   }

   if (covered == 0) {
      printf("FAIL case null_vertex_buffer zero covered pixels\n");
      (*fails)++;
   } else if (bad_covered > 0 || bad_uncovered > 0) {
      printf("FAIL case null_vertex_buffer bad pixel at (%d,%d) got RGBA=(%u,%u,%u,%u) bad_covered=%d bad_uncovered=%d\n",
             fx, fy, f_rgba[0], f_rgba[1], f_rgba[2], f_rgba[3], bad_covered, bad_uncovered);
      (*fails)++;
   } else {
      printf("PASS case null_vertex_buffer\n");
      (*passes)++;
   }

   vkDestroyPipeline(t->dev, pipe, NULL);
   vkDestroyBuffer(t->dev, vbuf, NULL);
   vkFreeMemory(t->dev, vmem, NULL);
}

int
main(int argc, char **argv)
{
   if (argc < 2) {
      printf("Usage: %s <path-to-libvulkan_panfrost.so>\n", argv[0]);
      return 1;
   }

   void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
   if (!h) {
      printf("FAIL dlopen %s\n", dlerror());
      return 1;
   }
   gipa = (PFN_vkGetInstanceProcAddr)dlsym(h, "vk_icdGetInstanceProcAddr");
   if (!gipa)
      gipa = (PFN_vkGetInstanceProcAddr)dlsym(h, "vkGetInstanceProcAddr");
   if (!gipa) {
      printf("FAIL missing gipa\n");
      return 1;
   }

   dx7_device_hook = device_hook;
   struct dx7 t;
   dx7_init(&t, argv[1], NULL);
   load_extra_funcs(&t, argv[1]);

   int passes = 0;
   int fails = 0;
   int skips = 0;

   test_null_descriptors(&t, &passes, &fails, &skips);
   test_oob_cases(&t, &passes, &fails, &skips);
   test_null_vertex_buffer(&t, &passes, &fails, &skips);

   if (fails > 0) {
      printf("RESULT FAIL\n");
      return 1;
   } else if (passes == 0) {
      printf("RESULT SKIP\n");
      return 0;
   } else {
      printf("RESULT PASS\n");
      return 0;
   }
}

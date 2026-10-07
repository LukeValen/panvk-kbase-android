/* Vulkan Descriptor Model & Layout Tests.
 * Executes compute workloads verifying Vulkan 1.2+ descriptor models & layouts:
 *   (1) bda: buffer device address with pointer in memory via GL_EXT_buffer_reference_uvec2
 *   (2) runtime_array: runtime descriptor array with variable descriptor count
 *   (3) update_after_bind: descriptor update after command buffer recording/end
 *   (4) partially_bound: descriptor array with partially bound descriptors
 *   (5) inline_uniform: inline uniform block descriptors
 *   (6) scalar_layout: GL_EXT_scalar_block_layout packing
 *
 * Usage: descriptor_model <libvulkan_panfrost.so>
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

#include "descriptor_model_spv.h"

typedef PFN_vkVoidFunction (*icd_gipa_fn)(VkInstance, const char *);

struct exec_buf {
   VkBuffer buffer;
   VkDeviceMemory memory;
   VkDeviceSize size;
   void *map;
   VkDeviceAddress address;
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

   int has_inline_ub_ext = has_extension(exts, ext_count, "VK_EXT_inline_uniform_block");
   free(exts);

   VkPhysicalDeviceProperties dev_props;
   p_GetPhysicalDeviceProperties(phys, &dev_props);

   int can_chain_inline_ub = (dev_props.apiVersion >= VK_API_VERSION_1_3) || has_inline_ub_ext;

   /* Query Features */
   VkPhysicalDeviceInlineUniformBlockFeatures supported_inline_ub = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_FEATURES,
   };
   VkPhysicalDeviceVulkan12Features supported_f12 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
   };
   if (can_chain_inline_ub) {
      supported_f12.pNext = &supported_inline_ub;
   }
   VkPhysicalDeviceFeatures2 supported_f2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &supported_f12,
   };
   p_GetPhysicalDeviceFeatures2(phys, &supported_f2);

   /* Query Properties */
   VkPhysicalDeviceInlineUniformBlockProperties inline_props = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_PROPERTIES,
   };
   VkPhysicalDeviceProperties2 props2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
   };
   if (can_chain_inline_ub) {
      props2.pNext = &inline_props;
   }
   if (p_GetPhysicalDeviceProperties2) {
      p_GetPhysicalDeviceProperties2(phys, &props2);
   }

   /* Print one INFO line listing the relevant feature bits */
   printf("INFO features: bda=%d rda=%d ssbo_nonuniform=%d var_desc_count=%d uab_ssbo=%d partially_bound=%d inline_ub=%d scalar_layout=%d\n",
          (int)supported_f12.bufferDeviceAddress,
          (int)supported_f12.runtimeDescriptorArray,
          (int)supported_f12.shaderStorageBufferArrayNonUniformIndexing,
          (int)supported_f12.descriptorBindingVariableDescriptorCount,
          (int)supported_f12.descriptorBindingStorageBufferUpdateAfterBind,
          (int)supported_f12.descriptorBindingPartiallyBound,
          (int)(can_chain_inline_ub ? supported_inline_ub.inlineUniformBlock : 0),
          (int)supported_f12.scalarBlockLayout);

   /* Find Queue Family */
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

   /* Device Creation: enable features and extensions if present */
   const char *dev_exts[4];
   uint32_t dev_ext_count = 0;
   if (dev_props.apiVersion < VK_API_VERSION_1_3 && has_inline_ub_ext && supported_inline_ub.inlineUniformBlock) {
      dev_exts[dev_ext_count++] = "VK_EXT_inline_uniform_block";
   }

   VkPhysicalDeviceFeatures2 dev_feat2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
   };

   VkPhysicalDeviceVulkan12Features dev_f12 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .bufferDeviceAddress = supported_f12.bufferDeviceAddress,
      .runtimeDescriptorArray = supported_f12.runtimeDescriptorArray,
      .shaderStorageBufferArrayNonUniformIndexing = supported_f12.shaderStorageBufferArrayNonUniformIndexing,
      .descriptorBindingVariableDescriptorCount = supported_f12.descriptorBindingVariableDescriptorCount,
      .descriptorBindingStorageBufferUpdateAfterBind = supported_f12.descriptorBindingStorageBufferUpdateAfterBind,
      .descriptorBindingPartiallyBound = supported_f12.descriptorBindingPartiallyBound,
      .scalarBlockLayout = supported_f12.scalarBlockLayout,
   };
   dev_feat2.pNext = &dev_f12;

   VkPhysicalDeviceInlineUniformBlockFeatures dev_inline_ub = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_FEATURES,
   };
   if (can_chain_inline_ub && supported_inline_ub.inlineUniformBlock) {
      dev_inline_ub.inlineUniformBlock = VK_TRUE;
      dev_f12.pNext = &dev_inline_ub;
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
   GD(CmdPushConstants)
   GD(CmdDispatch)
   GD(CmdPipelineBarrier)
   GD(QueueSubmit)
   GD(QueueWaitIdle)
#undef GD

   PFN_vkGetBufferDeviceAddress g_bda =
      (PFN_vkGetBufferDeviceAddress)p_GetDeviceProcAddr(dev, "vkGetBufferDeviceAddress");
   if (!g_bda)
      g_bda = (PFN_vkGetBufferDeviceAddress)gipa(inst, "vkGetBufferDeviceAddress");
   if (!g_bda)
      g_bda = (PFN_vkGetBufferDeviceAddress)p_GetDeviceProcAddr(dev, "vkGetBufferDeviceAddressKHR");
   if (!g_bda)
      g_bda = (PFN_vkGetBufferDeviceAddress)gipa(inst, "vkGetBufferDeviceAddressKHR");

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
    * Case 1: bda
    * ────────────────────────────────────────────────────────────────────────── */
   if (!supported_f12.bufferDeviceAddress || !g_bda) {
      printf("SKIP case bda bufferDeviceAddress missing\n");
      skips++;
   } else {
      struct exec_buf buf_a = {0}, buf_b = {0}, buf_c = {0};
      VkPipelineLayout playout = VK_NULL_HANDLE;
      VkPipeline pipe = VK_NULL_HANDLE;

      if (create_buffer(dev, &mp, 64 * sizeof(uint32_t),
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                        VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        g_bda, &buf_a) ||
          create_buffer(dev, &mp, 64 * sizeof(uint32_t),
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                        VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        g_bda, &buf_b) ||
          create_buffer(dev, &mp, 64,
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                        VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        g_bda, &buf_c)) {
         printf("FAIL case bda create_buffer\n");
         fails++;
         goto bda_cleanup;
      }

      uint32_t *p_a = (uint32_t *)buf_a.map;
      for (uint32_t i = 0; i < 64; i++)
         p_a[i] = i * 3u + 7u;

      uint32_t *p_b = (uint32_t *)buf_b.map;
      for (uint32_t i = 0; i < 64; i++)
         p_b[i] = 0xDEADBEEFu;

      uint64_t *p_c = (uint64_t *)buf_c.map;
      p_c[0] = (uint64_t)buf_a.address;

      struct {
         uint64_t dst_addr;
         uint64_t ptr_buf_addr;
      } pc = {
         .dst_addr = (uint64_t)buf_b.address,
         .ptr_buf_addr = (uint64_t)buf_c.address,
      };

      VkPushConstantRange pcr = {
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
         .offset = 0,
         .size = sizeof(pc),
      };
      VkPipelineLayoutCreateInfo plci = {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
         .pushConstantRangeCount = 1,
         .pPushConstantRanges = &pcr,
      };
      VkResult vr = p_CreatePipelineLayout(dev, &plci, NULL, &playout);
      if (vr != VK_SUCCESS) {
         printf("FAIL case bda CreatePipelineLayout r=%d\n", (int)vr);
         fails++;
         goto bda_cleanup;
      }

      vr = create_compute_pipe(dev, p_CreateShaderModule, p_DestroyShaderModule,
                               p_CreateComputePipelines, bda_spv, sizeof(bda_spv),
                               playout, &pipe);
      if (vr != VK_SUCCESS) {
         printf("FAIL case bda CreateComputePipelines r=%d\n", (int)vr);
         fails++;
         goto bda_cleanup;
      }

      VkCommandBufferBeginInfo bi = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      };
      p_BeginCommandBuffer(cmd, &bi);
      p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
      p_CmdPushConstants(cmd, playout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
      p_CmdDispatch(cmd, 1, 1, 1);
      VkMemoryBarrier host_barrier = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
      };
      p_CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_HOST_BIT, 0,
                           1, &host_barrier, 0, NULL, 0, NULL);
      p_EndCommandBuffer(cmd);

      VkSubmitInfo si = {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .commandBufferCount = 1,
         .pCommandBuffers = &cmd,
      };
      vr = p_QueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
      if (vr != VK_SUCCESS) {
         printf("FAIL case bda QueueSubmit r=%d\n", (int)vr);
         fails++;
         goto bda_cleanup;
      }
      if (!check_wait_idle(queue, p_QueueWaitIdle, "bda")) {
         fails++;
         goto bda_cleanup;
      }

      int match = 1;
      for (uint32_t i = 0; i < 64; i++) {
         uint32_t exp = ((i * 3u + 7u) ^ 0x5A5A0000u) + i;
         if (p_b[i] != exp) {
            printf("FAIL case bda mismatch at [%u]: got 0x%08x exp 0x%08x\n", i, p_b[i], exp);
            fails++;
            match = 0;
            break;
         }
      }
      if (match) {
         printf("PASS case bda\n");
         passes++;
      }

bda_cleanup:
      if (pipe)
         p_DestroyPipeline(dev, pipe, NULL);
      if (playout)
         p_DestroyPipelineLayout(dev, playout, NULL);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &buf_c);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &buf_b);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &buf_a);
   }

   /* ──────────────────────────────────────────────────────────────────────────
    * Case 2: runtime_array
    * ────────────────────────────────────────────────────────────────────────── */
   if (!supported_f12.runtimeDescriptorArray ||
       !supported_f12.shaderStorageBufferArrayNonUniformIndexing ||
       !supported_f12.descriptorBindingVariableDescriptorCount) {
      printf("SKIP case runtime_array descriptor indexing features missing\n");
      skips++;
   } else {
      struct exec_buf out_buf = {0};
      struct exec_buf in_bufs[8] = {{0}};
      VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
      VkPipelineLayout playout = VK_NULL_HANDLE;
      VkDescriptorPool dpool = VK_NULL_HANDLE;
      VkPipeline pipe = VK_NULL_HANDLE;

      if (create_buffer(dev, &mp, 64 * sizeof(uint32_t),
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        NULL, &out_buf)) {
         printf("FAIL case runtime_array create_buffer\n");
         fails++;
         goto rda_cleanup;
      }
      uint32_t *p_out = (uint32_t *)out_buf.map;
      for (uint32_t i = 0; i < 64; i++)
         p_out[i] = 0xDEADBEEFu;

      for (uint32_t k = 0; k < 8; k++) {
         if (create_buffer(dev, &mp, 16,
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0,
                           p_CreateBuffer, p_GetBufferMemoryRequirements,
                           p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                           NULL, &in_bufs[k])) {
            printf("FAIL case runtime_array create_buffer\n");
            fails++;
            goto rda_cleanup;
         }
         uint32_t *p_in = (uint32_t *)in_bufs[k].map;
         p_in[0] = 0x1000u + k * 0x11u;
      }

      VkDescriptorSetLayoutBinding bnds[2] = {
         {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
         },
         {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 16,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
         },
      };
      VkDescriptorBindingFlags bflags[2] = {
         0,
         VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT,
      };
      VkDescriptorSetLayoutBindingFlagsCreateInfo flags_info = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
         .bindingCount = 2,
         .pBindingFlags = bflags,
      };
      VkDescriptorSetLayoutCreateInfo dslci = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
         .pNext = &flags_info,
         .bindingCount = 2,
         .pBindings = bnds,
      };
      VkResult vr = p_CreateDescriptorSetLayout(dev, &dslci, NULL, &dsl);
      if (vr != VK_SUCCESS) {
         printf("FAIL case runtime_array CreateDescriptorSetLayout r=%d\n", (int)vr);
         fails++;
         goto rda_cleanup;
      }

      VkPipelineLayoutCreateInfo plci = {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
         .setLayoutCount = 1,
         .pSetLayouts = &dsl,
      };
      vr = p_CreatePipelineLayout(dev, &plci, NULL, &playout);
      if (vr != VK_SUCCESS) {
         printf("FAIL case runtime_array CreatePipelineLayout r=%d\n", (int)vr);
         fails++;
         goto rda_cleanup;
      }

      VkDescriptorPoolSize ps = {
         .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 1 + 16,
      };
      VkDescriptorPoolCreateInfo dpci = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
         .maxSets = 1,
         .poolSizeCount = 1,
         .pPoolSizes = &ps,
      };
      vr = p_CreateDescriptorPool(dev, &dpci, NULL, &dpool);
      if (vr != VK_SUCCESS) {
         printf("FAIL case runtime_array CreateDescriptorPool r=%d\n", (int)vr);
         fails++;
         goto rda_cleanup;
      }

      uint32_t var_count = 8;
      VkDescriptorSetVariableDescriptorCountAllocateInfo var_alloc = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
         .descriptorSetCount = 1,
         .pDescriptorCounts = &var_count,
      };
      VkDescriptorSetAllocateInfo dsai = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
         .pNext = &var_alloc,
         .descriptorPool = dpool,
         .descriptorSetCount = 1,
         .pSetLayouts = &dsl,
      };
      VkDescriptorSet dset = VK_NULL_HANDLE;
      vr = p_AllocateDescriptorSets(dev, &dsai, &dset);
      if (vr != VK_SUCCESS) {
         printf("FAIL case runtime_array AllocateDescriptorSets r=%d\n", (int)vr);
         fails++;
         goto rda_cleanup;
      }

      VkDescriptorBufferInfo dbi_out = {
         .buffer = out_buf.buffer,
         .offset = 0,
         .range = 64 * sizeof(uint32_t),
      };
      VkDescriptorBufferInfo dbi_ins[8];
      for (uint32_t k = 0; k < 8; k++) {
         dbi_ins[k].buffer = in_bufs[k].buffer;
         dbi_ins[k].offset = 0;
         dbi_ins[k].range = 16;
      }
      VkWriteDescriptorSet wds[2] = {
         {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = dset,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &dbi_out,
         },
         {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = dset,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 8,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = dbi_ins,
         },
      };
      p_UpdateDescriptorSets(dev, 2, wds, 0, NULL);

      vr = create_compute_pipe(dev, p_CreateShaderModule, p_DestroyShaderModule,
                               p_CreateComputePipelines, runtime_array_spv,
                               sizeof(runtime_array_spv), playout, &pipe);
      if (vr != VK_SUCCESS) {
         printf("FAIL case runtime_array CreateComputePipelines r=%d\n", (int)vr);
         fails++;
         goto rda_cleanup;
      }

      VkCommandBufferBeginInfo bi = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      };
      p_BeginCommandBuffer(cmd, &bi);
      p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
      p_CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &dset, 0, NULL);
      p_CmdDispatch(cmd, 1, 1, 1);
      VkMemoryBarrier host_barrier = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
      };
      p_CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_HOST_BIT, 0,
                           1, &host_barrier, 0, NULL, 0, NULL);
      p_EndCommandBuffer(cmd);

      VkSubmitInfo si = {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .commandBufferCount = 1,
         .pCommandBuffers = &cmd,
      };
      vr = p_QueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
      if (vr != VK_SUCCESS) {
         printf("FAIL case runtime_array QueueSubmit r=%d\n", (int)vr);
         fails++;
         goto rda_cleanup;
      }
      if (!check_wait_idle(queue, p_QueueWaitIdle, "runtime_array")) {
         fails++;
         goto rda_cleanup;
      }

      int match = 1;
      for (uint32_t i = 0; i < 64; i++) {
         uint32_t exp = (0x1000u + (i % 8u) * 0x11u) + i;
         if (p_out[i] != exp) {
            printf("FAIL case runtime_array mismatch at [%u]: got 0x%08x exp 0x%08x\n", i, p_out[i], exp);
            fails++;
            match = 0;
            break;
         }
      }
      if (match) {
         printf("PASS case runtime_array\n");
         passes++;
      }

rda_cleanup:
      if (pipe)
         p_DestroyPipeline(dev, pipe, NULL);
      if (dpool)
         p_DestroyDescriptorPool(dev, dpool, NULL);
      if (playout)
         p_DestroyPipelineLayout(dev, playout, NULL);
      if (dsl)
         p_DestroyDescriptorSetLayout(dev, dsl, NULL);
      for (uint32_t k = 0; k < 8; k++)
         destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &in_bufs[k]);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &out_buf);
   }

   /* ──────────────────────────────────────────────────────────────────────────
    * Case 3: update_after_bind
    * ────────────────────────────────────────────────────────────────────────── */
   if (!supported_f12.descriptorBindingStorageBufferUpdateAfterBind) {
      printf("SKIP case update_after_bind descriptorBindingStorageBufferUpdateAfterBind missing\n");
      skips++;
   } else {
      struct exec_buf out_buf = {0};
      struct exec_buf buf_x = {0};
      struct exec_buf buf_y = {0};
      VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
      VkPipelineLayout playout = VK_NULL_HANDLE;
      VkDescriptorPool dpool = VK_NULL_HANDLE;
      VkPipeline pipe = VK_NULL_HANDLE;

      if (create_buffer(dev, &mp, 64 * sizeof(uint32_t),
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        NULL, &out_buf) ||
          create_buffer(dev, &mp, 64 * sizeof(uint32_t),
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        NULL, &buf_x) ||
          create_buffer(dev, &mp, 64 * sizeof(uint32_t),
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        NULL, &buf_y)) {
         printf("FAIL case update_after_bind create_buffer\n");
         fails++;
         goto uab_cleanup;
      }

      uint32_t *p_out = (uint32_t *)out_buf.map;
      for (uint32_t i = 0; i < 64; i++)
         p_out[i] = 0xDEADBEEFu;

      uint32_t *p_x = (uint32_t *)buf_x.map;
      for (uint32_t i = 0; i < 64; i++)
         p_x[i] = 0x11111111u;

      uint32_t *p_y = (uint32_t *)buf_y.map;
      for (uint32_t i = 0; i < 64; i++)
         p_y[i] = 0x22220000u + i;

      VkDescriptorSetLayoutBinding bnds[2] = {
         {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
         },
         {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
         },
      };
      VkDescriptorBindingFlags bflags[2] = {
         0,
         VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
      };
      VkDescriptorSetLayoutBindingFlagsCreateInfo flags_info = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
         .bindingCount = 2,
         .pBindingFlags = bflags,
      };
      VkDescriptorSetLayoutCreateInfo dslci = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
         .pNext = &flags_info,
         .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
         .bindingCount = 2,
         .pBindings = bnds,
      };
      VkResult vr = p_CreateDescriptorSetLayout(dev, &dslci, NULL, &dsl);
      if (vr != VK_SUCCESS) {
         printf("FAIL case update_after_bind CreateDescriptorSetLayout r=%d\n", (int)vr);
         fails++;
         goto uab_cleanup;
      }

      VkPipelineLayoutCreateInfo plci = {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
         .setLayoutCount = 1,
         .pSetLayouts = &dsl,
      };
      vr = p_CreatePipelineLayout(dev, &plci, NULL, &playout);
      if (vr != VK_SUCCESS) {
         printf("FAIL case update_after_bind CreatePipelineLayout r=%d\n", (int)vr);
         fails++;
         goto uab_cleanup;
      }

      VkDescriptorPoolSize ps = {
         .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 2,
      };
      VkDescriptorPoolCreateInfo dpci = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
         .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
         .maxSets = 1,
         .poolSizeCount = 1,
         .pPoolSizes = &ps,
      };
      vr = p_CreateDescriptorPool(dev, &dpci, NULL, &dpool);
      if (vr != VK_SUCCESS) {
         printf("FAIL case update_after_bind CreateDescriptorPool r=%d\n", (int)vr);
         fails++;
         goto uab_cleanup;
      }

      VkDescriptorSetAllocateInfo dsai = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
         .descriptorPool = dpool,
         .descriptorSetCount = 1,
         .pSetLayouts = &dsl,
      };
      VkDescriptorSet dset = VK_NULL_HANDLE;
      vr = p_AllocateDescriptorSets(dev, &dsai, &dset);
      if (vr != VK_SUCCESS) {
         printf("FAIL case update_after_bind AllocateDescriptorSets r=%d\n", (int)vr);
         fails++;
         goto uab_cleanup;
      }

      /* Initial update: binding 1 = buffer X */
      VkDescriptorBufferInfo dbi_out = {
         .buffer = out_buf.buffer,
         .offset = 0,
         .range = 64 * sizeof(uint32_t),
      };
      VkDescriptorBufferInfo dbi_x = {
         .buffer = buf_x.buffer,
         .offset = 0,
         .range = 64 * sizeof(uint32_t),
      };
      VkWriteDescriptorSet wds_init[2] = {
         {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = dset,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &dbi_out,
         },
         {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = dset,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &dbi_x,
         },
      };
      p_UpdateDescriptorSets(dev, 2, wds_init, 0, NULL);

      vr = create_compute_pipe(dev, p_CreateShaderModule, p_DestroyShaderModule,
                               p_CreateComputePipelines, update_after_bind_spv,
                               sizeof(update_after_bind_spv), playout, &pipe);
      if (vr != VK_SUCCESS) {
         printf("FAIL case update_after_bind CreateComputePipelines r=%d\n", (int)vr);
         fails++;
         goto uab_cleanup;
      }

      /* Record command buffer and End it */
      VkCommandBufferBeginInfo bi = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      };
      p_BeginCommandBuffer(cmd, &bi);
      p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
      p_CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &dset, 0, NULL);
      p_CmdDispatch(cmd, 1, 1, 1);
      VkMemoryBarrier host_barrier = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
      };
      p_CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_HOST_BIT, 0,
                           1, &host_barrier, 0, NULL, 0, NULL);
      p_EndCommandBuffer(cmd);

      /* THEN update binding 1 to buffer Y */
      VkDescriptorBufferInfo dbi_y = {
         .buffer = buf_y.buffer,
         .offset = 0,
         .range = 64 * sizeof(uint32_t),
      };
      VkWriteDescriptorSet wds_y = {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = dset,
         .dstBinding = 1,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .pBufferInfo = &dbi_y,
      };
      p_UpdateDescriptorSets(dev, 1, &wds_y, 0, NULL);

      /* Submit and wait */
      VkSubmitInfo si = {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .commandBufferCount = 1,
         .pCommandBuffers = &cmd,
      };
      vr = p_QueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
      if (vr != VK_SUCCESS) {
         printf("FAIL case update_after_bind QueueSubmit r=%d\n", (int)vr);
         fails++;
         goto uab_cleanup;
      }
      if (!check_wait_idle(queue, p_QueueWaitIdle, "update_after_bind")) {
         fails++;
         goto uab_cleanup;
      }

      int match = 1;
      for (uint32_t i = 0; i < 64; i++) {
         uint32_t exp = 0x22220000u + i;
         if (p_out[i] != exp) {
            printf("FAIL case update_after_bind mismatch at [%u]: got 0x%08x exp 0x%08x\n", i, p_out[i], exp);
            fails++;
            match = 0;
            break;
         }
      }
      if (match) {
         printf("PASS case update_after_bind\n");
         passes++;
      }

uab_cleanup:
      if (pipe)
         p_DestroyPipeline(dev, pipe, NULL);
      if (dpool)
         p_DestroyDescriptorPool(dev, dpool, NULL);
      if (playout)
         p_DestroyPipelineLayout(dev, playout, NULL);
      if (dsl)
         p_DestroyDescriptorSetLayout(dev, dsl, NULL);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &buf_y);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &buf_x);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &out_buf);
   }

   /* ──────────────────────────────────────────────────────────────────────────
    * Case 4: partially_bound
    * ────────────────────────────────────────────────────────────────────────── */
   if (!supported_f12.descriptorBindingPartiallyBound) {
      printf("SKIP case partially_bound descriptorBindingPartiallyBound missing\n");
      skips++;
   } else {
      struct exec_buf out_buf = {0};
      struct exec_buf buf_0 = {0};
      struct exec_buf buf_2 = {0};
      VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
      VkPipelineLayout playout = VK_NULL_HANDLE;
      VkDescriptorPool dpool = VK_NULL_HANDLE;
      VkPipeline pipe = VK_NULL_HANDLE;

      if (create_buffer(dev, &mp, 64 * sizeof(uint32_t),
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        NULL, &out_buf) ||
          create_buffer(dev, &mp, 64 * sizeof(uint32_t),
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        NULL, &buf_0) ||
          create_buffer(dev, &mp, 64 * sizeof(uint32_t),
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        NULL, &buf_2)) {
         printf("FAIL case partially_bound create_buffer\n");
         fails++;
         goto pb_cleanup;
      }

      uint32_t *p_out = (uint32_t *)out_buf.map;
      for (uint32_t i = 0; i < 64; i++)
         p_out[i] = 0xDEADBEEFu;

      uint32_t *p_0 = (uint32_t *)buf_0.map;
      for (uint32_t i = 0; i < 64; i++)
         p_0[i] = 0xA0u + i;

      uint32_t *p_2 = (uint32_t *)buf_2.map;
      for (uint32_t i = 0; i < 64; i++)
         p_2[i] = 0xC0u + i;

      VkDescriptorSetLayoutBinding bnds[2] = {
         {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
         },
         {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 4,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
         },
      };
      VkDescriptorBindingFlags bflags[2] = {
         0,
         VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
      };
      VkDescriptorSetLayoutBindingFlagsCreateInfo flags_info = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
         .bindingCount = 2,
         .pBindingFlags = bflags,
      };
      VkDescriptorSetLayoutCreateInfo dslci = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
         .pNext = &flags_info,
         .bindingCount = 2,
         .pBindings = bnds,
      };
      VkResult vr = p_CreateDescriptorSetLayout(dev, &dslci, NULL, &dsl);
      if (vr != VK_SUCCESS) {
         printf("FAIL case partially_bound CreateDescriptorSetLayout r=%d\n", (int)vr);
         fails++;
         goto pb_cleanup;
      }

      VkPipelineLayoutCreateInfo plci = {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
         .setLayoutCount = 1,
         .pSetLayouts = &dsl,
      };
      vr = p_CreatePipelineLayout(dev, &plci, NULL, &playout);
      if (vr != VK_SUCCESS) {
         printf("FAIL case partially_bound CreatePipelineLayout r=%d\n", (int)vr);
         fails++;
         goto pb_cleanup;
      }

      VkDescriptorPoolSize ps = {
         .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 5,
      };
      VkDescriptorPoolCreateInfo dpci = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
         .maxSets = 1,
         .poolSizeCount = 1,
         .pPoolSizes = &ps,
      };
      vr = p_CreateDescriptorPool(dev, &dpci, NULL, &dpool);
      if (vr != VK_SUCCESS) {
         printf("FAIL case partially_bound CreateDescriptorPool r=%d\n", (int)vr);
         fails++;
         goto pb_cleanup;
      }

      VkDescriptorSetAllocateInfo dsai = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
         .descriptorPool = dpool,
         .descriptorSetCount = 1,
         .pSetLayouts = &dsl,
      };
      VkDescriptorSet dset = VK_NULL_HANDLE;
      vr = p_AllocateDescriptorSets(dev, &dsai, &dset);
      if (vr != VK_SUCCESS) {
         printf("FAIL case partially_bound AllocateDescriptorSets r=%d\n", (int)vr);
         fails++;
         goto pb_cleanup;
      }

      /* Write binding 0, and binding 1 elements 0 and 2 only (1 and 3 remain unbound) */
      VkDescriptorBufferInfo dbi_out = {
         .buffer = out_buf.buffer,
         .offset = 0,
         .range = 64 * sizeof(uint32_t),
      };
      VkDescriptorBufferInfo dbi_0 = {
         .buffer = buf_0.buffer,
         .offset = 0,
         .range = 64 * sizeof(uint32_t),
      };
      VkDescriptorBufferInfo dbi_2 = {
         .buffer = buf_2.buffer,
         .offset = 0,
         .range = 64 * sizeof(uint32_t),
      };
      VkWriteDescriptorSet wds[3] = {
         {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = dset,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &dbi_out,
         },
         {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = dset,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &dbi_0,
         },
         {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = dset,
            .dstBinding = 1,
            .dstArrayElement = 2,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &dbi_2,
         },
      };
      p_UpdateDescriptorSets(dev, 3, wds, 0, NULL);

      vr = create_compute_pipe(dev, p_CreateShaderModule, p_DestroyShaderModule,
                               p_CreateComputePipelines, partially_bound_spv,
                               sizeof(partially_bound_spv), playout, &pipe);
      if (vr != VK_SUCCESS) {
         printf("FAIL case partially_bound CreateComputePipelines r=%d\n", (int)vr);
         fails++;
         goto pb_cleanup;
      }

      VkCommandBufferBeginInfo bi = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      };
      p_BeginCommandBuffer(cmd, &bi);
      p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
      p_CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &dset, 0, NULL);
      p_CmdDispatch(cmd, 1, 1, 1);
      VkMemoryBarrier host_barrier = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
      };
      p_CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_HOST_BIT, 0,
                           1, &host_barrier, 0, NULL, 0, NULL);
      p_EndCommandBuffer(cmd);

      VkSubmitInfo si = {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .commandBufferCount = 1,
         .pCommandBuffers = &cmd,
      };
      vr = p_QueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
      if (vr != VK_SUCCESS) {
         printf("FAIL case partially_bound QueueSubmit r=%d\n", (int)vr);
         fails++;
         goto pb_cleanup;
      }
      if (!check_wait_idle(queue, p_QueueWaitIdle, "partially_bound")) {
         fails++;
         goto pb_cleanup;
      }

      int match = 1;
      for (uint32_t i = 0; i < 64; i++) {
         uint32_t exp = (0xA0u + i) + ((0xC0u + i) << 16u);
         if (p_out[i] != exp) {
            printf("FAIL case partially_bound mismatch at [%u]: got 0x%08x exp 0x%08x\n", i, p_out[i], exp);
            fails++;
            match = 0;
            break;
         }
      }
      if (match) {
         printf("PASS case partially_bound\n");
         passes++;
      }

pb_cleanup:
      if (pipe)
         p_DestroyPipeline(dev, pipe, NULL);
      if (dpool)
         p_DestroyDescriptorPool(dev, dpool, NULL);
      if (playout)
         p_DestroyPipelineLayout(dev, playout, NULL);
      if (dsl)
         p_DestroyDescriptorSetLayout(dev, dsl, NULL);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &buf_2);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &buf_0);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &out_buf);
   }

   /* ──────────────────────────────────────────────────────────────────────────
    * Case 5: inline_uniform
    * ────────────────────────────────────────────────────────────────────────── */
   if (!can_chain_inline_ub || !supported_inline_ub.inlineUniformBlock) {
      printf("SKIP case inline_uniform inlineUniformBlock missing\n");
      skips++;
   } else {
      struct exec_buf out_buf = {0};
      VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
      VkPipelineLayout playout = VK_NULL_HANDLE;
      VkDescriptorPool dpool = VK_NULL_HANDLE;
      VkPipeline pipe = VK_NULL_HANDLE;

      if (create_buffer(dev, &mp, 64 * sizeof(uint32_t),
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        NULL, &out_buf)) {
         printf("FAIL case inline_uniform create_buffer\n");
         fails++;
         goto iub_cleanup;
      }

      uint32_t *p_out = (uint32_t *)out_buf.map;
      for (uint32_t i = 0; i < 64; i++)
         p_out[i] = 0xDEADBEEFu;

      uint32_t inline_data[16];
      for (uint32_t k = 0; k < 16; k++)
         inline_data[k] = 0xB000u + k * 7u;

      VkDescriptorSetLayoutBinding bnds[2] = {
         {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
         },
         {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK,
            .descriptorCount = 64, /* 64 bytes */
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
         },
      };
      VkDescriptorSetLayoutCreateInfo dslci = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
         .bindingCount = 2,
         .pBindings = bnds,
      };
      VkResult vr = p_CreateDescriptorSetLayout(dev, &dslci, NULL, &dsl);
      if (vr != VK_SUCCESS) {
         printf("FAIL case inline_uniform CreateDescriptorSetLayout r=%d\n", (int)vr);
         fails++;
         goto iub_cleanup;
      }

      VkPipelineLayoutCreateInfo plci = {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
         .setLayoutCount = 1,
         .pSetLayouts = &dsl,
      };
      vr = p_CreatePipelineLayout(dev, &plci, NULL, &playout);
      if (vr != VK_SUCCESS) {
         printf("FAIL case inline_uniform CreatePipelineLayout r=%d\n", (int)vr);
         fails++;
         goto iub_cleanup;
      }

      VkDescriptorPoolInlineUniformBlockCreateInfo pool_inline_info = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_INLINE_UNIFORM_BLOCK_CREATE_INFO,
         .maxInlineUniformBlockBindings = 1,
      };
      VkDescriptorPoolSize pool_sizes[2] = {
         {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
         },
         {
            .type = VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK,
            .descriptorCount = 64,
         },
      };
      VkDescriptorPoolCreateInfo dpci = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
         .pNext = &pool_inline_info,
         .maxSets = 1,
         .poolSizeCount = 2,
         .pPoolSizes = pool_sizes,
      };
      vr = p_CreateDescriptorPool(dev, &dpci, NULL, &dpool);
      if (vr != VK_SUCCESS) {
         printf("FAIL case inline_uniform CreateDescriptorPool r=%d\n", (int)vr);
         fails++;
         goto iub_cleanup;
      }

      VkDescriptorSetAllocateInfo dsai = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
         .descriptorPool = dpool,
         .descriptorSetCount = 1,
         .pSetLayouts = &dsl,
      };
      VkDescriptorSet dset = VK_NULL_HANDLE;
      vr = p_AllocateDescriptorSets(dev, &dsai, &dset);
      if (vr != VK_SUCCESS) {
         printf("FAIL case inline_uniform AllocateDescriptorSets r=%d\n", (int)vr);
         fails++;
         goto iub_cleanup;
      }

      VkDescriptorBufferInfo dbi_out = {
         .buffer = out_buf.buffer,
         .offset = 0,
         .range = 64 * sizeof(uint32_t),
      };
      VkWriteDescriptorSetInlineUniformBlock write_inline1 = {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK,
         .dataSize = 32,
         .pData = &inline_data[0],
      };
      VkWriteDescriptorSetInlineUniformBlock write_inline2 = {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK,
         .dataSize = 32,
         .pData = &inline_data[8],
      };
      VkWriteDescriptorSet wds[3] = {
         {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = dset,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &dbi_out,
         },
         {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = &write_inline1,
            .dstSet = dset,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 32,
            .descriptorType = VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK,
         },
         {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = &write_inline2,
            .dstSet = dset,
            .dstBinding = 1,
            .dstArrayElement = 32,
            .descriptorCount = 32,
            .descriptorType = VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK,
         },
      };
      p_UpdateDescriptorSets(dev, 3, wds, 0, NULL);

      vr = create_compute_pipe(dev, p_CreateShaderModule, p_DestroyShaderModule,
                               p_CreateComputePipelines, inline_uniform_spv,
                               sizeof(inline_uniform_spv), playout, &pipe);
      if (vr != VK_SUCCESS) {
         printf("FAIL case inline_uniform CreateComputePipelines r=%d\n", (int)vr);
         fails++;
         goto iub_cleanup;
      }

      VkCommandBufferBeginInfo bi = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      };
      p_BeginCommandBuffer(cmd, &bi);
      p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
      p_CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &dset, 0, NULL);
      p_CmdDispatch(cmd, 1, 1, 1);
      VkMemoryBarrier host_barrier = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
      };
      p_CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_HOST_BIT, 0,
                           1, &host_barrier, 0, NULL, 0, NULL);
      p_EndCommandBuffer(cmd);

      VkSubmitInfo si = {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .commandBufferCount = 1,
         .pCommandBuffers = &cmd,
      };
      vr = p_QueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
      if (vr != VK_SUCCESS) {
         printf("FAIL case inline_uniform QueueSubmit r=%d\n", (int)vr);
         fails++;
         goto iub_cleanup;
      }
      if (!check_wait_idle(queue, p_QueueWaitIdle, "inline_uniform")) {
         fails++;
         goto iub_cleanup;
      }

      int match = 1;
      for (uint32_t i = 0; i < 64; i++) {
         uint32_t exp = inline_data[i % 16u] + i;
         if (p_out[i] != exp) {
            printf("FAIL case inline_uniform mismatch at [%u]: got 0x%08x exp 0x%08x\n", i, p_out[i], exp);
            fails++;
            match = 0;
            break;
         }
      }
      if (match) {
         printf("PASS case inline_uniform\n");
         passes++;
      }

iub_cleanup:
      if (pipe)
         p_DestroyPipeline(dev, pipe, NULL);
      if (dpool)
         p_DestroyDescriptorPool(dev, dpool, NULL);
      if (playout)
         p_DestroyPipelineLayout(dev, playout, NULL);
      if (dsl)
         p_DestroyDescriptorSetLayout(dev, dsl, NULL);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &out_buf);
   }

   /* ──────────────────────────────────────────────────────────────────────────
    * Case 6: scalar_layout
    * ────────────────────────────────────────────────────────────────────────── */
   if (!supported_f12.scalarBlockLayout) {
      printf("SKIP case scalar_layout scalarBlockLayout missing\n");
      skips++;
   } else {
      struct exec_buf out_buf = {0};
      struct exec_buf in_buf = {0};
      VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
      VkPipelineLayout playout = VK_NULL_HANDLE;
      VkDescriptorPool dpool = VK_NULL_HANDLE;
      VkPipeline pipe = VK_NULL_HANDLE;

      if (create_buffer(dev, &mp, 64 * 4 * sizeof(uint32_t),
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        NULL, &out_buf) ||
          create_buffer(dev, &mp, 64 * 28,
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        NULL, &in_buf)) {
         printf("FAIL case scalar_layout create_buffer\n");
         fails++;
         goto scalar_cleanup;
      }

      uint32_t *p_out = (uint32_t *)out_buf.map;
      for (uint32_t j = 0; j < 256; j++)
         p_out[j] = 0xDEADBEEFu;

      uint32_t *p_in = (uint32_t *)in_buf.map;
      for (uint32_t i = 0; i < 64; i++) {
         float a_x = (float)i;
         float a_y = (float)i + 0.5f;
         float a_z = (float)i + 0.25f;
         uint32_t u_a_x, u_a_y, u_a_z;
         memcpy(&u_a_x, &a_x, sizeof(float));
         memcpy(&u_a_y, &a_y, sizeof(float));
         memcpy(&u_a_z, &a_z, sizeof(float));

         p_in[7 * i + 0] = u_a_x;
         p_in[7 * i + 1] = u_a_y;
         p_in[7 * i + 2] = u_a_z;
         p_in[7 * i + 3] = 0xE000u + i;
         p_in[7 * i + 4] = i * 2u;
         p_in[7 * i + 5] = i * 3u;
         p_in[7 * i + 6] = i * 5u;
      }

      VkDescriptorSetLayoutBinding bnds[2] = {
         {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
         },
         {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
         },
      };
      VkDescriptorSetLayoutCreateInfo dslci = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
         .bindingCount = 2,
         .pBindings = bnds,
      };
      VkResult vr = p_CreateDescriptorSetLayout(dev, &dslci, NULL, &dsl);
      if (vr != VK_SUCCESS) {
         printf("FAIL case scalar_layout CreateDescriptorSetLayout r=%d\n", (int)vr);
         fails++;
         goto scalar_cleanup;
      }

      VkPipelineLayoutCreateInfo plci = {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
         .setLayoutCount = 1,
         .pSetLayouts = &dsl,
      };
      vr = p_CreatePipelineLayout(dev, &plci, NULL, &playout);
      if (vr != VK_SUCCESS) {
         printf("FAIL case scalar_layout CreatePipelineLayout r=%d\n", (int)vr);
         fails++;
         goto scalar_cleanup;
      }

      VkDescriptorPoolSize ps = {
         .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .descriptorCount = 2,
      };
      VkDescriptorPoolCreateInfo dpci = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
         .maxSets = 1,
         .poolSizeCount = 1,
         .pPoolSizes = &ps,
      };
      vr = p_CreateDescriptorPool(dev, &dpci, NULL, &dpool);
      if (vr != VK_SUCCESS) {
         printf("FAIL case scalar_layout CreateDescriptorPool r=%d\n", (int)vr);
         fails++;
         goto scalar_cleanup;
      }

      VkDescriptorSetAllocateInfo dsai = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
         .descriptorPool = dpool,
         .descriptorSetCount = 1,
         .pSetLayouts = &dsl,
      };
      VkDescriptorSet dset = VK_NULL_HANDLE;
      vr = p_AllocateDescriptorSets(dev, &dsai, &dset);
      if (vr != VK_SUCCESS) {
         printf("FAIL case scalar_layout AllocateDescriptorSets r=%d\n", (int)vr);
         fails++;
         goto scalar_cleanup;
      }

      VkDescriptorBufferInfo dbi_out = {
         .buffer = out_buf.buffer,
         .offset = 0,
         .range = 64 * 4 * sizeof(uint32_t),
      };
      VkDescriptorBufferInfo dbi_in = {
         .buffer = in_buf.buffer,
         .offset = 0,
         .range = 64 * 28,
      };
      VkWriteDescriptorSet wds[2] = {
         {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = dset,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &dbi_out,
         },
         {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = dset,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &dbi_in,
         },
      };
      p_UpdateDescriptorSets(dev, 2, wds, 0, NULL);

      vr = create_compute_pipe(dev, p_CreateShaderModule, p_DestroyShaderModule,
                               p_CreateComputePipelines, scalar_layout_spv,
                               sizeof(scalar_layout_spv), playout, &pipe);
      if (vr != VK_SUCCESS) {
         printf("FAIL case scalar_layout CreateComputePipelines r=%d\n", (int)vr);
         fails++;
         goto scalar_cleanup;
      }

      VkCommandBufferBeginInfo bi = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      };
      p_BeginCommandBuffer(cmd, &bi);
      p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
      p_CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &dset, 0, NULL);
      p_CmdDispatch(cmd, 1, 1, 1);
      VkMemoryBarrier host_barrier = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
      };
      p_CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_HOST_BIT, 0,
                           1, &host_barrier, 0, NULL, 0, NULL);
      p_EndCommandBuffer(cmd);

      VkSubmitInfo si = {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .commandBufferCount = 1,
         .pCommandBuffers = &cmd,
      };
      vr = p_QueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
      if (vr != VK_SUCCESS) {
         printf("FAIL case scalar_layout QueueSubmit r=%d\n", (int)vr);
         fails++;
         goto scalar_cleanup;
      }
      if (!check_wait_idle(queue, p_QueueWaitIdle, "scalar_layout")) {
         fails++;
         goto scalar_cleanup;
      }

      int match = 1;
      for (uint32_t i = 0; i < 64; i++) {
         float a_x = (float)i;
         float a_y = (float)i + 0.5f;
         uint32_t u_a_y;
         memcpy(&u_a_y, &a_y, sizeof(float));

         uint32_t exps[4] = {
            u_a_y,
            0xE000u + i,
            i * 5u,
            (uint32_t)a_x + (i * 2u),
         };

         for (uint32_t comp = 0; comp < 4; comp++) {
            uint32_t idx = 4u * i + comp;
            if (p_out[idx] != exps[comp]) {
               printf("FAIL case scalar_layout mismatch at [%u]: got 0x%08x exp 0x%08x\n",
                      idx, p_out[idx], exps[comp]);
               fails++;
               match = 0;
               break;
            }
         }
         if (!match)
            break;
      }
      if (match) {
         printf("PASS case scalar_layout\n");
         passes++;
      }

scalar_cleanup:
      if (pipe)
         p_DestroyPipeline(dev, pipe, NULL);
      if (dpool)
         p_DestroyDescriptorPool(dev, dpool, NULL);
      if (playout)
         p_DestroyPipelineLayout(dev, playout, NULL);
      if (dsl)
         p_DestroyDescriptorSetLayout(dev, dsl, NULL);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &in_buf);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &out_buf);
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
   if (skips == 6) {
      printf("RESULT SKIP\n");
      return 0;
   }
   printf("RESULT PASS\n");
   return 0;
}

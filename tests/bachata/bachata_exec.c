/* Bachata S4 Vulkan Execution Tests.
 * Executes compute workloads for features critical to Bachata S4 PS4 emulator:
 *   (a) int64_atomics
 *   (b) null_descriptor
 *   (c) bda_int64
 *
 * Usage: bachata_exec <libvulkan_panfrost.so>
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

#include "bachata_exec_spv.h"

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

static int
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
   if (p_CreateShaderModule(dev, &smci, NULL, &sm) != VK_SUCCESS)
      return 1;

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
   VkResult r = p_CreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, pipeline);
   p_DestroyShaderModule(dev, sm, NULL);
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

   int has_robust2 = has_extension(exts, ext_count, "VK_EXT_robustness2");
   free(exts);

   /* Query Features */
   VkPhysicalDeviceVulkan12Features supported_f12 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
   };
   VkPhysicalDeviceRobustness2FeaturesEXT supported_rob2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
   };
   if (has_robust2) {
      supported_f12.pNext = &supported_rob2;
   }
   VkPhysicalDeviceFeatures2 supported_f2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &supported_f12,
   };
   p_GetPhysicalDeviceFeatures2(phys, &supported_f2);

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
   if (has_robust2)
      dev_exts[dev_ext_count++] = "VK_EXT_robustness2";

   VkPhysicalDeviceFeatures2 dev_feat2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
   };
   dev_feat2.features.shaderInt64 = supported_f2.features.shaderInt64;

   VkPhysicalDeviceVulkan12Features dev_f12 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .shaderBufferInt64Atomics = supported_f12.shaderBufferInt64Atomics,
      .bufferDeviceAddress = supported_f12.bufferDeviceAddress,
   };
   dev_feat2.pNext = &dev_f12;

   VkPhysicalDeviceRobustness2FeaturesEXT dev_rob2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
   };
   if (has_robust2) {
      dev_rob2.robustBufferAccess2 = supported_rob2.robustBufferAccess2;
      dev_rob2.nullDescriptor = supported_rob2.nullDescriptor;
      dev_f12.pNext = &dev_rob2;
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
    * Case (a): int64_atomics
    * ────────────────────────────────────────────────────────────────────────── */
   if (!supported_f2.features.shaderInt64 || !supported_f12.shaderBufferInt64Atomics) {
      printf("SKIP case int64_atomics shaderInt64 or shaderBufferInt64Atomics missing\n");
      skips++;
   } else {
      struct exec_buf atom_buf;
      if (create_buffer(dev, &mp, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        NULL, &atom_buf)) {
         printf("FAIL case int64_atomics buffer creation\n");
         fails++;
      } else {
         uint64_t *p_atom = (uint64_t *)atom_buf.map;
         p_atom[0] = 0;
         p_atom[1] = 0;

         VkDescriptorSetLayoutBinding bnd = {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
         };
         VkDescriptorSetLayoutCreateInfo dslci = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1,
            .pBindings = &bnd,
         };
         VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
         p_CreateDescriptorSetLayout(dev, &dslci, NULL, &dsl);

         VkPipelineLayoutCreateInfo plci = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &dsl,
         };
         VkPipelineLayout playout = VK_NULL_HANDLE;
         p_CreatePipelineLayout(dev, &plci, NULL, &playout);

         VkDescriptorPoolSize pool_size = {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
         };
         VkDescriptorPoolCreateInfo dpci = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 1,
            .poolSizeCount = 1,
            .pPoolSizes = &pool_size,
         };
         VkDescriptorPool dpool = VK_NULL_HANDLE;
         p_CreateDescriptorPool(dev, &dpci, NULL, &dpool);

         VkDescriptorSetAllocateInfo dsai = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = dpool,
            .descriptorSetCount = 1,
            .pSetLayouts = &dsl,
         };
         VkDescriptorSet dset = VK_NULL_HANDLE;
         p_AllocateDescriptorSets(dev, &dsai, &dset);

         VkDescriptorBufferInfo dbi = {
            .buffer = atom_buf.buffer,
            .offset = 0,
            .range = 16,
         };
         VkWriteDescriptorSet wds = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = dset,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &dbi,
         };
         p_UpdateDescriptorSets(dev, 1, &wds, 0, NULL);

         VkPipeline pipe = VK_NULL_HANDLE;
         if (create_compute_pipe(dev, p_CreateShaderModule, p_DestroyShaderModule,
                                 p_CreateComputePipelines, int64_atomics_spv,
                                 sizeof(int64_atomics_spv), playout, &pipe)) {
            printf("FAIL case int64_atomics pipeline creation\n");
            fails++;
         } else {
            VkCommandBufferBeginInfo bi = {
               .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            };
            p_BeginCommandBuffer(cmd, &bi);
            p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
            p_CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &dset, 0, NULL);
            p_CmdDispatch(cmd, 64, 1, 1);
            p_EndCommandBuffer(cmd);

            VkSubmitInfo si = {
               .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
               .commandBufferCount = 1,
               .pCommandBuffers = &cmd,
            };
            p_QueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
            p_QueueWaitIdle(queue);

            uint64_t exp_counter = 4096ULL * ((1ULL << 32) + 1ULL);
            uint64_t exp_max = 4095ULL + 1000ULL;

            if (p_atom[0] == exp_counter && p_atom[1] == exp_max) {
               printf("PASS case int64_atomics\n");
               passes++;
            } else {
               printf("FAIL case int64_atomics counter=0x%" PRIx64 " (exp 0x%" PRIx64 ") max=%" PRIu64 " (exp %" PRIu64 ")\n",
                      p_atom[0], exp_counter, p_atom[1], exp_max);
               fails++;
            }
            p_DestroyPipeline(dev, pipe, NULL);
         }
         p_DestroyDescriptorPool(dev, dpool, NULL);
         p_DestroyPipelineLayout(dev, playout, NULL);
         p_DestroyDescriptorSetLayout(dev, dsl, NULL);
         destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &atom_buf);
      }
   }

   /* ──────────────────────────────────────────────────────────────────────────
    * Case (b): null_descriptor
    * ────────────────────────────────────────────────────────────────────────── */
   /* The out-of-bounds half needs robustBufferAccess2. Without it the real
    * buffer range covers index 1000 (zero-filled), so only the null-descriptor
    * read is checked and no undefined OOB access happens. */
   const int oob = has_robust2 && supported_rob2.robustBufferAccess2;
   if (!has_robust2 || !supported_rob2.nullDescriptor) {
      printf("SKIP case null_descriptor robustness2 nullDescriptor missing\n");
      skips++;
   } else {
      if (!oob)
         printf("INFO case null_descriptor robustBufferAccess2 missing, OOB read not tested\n");
      struct exec_buf out_buf;
      struct exec_buf real_buf;

      if (create_buffer(dev, &mp, 512, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        NULL, &out_buf) ||
          create_buffer(dev, &mp, 4096, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        NULL, &real_buf)) {
         printf("FAIL case null_descriptor buffer creation\n");
         fails++;
      } else {
         uint32_t *p_out = (uint32_t *)out_buf.map;
         for (int i = 0; i < 128; i++)
            p_out[i] = 0xDEADBEEFu;

         uint32_t *p_real = (uint32_t *)real_buf.map;
         memset(p_real, 0, 4096);
         p_real[0] = 0x11111111u;
         p_real[1] = 0x22222222u;
         p_real[2] = 0x33333333u;
         p_real[3] = 0x44444444u;

         VkDescriptorSetLayoutBinding bnds[3] = {
            { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
            { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
            { .binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
         };
         VkDescriptorSetLayoutCreateInfo dslci = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 3,
            .pBindings = bnds,
         };
         VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
         p_CreateDescriptorSetLayout(dev, &dslci, NULL, &dsl);

         VkPipelineLayoutCreateInfo plci = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &dsl,
         };
         VkPipelineLayout playout = VK_NULL_HANDLE;
         p_CreatePipelineLayout(dev, &plci, NULL, &playout);

         VkDescriptorPoolSize pool_size = {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 3,
         };
         VkDescriptorPoolCreateInfo dpci = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 1,
            .poolSizeCount = 1,
            .pPoolSizes = &pool_size,
         };
         VkDescriptorPool dpool = VK_NULL_HANDLE;
         p_CreateDescriptorPool(dev, &dpci, NULL, &dpool);

         VkDescriptorSetAllocateInfo dsai = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = dpool,
            .descriptorSetCount = 1,
            .pSetLayouts = &dsl,
         };
         VkDescriptorSet dset = VK_NULL_HANDLE;
         p_AllocateDescriptorSets(dev, &dsai, &dset);

         VkDescriptorBufferInfo dbi0 = {
            .buffer = VK_NULL_HANDLE,
            .offset = 0,
            .range = VK_WHOLE_SIZE,
         };
         VkDescriptorBufferInfo dbi1 = {
            .buffer = out_buf.buffer,
            .offset = 0,
            .range = 512,
         };
         VkDescriptorBufferInfo dbi2 = {
            .buffer = real_buf.buffer,
            .offset = 0,
            .range = oob ? 16 : 4096,
         };

         VkWriteDescriptorSet wds[3] = {
            { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = dset, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &dbi0 },
            { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = dset, .dstBinding = 1, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &dbi1 },
            { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = dset, .dstBinding = 2, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &dbi2 },
         };
         p_UpdateDescriptorSets(dev, 3, wds, 0, NULL);

         VkPipeline pipe = VK_NULL_HANDLE;
         if (create_compute_pipe(dev, p_CreateShaderModule, p_DestroyShaderModule,
                                 p_CreateComputePipelines, null_descriptor_spv,
                                 sizeof(null_descriptor_spv), playout, &pipe)) {
            printf("FAIL case null_descriptor pipeline creation\n");
            fails++;
         } else {
            VkCommandBufferBeginInfo bi = {
               .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            };
            p_BeginCommandBuffer(cmd, &bi);
            p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
            p_CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &dset, 0, NULL);
            p_CmdDispatch(cmd, 1, 1, 1);
            p_EndCommandBuffer(cmd);

            VkSubmitInfo si = {
               .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
               .commandBufferCount = 1,
               .pCommandBuffers = &cmd,
            };
            p_QueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
            p_QueueWaitIdle(queue);

            int all_zeros = 1;
            for (int i = 0; i < 128; i++) {
               if (p_out[i] != 0) {
                  all_zeros = 0;
                  break;
               }
            }

            if (all_zeros) {
               printf("PASS case null_descriptor\n");
               passes++;
            } else {
               printf("FAIL case null_descriptor output not all zeros (e.g. out[0]=0x%08x)\n", p_out[0]);
               fails++;
            }
            p_DestroyPipeline(dev, pipe, NULL);
         }
         p_DestroyDescriptorPool(dev, dpool, NULL);
         p_DestroyPipelineLayout(dev, playout, NULL);
         p_DestroyDescriptorSetLayout(dev, dsl, NULL);
         destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &out_buf);
         destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &real_buf);
      }
   }

   /* ──────────────────────────────────────────────────────────────────────────
    * Case (c): bda_int64
    * ────────────────────────────────────────────────────────────────────────── */
   if (!supported_f2.features.shaderInt64 || !supported_f12.bufferDeviceAddress || !g_bda) {
      printf("SKIP case bda_int64 shaderInt64 or bufferDeviceAddress missing\n");
      skips++;
   } else {
      struct exec_buf bda_buf;
      if (create_buffer(dev, &mp, 512,
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                        VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        g_bda, &bda_buf)) {
         printf("FAIL case bda_int64 buffer creation\n");
         fails++;
      } else {
         uint64_t *p_bda = (uint64_t *)bda_buf.map;
         memset(p_bda, 0, 512);

         VkPushConstantRange pcr = {
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0,
            .size = sizeof(uint64_t),
         };
         VkPipelineLayoutCreateInfo plci = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pcr,
         };
         VkPipelineLayout playout = VK_NULL_HANDLE;
         p_CreatePipelineLayout(dev, &plci, NULL, &playout);

         VkPipeline pipe = VK_NULL_HANDLE;
         if (create_compute_pipe(dev, p_CreateShaderModule, p_DestroyShaderModule,
                                 p_CreateComputePipelines, bda_int64_spv,
                                 sizeof(bda_int64_spv), playout, &pipe)) {
            printf("FAIL case bda_int64 pipeline creation\n");
            fails++;
         } else {
            VkCommandBufferBeginInfo bi = {
               .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            };
            p_BeginCommandBuffer(cmd, &bi);
            p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
            p_CmdPushConstants(cmd, playout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                               sizeof(uint64_t), &bda_buf.address);
            p_CmdDispatch(cmd, 1, 1, 1);
            p_EndCommandBuffer(cmd);

            VkSubmitInfo si = {
               .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
               .commandBufferCount = 1,
               .pCommandBuffers = &cmd,
            };
            p_QueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
            p_QueueWaitIdle(queue);

            int match = 1;
            for (uint64_t i = 0; i < 64; i++) {
               uint64_t exp = i * 0x100000001ULL;
               if (p_bda[i] != exp) {
                  match = 0;
                  break;
               }
            }

            if (match) {
               printf("PASS case bda_int64\n");
               passes++;
            } else {
               printf("FAIL case bda_int64 value mismatch (got 0x%" PRIx64 " exp 0x%" PRIx64 ")\n",
                      p_bda[0], (uint64_t)0);
               fails++;
            }
            p_DestroyPipeline(dev, pipe, NULL);
         }
         p_DestroyPipelineLayout(dev, playout, NULL);
         destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &bda_buf);
      }
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
   if (skips == 3) {
      printf("RESULT SKIP\n");
      return 0;
   }
   printf("RESULT PASS\n");
   return 0;
}

/* Bachata S4 Vulkan Execution Test: Formatless Storage Images.
 * Tests shaderStorageImageReadWithoutFormat and shaderStorageImageWriteWithoutFormat
 * on storage images across 4 formats:
 *   - VK_FORMAT_R32_SFLOAT
 *   - VK_FORMAT_R8G8B8A8_UNORM
 *   - VK_FORMAT_R16G16B16A16_SFLOAT
 *   - VK_FORMAT_R32G32B32A32_SFLOAT
 *
 * Usage: storage_fmtless <libvulkan_panfrost.so>
 *
 * Header regeneration:
 *   glslangValidator -V --target-env vulkan1.3 --vn storage_fmtless_spv -o /tmp/x.h tests/bachata/storage_fmtless.comp
 *   sed 's/^const uint32_t/static const uint32_t/' /tmp/x.h > tests/bachata/storage_fmtless_spv.h
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

#include "storage_fmtless_spv.h"

typedef PFN_vkVoidFunction (*icd_gipa_fn)(VkInstance, const char *);

struct exec_buf {
   VkBuffer buffer;
   VkDeviceMemory memory;
   VkDeviceSize size;
   void *map;
};

struct fmt_case {
   const char *name;
   VkFormat format;
   uint32_t channels;
   uint32_t bytes_per_channel;
};

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
   GI(GetPhysicalDeviceFormatProperties)
   GI(CreateDevice)
   GI(DestroyDevice)
   GI(GetDeviceProcAddr)
   GI(DestroyInstance)
#undef GI

   PFN_vkGetPhysicalDeviceFormatProperties2 p_GetPhysicalDeviceFormatProperties2 =
      (PFN_vkGetPhysicalDeviceFormatProperties2)gipa(inst, "vkGetPhysicalDeviceFormatProperties2");
   if (!p_GetPhysicalDeviceFormatProperties2)
      p_GetPhysicalDeviceFormatProperties2 =
         (PFN_vkGetPhysicalDeviceFormatProperties2)gipa(inst, "vkGetPhysicalDeviceFormatProperties2KHR");

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

   /* Query Features */
   VkPhysicalDeviceFeatures2 supported_f2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
   };
   p_GetPhysicalDeviceFeatures2(phys, &supported_f2);

   if (!supported_f2.features.shaderStorageImageReadWithoutFormat ||
       !supported_f2.features.shaderStorageImageWriteWithoutFormat) {
      printf("SKIP storage_fmtless shaderStorageImageRead/WriteWithoutFormat missing\n");
      printf("RESULT SKIP\n");
      p_DestroyInstance(inst, NULL);
      dlclose(h);
      return 0;
   }

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

   /* Device Creation: enable formatless storage read and write */
   VkPhysicalDeviceFeatures2 dev_feat2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
   };
   dev_feat2.features.shaderStorageImageReadWithoutFormat = VK_TRUE;
   dev_feat2.features.shaderStorageImageWriteWithoutFormat = VK_TRUE;

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
      .enabledExtensionCount = 0,
      .ppEnabledExtensionNames = NULL,
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
   GD(CreateImage)
   GD(DestroyImage)
   GD(GetImageMemoryRequirements)
   GD(BindImageMemory)
   GD(CreateImageView)
   GD(DestroyImageView)
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
   GD(ResetCommandBuffer)
   GD(BeginCommandBuffer)
   GD(EndCommandBuffer)
   GD(CmdPipelineBarrier)
   GD(CmdCopyBufferToImage)
   GD(CmdCopyImageToBuffer)
   GD(CmdBindPipeline)
   GD(CmdBindDescriptorSets)
   GD(CmdDispatch)
   GD(CreateFence)
   GD(DestroyFence)
   GD(WaitForFences)
   GD(QueueSubmit)
   GD(QueueWaitIdle)
#undef GD

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

   /* Create descriptor set layout for formatless storage image at binding 0 */
   VkDescriptorSetLayoutBinding bnd = {
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
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
      return 1;
   }

   VkPipelineLayoutCreateInfo plci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &dsl,
   };
   VkPipelineLayout playout = VK_NULL_HANDLE;
   if (p_CreatePipelineLayout(dev, &plci, NULL, &playout) != VK_SUCCESS) {
      printf("FAIL CreatePipelineLayout\n");
      printf("RESULT FAIL\n");
      return 1;
   }

   VkDescriptorPoolSize pool_size = {
      .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
      .descriptorCount = 4,
   };
   VkDescriptorPoolCreateInfo dpci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 4,
      .poolSizeCount = 1,
      .pPoolSizes = &pool_size,
   };
   VkDescriptorPool dpool = VK_NULL_HANDLE;
   if (p_CreateDescriptorPool(dev, &dpci, NULL, &dpool) != VK_SUCCESS) {
      printf("FAIL CreateDescriptorPool\n");
      printf("RESULT FAIL\n");
      return 1;
   }

   VkPipeline pipe = VK_NULL_HANDLE;
   if (create_compute_pipe(dev, p_CreateShaderModule, p_DestroyShaderModule,
                           p_CreateComputePipelines, storage_fmtless_spv,
                           sizeof(storage_fmtless_spv), playout, &pipe)) {
      printf("FAIL pipeline creation\n");
      printf("RESULT FAIL\n");
      return 1;
   }

   static const struct fmt_case cases[4] = {
      { "r32_sfloat", VK_FORMAT_R32_SFLOAT, 1, 4 },
      { "r8g8b8a8_unorm", VK_FORMAT_R8G8B8A8_UNORM, 4, 1 },
      { "r16g16b16a16_sfloat", VK_FORMAT_R16G16B16A16_SFLOAT, 4, 2 },
      { "r32g32b32a32_sfloat", VK_FORMAT_R32G32B32A32_SFLOAT, 4, 4 },
   };

   static const uint16_t half_table[5] = {
      0x0000, /* 0.0  */
      0x3400, /* 0.25 */
      0x3800, /* 0.5  */
      0x3A00, /* 0.75 */
      0x3C00  /* 1.0  */
   };

   int passes = 0;
   int fails = 0;
   int skips = 0;

   for (int i = 0; i < 4; i++) {
      const struct fmt_case *tc = &cases[i];

      /* Format gating: check optimalTilingFeatures has VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT */
      VkFormatProperties fp;
      p_GetPhysicalDeviceFormatProperties(phys, tc->format, &fp);
      if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) {
         printf("SKIP case %s VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT missing\n", tc->name);
         skips++;
         continue;
      }

      /* Additionally check format properties 2 / 3 without-format bits if available */
      if (p_GetPhysicalDeviceFormatProperties2) {
         VkFormatProperties3 fp3 = {
            .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3,
         };
         VkFormatProperties2 fp2 = {
            .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
            .pNext = &fp3,
         };
         p_GetPhysicalDeviceFormatProperties2(phys, tc->format, &fp2);
         VkFormatFeatureFlags2 needed = VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT |
                                        VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT;
         if ((fp3.optimalTilingFeatures & needed) != needed) {
            printf("SKIP case %s STORAGE_READ/WRITE_WITHOUT_FORMAT feature missing\n", tc->name);
            skips++;
            continue;
         }
      }

      VkDeviceSize buf_size = (VkDeviceSize)16 * 16 * tc->channels * tc->bytes_per_channel;
      struct exec_buf staging;
      if (create_buffer(dev, &mp, buf_size,
                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                        p_CreateBuffer, p_GetBufferMemoryRequirements,
                        p_AllocateMemory, p_BindBufferMemory, p_MapMemory,
                        &staging)) {
         printf("FAIL case %s staging buffer creation\n", tc->name);
         fails++;
         continue;
      }

      /* Fill deterministic pattern */
      for (uint32_t y = 0; y < 16; y++) {
         for (uint32_t x = 0; x < 16; x++) {
            uint32_t pixel_idx = y * 16 + x;
            for (uint32_t c = 0; c < tc->channels; c++) {
               uint32_t offset = pixel_idx * tc->channels + c;
               int k = (int)((x + 2 * y + 3 * c) % 5);
               if (tc->bytes_per_channel == 1) {
                  uint8_t *p8 = (uint8_t *)staging.map;
                  p8[offset] = (uint8_t)((x * 13 + y * 7 + c * 29) & 0xff);
               } else if (tc->bytes_per_channel == 2) {
                  uint16_t *p16 = (uint16_t *)staging.map;
                  p16[offset] = half_table[k];
               } else {
                  float *pf = (float *)staging.map;
                  pf[offset] = (float)k / 4.0f;
               }
            }
         }
      }

      /* Create 16x16 OPTIMAL image with STORAGE | TRANSFER_SRC | TRANSFER_DST */
      VkImageCreateInfo image_ci = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
         .imageType = VK_IMAGE_TYPE_2D,
         .format = tc->format,
         .extent = {
            .width = 16,
            .height = 16,
            .depth = 1,
         },
         .mipLevels = 1,
         .arrayLayers = 1,
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .tiling = VK_IMAGE_TILING_OPTIMAL,
         .usage = VK_IMAGE_USAGE_STORAGE_BIT |
                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                  VK_IMAGE_USAGE_TRANSFER_DST_BIT,
         .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      };
      VkImage img = VK_NULL_HANDLE;
      if (p_CreateImage(dev, &image_ci, NULL, &img) != VK_SUCCESS) {
         printf("FAIL case %s createImage\n", tc->name);
         destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &staging);
         fails++;
         continue;
      }

      VkMemoryRequirements imr;
      p_GetImageMemoryRequirements(dev, img, &imr);

      uint32_t mi = ~0u;
      /* Prefer device-local memory */
      for (uint32_t j = 0; j < mp.memoryTypeCount; j++) {
         if ((imr.memoryTypeBits & (1u << j)) &&
             (mp.memoryTypes[j].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            mi = j;
            break;
         }
      }
      /* Fallback to any compatible type */
      if (mi == ~0u) {
         for (uint32_t j = 0; j < mp.memoryTypeCount; j++) {
            if (imr.memoryTypeBits & (1u << j)) {
               mi = j;
               break;
            }
         }
      }
      if (mi == ~0u) {
         printf("FAIL case %s no compatible memory type for image\n", tc->name);
         p_DestroyImage(dev, img, NULL);
         destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &staging);
         fails++;
         continue;
      }

      VkMemoryAllocateInfo imai = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
         .allocationSize = imr.size,
         .memoryTypeIndex = mi,
      };
      VkDeviceMemory img_mem = VK_NULL_HANDLE;
      if (p_AllocateMemory(dev, &imai, NULL, &img_mem) != VK_SUCCESS) {
         printf("FAIL case %s allocateImageMemory\n", tc->name);
         p_DestroyImage(dev, img, NULL);
         destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &staging);
         fails++;
         continue;
      }

      if (p_BindImageMemory(dev, img, img_mem, 0) != VK_SUCCESS) {
         printf("FAIL case %s bindImageMemory\n", tc->name);
         p_FreeMemory(dev, img_mem, NULL);
         p_DestroyImage(dev, img, NULL);
         destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &staging);
         fails++;
         continue;
      }

      VkImageViewCreateInfo ivci = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
         .image = img,
         .viewType = VK_IMAGE_VIEW_TYPE_2D,
         .format = tc->format,
         .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
      };
      VkImageView view = VK_NULL_HANDLE;
      if (p_CreateImageView(dev, &ivci, NULL, &view) != VK_SUCCESS) {
         printf("FAIL case %s createImageView\n", tc->name);
         p_FreeMemory(dev, img_mem, NULL);
         p_DestroyImage(dev, img, NULL);
         destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &staging);
         fails++;
         continue;
      }

      VkDescriptorSetAllocateInfo dsai = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
         .descriptorPool = dpool,
         .descriptorSetCount = 1,
         .pSetLayouts = &dsl,
      };
      VkDescriptorSet dset = VK_NULL_HANDLE;
      if (p_AllocateDescriptorSets(dev, &dsai, &dset) != VK_SUCCESS) {
         printf("FAIL case %s allocateDescriptorSets\n", tc->name);
         p_DestroyImageView(dev, view, NULL);
         p_FreeMemory(dev, img_mem, NULL);
         p_DestroyImage(dev, img, NULL);
         destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &staging);
         fails++;
         continue;
      }

      VkDescriptorImageInfo dii = {
         .imageView = view,
         .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
      };
      VkWriteDescriptorSet wds = {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = dset,
         .dstBinding = 0,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .pImageInfo = &dii,
      };
      p_UpdateDescriptorSets(dev, 1, &wds, 0, NULL);

      p_ResetCommandBuffer(cmd, 0);

      VkCommandBufferBeginInfo bi = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      };
      if (p_BeginCommandBuffer(cmd, &bi) != VK_SUCCESS) {
         printf("FAIL case %s beginCommandBuffer\n", tc->name);
         p_DestroyImageView(dev, view, NULL);
         p_FreeMemory(dev, img_mem, NULL);
         p_DestroyImage(dev, img, NULL);
         destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &staging);
         fails++;
         continue;
      }

      /* Barrier UNDEFINED -> TRANSFER_DST */
      VkImageMemoryBarrier imb1 = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .srcAccessMask = 0,
         .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
         .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
         .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .image = img,
         .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
      };
      p_CmdPipelineBarrier(cmd,
                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0, 0, NULL, 0, NULL, 1, &imb1);

      /* Copy buffer to image */
      VkBufferImageCopy copy_to_img = {
         .bufferOffset = 0,
         .bufferRowLength = 0,
         .bufferImageHeight = 0,
         .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
         .imageOffset = { 0, 0, 0 },
         .imageExtent = { 16, 16, 1 },
      };
      p_CmdCopyBufferToImage(cmd, staging.buffer, img,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             1, &copy_to_img);

      /* Barrier TRANSFER_DST -> GENERAL (transfer write -> compute shader read|write) */
      VkImageMemoryBarrier imb2 = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
         .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
         .newLayout = VK_IMAGE_LAYOUT_GENERAL,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .image = img,
         .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
      };
      p_CmdPipelineBarrier(cmd,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           0, 0, NULL, 0, NULL, 1, &imb2);

      /* Bind compute pipeline and descriptor set */
      p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
      p_CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &dset, 0, NULL);

      /* Dispatch 2x2x1 */
      p_CmdDispatch(cmd, 2, 2, 1);

      /* Barrier GENERAL -> TRANSFER_SRC (shader write -> transfer read) */
      VkImageMemoryBarrier imb3 = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
         .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
         .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .image = img,
         .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
      };
      p_CmdPipelineBarrier(cmd,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0, 0, NULL, 0, NULL, 1, &imb3);

      /* Copy image back to staging buffer */
      VkBufferImageCopy copy_from_img = {
         .bufferOffset = 0,
         .bufferRowLength = 0,
         .bufferImageHeight = 0,
         .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
         .imageOffset = { 0, 0, 0 },
         .imageExtent = { 16, 16, 1 },
      };
      p_CmdCopyImageToBuffer(cmd, img,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             staging.buffer, 1, &copy_from_img);

      /* Buffer barrier: transfer write -> host read */
      VkBufferMemoryBarrier bmb = {
         .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .buffer = staging.buffer,
         .offset = 0,
         .size = VK_WHOLE_SIZE,
      };
      p_CmdPipelineBarrier(cmd,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_HOST_BIT,
                           0, 0, NULL, 1, &bmb, 0, NULL);

      if (p_EndCommandBuffer(cmd) != VK_SUCCESS) {
         printf("FAIL case %s endCommandBuffer\n", tc->name);
         p_DestroyImageView(dev, view, NULL);
         p_FreeMemory(dev, img_mem, NULL);
         p_DestroyImage(dev, img, NULL);
         destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &staging);
         fails++;
         continue;
      }

      VkFenceCreateInfo fci = {
         .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      };
      VkFence fence = VK_NULL_HANDLE;
      if (p_CreateFence(dev, &fci, NULL, &fence) != VK_SUCCESS) {
         printf("FAIL case %s createFence\n", tc->name);
         printf("RESULT FAIL\n");
         p_DestroyImageView(dev, view, NULL);
         p_FreeMemory(dev, img_mem, NULL);
         p_DestroyImage(dev, img, NULL);
         destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &staging);
         p_DestroyPipeline(dev, pipe, NULL);
         p_DestroyPipelineLayout(dev, playout, NULL);
         p_DestroyDescriptorSetLayout(dev, dsl, NULL);
         p_DestroyDescriptorPool(dev, dpool, NULL);
         p_DestroyCommandPool(dev, pool, NULL);
         p_DestroyDevice(dev, NULL);
         p_DestroyInstance(inst, NULL);
         dlclose(h);
         return 1;
      }

      VkSubmitInfo si = {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .commandBufferCount = 1,
         .pCommandBuffers = &cmd,
      };
      VkResult sr = p_QueueSubmit(queue, 1, &si, fence);
      if (sr != VK_SUCCESS) {
         printf("FAIL case %s submit r=%d\n", tc->name, (int)sr);
         printf("RESULT FAIL\n");
         p_DestroyFence(dev, fence, NULL);
         p_DestroyImageView(dev, view, NULL);
         p_FreeMemory(dev, img_mem, NULL);
         p_DestroyImage(dev, img, NULL);
         destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &staging);
         p_DestroyPipeline(dev, pipe, NULL);
         p_DestroyPipelineLayout(dev, playout, NULL);
         p_DestroyDescriptorSetLayout(dev, dsl, NULL);
         p_DestroyDescriptorPool(dev, dpool, NULL);
         p_DestroyCommandPool(dev, pool, NULL);
         p_DestroyDevice(dev, NULL);
         p_DestroyInstance(inst, NULL);
         dlclose(h);
         return 1;
      }

      /* Wait with 10 s timeout (10,000,000,000 ns) */
      VkResult wr = p_WaitForFences(dev, 1, &fence, VK_TRUE, 10000000000ULL);
      if (wr != VK_SUCCESS) {
         printf("FAIL case %s wait r=%d\n", tc->name, (int)wr);
         printf("RESULT FAIL\n");
         p_DestroyFence(dev, fence, NULL);
         p_DestroyImageView(dev, view, NULL);
         p_FreeMemory(dev, img_mem, NULL);
         p_DestroyImage(dev, img, NULL);
         destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &staging);
         p_DestroyPipeline(dev, pipe, NULL);
         p_DestroyPipelineLayout(dev, playout, NULL);
         p_DestroyDescriptorSetLayout(dev, dsl, NULL);
         p_DestroyDescriptorPool(dev, dpool, NULL);
         p_DestroyCommandPool(dev, pool, NULL);
         p_DestroyDevice(dev, NULL);
         p_DestroyInstance(inst, NULL);
         dlclose(h);
         return 1;
      }
      p_DestroyFence(dev, fence, NULL);

      /* Compare against expected values */
      uint32_t mismatches = 0;
      uint32_t first_x = 0, first_y = 0, first_c = 0;
      uint32_t got_raw = 0, exp_raw = 0;

      for (uint32_t y = 0; y < 16; y++) {
         for (uint32_t x = 0; x < 16; x++) {
            uint32_t pixel_idx = y * 16 + x;
            for (uint32_t c = 0; c < tc->channels; c++) {
               uint32_t offset = pixel_idx * tc->channels + c;
               int k = (int)((x + 2 * y + 3 * c) % 5);
               if (tc->bytes_per_channel == 1) {
                  const uint8_t *p8 = (const uint8_t *)staging.map;
                  uint8_t init_b = (uint8_t)((x * 13 + y * 7 + c * 29) & 0xff);
                  uint8_t exp_b = (uint8_t)(255 - init_b);
                  uint8_t got_b = p8[offset];
                  if (got_b != exp_b) {
                     if (mismatches == 0) {
                        first_x = x;
                        first_y = y;
                        first_c = c;
                        got_raw = got_b;
                        exp_raw = exp_b;
                     }
                     mismatches++;
                  }
               } else if (tc->bytes_per_channel == 2) {
                  const uint16_t *p16 = (const uint16_t *)staging.map;
                  uint16_t exp_h = half_table[4 - k];
                  uint16_t got_h = p16[offset];
                  if (got_h != exp_h) {
                     if (mismatches == 0) {
                        first_x = x;
                        first_y = y;
                        first_c = c;
                        got_raw = got_h;
                        exp_raw = exp_h;
                     }
                     mismatches++;
                  }
               } else {
                  const float *pf = (const float *)staging.map;
                  float exp_f = (float)(4 - k) / 4.0f;
                  float got_f = pf[offset];
                  if (got_f != exp_f) {
                     if (mismatches == 0) {
                        first_x = x;
                        first_y = y;
                        first_c = c;
                        memcpy(&got_raw, &got_f, sizeof(float));
                        memcpy(&exp_raw, &exp_f, sizeof(float));
                     }
                     mismatches++;
                  }
               }
            }
         }
      }

      if (mismatches == 0) {
         printf("PASS case %s\n", tc->name);
         passes++;
      } else {
         if (tc->bytes_per_channel == 1) {
            printf("FAIL case %s mismatches=%u first=(%u,%u,%u) got=0x%02x exp=0x%02x\n",
                   tc->name, mismatches, first_x, first_y, first_c, got_raw, exp_raw);
         } else if (tc->bytes_per_channel == 2) {
            printf("FAIL case %s mismatches=%u first=(%u,%u,%u) got=0x%04x exp=0x%04x\n",
                   tc->name, mismatches, first_x, first_y, first_c, got_raw, exp_raw);
         } else {
            printf("FAIL case %s mismatches=%u first=(%u,%u,%u) got=0x%08x exp=0x%08x\n",
                   tc->name, mismatches, first_x, first_y, first_c, got_raw, exp_raw);
         }
         fails++;
      }

      p_DestroyImageView(dev, view, NULL);
      p_DestroyImage(dev, img, NULL);
      p_FreeMemory(dev, img_mem, NULL);
      destroy_buffer(dev, p_UnmapMemory, p_DestroyBuffer, p_FreeMemory, &staging);
   }

   (void)passes;
   p_DestroyPipeline(dev, pipe, NULL);
   p_DestroyPipelineLayout(dev, playout, NULL);
   p_DestroyDescriptorSetLayout(dev, dsl, NULL);
   p_DestroyDescriptorPool(dev, dpool, NULL);
   p_DestroyCommandPool(dev, pool, NULL);
   p_DestroyDevice(dev, NULL);
   p_DestroyInstance(inst, NULL);
   dlclose(h);

   if (fails > 0) {
      printf("RESULT FAIL\n");
      return 1;
   }
   if (skips == 4) {
      printf("RESULT SKIP\n");
      return 0;
   }
   printf("RESULT PASS\n");
   return 0;
}

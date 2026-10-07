/* Standalone vkd3d-proton descriptor heap execution tests.
 * Usage: vkd3d_heap <ICD.so>
 * Each case owns a fresh device in a supervised child process. In addition to
 * five-second fence waits, process deadlines bound broken ICD calls/teardown.
 * A timed-out pending submission cannot legally have its objects destroyed;
 * exiting its child releases process resources without an unbounded idle wait.
 */
#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <vulkan/vulkan.h>
#include "vkd3d_heap_spv.h"

#define TARGET_COUNT 1000000u
#define FENCE_TIMEOUT_NS 5000000000ull
#define CASE_TIMEOUT_SECONDS 12 /* 4 cases must fit PanProbe's 60 s per-test budget */
#define DEVICE_FUNCTIONS(X) \
   X(GetDeviceQueue) X(CreateBuffer) X(DestroyBuffer) \
   X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory) \
   X(BindBufferMemory) X(MapMemory) X(UnmapMemory) \
   X(CreateBufferView) X(DestroyBufferView) X(CreateImage) X(DestroyImage) \
   X(GetImageMemoryRequirements) X(BindImageMemory) \
   X(CreateImageView) X(DestroyImageView) \
   X(CreateShaderModule) X(DestroyShaderModule) \
   X(GetDescriptorSetLayoutSupport) X(CreateDescriptorSetLayout) \
   X(DestroyDescriptorSetLayout) X(CreateDescriptorPool) X(DestroyDescriptorPool) \
   X(AllocateDescriptorSets) X(UpdateDescriptorSets) \
   X(CreatePipelineLayout) X(DestroyPipelineLayout) \
   X(CreateComputePipelines) X(DestroyPipeline) \
   X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) \
   X(BeginCommandBuffer) X(EndCommandBuffer) X(CmdPipelineBarrier) \
   X(CmdCopyBufferToImage) X(CmdBindPipeline) X(CmdBindDescriptorSets) \
   X(CmdDispatch) X(CreateFence) X(DestroyFence) X(QueueSubmit) X(WaitForFences)
#define DECLARE(n) static PFN_vk##n p_##n;
DEVICE_FUNCTIONS(DECLARE)
DECLARE(DestroyDevice)
DECLARE(DestroyInstance)
DECLARE(GetDeviceProcAddr)
DECLARE(CreateDevice)
#undef DECLARE

typedef PFN_vkVoidFunction (*icd_gipa_fn)(VkInstance, const char *);
enum case_kind { SSBO, TEXEL, IMAGE, MUTABLE, CASE_COUNT };
static const char *case_names[] = { "ssbo_heap", "texel_heap", "image_heap", "mutable_heap" };
static const VkDescriptorType mutable_types[] = {
   VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
   VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
   VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
};
struct context {
   VkInstance instance;
   VkPhysicalDevice physical;
   VkDevice device;
   VkQueue queue;
   uint32_t family;
   VkPhysicalDeviceProperties properties;
   VkPhysicalDeviceMemoryProperties memory;
   VkPhysicalDeviceVulkan12Properties limits;
   VkPhysicalDeviceVulkan12Features features;
   VkBool32 mutable_supported, maintenance4;
   VkFormatProperties r32;
};
struct buffer {
   VkBuffer handle;
   VkDeviceMemory memory;
   void *map;
};
struct image {
   VkImage handle;
   VkDeviceMemory memory;
   VkImageView view;
};
struct heaps {
   uint32_t number, counts[2];
   VkDescriptorType types[2];
   VkDescriptorSetLayout layouts[2];
   VkDescriptorSet sets[2];
   VkDescriptorPool pool;
};

static uint32_t
minimum(uint32_t a, uint32_t b)
{
   return a < b ? a : b;
}

static uint32_t
heap_count(uint32_t per_stage, uint32_t per_set)
{
   return minimum(TARGET_COUNT, minimum(per_stage, per_set));
}

static uint32_t
memory_type(const struct context *c, uint32_t bits, VkMemoryPropertyFlags flags)
{
   for (uint32_t i = 0; i < c->memory.memoryTypeCount; i++)
      if ((bits & (1u << i)) && (c->memory.memoryTypes[i].propertyFlags & flags) == flags)
         return i;
   return UINT32_MAX;
}

static VkResult
create_buffer(struct context *c, VkDeviceSize size, VkBufferUsageFlags usage, struct buffer *b)
{
   VkBufferCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = size, .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   };
   VkResult r = p_CreateBuffer(c->device, &info, NULL, &b->handle);
   if (r != VK_SUCCESS)
      return r;
   VkMemoryRequirements req;
   p_GetBufferMemoryRequirements(c->device, b->handle, &req);
   uint32_t type = memory_type(c, req.memoryTypeBits,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
   if (type == UINT32_MAX)
      return VK_ERROR_FEATURE_NOT_PRESENT;
   VkMemoryAllocateInfo alloc = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = req.size, .memoryTypeIndex = type,
   };
   r = p_AllocateMemory(c->device, &alloc, NULL, &b->memory);
   if (r == VK_SUCCESS)
      r = p_BindBufferMemory(c->device, b->handle, b->memory, 0);
   if (r == VK_SUCCESS)
      r = p_MapMemory(c->device, b->memory, 0, size, 0, &b->map);
   return r;
}

static void
destroy_buffer(struct context *c, struct buffer *b)
{
   if (b->map)
      p_UnmapMemory(c->device, b->memory);
   if (b->handle)
      p_DestroyBuffer(c->device, b->handle, NULL);
   if (b->memory)
      p_FreeMemory(c->device, b->memory, NULL);
}

static VkResult
create_image(struct context *c, struct image *im)
{
   VkImageCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R32_UINT,
      .extent = {1, 1, 1}, .mipLevels = 1, .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };
   VkResult r = p_CreateImage(c->device, &info, NULL, &im->handle);
   if (r != VK_SUCCESS)
      return r;
   VkMemoryRequirements req;
   p_GetImageMemoryRequirements(c->device, im->handle, &req);
   uint32_t type = memory_type(c, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
   if (type == UINT32_MAX)
      type = memory_type(c, req.memoryTypeBits, 0);
   if (type == UINT32_MAX)
      return VK_ERROR_FEATURE_NOT_PRESENT;
   VkMemoryAllocateInfo alloc = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = req.size, .memoryTypeIndex = type,
   };
   r = p_AllocateMemory(c->device, &alloc, NULL, &im->memory);
   if (r == VK_SUCCESS)
      r = p_BindImageMemory(c->device, im->handle, im->memory, 0);
   if (r != VK_SUCCESS)
      return r;
   VkImageViewCreateInfo view = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = im->handle, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R32_UINT,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
   };
   return p_CreateImageView(c->device, &view, NULL, &im->view);
}

static void
destroy_heaps(struct context *c, struct heaps *h)
{
   if (h->pool)
      p_DestroyDescriptorPool(c->device, h->pool, NULL);
   h->pool = VK_NULL_HANDLE;
   for (uint32_t i = 0; i < h->number; i++) {
      if (h->layouts[i])
         p_DestroyDescriptorSetLayout(c->device, h->layouts[i], NULL);
      h->layouts[i] = VK_NULL_HANDLE;
      h->sets[i] = VK_NULL_HANDLE;
   }
}

static VkResult
create_heaps(struct context *c, struct heaps *h)
{
   VkMutableDescriptorTypeListEXT list = {
      .descriptorTypeCount = sizeof(mutable_types) / sizeof(mutable_types[0]),
      .pDescriptorTypes = mutable_types,
   };
   VkMutableDescriptorTypeCreateInfoEXT mutable = {
      .sType = VK_STRUCTURE_TYPE_MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT,
      .mutableDescriptorTypeListCount = 1, .pMutableDescriptorTypeLists = &list,
   };
   VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
      VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
      VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
   VkDescriptorPoolSize sizes[2];
   for (uint32_t i = 0; i < h->number; i++) {
      bool is_mutable = h->types[i] == VK_DESCRIPTOR_TYPE_MUTABLE_EXT;
      VkDescriptorSetLayoutBinding binding = {
         .binding = 0, .descriptorType = h->types[i], .descriptorCount = h->counts[i],
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      };
      VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
         .pNext = is_mutable ? &mutable : NULL,
         .bindingCount = 1, .pBindingFlags = &flags,
      };
      VkDescriptorSetLayoutCreateInfo info = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
         .pNext = &binding_flags,
         .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
         .bindingCount = 1, .pBindings = &binding,
      };
      VkDescriptorSetVariableDescriptorCountLayoutSupport variable = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_LAYOUT_SUPPORT,
      };
      VkDescriptorSetLayoutSupport support = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT, .pNext = &variable,
      };
      p_GetDescriptorSetLayoutSupport(c->device, &info, &support);
      if (!support.supported || variable.maxVariableDescriptorCount < h->counts[i])
         return VK_ERROR_FEATURE_NOT_PRESENT;
      VkResult r = p_CreateDescriptorSetLayout(c->device, &info, NULL, &h->layouts[i]);
      if (r != VK_SUCCESS)
         return r;
      sizes[i] = (VkDescriptorPoolSize){h->types[i], h->counts[i]};
   }
   VkDescriptorPoolCreateInfo pool = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .pNext = h->types[0] == VK_DESCRIPTOR_TYPE_MUTABLE_EXT ? &mutable : NULL,
      .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
      .maxSets = h->number, .poolSizeCount = h->number, .pPoolSizes = sizes,
   };
   VkResult r = p_CreateDescriptorPool(c->device, &pool, NULL, &h->pool);
   if (r != VK_SUCCESS)
      return r;
   VkDescriptorSetVariableDescriptorCountAllocateInfo counts = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
      .descriptorSetCount = h->number, .pDescriptorCounts = h->counts,
   };
   VkDescriptorSetAllocateInfo alloc = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .pNext = &counts,
      .descriptorPool = h->pool, .descriptorSetCount = h->number, .pSetLayouts = h->layouts,
   };
   return p_AllocateDescriptorSets(c->device, &alloc, h->sets);
}

static bool
allocation_failure(VkResult r)
{
   return r == VK_ERROR_OUT_OF_HOST_MEMORY || r == VK_ERROR_OUT_OF_DEVICE_MEMORY ||
          r == VK_ERROR_OUT_OF_POOL_MEMORY || r == VK_ERROR_FRAGMENTED_POOL ||
          r == VK_ERROR_FRAGMENTATION;
}

static bool
make_indices(uint32_t n, uint32_t *indices)
{
   if (n <= 1000)
      return false;
   uint32_t values[] = {0, 1, 7, n / 3, n / 2, n - 1000, n - 2, n - 1};
   for (uint32_t i = 0; i < 8; i++) {
      for (uint32_t j = 0; j < i; j++)
         if (values[j] == values[i])
            return false;
      indices[i] = values[i];
   }
   return true;
}

static void
write_buffer(struct context *c, VkDescriptorSet set, uint32_t binding, uint32_t index,
             VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size)
{
   VkDescriptorBufferInfo info = {buffer, offset, size};
   VkWriteDescriptorSet write = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = set, .dstBinding = binding, .dstArrayElement = index,
      .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &info,
   };
   p_UpdateDescriptorSets(c->device, 1, &write, 0, NULL);
}

static void
write_texel(struct context *c, VkDescriptorSet set, uint32_t index,
            VkDescriptorType type, VkBufferView view)
{
   VkWriteDescriptorSet write = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = set, .dstBinding = 0, .dstArrayElement = index,
      .descriptorCount = 1, .descriptorType = type, .pTexelBufferView = &view,
   };
   p_UpdateDescriptorSets(c->device, 1, &write, 0, NULL);
}

/* Limits count the two small control SSBOs as well as the heap descriptors.
 * Keep N exactly as requested and fail if the resulting pipeline would be
 * illegal, rather than silently reserving descriptors by shrinking N. */
static const char *
check_limits(const struct context *c, enum case_kind kind, const struct heaps *h)
{
   uint64_t sampled = 0, storage_image = 0, storage_buffer = 2;
   if (kind == SSBO)
      storage_buffer += h->counts[0];
   else if (kind == TEXEL) {
      sampled = h->counts[0];
      storage_image = h->counts[1];
   } else if (kind == IMAGE)
      sampled = h->counts[0];
   else {
      /* Each mutable descriptor counts against every allowed type's limit.
       * Sampled image + uniform texel and storage image + storage texel each
       * share one category; an element counts once within that category. */
      sampled = storage_image = h->counts[0];
      storage_buffer += h->counts[0];
   }
#define LIMIT(value, name) if ((value) > c->limits.name) return #name
   LIMIT(sampled, maxPerStageDescriptorUpdateAfterBindSampledImages);
   LIMIT(sampled, maxDescriptorSetUpdateAfterBindSampledImages);
   LIMIT(storage_image, maxPerStageDescriptorUpdateAfterBindStorageImages);
   LIMIT(storage_image, maxDescriptorSetUpdateAfterBindStorageImages);
   LIMIT(storage_buffer, maxPerStageDescriptorUpdateAfterBindStorageBuffers);
   LIMIT(storage_buffer, maxDescriptorSetUpdateAfterBindStorageBuffers);
   uint64_t resources = 2;
   /* Texel arrays execute in separate pipelines, each with an empty set
    * layout in place of the other heap. Both heaps are allocated at full N. */
   if (kind == TEXEL)
      resources += h->counts[0] > h->counts[1] ? h->counts[0] : h->counts[1];
   else
      resources += h->counts[0];
   LIMIT(resources, maxPerStageUpdateAfterBindResources);
#undef LIMIT
   return NULL;
}

static const char *
missing_feature(const struct context *c, enum case_kind kind)
{
#define NEED(bit) if (!c->features.bit) return #bit
   NEED(descriptorIndexing);
   NEED(runtimeDescriptorArray);
   NEED(descriptorBindingPartiallyBound);
   NEED(descriptorBindingVariableDescriptorCount);
   NEED(descriptorBindingUpdateUnusedWhilePending);
   if (kind == SSBO || kind == MUTABLE) {
      NEED(descriptorBindingStorageBufferUpdateAfterBind);
      NEED(shaderStorageBufferArrayNonUniformIndexing);
   }
   if (kind == TEXEL || kind == MUTABLE) {
      NEED(descriptorBindingUniformTexelBufferUpdateAfterBind);
      NEED(shaderUniformTexelBufferArrayNonUniformIndexing);
   }
   if (kind == TEXEL || kind == MUTABLE)
      NEED(descriptorBindingStorageTexelBufferUpdateAfterBind);
   if (kind == TEXEL)
      NEED(shaderStorageTexelBufferArrayNonUniformIndexing);
   if (kind == IMAGE || kind == MUTABLE)
      NEED(descriptorBindingSampledImageUpdateAfterBind);
   if (kind == IMAGE)
      NEED(shaderSampledImageArrayNonUniformIndexing);
   if (kind == MUTABLE)
      NEED(descriptorBindingStorageImageUpdateAfterBind);
#undef NEED
   return NULL;
}

static VkResult
create_device(struct context *c, icd_gipa_fn gipa, const char **missing)
{
   /* Only enable supported bits actually required by these shader/layout types. */
   VkPhysicalDeviceVulkan12Features enabled = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
   };
#define ENABLE(bit) enabled.bit = c->features.bit
   ENABLE(descriptorIndexing);
   ENABLE(runtimeDescriptorArray);
   ENABLE(descriptorBindingPartiallyBound);
   ENABLE(descriptorBindingVariableDescriptorCount);
   ENABLE(descriptorBindingUpdateUnusedWhilePending);
   ENABLE(descriptorBindingSampledImageUpdateAfterBind);
   ENABLE(descriptorBindingStorageImageUpdateAfterBind);
   ENABLE(descriptorBindingStorageBufferUpdateAfterBind);
   ENABLE(descriptorBindingUniformTexelBufferUpdateAfterBind);
   ENABLE(descriptorBindingStorageTexelBufferUpdateAfterBind);
   ENABLE(shaderSampledImageArrayNonUniformIndexing);
   ENABLE(shaderStorageBufferArrayNonUniformIndexing);
   ENABLE(shaderUniformTexelBufferArrayNonUniformIndexing);
   ENABLE(shaderStorageTexelBufferArrayNonUniformIndexing);
#undef ENABLE
   VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT mutable = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT,
      .mutableDescriptorType = c->mutable_supported,
   };
   VkPhysicalDeviceVulkan13Features enabled13 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .pNext = c->mutable_supported ? &mutable : NULL,
      .maintenance4 = c->maintenance4,
   };
   enabled.pNext = &enabled13;
   const char *extension = VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME;
   float priority = 1.0f;
   VkDeviceQueueCreateInfo queue = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = c->family, .queueCount = 1, .pQueuePriorities = &priority,
   };
   VkDeviceCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &enabled,
      .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue,
      .enabledExtensionCount = c->mutable_supported ? 1 : 0,
      .ppEnabledExtensionNames = c->mutable_supported ? &extension : NULL,
   };
   VkResult r = p_CreateDevice(c->physical, &info, NULL, &c->device);
   if (r != VK_SUCCESS)
      return r;
#define LOAD(n) \
   p_##n = (PFN_vk##n)p_GetDeviceProcAddr(c->device, "vk" #n); \
   if (!p_##n) p_##n = (PFN_vk##n)gipa(c->instance, "vk" #n); \
   if (!p_##n) { *missing = "vk" #n; return VK_ERROR_INITIALIZATION_FAILED; }
   DEVICE_FUNCTIONS(LOAD)
#undef LOAD
   p_GetDeviceQueue(c->device, c->family, 0, &c->queue);
   return VK_SUCCESS;
}

static int
run_case(struct context *c, enum case_kind kind, struct heaps h)
{
   const char *name = case_names[kind];
   char reason[256] = "";
   bool reduced = false;
   struct buffer source = {0}, indices = {0}, output = {0};
   struct image images[4] = {0};
   VkBufferView views[18] = {0};
   VkDescriptorPool control_pool = VK_NULL_HANDLE;
   VkDescriptorSetLayout control_layout = VK_NULL_HANDLE;
   VkDescriptorSetLayout empty_layout = VK_NULL_HANDLE;
   VkPipelineLayout pipeline_layout = VK_NULL_HANDLE, storage_pipeline_layout = VK_NULL_HANDLE;
   VkPipeline pipeline = VK_NULL_HANDLE, storage_pipeline = VK_NULL_HANDLE;
   VkShaderModule shader = VK_NULL_HANDLE;
   VkCommandPool command_pool = VK_NULL_HANDLE;
   VkFence fence = VK_NULL_HANDLE;
   VkResult r;
#define CHECK(call) do { \
   r = (call); \
   if (r != VK_SUCCESS) { \
      snprintf(reason, sizeof(reason), "%s r=%d", #call, (int)r); \
      goto done; \
   } \
} while (0)
#define BAD(message) do { snprintf(reason, sizeof(reason), "%s", (message)); goto done; } while (0)
   const char *limit = check_limits(c, kind, &h);
   if (limit)
      BAD(limit);
   if (kind == IMAGE &&
       (c->r32.optimalTilingFeatures & (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT)) !=
       (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT))
      BAD("R32_UINT sampled image/transfer format support");
   if ((kind == TEXEL || kind == MUTABLE) &&
       !(c->r32.bufferFeatures & VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT))
      BAD("R32_UINT uniform texel format support");
   if (kind == TEXEL && !(c->r32.bufferFeatures & VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT))
      BAD("R32_UINT storage texel format support");

   r = create_heaps(c, &h);
   if (allocation_failure(r)) {
      printf("INFO heap_allocation_%s r=%d\n", name, (int)r);
      destroy_heaps(c, &h);
      reduced = true;
      for (uint32_t i = 0; i < h.number; i++) {
         h.counts[i] /= 4;
         printf("INFO heap_count_reduced %u\n", h.counts[i]);
         if (!h.counts[i])
            BAD("heap allocation retry count is zero");
      }
      r = create_heaps(c, &h);
   }
   if (r != VK_SUCCESS) {
      snprintf(reason, sizeof(reason), "heap layout/pool/set allocation r=%d", (int)r);
      goto done;
   }

   uint32_t count = kind == TEXEL ? 16 : kind == IMAGE ? 4 : kind == MUTABLE ? 2 : 8;
   CHECK(create_buffer(c, 16 * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &indices));
   CHECK(create_buffer(c, 16 * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &output));
   uint32_t *idx = indices.map;
   uint32_t *out = output.map;
   uint32_t expected[16] = {0};
   for (uint32_t i = 0; i < 16; i++)
      out[i] = 0xdeadbeefu;
   if (kind == SSBO || kind == TEXEL) {
      if (!make_indices(h.counts[0], idx) ||
          (kind == TEXEL && !make_indices(h.counts[1], idx + 8)))
         BAD("heap count cannot represent eight distinct requested indices");
   } else if (kind == IMAGE) {
      if (h.counts[0] < 6)
         BAD("heap count cannot represent four distinct requested indices");
      idx[0] = 0; idx[1] = h.counts[0] / 2;
      idx[2] = h.counts[0] - 2; idx[3] = h.counts[0] - 1;
   } else {
      if (h.counts[0] < 7)
         BAD("mutable heap count cannot represent indices 5 and N-1");
      idx[0] = 5; idx[1] = h.counts[0] - 1;
   }

   /* A multiple of 256 also satisfying both Vulkan offset alignment limits. */
   VkDeviceSize alignment = c->properties.limits.minStorageBufferOffsetAlignment;
   if (alignment < c->properties.limits.minTexelBufferOffsetAlignment)
      alignment = c->properties.limits.minTexelBufferOffsetAlignment;
   if (alignment < 256)
      alignment = 256;
   VkDeviceSize stride = alignment; /* Vulkan alignment limits are powers of two. */
   VkBufferUsageFlags source_usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
   if (kind == TEXEL || kind == MUTABLE)
      source_usage |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
   if (kind == TEXEL)
      source_usage |= VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
   if (kind == IMAGE)
      source_usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
   CHECK(create_buffer(c, stride * 18, source_usage, &source));
   uint32_t values[18];
   for (uint32_t i = 0; i < 18; i++) {
      values[i] = 0x13570000u + 0x101u * (i + 1);
      *(uint32_t *)((char *)source.map + stride * i) = values[i];
   }

   uint32_t view_count = kind == TEXEL ? 18 : kind == MUTABLE ? 2 : 0;
   for (uint32_t i = 0; i < view_count; i++) {
      VkBufferViewCreateInfo info = {
         .sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
         .buffer = source.handle, .format = VK_FORMAT_R32_UINT,
         .offset = stride * i, .range = sizeof(uint32_t),
      };
      CHECK(p_CreateBufferView(c->device, &info, NULL, &views[i]));
   }
   if (kind == SSBO) {
      for (uint32_t i = 0; i < 8; i++) {
         write_buffer(c, h.sets[0], 0, idx[i], source.handle, stride * i, sizeof(uint32_t));
         expected[i] = values[i];
      }
      expected[7] = values[8];
   } else if (kind == TEXEL) {
      for (uint32_t i = 0; i < 8; i++) {
         write_texel(c, h.sets[0], idx[i], h.types[0], views[i]);
         write_texel(c, h.sets[1], idx[8 + i], h.types[1], views[9 + i]);
         expected[i] = values[i];
         expected[8 + i] = values[9 + i];
      }
      expected[7] = values[8]; expected[15] = values[17];
   } else if (kind == IMAGE) {
      for (uint32_t i = 0; i < 4; i++) {
         CHECK(create_image(c, &images[i]));
         VkDescriptorImageInfo info = {
            .imageView = images[i].view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
         };
         VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = h.sets[0], .dstBinding = 0, .dstArrayElement = idx[i],
            .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo = &info,
         };
         p_UpdateDescriptorSets(c->device, 1, &write, 0, NULL);
         expected[i] = values[i];
      }
   } else {
      write_buffer(c, h.sets[0], 0, 5, source.handle, 0, sizeof(uint32_t));
      write_texel(c, h.sets[0], idx[1], VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, views[1]);
      expected[0] = values[0]; expected[1] = values[1];
   }

   VkDescriptorSetLayoutBinding bindings[] = {
      {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1,
       .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
      {.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1,
       .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
   };
   VkDescriptorSetLayoutCreateInfo layout_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 2, .pBindings = bindings,
   };
   CHECK(p_CreateDescriptorSetLayout(c->device, &layout_info, NULL, &control_layout));
   VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};
   VkDescriptorPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &pool_size,
   };
   CHECK(p_CreateDescriptorPool(c->device, &pool_info, NULL, &control_pool));
   VkDescriptorSet control_set;
   VkDescriptorSetAllocateInfo alloc = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = control_pool, .descriptorSetCount = 1, .pSetLayouts = &control_layout,
   };
   CHECK(p_AllocateDescriptorSets(c->device, &alloc, &control_set));
   write_buffer(c, control_set, 0, 0, indices.handle, 0, 64);
   write_buffer(c, control_set, 1, 0, output.handle, 0, 64);

   VkDescriptorSetLayout layouts[3] = {h.layouts[0], h.layouts[1], VK_NULL_HANDLE};
   VkDescriptorSet sets[3] = {h.sets[0], h.sets[1], VK_NULL_HANDLE};
   if (kind == TEXEL) {
      VkDescriptorSetLayoutCreateInfo empty_info = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      };
      CHECK(p_CreateDescriptorSetLayout(c->device, &empty_info, NULL, &empty_layout));
      layouts[1] = empty_layout;
   }
   layouts[h.number] = control_layout;
   sets[h.number] = control_set;
   VkPipelineLayoutCreateInfo pipeline_layout_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = h.number + 1, .pSetLayouts = layouts,
   };
   CHECK(p_CreatePipelineLayout(c->device, &pipeline_layout_info, NULL, &pipeline_layout));
   const uint32_t *spv[] = {ssbo_heap_spv, uniform_texel_heap_spv, image_heap_spv, mutable_heap_spv};
   const size_t spv_size[] = {sizeof(ssbo_heap_spv), sizeof(uniform_texel_heap_spv), sizeof(image_heap_spv), sizeof(mutable_heap_spv)};
   VkShaderModuleCreateInfo shader_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = spv_size[kind], .pCode = spv[kind],
   };
   CHECK(p_CreateShaderModule(c->device, &shader_info, NULL, &shader));
   VkComputePipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = shader, .pName = "main"},
      .layout = pipeline_layout,
   };
   CHECK(p_CreateComputePipelines(c->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline));
   if (kind == TEXEL) {
      layouts[0] = empty_layout;
      layouts[1] = h.layouts[1];
      CHECK(p_CreatePipelineLayout(c->device, &pipeline_layout_info, NULL, &storage_pipeline_layout));
      p_DestroyShaderModule(c->device, shader, NULL);
      shader = VK_NULL_HANDLE;
      shader_info.codeSize = sizeof(storage_texel_heap_spv);
      shader_info.pCode = storage_texel_heap_spv;
      CHECK(p_CreateShaderModule(c->device, &shader_info, NULL, &shader));
      pipeline_info.stage.module = shader;
      pipeline_info.layout = storage_pipeline_layout;
      CHECK(p_CreateComputePipelines(c->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &storage_pipeline));
   }
   VkCommandPoolCreateInfo command_pool_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = c->family,
   };
   CHECK(p_CreateCommandPool(c->device, &command_pool_info, NULL, &command_pool));
   VkCommandBuffer cmd;
   VkCommandBufferAllocateInfo command_alloc = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = command_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1,
   };
   CHECK(p_AllocateCommandBuffers(c->device, &command_alloc, &cmd));
   VkCommandBufferBeginInfo begin = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
   };
   CHECK(p_BeginCommandBuffer(cmd, &begin));
   if (kind == IMAGE) {
      for (uint32_t i = 0; i < 4; i++) {
         VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = images[i].handle, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
         };
         p_CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                              0, 0, NULL, 0, NULL, 1, &barrier);
         VkBufferImageCopy copy = {
            .bufferOffset = stride * i,
            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {1, 1, 1},
         };
         p_CmdCopyBufferToImage(cmd, source.handle, images[i].handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
         barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
         barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
         barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
         barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
         p_CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                              0, 0, NULL, 0, NULL, 1, &barrier);
      }
   }
   p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
   if (kind == TEXEL) {
      p_CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &sets[0], 0, NULL);
      p_CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 2, 1, &control_set, 0, NULL);
   } else {
      p_CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, h.number + 1, sets, 0, NULL);
   }
   p_CmdDispatch(cmd, 1, 1, 1);
   if (kind == TEXEL) {
      p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, storage_pipeline);
      p_CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, storage_pipeline_layout, 1, 1, &sets[1], 0, NULL);
      p_CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, storage_pipeline_layout, 2, 1, &control_set, 0, NULL);
      p_CmdDispatch(cmd, 1, 1, 1);
   }
   VkMemoryBarrier host_read = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
   };
   p_CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                        0, 1, &host_read, 0, NULL, 0, NULL);
   CHECK(p_EndCommandBuffer(cmd));

   /* This must happen AFTER binding the sets and ending the command buffer. */
   if (kind == SSBO)
      write_buffer(c, h.sets[0], 0, h.counts[0] - 1, source.handle, stride * 8, sizeof(uint32_t));
   else if (kind == TEXEL) {
      write_texel(c, h.sets[0], h.counts[0] - 1, h.types[0], views[8]);
      write_texel(c, h.sets[1], h.counts[1] - 1, h.types[1], views[17]);
   }
   VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   CHECK(p_CreateFence(c->device, &fence_info, NULL, &fence));
   VkSubmitInfo submit = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1, .pCommandBuffers = &cmd,
   };
   CHECK(p_QueueSubmit(c->queue, 1, &submit, fence));
   r = p_WaitForFences(c->device, 1, &fence, VK_TRUE, FENCE_TIMEOUT_NS);
   if (r != VK_SUCCESS) {
      /* No idle wait or destruction of possibly pending resources. The parent
       * continues the other cases after this process releases its resources. */
      printf("FAIL case %s %s r=%d\n", name, r == VK_TIMEOUT ? "fence timeout (5 s)" : "vkWaitForFences", (int)r);
      _Exit(1);
   }
   for (uint32_t i = 0; i < count; i++) {
      if (out[i] != expected[i]) {
         snprintf(reason, sizeof(reason), "output[%u]=0x%08x expected=0x%08x index=%u",
                  i, out[i], expected[i], idx[i]);
         goto done;
      }
   }
   if (reduced)
      snprintf(reason, sizeof(reason), "full heap allocation failed; N/4 retry executed successfully");

done:
   if (fence) p_DestroyFence(c->device, fence, NULL);
   if (command_pool) p_DestroyCommandPool(c->device, command_pool, NULL);
   if (storage_pipeline) p_DestroyPipeline(c->device, storage_pipeline, NULL);
   if (pipeline) p_DestroyPipeline(c->device, pipeline, NULL);
   if (shader) p_DestroyShaderModule(c->device, shader, NULL);
   if (pipeline_layout) p_DestroyPipelineLayout(c->device, pipeline_layout, NULL);
   if (storage_pipeline_layout) p_DestroyPipelineLayout(c->device, storage_pipeline_layout, NULL);
   if (control_pool) p_DestroyDescriptorPool(c->device, control_pool, NULL);
   if (control_layout) p_DestroyDescriptorSetLayout(c->device, control_layout, NULL);
   if (empty_layout) p_DestroyDescriptorSetLayout(c->device, empty_layout, NULL);
   destroy_heaps(c, &h);
   for (uint32_t i = 0; i < 18; i++)
      if (views[i]) p_DestroyBufferView(c->device, views[i], NULL);
   for (uint32_t i = 0; i < 4; i++) {
      if (images[i].view) p_DestroyImageView(c->device, images[i].view, NULL);
      if (images[i].handle) p_DestroyImage(c->device, images[i].handle, NULL);
      if (images[i].memory) p_FreeMemory(c->device, images[i].memory, NULL);
   }
   destroy_buffer(c, &output);
   destroy_buffer(c, &indices);
   destroy_buffer(c, &source);
   p_DestroyDevice(c->device, NULL);
   c->device = VK_NULL_HANDLE;
   p_DestroyInstance(c->instance, NULL);
   if (reason[0])
      printf("FAIL case %s %s\n", name, reason);
   else
      printf("PASS case %s\n", name);
   return reason[0] ? 1 : 0;
#undef CHECK
#undef BAD
}

static void
watchdog(int signal_number)
{
   (void)signal_number;
   static const char message[] = "INFO setup_or_teardown_watchdog_timeout\nRESULT FAIL\n";
   (void)write(STDOUT_FILENO, message, sizeof(message) - 1);
   _Exit(1);
}

static int
supervise_case(struct context *c, icd_gipa_fn gipa, enum case_kind kind, struct heaps h)
{
   const char *name = case_names[kind];
   pid_t child = fork();
   if (child < 0) {
      printf("FAIL case %s fork errno=%d\n", name, errno);
      return 1;
   }
   if (!child) {
      const char *missing = "vkCreateDevice";
      VkResult r = create_device(c, gipa, &missing);
      if (r != VK_SUCCESS) {
         if (c->device)
            p_DestroyDevice(c->device, NULL);
         p_DestroyInstance(c->instance, NULL);
         printf("FAIL case %s %s r=%d\n", name, missing, (int)r);
         _Exit(1);
      }
      int status = run_case(c, kind, h);
      _Exit(status);
   }
   struct timespec start, now;
   clock_gettime(CLOCK_MONOTONIC, &start);
   for (;;) {
      int status;
      pid_t done = waitpid(child, &status, WNOHANG);
      if (done == child) {
         if (WIFEXITED(status))
            return WEXITSTATUS(status) == 0 ? 0 : 1;
         printf("FAIL case %s child terminated signal=%d\n", name,
                WIFSIGNALED(status) ? WTERMSIG(status) : 0);
         return 1;
      }
      if (done < 0 && errno != EINTR) {
         printf("FAIL case %s waitpid errno=%d\n", name, errno);
         kill(child, SIGKILL);
         while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {}
         return 1;
      }
      clock_gettime(CLOCK_MONOTONIC, &now);
      if (now.tv_sec - start.tv_sec >= CASE_TIMEOUT_SECONDS) {
         printf("FAIL case %s ICD watchdog timeout (%d s)\n", name, CASE_TIMEOUT_SECONDS);
         kill(child, SIGKILL);
         while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {}
         return 1;
      }
      struct timespec pause = {.tv_nsec = 10000000};
      nanosleep(&pause, NULL);
   }
}

int
main(int argc, char **argv)
{
   setvbuf(stdout, NULL, _IONBF, 0);
   struct sigaction action = {.sa_handler = watchdog};
   sigemptyset(&action.sa_mask);
   sigaction(SIGALRM, &action, NULL);
   alarm(CASE_TIMEOUT_SECONDS);
   char reason[256] = "";
   void *library = NULL;
   struct context c = {0};
   VkPhysicalDevice *devices = NULL;
   VkExtensionProperties *extensions = NULL;
   VkQueueFamilyProperties *families = NULL;
   int fails = 0, passes = 0, skips = 0;
#define SETUP_BAD(message) do { snprintf(reason, sizeof(reason), "%s", (message)); goto setup_failed; } while (0)
#define SETUP_CHECK(call) do { \
   VkResult result = (call); \
   if (result != VK_SUCCESS) { \
      snprintf(reason, sizeof(reason), "%s r=%d", #call, (int)result); \
      goto setup_failed; \
   } \
} while (0)
   if (argc != 2)
      SETUP_BAD("usage: vkd3d_heap <ICD.so>");
   library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
   if (!library) {
      snprintf(reason, sizeof(reason), "dlopen %s", dlerror());
      goto setup_failed;
   }
   icd_gipa_fn gipa = (icd_gipa_fn)dlsym(library, "vk_icdGetInstanceProcAddr");
   if (!gipa)
      gipa = (icd_gipa_fn)dlsym(library, "vkGetInstanceProcAddr");
   if (!gipa)
      SETUP_BAD("missing vk_icdGetInstanceProcAddr");
   PFN_vkCreateInstance CreateInstance = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
   if (!CreateInstance)
      SETUP_BAD("missing vkCreateInstance");
   VkApplicationInfo app = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "vkd3d_heap", .apiVersion = VK_API_VERSION_1_3,
   };
   VkInstanceCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app,
   };
   SETUP_CHECK(CreateInstance(&info, NULL, &c.instance));
#define GI(n) \
   PFN_vk##n p_##n = (PFN_vk##n)gipa(c.instance, "vk" #n); \
   if (!p_##n) SETUP_BAD("missing vk" #n)
   p_DestroyInstance = (PFN_vkDestroyInstance)gipa(c.instance, "vkDestroyInstance");
   if (!p_DestroyInstance)
      SETUP_BAD("missing vkDestroyInstance");
   GI(EnumeratePhysicalDevices);
   GI(GetPhysicalDeviceProperties);
   GI(GetPhysicalDeviceProperties2);
   GI(GetPhysicalDeviceFeatures2);
   GI(GetPhysicalDeviceMemoryProperties);
   GI(GetPhysicalDeviceQueueFamilyProperties);
   GI(GetPhysicalDeviceFormatProperties);
   GI(EnumerateDeviceExtensionProperties);
#undef GI
#define GI_GLOBAL(n) \
   p_##n = (PFN_vk##n)gipa(c.instance, "vk" #n); \
   if (!p_##n) SETUP_BAD("missing vk" #n)
   GI_GLOBAL(CreateDevice);
   GI_GLOBAL(DestroyDevice);
   GI_GLOBAL(GetDeviceProcAddr);
#undef GI_GLOBAL
   uint32_t number = 0;
   SETUP_CHECK(p_EnumeratePhysicalDevices(c.instance, &number, NULL));
   if (!number)
      SETUP_BAD("no physical devices");
   devices = calloc(number, sizeof(*devices));
   if (!devices)
      SETUP_BAD("physical device host allocation");
   SETUP_CHECK(p_EnumeratePhysicalDevices(c.instance, &number, devices));
   if (!number)
      SETUP_BAD("no physical devices");
   c.physical = devices[0];
   for (uint32_t i = 0; i < number; i++) {
      VkPhysicalDeviceProperties properties;
      p_GetPhysicalDeviceProperties(devices[i], &properties);
      if (strstr(properties.deviceName, "Mali")) {
         c.physical = devices[i];
         break;
      }
   }
   free(devices); devices = NULL;
   c.limits.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
   VkPhysicalDeviceProperties2 properties = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &c.limits,
   };
   p_GetPhysicalDeviceProperties2(c.physical, &properties);
   c.properties = properties.properties;
   printf("INFO device %s\n", c.properties.deviceName);
   printf("INFO limit_ok %u\n",
          c.limits.maxPerStageDescriptorUpdateAfterBindSampledImages >= TARGET_COUNT &&
          c.limits.maxPerStageDescriptorUpdateAfterBindStorageImages >= TARGET_COUNT &&
          c.limits.maxPerStageDescriptorUpdateAfterBindStorageBuffers >= TARGET_COUNT);
   if (c.properties.apiVersion < VK_API_VERSION_1_3)
      SETUP_BAD("physical device Vulkan 1.3 unsupported");
   uint32_t extension_count = 0;
   SETUP_CHECK(p_EnumerateDeviceExtensionProperties(c.physical, NULL, &extension_count, NULL));
   if (extension_count) {
      extensions = calloc(extension_count, sizeof(*extensions));
      if (!extensions)
         SETUP_BAD("extension host allocation");
      SETUP_CHECK(p_EnumerateDeviceExtensionProperties(c.physical, NULL, &extension_count, extensions));
   }
   bool has_mutable = false;
   for (uint32_t i = 0; i < extension_count; i++)
      if (!strcmp(extensions[i].extensionName, VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME))
         has_mutable = true;
   free(extensions); extensions = NULL;
   VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT mutable = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT,
   };
   VkPhysicalDeviceVulkan13Features f13 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .pNext = has_mutable ? &mutable : NULL,
   };
   c.features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
   c.features.pNext = &f13;
   VkPhysicalDeviceFeatures2 features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &c.features,
   };
   p_GetPhysicalDeviceFeatures2(c.physical, &features);
   c.features.pNext = NULL;
   c.maintenance4 = f13.maintenance4;
   c.mutable_supported = has_mutable && mutable.mutableDescriptorType;
   p_GetPhysicalDeviceMemoryProperties(c.physical, &c.memory);
   p_GetPhysicalDeviceFormatProperties(c.physical, VK_FORMAT_R32_UINT, &c.r32);
   uint32_t family_count = 0;
   p_GetPhysicalDeviceQueueFamilyProperties(c.physical, &family_count, NULL);
   if (!family_count)
      SETUP_BAD("no queue families");
   families = calloc(family_count, sizeof(*families));
   if (!families)
      SETUP_BAD("queue family host allocation");
   p_GetPhysicalDeviceQueueFamilyProperties(c.physical, &family_count, families);
   c.family = UINT32_MAX;
   for (uint32_t i = 0; i < family_count; i++)
      if (families[i].queueCount && (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
         c.family = i;
         break;
      }
   free(families); families = NULL;
   if (c.family == UINT32_MAX)
      SETUP_BAD("no compute queue");

   uint32_t storage = heap_count(c.limits.maxPerStageDescriptorUpdateAfterBindStorageBuffers,
                                c.limits.maxDescriptorSetUpdateAfterBindStorageBuffers);
   uint32_t sampled = heap_count(c.limits.maxPerStageDescriptorUpdateAfterBindSampledImages,
                                c.limits.maxDescriptorSetUpdateAfterBindSampledImages);
   uint32_t storage_image = heap_count(c.limits.maxPerStageDescriptorUpdateAfterBindStorageImages,
                                      c.limits.maxDescriptorSetUpdateAfterBindStorageImages);
   struct heaps cases[CASE_COUNT] = {
      [SSBO] = {.number = 1, .counts = {storage}, .types = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}},
      [TEXEL] = {.number = 2, .counts = {sampled, storage_image},
                 .types = {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER}},
      [IMAGE] = {.number = 1, .counts = {sampled}, .types = {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE}},
      [MUTABLE] = {.number = 1, .counts = {minimum(storage, minimum(sampled, storage_image))},
                   .types = {VK_DESCRIPTOR_TYPE_MUTABLE_EXT}},
   };
   alarm(0);
   for (enum case_kind kind = SSBO; kind < CASE_COUNT; kind++) {
      struct heaps h = cases[kind];
      uint32_t count = h.number == 2 ? minimum(h.counts[0], h.counts[1]) : h.counts[0];
      printf("INFO heap_count_%s %u\n", case_names[kind], count);
      if (kind == TEXEL) {
         printf("INFO heap_count_texel_heap_uniform %u\n", h.counts[0]);
         printf("INFO heap_count_texel_heap_storage %u\n", h.counts[1]);
      }
      if (kind == MUTABLE && !c.mutable_supported) {
         printf("SKIP case %s mutableDescriptorType unsupported\n", case_names[kind]);
         skips++;
         continue;
      }
      const char *missing = missing_feature(&c, kind);
      if (!missing && !c.maintenance4)
         missing = "maintenance4 (SPIR-V LocalSizeId)";
      if (missing) {
         printf("FAIL case %s missing %s\n", case_names[kind], missing);
         fails++;
         continue;
      }
      if (supervise_case(&c, gipa, kind, h))
         fails++;
      else
         passes++;
   }
   goto cleanup;

setup_failed:
   for (unsigned i = 0; i < CASE_COUNT; i++)
      printf("FAIL case %s %s\n", case_names[i], reason);
   fails = CASE_COUNT;
cleanup:
   free(families);
   free(extensions);
   free(devices);
   alarm(CASE_TIMEOUT_SECONDS);
   if (c.instance && p_DestroyInstance)
      p_DestroyInstance(c.instance, NULL);
   if (library)
      dlclose(library);
   alarm(0);
   const char *result = fails ? "FAIL" : !passes && skips == CASE_COUNT ? "SKIP" : "PASS";
   printf("RESULT %s\n", result);
   return fails ? 1 : 0;
#undef SETUP_BAD
#undef SETUP_CHECK
}

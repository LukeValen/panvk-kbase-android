/* Bachata S4 Vulkan Requirements Test.
 * Checks Vulkan requirements for the Bachata S4 PS4 emulator (shadPS4 fork).
 *
 * Usage: bachata_reqs <libvulkan_panfrost.so>
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

typedef PFN_vkVoidFunction (*icd_gipa_fn)(VkInstance, const char *);

static int hard_count = 0;
static char hard_missing_str[4096] = "";

static void
check_hard(int cond, const char *item)
{
   if (cond) {
      printf("HARD ok %s\n", item); /* PanProbe compliance list */
   } else {
      printf("FAIL hard %s\n", item);
      hard_count++;
      if (hard_missing_str[0])
         strcat(hard_missing_str, ",");
      strcat(hard_missing_str, item);
   }
}

static int soft_count = 0;
static char soft_missing_str[4096] = "";

static void
check_soft(int ok, const char *item, const char *effect)
{
   if (ok) {
      printf("SOFT ok %s\n", item);
   } else {
      printf("SOFT missing %s -- %s\n", item, effect);
      soft_count++;
      if (soft_missing_str[0])
         strcat(soft_missing_str, ",");
      strcat(soft_missing_str, item);
   }
}

static int
has_extension(const VkExtensionProperties *exts, uint32_t count, const char *name)
{
   for (uint32_t i = 0; i < count; i++) {
      if (!strcmp(exts[i].extensionName, name))
         return 1;
   }
   return 0;
}

static VkFormatFeatureFlags2
get_optimal_features(VkPhysicalDevice phys,
                     PFN_vkGetPhysicalDeviceFormatProperties2 gfp2,
                     VkFormat format)
{
   VkFormatProperties3 fp3 = {
      .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3,
   };
   VkFormatProperties2 fp2 = {
      .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
      .pNext = &fp3,
   };
   gfp2(phys, format, &fp2);
   return fp3.optimalTilingFeatures;
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
   GI(GetPhysicalDeviceProperties2)
   GI(GetPhysicalDeviceFeatures2)
   GI(GetPhysicalDeviceQueueFamilyProperties)
   GI(GetPhysicalDeviceFormatProperties2)
   GI(EnumerateDeviceExtensionProperties)
   GI(CreateDevice)
   GI(DestroyDevice)
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
      VkPhysicalDeviceProperties2 p2 = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
      };
      p_GetPhysicalDeviceProperties2(devs[i], &p2);
      printf("PHYS %u %s vendor=0x%x device=0x%x\n", i, p2.properties.deviceName,
             p2.properties.vendorID, p2.properties.deviceID);
      if (strstr(p2.properties.deviceName, "Mali"))
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

#define HAS_EXT(name) has_extension(exts, ext_count, name)

   /* Query Properties2 */
   VkPhysicalDeviceSubgroupSizeControlProperties subgroup_size_props = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES,
   };
   VkPhysicalDeviceSubgroupProperties subgroup_props = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES,
      .pNext = &subgroup_size_props,
   };
   VkPhysicalDeviceVulkan13Properties props13 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES,
      .pNext = &subgroup_props,
   };
   VkPhysicalDeviceVulkan12Properties props12 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES,
      .pNext = &props13,
   };
   VkPhysicalDeviceVulkan11Properties props11 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES,
      .pNext = &props12,
   };
   VkPhysicalDeviceProperties2 props2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
      .pNext = &props11,
   };
   p_GetPhysicalDeviceProperties2(phys, &props2);

   /* Query Features2 (Core, 1.1, 1.2, 1.3) */
   VkPhysicalDeviceVulkan13Features f13 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
   };
   VkPhysicalDeviceVulkan12Features f12 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .pNext = &f13,
   };
   VkPhysicalDeviceVulkan11Features f11 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
      .pNext = &f12,
   };
   VkPhysicalDeviceFeatures2 feat2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &f11,
   };
   p_GetPhysicalDeviceFeatures2(phys, &feat2);

   /* Robustness2 */
   VkPhysicalDeviceRobustness2FeaturesEXT rob2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
   };
   if (HAS_EXT("VK_EXT_robustness2")) {
      VkPhysicalDeviceFeatures2 rf2 = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
         .pNext = &rob2,
      };
      p_GetPhysicalDeviceFeatures2(phys, &rf2);
   }

   /* Extension-advertised features */
   VkPhysicalDeviceCustomBorderColorFeaturesEXT border_feat = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT,
   };
   if (HAS_EXT("VK_EXT_custom_border_color")) {
      VkPhysicalDeviceFeatures2 q = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
         .pNext = &border_feat,
      };
      p_GetPhysicalDeviceFeatures2(phys, &q);
   }

   VkPhysicalDeviceDepthClipControlFeaturesEXT depth_clip_ctrl_feat = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT,
   };
   if (HAS_EXT("VK_EXT_depth_clip_control")) {
      VkPhysicalDeviceFeatures2 q = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
         .pNext = &depth_clip_ctrl_feat,
      };
      p_GetPhysicalDeviceFeatures2(phys, &q);
   }

   VkPhysicalDeviceDepthClipEnableFeaturesEXT depth_clip_en_feat = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT,
   };
   if (HAS_EXT("VK_EXT_depth_clip_enable")) {
      VkPhysicalDeviceFeatures2 q = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
         .pNext = &depth_clip_en_feat,
      };
      p_GetPhysicalDeviceFeatures2(phys, &q);
   }

   VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT vinput_dyn_feat = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT,
   };
   if (HAS_EXT("VK_EXT_vertex_input_dynamic_state")) {
      VkPhysicalDeviceFeatures2 q = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
         .pNext = &vinput_dyn_feat,
      };
      p_GetPhysicalDeviceFeatures2(phys, &q);
   }

   VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR bary_feat = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR,
   };
   if (HAS_EXT("VK_KHR_fragment_shader_barycentric")) {
      VkPhysicalDeviceFeatures2 q = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
         .pNext = &bary_feat,
      };
      p_GetPhysicalDeviceFeatures2(phys, &q);
   }

   VkPhysicalDeviceProvokingVertexFeaturesEXT prov_feat = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_FEATURES_EXT,
   };
   if (HAS_EXT("VK_EXT_provoking_vertex")) {
      VkPhysicalDeviceFeatures2 q = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
         .pNext = &prov_feat,
      };
      p_GetPhysicalDeviceFeatures2(phys, &q);
   }

   VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT div_feat = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT,
   };
   if (HAS_EXT("VK_EXT_vertex_attribute_divisor")) {
      VkPhysicalDeviceFeatures2 q = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
         .pNext = &div_feat,
      };
      p_GetPhysicalDeviceFeatures2(phys, &q);
   }

#ifdef VK_KHR_maintenance8
   VkPhysicalDeviceMaintenance8FeaturesKHR maint8_feat = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_8_FEATURES_KHR,
   };
   if (HAS_EXT("VK_KHR_maintenance8")) {
      VkPhysicalDeviceFeatures2 q = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
         .pNext = &maint8_feat,
      };
      p_GetPhysicalDeviceFeatures2(phys, &q);
   }
#endif

   VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT fb_layout_feat = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ATTACHMENT_FEEDBACK_LOOP_LAYOUT_FEATURES_EXT,
   };
   if (HAS_EXT("VK_EXT_attachment_feedback_loop_layout")) {
      VkPhysicalDeviceFeatures2 q = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
         .pNext = &fb_layout_feat,
      };
      p_GetPhysicalDeviceFeatures2(phys, &q);
   }

   VkPhysicalDeviceAttachmentFeedbackLoopDynamicStateFeaturesEXT fb_dyn_feat = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ATTACHMENT_FEEDBACK_LOOP_DYNAMIC_STATE_FEATURES_EXT,
   };
   if (HAS_EXT("VK_EXT_attachment_feedback_loop_dynamic_state")) {
      VkPhysicalDeviceFeatures2 q = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
         .pNext = &fb_dyn_feat,
      };
      p_GetPhysicalDeviceFeatures2(phys, &q);
   }

   VkPhysicalDeviceImageViewMinLodFeaturesEXT min_lod_feat = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_MIN_LOD_FEATURES_EXT,
   };
   if (HAS_EXT("VK_EXT_image_view_min_lod")) {
      VkPhysicalDeviceFeatures2 q = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
         .pNext = &min_lod_feat,
      };
      p_GetPhysicalDeviceFeatures2(phys, &q);
   }

   VkPhysicalDeviceExtendedDynamicState3FeaturesEXT eds3_feat = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT,
   };
   if (HAS_EXT("VK_EXT_extended_dynamic_state3")) {
      VkPhysicalDeviceFeatures2 q = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
         .pNext = &eds3_feat,
      };
      p_GetPhysicalDeviceFeatures2(phys, &q);
   }

   VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT list_restart_feat = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIMITIVE_TOPOLOGY_LIST_RESTART_FEATURES_EXT,
   };
   if (HAS_EXT("VK_EXT_primitive_topology_list_restart")) {
      VkPhysicalDeviceFeatures2 q = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
         .pNext = &list_restart_feat,
      };
      p_GetPhysicalDeviceFeatures2(phys, &q);
   }

   VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT atomic_float2_feat = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_2_FEATURES_EXT,
   };
   if (HAS_EXT("VK_EXT_shader_atomic_float2")) {
      VkPhysicalDeviceFeatures2 q = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
         .pNext = &atomic_float2_feat,
      };
      p_GetPhysicalDeviceFeatures2(phys, &q);
   }

   VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR workgroup_feat = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_FEATURES_KHR,
   };
   if (HAS_EXT("VK_KHR_workgroup_memory_explicit_layout")) {
      VkPhysicalDeviceFeatures2 q = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
         .pNext = &workgroup_feat,
      };
      p_GetPhysicalDeviceFeatures2(phys, &q);
   }

   VkPhysicalDeviceImage2DViewOf3DFeaturesEXT img2d_feat = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_2D_VIEW_OF_3D_FEATURES_EXT,
   };
   if (HAS_EXT("VK_EXT_image_2d_view_of_3d")) {
      VkPhysicalDeviceFeatures2 q = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
         .pNext = &img2d_feat,
      };
      p_GetPhysicalDeviceFeatures2(phys, &q);
   }

   VkPhysicalDeviceShaderClockFeaturesKHR clock_feat = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR,
   };
   if (HAS_EXT("VK_KHR_shader_clock")) {
      VkPhysicalDeviceFeatures2 q = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
         .pNext = &clock_feat,
      };
      p_GetPhysicalDeviceFeatures2(phys, &q);
   }

   /* Queue Family Properties */
   uint32_t qn = 0;
   p_GetPhysicalDeviceQueueFamilyProperties(phys, &qn, NULL);
   VkQueueFamilyProperties *qp = calloc(qn, sizeof(*qp));
   if (qn > 0 && !qp)
      return 1;
   p_GetPhysicalDeviceQueueFamilyProperties(phys, &qn, qp);
   uint32_t qi = ~0u;
   for (uint32_t i = 0; i < qn; i++) {
      if (qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
         qi = i;
         break;
      }
   }
   free(qp);

   /* ──────────────────────────────────────────────────────────────────────────
    * Section HARD
    * ────────────────────────────────────────────────────────────────────────── */
   check_hard(props2.properties.apiVersion >= VK_API_VERSION_1_3, "apiVersion");
   check_hard(HAS_EXT("VK_KHR_swapchain"), "VK_KHR_swapchain");
   check_hard(HAS_EXT("VK_EXT_robustness2"), "VK_EXT_robustness2");

   if (HAS_EXT("VK_EXT_robustness2")) {
      check_hard(rob2.robustBufferAccess2, "robustBufferAccess2");
      check_hard(rob2.nullDescriptor, "nullDescriptor");
      check_hard(rob2.robustImageAccess2 || f13.robustImageAccess, "robustImageAccess");
   } else {
      check_hard(0, "robustBufferAccess2");
      check_hard(0, "nullDescriptor");
      check_hard(0, "robustImageAccess");
   }

   check_hard(qi != ~0u, "graphics_queue");

   /* Vulkan 1.2 */
   check_hard(f12.timelineSemaphore, "timelineSemaphore");
   check_hard(f12.bufferDeviceAddress, "bufferDeviceAddress");
   check_hard(f12.shaderInt8, "shaderInt8");
   check_hard(f12.storageBuffer8BitAccess, "storageBuffer8BitAccess");
   check_hard(f12.scalarBlockLayout, "scalarBlockLayout");
   check_hard(f12.uniformBufferStandardLayout, "uniformBufferStandardLayout");
   check_hard(f12.shaderBufferInt64Atomics, "shaderBufferInt64Atomics");
   check_hard(f12.shaderSharedInt64Atomics, "shaderSharedInt64Atomics");

   /* Vulkan 1.1 */
   check_hard(f11.storageBuffer16BitAccess, "storageBuffer16BitAccess");
   check_hard(f11.shaderDrawParameters, "shaderDrawParameters");

   /* Vulkan 1.3 */
   check_hard(f13.synchronization2, "synchronization2");
   check_hard(f13.dynamicRendering, "dynamicRendering");
   check_hard(f13.maintenance4, "maintenance4");
   check_hard(f13.shaderDemoteToHelperInvocation, "shaderDemoteToHelperInvocation");
   check_hard(f13.subgroupSizeControl, "subgroupSizeControl");

   /* Core features */
   check_hard(feat2.features.shaderInt64, "shaderInt64");
   check_hard(feat2.features.shaderInt16, "shaderInt16");
   check_hard(feat2.features.imageCubeArray, "imageCubeArray");
   check_hard(feat2.features.independentBlend, "independentBlend");
   check_hard(feat2.features.geometryShader, "geometryShader");
   check_hard(feat2.features.tessellationShader, "tessellationShader");
   check_hard(feat2.features.dualSrcBlend, "dualSrcBlend");
   check_hard(feat2.features.depthClamp, "depthClamp");
   check_hard(feat2.features.sampleRateShading, "sampleRateShading");
   check_hard(feat2.features.fillModeNonSolid, "fillModeNonSolid");
   check_hard(feat2.features.multiViewport, "multiViewport");
   check_hard(feat2.features.fragmentStoresAndAtomics, "fragmentStoresAndAtomics");
   check_hard(feat2.features.vertexPipelineStoresAndAtomics, "vertexPipelineStoresAndAtomics");
   check_hard(feat2.features.shaderStorageImageExtendedFormats, "shaderStorageImageExtendedFormats");
   check_hard(feat2.features.shaderClipDistance, "shaderClipDistance");
   check_hard(feat2.features.shaderStorageImageWriteWithoutFormat, "shaderStorageImageWriteWithoutFormat");
   check_hard(feat2.features.shaderStorageImageReadWithoutFormat, "shaderStorageImageReadWithoutFormat");

   /* Advertised extensions => features must be true */
   if (HAS_EXT("VK_EXT_custom_border_color")) {
      check_hard(border_feat.customBorderColors, "customBorderColors");
      check_hard(border_feat.customBorderColorWithoutFormat, "customBorderColorWithoutFormat");
   }
   if (HAS_EXT("VK_EXT_depth_clip_control"))
      check_hard(depth_clip_ctrl_feat.depthClipControl, "depthClipControl");
   if (HAS_EXT("VK_EXT_depth_clip_enable"))
      check_hard(depth_clip_en_feat.depthClipEnable, "depthClipEnable");
   if (HAS_EXT("VK_EXT_vertex_input_dynamic_state"))
      check_hard(vinput_dyn_feat.vertexInputDynamicState, "vertexInputDynamicState");
   if (HAS_EXT("VK_KHR_fragment_shader_barycentric"))
      check_hard(bary_feat.fragmentShaderBarycentric, "fragmentShaderBarycentric");
   if (HAS_EXT("VK_EXT_provoking_vertex"))
      check_hard(prov_feat.provokingVertexLast, "provokingVertexLast");
   if (HAS_EXT("VK_EXT_vertex_attribute_divisor"))
      check_hard(div_feat.vertexAttributeInstanceRateDivisor, "vertexAttributeInstanceRateDivisor");
#ifdef VK_KHR_maintenance8
   if (HAS_EXT("VK_KHR_maintenance8"))
      check_hard(maint8_feat.maintenance8, "maintenance8");
#endif
   if (HAS_EXT("VK_EXT_attachment_feedback_loop_layout"))
      check_hard(fb_layout_feat.attachmentFeedbackLoopLayout, "attachmentFeedbackLoopLayout");
   if (HAS_EXT("VK_EXT_attachment_feedback_loop_dynamic_state"))
      check_hard(fb_dyn_feat.attachmentFeedbackLoopDynamicState, "attachmentFeedbackLoopDynamicState");
   if (HAS_EXT("VK_EXT_image_view_min_lod"))
      check_hard(min_lod_feat.minLod, "minLod");

   /* Limits */
   VkPhysicalDeviceLimits lim = props2.properties.limits;
   printf("LIMIT maxPushConstantsSize=%u\n", lim.maxPushConstantsSize);
   check_hard(lim.maxPushConstantsSize >= 128, "maxPushConstantsSize");

   printf("LIMIT minStorageBufferOffsetAlignment=%" PRIu64 "\n", (uint64_t)lim.minStorageBufferOffsetAlignment);
   check_hard(lim.minStorageBufferOffsetAlignment <= 256, "minStorageBufferOffsetAlignment");

   printf("LIMIT minUniformBufferOffsetAlignment=%" PRIu64 "\n", (uint64_t)lim.minUniformBufferOffsetAlignment);
   check_hard(lim.minUniformBufferOffsetAlignment <= 256, "minUniformBufferOffsetAlignment");

   printf("LIMIT maxComputeSharedMemorySize=%u\n", lim.maxComputeSharedMemorySize);
   check_hard(lim.maxComputeSharedMemorySize >= 32768, "maxComputeSharedMemorySize");

   printf("LIMIT maxSamplerAllocationCount=%u\n", lim.maxSamplerAllocationCount);
   check_hard(lim.maxSamplerAllocationCount >= 1024, "maxSamplerAllocationCount");

   printf("LIMIT maxPerStageDescriptorStorageBuffers=%u\n", lim.maxPerStageDescriptorStorageBuffers);
   check_hard(lim.maxPerStageDescriptorStorageBuffers >= 40, "maxPerStageDescriptorStorageBuffers");

   printf("LIMIT maxBoundDescriptorSets=%u\n", lim.maxBoundDescriptorSets);
   check_hard(lim.maxBoundDescriptorSets >= 4, "maxBoundDescriptorSets");

   /* Formats */
   VkFormatFeatureFlags2 f_b10g11r11 = get_optimal_features(phys, p_GetPhysicalDeviceFormatProperties2, VK_FORMAT_B10G11R11_UFLOAT_PACK32);
   VkFormatFeatureFlags2 f_a2b10 = get_optimal_features(phys, p_GetPhysicalDeviceFormatProperties2, VK_FORMAT_A2B10G10R10_UNORM_PACK32);
   VkFormatFeatureFlags2 f_a2r10 = get_optimal_features(phys, p_GetPhysicalDeviceFormatProperties2, VK_FORMAT_A2R10G10B10_UNORM_PACK32);
   VkFormatFeatureFlags2 f_r16g16b16a16_f = get_optimal_features(phys, p_GetPhysicalDeviceFormatProperties2, VK_FORMAT_R16G16B16A16_SFLOAT);
   VkFormatFeatureFlags2 f_r32g32b32a32_f = get_optimal_features(phys, p_GetPhysicalDeviceFormatProperties2, VK_FORMAT_R32G32B32A32_SFLOAT);
   VkFormatFeatureFlags2 f_rgba8 = get_optimal_features(phys, p_GetPhysicalDeviceFormatProperties2, VK_FORMAT_R8G8B8A8_UNORM);
   VkFormatFeatureFlags2 f_bgra8 = get_optimal_features(phys, p_GetPhysicalDeviceFormatProperties2, VK_FORMAT_B8G8R8A8_UNORM);
   VkFormatFeatureFlags2 f_r32_u = get_optimal_features(phys, p_GetPhysicalDeviceFormatProperties2, VK_FORMAT_R32_UINT);
   VkFormatFeatureFlags2 f_r32_f = get_optimal_features(phys, p_GetPhysicalDeviceFormatProperties2, VK_FORMAT_R32_SFLOAT);
   VkFormatFeatureFlags2 f_d32 = get_optimal_features(phys, p_GetPhysicalDeviceFormatProperties2, VK_FORMAT_D32_SFLOAT);
   VkFormatFeatureFlags2 f_d16 = get_optimal_features(phys, p_GetPhysicalDeviceFormatProperties2, VK_FORMAT_D16_UNORM);
   VkFormatFeatureFlags2 f_d24s8 = get_optimal_features(phys, p_GetPhysicalDeviceFormatProperties2, VK_FORMAT_D24_UNORM_S8_UINT);
   VkFormatFeatureFlags2 f_d32s8 = get_optimal_features(phys, p_GetPhysicalDeviceFormatProperties2, VK_FORMAT_D32_SFLOAT_S8_UINT);

   VkFormatFeatureFlags2 sc_mask = VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT;
   check_hard((f_b10g11r11 & sc_mask) == sc_mask, "format_B10G11R11_UFLOAT_PACK32_sampled_color");
   check_hard((f_a2b10 & sc_mask) == sc_mask, "format_A2B10G10R10_UNORM_PACK32_sampled_color");
   check_hard((f_a2r10 & sc_mask) == sc_mask, "format_A2R10G10B10_UNORM_PACK32_sampled_color");
   check_hard((f_r16g16b16a16_f & sc_mask) == sc_mask, "format_R16G16B16A16_SFLOAT_sampled_color");
   check_hard((f_r32g32b32a32_f & sc_mask) == sc_mask, "format_R32G32B32A32_SFLOAT_sampled_color");
   check_hard((f_rgba8 & sc_mask) == sc_mask, "format_R8G8B8A8_UNORM_sampled_color");
   check_hard((f_bgra8 & sc_mask) == sc_mask, "format_B8G8R8A8_UNORM_sampled_color");

   check_hard((f_r32_u & VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT) != 0, "format_R32_UINT_storage");
   check_hard((f_r32_f & VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT) != 0, "format_R32_SFLOAT_storage");
   check_hard((f_rgba8 & VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT) != 0, "format_R8G8B8A8_UNORM_storage");
   check_hard((f_r16g16b16a16_f & VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT) != 0, "format_R16G16B16A16_SFLOAT_storage");
   check_hard((f_r32g32b32a32_f & VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT) != 0, "format_R32G32B32A32_SFLOAT_storage");

   check_hard((f_r32_u & VK_FORMAT_FEATURE_2_STORAGE_IMAGE_ATOMIC_BIT) != 0, "format_R32_UINT_storage_atomic");

   check_hard((f_d32 & VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT) != 0, "format_D32_SFLOAT_depth_stencil");
   check_hard((f_d16 & VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT) != 0, "format_D16_UNORM_depth_stencil");

   check_hard(((f_d24s8 | f_d32s8) & VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT) != 0, "format_D24S8_or_D32S8_depth_stencil");

   /* ──────────────────────────────────────────────────────────────────────────
    * Section SOFT
    * ────────────────────────────────────────────────────────────────────────── */
   check_soft(HAS_EXT("VK_KHR_push_descriptor"), "VK_KHR_push_descriptor", "descriptor sets fallback, CPU cost");
   check_soft(HAS_EXT("VK_EXT_vertex_attribute_divisor"), "VK_EXT_vertex_attribute_divisor", "instanced step rate wrong");
   check_soft(HAS_EXT("VK_EXT_extended_dynamic_state3") && eds3_feat.extendedDynamicState3ColorWriteMask, "VK_EXT_extended_dynamic_state3", "more pipeline variants");
   check_soft(HAS_EXT("VK_EXT_vertex_input_dynamic_state"), "VK_EXT_vertex_input_dynamic_state", "more pipeline variants");
   check_soft(HAS_EXT("VK_EXT_custom_border_color"), "VK_EXT_custom_border_color", "border black");
   check_soft(HAS_EXT("VK_EXT_depth_clip_control"), "VK_EXT_depth_clip_control", "shader depth fixup");
   check_soft(HAS_EXT("VK_EXT_depth_clip_enable"), "VK_EXT_depth_clip_enable", "clip/clamp coupled");
   check_soft(HAS_EXT("VK_EXT_primitive_topology_list_restart") && list_restart_feat.primitiveTopologyListRestart, "primitiveTopologyListRestart", "list restart disabled");
   check_soft(HAS_EXT("VK_EXT_primitive_topology_list_restart") && list_restart_feat.primitiveTopologyPatchListRestart, "primitiveTopologyPatchListRestart", "patch list restart disabled");
   check_soft(HAS_EXT("VK_KHR_fragment_shader_barycentric"), "VK_KHR_fragment_shader_barycentric", "flat/manual interpolation");
   check_soft(HAS_EXT("VK_EXT_provoking_vertex"), "VK_EXT_provoking_vertex", "wrong flat shading");
   check_soft(HAS_EXT("VK_EXT_shader_stencil_export"), "VK_EXT_shader_stencil_export", "stencil export dropped");
   check_soft(HAS_EXT("VK_AMD_shader_image_load_store_lod"), "VK_AMD_shader_image_load_store_lod", "per-mip descriptors; storage read with lod aborts");
   check_soft(HAS_EXT("VK_EXT_shader_atomic_float"), "VK_EXT_shader_atomic_float", "int atomic emulation");
   check_soft(HAS_EXT("VK_EXT_shader_atomic_float2") && atomic_float2_feat.shaderBufferFloat32AtomicMinMax && atomic_float2_feat.shaderImageFloat32AtomicMinMax, "VK_EXT_shader_atomic_float2", "int atomic emulation");
   check_soft(HAS_EXT("VK_KHR_workgroup_memory_explicit_layout"), "VK_KHR_workgroup_memory_explicit_layout", "LDS split / spill to SSBO");
   check_soft(HAS_EXT("VK_EXT_image_2d_view_of_3d") && img2d_feat.image2DViewOf3D && img2d_feat.sampler2DViewOf3D, "VK_EXT_image_2d_view_of_3d", "3D slice views may assert");
   check_soft(HAS_EXT("VK_EXT_image_view_min_lod"), "VK_EXT_image_view_min_lod", "min LOD ignored");
#ifdef VK_KHR_maintenance8
   check_soft(HAS_EXT("VK_KHR_maintenance8"), "VK_KHR_maintenance8", "depth/color copy via buffer");
#else
   check_soft(0, "VK_KHR_maintenance8", "depth/color copy via buffer");
#endif
   check_soft(HAS_EXT("VK_EXT_attachment_feedback_loop_layout") && HAS_EXT("VK_EXT_attachment_feedback_loop_dynamic_state"), "VK_EXT_attachment_feedback_loop_layout+dynamic_state", "GENERAL layout");
   check_soft(HAS_EXT("VK_EXT_depth_range_unrestricted"), "VK_EXT_depth_range_unrestricted", "depth clamped to 0..1");
   check_soft(HAS_EXT("VK_EXT_memory_budget"), "VK_EXT_memory_budget", "fixed GC thresholds");
   check_soft(HAS_EXT("VK_KHR_shader_clock"), "VK_KHR_shader_clock", "shader clock unsupported");

   /* Features */
   check_soft(feat2.features.shaderFloat64, "shaderFloat64", "fp64 lowered to fp32");
   check_soft(f12.shaderFloat16, "shaderFloat16", "fp16 via fp32");
   check_soft(feat2.features.depthBounds, "depthBounds", "depth bounds test disabled");
   check_soft(feat2.features.logicOp, "logicOp", "logic op disabled");
   check_soft(feat2.features.wideLines, "wideLines", "wide lines disabled");
   check_soft(feat2.features.depthBiasClamp, "depthBiasClamp", "depth bias clamp disabled");
   check_soft(feat2.features.samplerAnisotropy, "samplerAnisotropy", "anisotropic filtering disabled");
   check_soft(feat2.features.pipelineStatisticsQuery, "pipelineStatisticsQuery", "pipeline statistics query disabled");
   check_soft(feat2.features.textureCompressionETC2, "textureCompressionETC2", "ETC2 fallback");
   check_soft(f12.drawIndirectCount, "drawIndirectCount", "draw indirect count fallback");
   check_soft(f12.shaderOutputLayer, "shaderOutputLayer", "layered rendering fallback");
   check_soft(f12.separateDepthStencilLayouts, "separateDepthStencilLayouts", "combined depth stencil layouts");
   check_soft(f12.hostQueryReset, "hostQueryReset", "command buffer query reset");
   check_soft(f12.samplerMirrorClampToEdge, "samplerMirrorClampToEdge", "mirror clamp to edge fallback");
   check_soft(feat2.features.multiDrawIndirect, "multiDrawIndirect", "single draw indirect fallback");

   /* Subgroup */
   int wave64_ok = (subgroup_size_props.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT) &&
                   (subgroup_size_props.minSubgroupSize <= 64 && subgroup_size_props.maxSubgroupSize >= 64);
   check_soft(wave64_ok, "subgroup_wave64", "wave64 forced; else lane semantics may break");

   int quad_ok = (subgroup_props.supportedStages & VK_SHADER_STAGE_FRAGMENT_BIT) &&
                 (subgroup_props.supportedOperations & VK_SUBGROUP_FEATURE_QUAD_BIT);
   check_soft(quad_ok, "subgroup_quad", "quad ops");

   /* BC Formats */
   VkFormatFeatureFlags2 f_bc1 = get_optimal_features(phys, p_GetPhysicalDeviceFormatProperties2, VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
   VkFormatFeatureFlags2 f_bc3 = get_optimal_features(phys, p_GetPhysicalDeviceFormatProperties2, VK_FORMAT_BC3_UNORM_BLOCK);
   VkFormatFeatureFlags2 f_bc4 = get_optimal_features(phys, p_GetPhysicalDeviceFormatProperties2, VK_FORMAT_BC4_UNORM_BLOCK);
   VkFormatFeatureFlags2 f_bc5 = get_optimal_features(phys, p_GetPhysicalDeviceFormatProperties2, VK_FORMAT_BC5_UNORM_BLOCK);
   VkFormatFeatureFlags2 f_bc6h = get_optimal_features(phys, p_GetPhysicalDeviceFormatProperties2, VK_FORMAT_BC6H_UFLOAT_BLOCK);
   VkFormatFeatureFlags2 f_bc7 = get_optimal_features(phys, p_GetPhysicalDeviceFormatProperties2, VK_FORMAT_BC7_UNORM_BLOCK);

   int bc_ok = ((f_bc1 & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT) &&
                (f_bc3 & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT) &&
                (f_bc4 & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT) &&
                (f_bc5 & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT) &&
                (f_bc6h & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT) &&
                (f_bc7 & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT));
   check_soft(bc_ok, "bc_formats", "GPU decode fallback limited");

   printf("INFO apiVersion=%u.%u.%u robustBufferAccess2=%u robustImageAccess2=%u nullDescriptor=%u robustImageAccess=%u\n",
          VK_API_VERSION_MAJOR(props2.properties.apiVersion),
          VK_API_VERSION_MINOR(props2.properties.apiVersion),
          VK_API_VERSION_PATCH(props2.properties.apiVersion), rob2.robustBufferAccess2,
          rob2.robustImageAccess2, rob2.nullDescriptor, f13.robustImageAccess);

   /* Summary */
   printf("HARD missing=%d: %s\n", hard_count, hard_missing_str);
   printf("SOFT missing=%d: %s\n", soft_count, soft_missing_str);

   /* ──────────────────────────────────────────────────────────────────────────
    * Section CREATE
    * ────────────────────────────────────────────────────────────────────────── */
   int create_device_failed = 0;
   int api_ext_rob_ok = (props2.properties.apiVersion >= VK_API_VERSION_1_3) &&
                        HAS_EXT("VK_KHR_swapchain") &&
                        HAS_EXT("VK_EXT_robustness2") &&
                        rob2.nullDescriptor &&
                        (rob2.robustImageAccess2 || f13.robustImageAccess);

   if (api_ext_rob_ok && qi != ~0u) {
      const char *dev_exts[64];
      uint32_t dev_ext_count = 0;
      dev_exts[dev_ext_count++] = "VK_KHR_swapchain";
      dev_exts[dev_ext_count++] = "VK_EXT_robustness2";

      if (HAS_EXT("VK_KHR_push_descriptor"))
         dev_exts[dev_ext_count++] = "VK_KHR_push_descriptor";
      if (HAS_EXT("VK_EXT_vertex_attribute_divisor"))
         dev_exts[dev_ext_count++] = "VK_EXT_vertex_attribute_divisor";
      if (HAS_EXT("VK_EXT_extended_dynamic_state3"))
         dev_exts[dev_ext_count++] = "VK_EXT_extended_dynamic_state3";
      if (HAS_EXT("VK_EXT_vertex_input_dynamic_state"))
         dev_exts[dev_ext_count++] = "VK_EXT_vertex_input_dynamic_state";
      if (HAS_EXT("VK_EXT_custom_border_color"))
         dev_exts[dev_ext_count++] = "VK_EXT_custom_border_color";
      if (HAS_EXT("VK_EXT_depth_clip_control"))
         dev_exts[dev_ext_count++] = "VK_EXT_depth_clip_control";
      if (HAS_EXT("VK_EXT_depth_clip_enable"))
         dev_exts[dev_ext_count++] = "VK_EXT_depth_clip_enable";
      if (HAS_EXT("VK_EXT_primitive_topology_list_restart"))
         dev_exts[dev_ext_count++] = "VK_EXT_primitive_topology_list_restart";
      if (HAS_EXT("VK_KHR_fragment_shader_barycentric"))
         dev_exts[dev_ext_count++] = "VK_KHR_fragment_shader_barycentric";
      if (HAS_EXT("VK_EXT_provoking_vertex"))
         dev_exts[dev_ext_count++] = "VK_EXT_provoking_vertex";
      if (HAS_EXT("VK_EXT_shader_stencil_export"))
         dev_exts[dev_ext_count++] = "VK_EXT_shader_stencil_export";
      if (HAS_EXT("VK_AMD_shader_image_load_store_lod"))
         dev_exts[dev_ext_count++] = "VK_AMD_shader_image_load_store_lod";
      if (HAS_EXT("VK_EXT_shader_atomic_float"))
         dev_exts[dev_ext_count++] = "VK_EXT_shader_atomic_float";
      if (HAS_EXT("VK_EXT_shader_atomic_float2"))
         dev_exts[dev_ext_count++] = "VK_EXT_shader_atomic_float2";
      if (HAS_EXT("VK_KHR_workgroup_memory_explicit_layout"))
         dev_exts[dev_ext_count++] = "VK_KHR_workgroup_memory_explicit_layout";
      if (HAS_EXT("VK_EXT_image_2d_view_of_3d"))
         dev_exts[dev_ext_count++] = "VK_EXT_image_2d_view_of_3d";
      if (HAS_EXT("VK_EXT_image_view_min_lod"))
         dev_exts[dev_ext_count++] = "VK_EXT_image_view_min_lod";
#ifdef VK_KHR_maintenance8
      if (HAS_EXT("VK_KHR_maintenance8"))
         dev_exts[dev_ext_count++] = "VK_KHR_maintenance8";
#endif
      if (HAS_EXT("VK_EXT_attachment_feedback_loop_layout") &&
          HAS_EXT("VK_EXT_attachment_feedback_loop_dynamic_state")) {
         dev_exts[dev_ext_count++] = "VK_EXT_attachment_feedback_loop_layout";
         dev_exts[dev_ext_count++] = "VK_EXT_attachment_feedback_loop_dynamic_state";
      }
      if (HAS_EXT("VK_EXT_depth_range_unrestricted"))
         dev_exts[dev_ext_count++] = "VK_EXT_depth_range_unrestricted";
      if (HAS_EXT("VK_EXT_memory_budget"))
         dev_exts[dev_ext_count++] = "VK_EXT_memory_budget";
      if (HAS_EXT("VK_KHR_shader_clock"))
         dev_exts[dev_ext_count++] = "VK_KHR_shader_clock";

      /* Assemble feature chain */
      VkPhysicalDeviceFeatures2 dev_feat2 = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      };
      dev_feat2.features.shaderInt64 = feat2.features.shaderInt64;
      dev_feat2.features.shaderInt16 = feat2.features.shaderInt16;
      dev_feat2.features.imageCubeArray = feat2.features.imageCubeArray;
      dev_feat2.features.independentBlend = feat2.features.independentBlend;
      dev_feat2.features.geometryShader = feat2.features.geometryShader;
      dev_feat2.features.tessellationShader = feat2.features.tessellationShader;
      dev_feat2.features.dualSrcBlend = feat2.features.dualSrcBlend;
      dev_feat2.features.depthClamp = feat2.features.depthClamp;
      dev_feat2.features.sampleRateShading = feat2.features.sampleRateShading;
      dev_feat2.features.fillModeNonSolid = feat2.features.fillModeNonSolid;
      dev_feat2.features.multiViewport = feat2.features.multiViewport;
      dev_feat2.features.fragmentStoresAndAtomics = feat2.features.fragmentStoresAndAtomics;
      dev_feat2.features.vertexPipelineStoresAndAtomics = feat2.features.vertexPipelineStoresAndAtomics;
      dev_feat2.features.shaderStorageImageExtendedFormats = feat2.features.shaderStorageImageExtendedFormats;
      dev_feat2.features.shaderClipDistance = feat2.features.shaderClipDistance;
      dev_feat2.features.shaderStorageImageWriteWithoutFormat = feat2.features.shaderStorageImageWriteWithoutFormat;
      dev_feat2.features.shaderStorageImageReadWithoutFormat = feat2.features.shaderStorageImageReadWithoutFormat;
      dev_feat2.features.shaderFloat64 = feat2.features.shaderFloat64;
      dev_feat2.features.depthBounds = feat2.features.depthBounds;
      dev_feat2.features.logicOp = feat2.features.logicOp;
      dev_feat2.features.wideLines = feat2.features.wideLines;
      dev_feat2.features.depthBiasClamp = feat2.features.depthBiasClamp;
      dev_feat2.features.samplerAnisotropy = feat2.features.samplerAnisotropy;
      dev_feat2.features.pipelineStatisticsQuery = feat2.features.pipelineStatisticsQuery;
      dev_feat2.features.textureCompressionETC2 = feat2.features.textureCompressionETC2;
      dev_feat2.features.multiDrawIndirect = feat2.features.multiDrawIndirect;
      dev_feat2.features.robustBufferAccess = feat2.features.robustBufferAccess;

      void **tail = &dev_feat2.pNext;

      VkPhysicalDeviceVulkan11Features dev_f11 = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
         .storageBuffer16BitAccess = f11.storageBuffer16BitAccess,
         .shaderDrawParameters = f11.shaderDrawParameters,
      };
      *tail = &dev_f11;
      tail = (void **)&dev_f11.pNext;

      VkPhysicalDeviceVulkan12Features dev_f12 = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
         .timelineSemaphore = f12.timelineSemaphore,
         .bufferDeviceAddress = f12.bufferDeviceAddress,
         .shaderInt8 = f12.shaderInt8,
         .storageBuffer8BitAccess = f12.storageBuffer8BitAccess,
         .scalarBlockLayout = f12.scalarBlockLayout,
         .uniformBufferStandardLayout = f12.uniformBufferStandardLayout,
         .shaderBufferInt64Atomics = f12.shaderBufferInt64Atomics,
         .shaderSharedInt64Atomics = f12.shaderSharedInt64Atomics,
         .drawIndirectCount = f12.drawIndirectCount,
         .separateDepthStencilLayouts = f12.separateDepthStencilLayouts,
         .hostQueryReset = f12.hostQueryReset,
         .samplerMirrorClampToEdge = f12.samplerMirrorClampToEdge,
         .shaderOutputLayer = f12.shaderOutputLayer,
         .shaderFloat16 = f12.shaderFloat16,
      };
      *tail = &dev_f12;
      tail = (void **)&dev_f12.pNext;

      VkPhysicalDeviceVulkan13Features dev_f13 = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
         .synchronization2 = f13.synchronization2,
         .dynamicRendering = f13.dynamicRendering,
         .maintenance4 = f13.maintenance4,
         .shaderDemoteToHelperInvocation = f13.shaderDemoteToHelperInvocation,
         .subgroupSizeControl = f13.subgroupSizeControl,
         .robustImageAccess = f13.robustImageAccess,
      };
      *tail = &dev_f13;
      tail = (void **)&dev_f13.pNext;

      VkPhysicalDeviceRobustness2FeaturesEXT dev_rob2 = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
         /* Bachata asserts robustBufferAccess2 before creating the device. When
          * it is missing, still try the rest of the chain ("relaxed") so the
          * report shows whether that bit is the only blocker. */
         .robustBufferAccess2 = rob2.robustBufferAccess2,
         .robustImageAccess2 = rob2.robustImageAccess2,
         .nullDescriptor = VK_TRUE,
      };
      *tail = &dev_rob2;
      tail = (void **)&dev_rob2.pNext;

      /* Extension feature structs with bits hard-coded to 1 */
      VkPhysicalDeviceCustomBorderColorFeaturesEXT dev_border = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT,
         .customBorderColors = VK_TRUE,
         .customBorderColorWithoutFormat = VK_TRUE,
      };
      if (HAS_EXT("VK_EXT_custom_border_color")) {
         *tail = &dev_border;
         tail = (void **)&dev_border.pNext;
      }

      VkPhysicalDeviceDepthClipControlFeaturesEXT dev_clip_ctrl = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT,
         .depthClipControl = VK_TRUE,
      };
      if (HAS_EXT("VK_EXT_depth_clip_control")) {
         *tail = &dev_clip_ctrl;
         tail = (void **)&dev_clip_ctrl.pNext;
      }

      VkPhysicalDeviceDepthClipEnableFeaturesEXT dev_clip_en = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT,
         .depthClipEnable = VK_TRUE,
      };
      if (HAS_EXT("VK_EXT_depth_clip_enable")) {
         *tail = &dev_clip_en;
         tail = (void **)&dev_clip_en.pNext;
      }

      VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT dev_vinput = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT,
         .vertexInputDynamicState = VK_TRUE,
      };
      if (HAS_EXT("VK_EXT_vertex_input_dynamic_state")) {
         *tail = &dev_vinput;
         tail = (void **)&dev_vinput.pNext;
      }

      VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR dev_bary = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR,
         .fragmentShaderBarycentric = VK_TRUE,
      };
      if (HAS_EXT("VK_KHR_fragment_shader_barycentric")) {
         *tail = &dev_bary;
         tail = (void **)&dev_bary.pNext;
      }

      VkPhysicalDeviceProvokingVertexFeaturesEXT dev_prov = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_FEATURES_EXT,
         .provokingVertexLast = VK_TRUE,
      };
      if (HAS_EXT("VK_EXT_provoking_vertex")) {
         *tail = &dev_prov;
         tail = (void **)&dev_prov.pNext;
      }

      VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT dev_div = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT,
         .vertexAttributeInstanceRateDivisor = VK_TRUE,
      };
      if (HAS_EXT("VK_EXT_vertex_attribute_divisor")) {
         *tail = &dev_div;
         tail = (void **)&dev_div.pNext;
      }

#ifdef VK_KHR_maintenance8
      VkPhysicalDeviceMaintenance8FeaturesKHR dev_maint8 = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_8_FEATURES_KHR,
         .maintenance8 = VK_TRUE,
      };
      if (HAS_EXT("VK_KHR_maintenance8")) {
         *tail = &dev_maint8;
         tail = (void **)&dev_maint8.pNext;
      }
#endif

      VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT dev_fb_layout = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ATTACHMENT_FEEDBACK_LOOP_LAYOUT_FEATURES_EXT,
         .attachmentFeedbackLoopLayout = VK_TRUE,
      };
      VkPhysicalDeviceAttachmentFeedbackLoopDynamicStateFeaturesEXT dev_fb_dyn = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ATTACHMENT_FEEDBACK_LOOP_DYNAMIC_STATE_FEATURES_EXT,
         .attachmentFeedbackLoopDynamicState = VK_TRUE,
      };
      if (HAS_EXT("VK_EXT_attachment_feedback_loop_layout") &&
          HAS_EXT("VK_EXT_attachment_feedback_loop_dynamic_state")) {
         *tail = &dev_fb_layout;
         tail = (void **)&dev_fb_layout.pNext;
         *tail = &dev_fb_dyn;
         tail = (void **)&dev_fb_dyn.pNext;
      }

      VkPhysicalDeviceImageViewMinLodFeaturesEXT dev_min_lod = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_MIN_LOD_FEATURES_EXT,
         .minLod = VK_TRUE,
      };
      if (HAS_EXT("VK_EXT_image_view_min_lod")) {
         *tail = &dev_min_lod;
         tail = (void **)&dev_min_lod.pNext;
      }

      /* Extension feature structs with supported bits */
      VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dev_eds3 = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT,
         .extendedDynamicState3ColorWriteMask = eds3_feat.extendedDynamicState3ColorWriteMask,
      };
      if (HAS_EXT("VK_EXT_extended_dynamic_state3")) {
         *tail = &dev_eds3;
         tail = (void **)&dev_eds3.pNext;
      }

      VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT dev_restart = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIMITIVE_TOPOLOGY_LIST_RESTART_FEATURES_EXT,
         .primitiveTopologyListRestart = list_restart_feat.primitiveTopologyListRestart,
         .primitiveTopologyPatchListRestart = list_restart_feat.primitiveTopologyPatchListRestart,
      };
      if (HAS_EXT("VK_EXT_primitive_topology_list_restart")) {
         *tail = &dev_restart;
         tail = (void **)&dev_restart.pNext;
      }

      VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT dev_atomic_float2 = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_2_FEATURES_EXT,
         .shaderBufferFloat32AtomicMinMax = atomic_float2_feat.shaderBufferFloat32AtomicMinMax,
         .shaderImageFloat32AtomicMinMax = atomic_float2_feat.shaderImageFloat32AtomicMinMax,
      };
      if (HAS_EXT("VK_EXT_shader_atomic_float2")) {
         *tail = &dev_atomic_float2;
         tail = (void **)&dev_atomic_float2.pNext;
      }

      VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR dev_workgroup = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_FEATURES_KHR,
         .workgroupMemoryExplicitLayout = workgroup_feat.workgroupMemoryExplicitLayout,
         .workgroupMemoryExplicitLayoutScalarBlockLayout = workgroup_feat.workgroupMemoryExplicitLayoutScalarBlockLayout,
         .workgroupMemoryExplicitLayout16BitAccess = workgroup_feat.workgroupMemoryExplicitLayout16BitAccess,
      };
      if (HAS_EXT("VK_KHR_workgroup_memory_explicit_layout")) {
         *tail = &dev_workgroup;
         tail = (void **)&dev_workgroup.pNext;
      }

      VkPhysicalDeviceImage2DViewOf3DFeaturesEXT dev_img2d = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_2D_VIEW_OF_3D_FEATURES_EXT,
         .image2DViewOf3D = img2d_feat.image2DViewOf3D,
         .sampler2DViewOf3D = img2d_feat.sampler2DViewOf3D,
      };
      if (HAS_EXT("VK_EXT_image_2d_view_of_3d")) {
         *tail = &dev_img2d;
         tail = (void **)&dev_img2d.pNext;
      }

      VkPhysicalDeviceShaderClockFeaturesKHR dev_clock = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR,
         .shaderSubgroupClock = clock_feat.shaderSubgroupClock,
      };
      if (HAS_EXT("VK_KHR_shader_clock")) {
         *tail = &dev_clock;
         tail = (void **)&dev_clock.pNext;
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
      VkResult cr = p_CreateDevice(phys, &dci, NULL, &dev);
      const char *mode = rob2.robustBufferAccess2 ? "exact" : "relaxed_no_robustBufferAccess2";
      if (cr == VK_SUCCESS) {
         printf("PASS create_device_bachata_chain mode=%s\n", mode);
         p_DestroyDevice(dev, NULL);
      } else {
         printf("FAIL create_device_bachata_chain mode=%s vkresult=%d\n", mode, (int)cr);
         create_device_failed = 1;
      }
   }

   free(exts);
   p_DestroyInstance(inst, NULL);
   dlclose(h);

   int fails = (hard_count > 0) || create_device_failed;
   printf("RESULT %s\n", fails ? "FAIL" : "PASS");
   return fails ? 1 : 0;
}

/* DXVK 3.1.1 device requirement gate (query only, no rendering).
 *
 * Hard list = the 56 requirements that make DXVK reject the adapter
 * (docs/panprobe-dxvk-coverage.md section 2, dxvk_device_info.cpp:747-1062).
 * Soft list = D3D11 feature-level gates, optional features DXVK uses when
 * present, and the format fallbacks/typed UAV loads (VkFormatProperties3).
 *
 * Output lines (same format as tests/bachata/bachata_reqs.c, parsed by PanProbe):
 *   HARD ok <name> | FAIL hard <name>
 *   SOFT ok <name> | SOFT missing <name> -- <effect>
 *   INFO <key> <value>
 *   RESULT PASS|FAIL   (FAIL when any hard requirement is missing)
 *
 * Usage: dxvk_reqs <libvulkan_panfrost.so>
 */
#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

typedef PFN_vkVoidFunction (*icd_gipa_fn)(VkInstance, const char *);

static int hard_missing = 0, hard_total = 0, soft_missing = 0, soft_total = 0;

static void
hard(int ok, const char *item)
{
   hard_total++;
   if (ok) {
      printf("HARD ok %s\n", item);
   } else {
      printf("FAIL hard %s\n", item);
      hard_missing++;
   }
}

static void
soft(int ok, const char *item, const char *effect)
{
   soft_total++;
   if (ok) {
      printf("SOFT ok %s\n", item);
   } else {
      printf("SOFT missing %s -- %s\n", item, effect);
      soft_missing++;
   }
}

static VkExtensionProperties *exts;
static uint32_t ext_count;

static int
has_ext(const char *name)
{
   for (uint32_t i = 0; i < ext_count; i++)
      if (!strcmp(exts[i].extensionName, name))
         return 1;
   return 0;
}

static PFN_vkGetPhysicalDeviceFeatures2 p_GetPhysicalDeviceFeatures2;
static PFN_vkGetPhysicalDeviceFormatProperties2 p_GetPhysicalDeviceFormatProperties2;
static VkPhysicalDevice phys;

/* Chains one extension feature struct, only when the extension is advertised. */
static void
query_ext_features(const char *ext, void *s)
{
   if (!has_ext(ext))
      return;
   VkPhysicalDeviceFeatures2 f = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = s };
   p_GetPhysicalDeviceFeatures2(phys, &f);
}

static VkFormatFeatureFlags2
fmt_features(VkFormat format, int optimal_only)
{
   VkFormatProperties3 fp3 = { .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3 };
   VkFormatProperties2 fp2 = { .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, .pNext = &fp3 };
   p_GetPhysicalDeviceFormatProperties2(phys, format, &fp2);
   return optimal_only ? fp3.optimalTilingFeatures : (fp3.optimalTilingFeatures | fp3.linearTilingFeatures);
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
   PFN_vkCreateInstance CreateInstance = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
   if (!CreateInstance) {
      printf("FAIL missing vkCreateInstance\n");
      return 1;
   }
   VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3 };
   VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
   VkInstance inst = VK_NULL_HANDLE;
   VkResult r = CreateInstance(&ici, NULL, &inst);
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
   GI(GetPhysicalDeviceQueueFamilyProperties)
   GI(EnumerateDeviceExtensionProperties)
   GI(DestroyInstance)
#undef GI
   p_GetPhysicalDeviceFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2)gipa(inst, "vkGetPhysicalDeviceFeatures2");
   p_GetPhysicalDeviceFormatProperties2 =
      (PFN_vkGetPhysicalDeviceFormatProperties2)gipa(inst, "vkGetPhysicalDeviceFormatProperties2");
   if (!p_GetPhysicalDeviceFeatures2 || !p_GetPhysicalDeviceFormatProperties2) {
      printf("FAIL missing Vulkan 1.1 query entry points\n");
      return 1;
   }

   uint32_t nd = 0;
   if (p_EnumeratePhysicalDevices(inst, &nd, NULL) != VK_SUCCESS || nd == 0) {
      printf("FAIL EnumeratePhysicalDevices count=0\n");
      return 1;
   }
   VkPhysicalDevice *devs = calloc(nd, sizeof(*devs));
   if (!devs)
      return 1;
   p_EnumeratePhysicalDevices(inst, &nd, devs);
   phys = devs[0];
   for (uint32_t i = 0; i < nd; i++) {
      VkPhysicalDeviceProperties2 p2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
      p_GetPhysicalDeviceProperties2(devs[i], &p2);
      if (strstr(p2.properties.deviceName, "Mali")) {
         phys = devs[i];
         break;
      }
   }
   free(devs);

   p_EnumerateDeviceExtensionProperties(phys, NULL, &ext_count, NULL);
   exts = calloc(ext_count ? ext_count : 1, sizeof(*exts));
   if (!exts)
      return 1;
   p_EnumerateDeviceExtensionProperties(phys, NULL, &ext_count, exts);

   VkPhysicalDeviceVulkan12Properties props12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES };
   VkPhysicalDeviceProperties2 props2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &props12 };
   p_GetPhysicalDeviceProperties2(phys, &props2);
   const VkPhysicalDeviceProperties *p = &props2.properties;
   printf("INFO device %s\n", p->deviceName);
   printf("INFO apiVersion %u.%u.%u\n", VK_API_VERSION_MAJOR(p->apiVersion),
          VK_API_VERSION_MINOR(p->apiVersion), VK_API_VERSION_PATCH(p->apiVersion));

   VkPhysicalDeviceVulkan13Features f13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
   VkPhysicalDeviceVulkan12Features f12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &f13 };
   VkPhysicalDeviceVulkan11Features f11 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, .pNext = &f12 };
   VkPhysicalDeviceFeatures2 feat2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &f11 };
   p_GetPhysicalDeviceFeatures2(phys, &feat2);
   const VkPhysicalDeviceFeatures *f = &feat2.features;

   VkPhysicalDeviceRobustness2FeaturesEXT rob2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT };
   VkPhysicalDeviceDepthClipEnableFeaturesEXT dce = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT };
   VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT fsi = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT };
   VkPhysicalDeviceTransformFeedbackFeaturesEXT xfb = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT };
   /* DXVK queries the EXT name (dxvk_device_info.h:158); a KHR-only alias does not count. */
   query_ext_features("VK_EXT_robustness2", &rob2);
   query_ext_features("VK_EXT_depth_clip_enable", &dce);
   query_ext_features("VK_EXT_fragment_shader_interlock", &fsi);
   query_ext_features("VK_EXT_transform_feedback", &xfb);

   uint32_t qn = 0;
   p_GetPhysicalDeviceQueueFamilyProperties(phys, &qn, NULL);
   VkQueueFamilyProperties *qf = calloc(qn ? qn : 1, sizeof(*qf));
   if (!qf)
      return 1;
   p_GetPhysicalDeviceQueueFamilyProperties(phys, &qn, qf);
   int gfx_compute = 0;
   for (uint32_t i = 0; i < qn; i++)
      if ((qf[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
          (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
         gfx_compute = 1;
   free(qf);

   /* ---- 56 hard requirements ---- */
   /* API and device (3) */
   hard(p->apiVersion >= VK_API_VERSION_1_3, "apiVersion >= 1.3");
   hard(gfx_compute, "graphics+compute queue");
   hard(p->limits.maxPushConstantsSize >= 256, "maxPushConstantsSize >= 256");
   /* Extensions (7) */
   hard(has_ext("VK_KHR_swapchain"), "VK_KHR_swapchain");
   hard(has_ext("VK_KHR_load_store_op_none"), "VK_KHR_load_store_op_none");
   hard(has_ext("VK_KHR_maintenance5"), "VK_KHR_maintenance5");
   hard(has_ext("VK_KHR_maintenance6"), "VK_KHR_maintenance6");
   hard(dce.depthClipEnable, "VK_EXT_depth_clip_enable.depthClipEnable");
   hard(rob2.robustBufferAccess2, "VK_EXT_robustness2.robustBufferAccess2");
   hard(rob2.nullDescriptor, "VK_EXT_robustness2.nullDescriptor");
   /* Core 1.0 (22) */
   hard(f->depthBiasClamp, "depthBiasClamp");
   hard(f->depthClamp, "depthClamp");
   hard(f->dualSrcBlend, "dualSrcBlend");
   hard(f->fillModeNonSolid, "fillModeNonSolid");
   hard(f->fragmentStoresAndAtomics, "fragmentStoresAndAtomics");
   hard(f->fullDrawIndexUint32, "fullDrawIndexUint32");
   hard(f->geometryShader, "geometryShader");
   hard(f->imageCubeArray, "imageCubeArray");
   hard(f->independentBlend, "independentBlend");
   hard(f->multiDrawIndirect, "multiDrawIndirect");
   hard(f->multiViewport, "multiViewport");
   hard(f->occlusionQueryPrecise, "occlusionQueryPrecise");
   hard(f->robustBufferAccess, "robustBufferAccess");
   hard(f->sampleRateShading, "sampleRateShading");
   hard(f->samplerAnisotropy, "samplerAnisotropy");
   hard(f->shaderClipDistance, "shaderClipDistance");
   hard(f->shaderCullDistance, "shaderCullDistance");
   hard(f->shaderImageGatherExtended, "shaderImageGatherExtended");
   hard(f->shaderInt16, "shaderInt16");
   hard(f->shaderInt64, "shaderInt64");
   hard(f->shaderSampledImageArrayDynamicIndexing, "shaderSampledImageArrayDynamicIndexing");
   hard(f->textureCompressionBC, "textureCompressionBC");
   /* Vulkan 1.1 (2) */
   hard(f11.shaderDrawParameters, "shaderDrawParameters");
   hard(f11.storageBuffer16BitAccess, "storageBuffer16BitAccess");
   /* Vulkan 1.2 (14) */
   hard(f12.bufferDeviceAddress, "bufferDeviceAddress");
   hard(f12.descriptorIndexing, "descriptorIndexing");
   hard(f12.storageBuffer8BitAccess, "storageBuffer8BitAccess");
   hard(f12.descriptorBindingSampledImageUpdateAfterBind, "descriptorBindingSampledImageUpdateAfterBind");
   hard(f12.descriptorBindingUpdateUnusedWhilePending, "descriptorBindingUpdateUnusedWhilePending");
   hard(f12.descriptorBindingPartiallyBound, "descriptorBindingPartiallyBound");
   hard(f12.hostQueryReset, "hostQueryReset");
   hard(f12.runtimeDescriptorArray, "runtimeDescriptorArray");
   hard(f12.samplerMirrorClampToEdge, "samplerMirrorClampToEdge");
   hard(f12.scalarBlockLayout, "scalarBlockLayout");
   hard(f12.shaderInt8, "shaderInt8");
   hard(f12.timelineSemaphore, "timelineSemaphore");
   hard(f12.uniformBufferStandardLayout, "uniformBufferStandardLayout");
   hard(f12.vulkanMemoryModel, "vulkanMemoryModel");
   /* Vulkan 1.3 (8) */
   hard(f13.inlineUniformBlock, "inlineUniformBlock");
   hard(f13.computeFullSubgroups, "computeFullSubgroups");
   hard(f13.dynamicRendering, "dynamicRendering");
   hard(f13.maintenance4, "maintenance4");
   hard(f13.shaderDemoteToHelperInvocation, "shaderDemoteToHelperInvocation");
   hard(f13.shaderZeroInitializeWorkgroupMemory, "shaderZeroInitializeWorkgroupMemory");
   hard(f13.subgroupSizeControl, "subgroupSizeControl");
   hard(f13.synchronization2, "synchronization2");

   /* ---- Soft: D3D11 feature-level gates (d3d11_features.cpp:186-215) ---- */
   int fl11_0 = f->drawIndirectFirstInstance && f->fragmentStoresAndAtomics && f->multiDrawIndirect &&
                f->tessellationShader;
   int fl11_1 = fl11_0 && f->logicOp && f->vertexPipelineStoresAndAtomics;
   soft(f->drawIndirectFirstInstance, "drawIndirectFirstInstance (FL11_0)", "D3D11 capped at FL10_1");
   soft(f->tessellationShader, "tessellationShader (FL11_0)", "D3D11 capped at FL10_1");
   soft(f->logicOp, "logicOp (FL11_1)", "D3D11 capped at FL11_0");
   soft(f->vertexPipelineStoresAndAtomics, "vertexPipelineStoresAndAtomics (FL11_1)", "D3D11 capped at FL11_0");
   const VkPhysicalDeviceSparseProperties *sp = &p->sparseProperties;
   int tiled_t2 = f->sparseBinding && f->sparseResidencyBuffer && f->sparseResidencyImage2D &&
                  f->sparseResidencyAliased && sp->residencyStandard2DBlockShape &&
                  f->shaderResourceResidency && f->shaderResourceMinLod && f12.samplerFilterMinmax &&
                  props12.filterMinmaxSingleComponentFormats && sp->residencyNonResidentStrict &&
                  !sp->residencyAlignedMipSize;
   soft(tiled_t2, "tiled resources tier 2 (FL12_0)", "D3D11 capped at FL11_1");

   /* Typed UAV loads: 18 formats need STORAGE_READ_WITHOUT_FORMAT (d3d11_features.cpp:353). */
   static const struct { VkFormat fmt; const char *name; } uav[18] = {
      { VK_FORMAT_R32_SFLOAT, "R32_SFLOAT" }, { VK_FORMAT_R32_UINT, "R32_UINT" },
      { VK_FORMAT_R32_SINT, "R32_SINT" }, { VK_FORMAT_R32G32B32A32_SFLOAT, "R32G32B32A32_SFLOAT" },
      { VK_FORMAT_R32G32B32A32_UINT, "R32G32B32A32_UINT" }, { VK_FORMAT_R32G32B32A32_SINT, "R32G32B32A32_SINT" },
      { VK_FORMAT_R16G16B16A16_SFLOAT, "R16G16B16A16_SFLOAT" }, { VK_FORMAT_R16G16B16A16_UINT, "R16G16B16A16_UINT" },
      { VK_FORMAT_R16G16B16A16_SINT, "R16G16B16A16_SINT" }, { VK_FORMAT_R8G8B8A8_UNORM, "R8G8B8A8_UNORM" },
      { VK_FORMAT_R8G8B8A8_UINT, "R8G8B8A8_UINT" }, { VK_FORMAT_R8G8B8A8_SINT, "R8G8B8A8_SINT" },
      { VK_FORMAT_R16_SFLOAT, "R16_SFLOAT" }, { VK_FORMAT_R16_UINT, "R16_UINT" },
      { VK_FORMAT_R16_SINT, "R16_SINT" }, { VK_FORMAT_R8_UNORM, "R8_UNORM" },
      { VK_FORMAT_R8_UINT, "R8_UINT" }, { VK_FORMAT_R8_SINT, "R8_SINT" },
   };
   int uav_ok = 0;
   char uav_missing[512] = "";
   for (int i = 0; i < 18; i++) {
      if (fmt_features(uav[i].fmt, 0) & VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT) {
         uav_ok++;
      } else {
         if (uav_missing[0])
            strcat(uav_missing, ",");
         strcat(uav_missing, uav[i].name);
      }
   }
   printf("INFO typed_uav_load %d/18%s%s\n", uav_ok, uav_missing[0] ? " missing=" : "", uav_missing);
   soft(uav_ok == 18, "typed UAV load, 18 formats (FL12_0)", "D3D11 capped at FL11_1");
   soft(has_ext("VK_EXT_conservative_rasterization"), "VK_EXT_conservative_rasterization (FL12_1)", "D3D11 capped at FL12_0");
   soft(fsi.fragmentShaderPixelInterlock, "fragmentShaderPixelInterlock (FL12_1)", "D3D11 capped at FL12_0");
   const char *fl = !fl11_0 ? "10_1" : !fl11_1 ? "11_0" : !(tiled_t2 && uav_ok == 18) ? "11_1"
                  : !(has_ext("VK_EXT_conservative_rasterization") && fsi.fragmentShaderPixelInterlock) ? "12_0" : "12_1";
   printf("INFO d3d11_max_feature_level %s\n", fl);

   /* ---- Soft: formats ---- */
   const VkFormatFeatureFlags2 ds = VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT;
   soft((fmt_features(VK_FORMAT_D24_UNORM_S8_UINT, 1) & ds) || (fmt_features(VK_FORMAT_D32_SFLOAT_S8_UINT, 1) & ds),
        "D24_UNORM_S8_UINT or D32_SFLOAT_S8_UINT", "no D3D depth-stencil format");
   soft(fmt_features(VK_FORMAT_D16_UNORM_S8_UINT, 1) & ds, "D16_UNORM_S8_UINT (D3D9)", "D3D9 D16S8 emulated");
   soft(fmt_features(VK_FORMAT_A8_UNORM_KHR, 1) & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT, "A8_UNORM",
        "R8 with swizzle fallback");

   /* ---- Soft: optional features DXVK enables when present (dxvk_device_info.cpp:827-1091) ---- */
   soft(f->depthBounds, "depthBounds", "depth bounds test ignored");
   soft(f->pipelineStatisticsQuery, "pipelineStatisticsQuery", "pipeline statistics queries return 0");
   soft(f->variableMultisampleRate, "variableMultisampleRate", "UAV-only rendering with forced sample count fails");
   soft(f->wideLines, "wideLines", "wide lines drawn 1 px");
   soft(f->largePoints, "largePoints", "point size clamped");
   soft(f->shaderFloat64, "shaderFloat64", "no D3D11 double-precision shader ops");
   soft(f12.shaderFloat16, "shaderFloat16", "fp16 math in fp32");
   soft(f12.drawIndirectCount, "drawIndirectCount", "multi-draw-indirect-count emulated");
   soft(f12.shaderOutputViewportIndex, "shaderOutputViewportIndex", "VS/DS viewport index not exposed");
   soft(f12.shaderOutputLayer, "shaderOutputLayer", "VS/DS render target array index not exposed");
   soft(f12.samplerFilterMinmax, "samplerFilterMinmax", "min/max filtering samplers unsupported");
   soft(rob2.robustImageAccess2 || f13.robustImageAccess, "robustImageAccess / robustImageAccess2", "OOB image access undefined");
   soft(xfb.transformFeedback, "VK_EXT_transform_feedback", "D3D10/11 stream output unsupported");
   soft(has_ext("VK_EXT_vertex_attribute_divisor") || has_ext("VK_KHR_vertex_attribute_divisor"),
        "VK_EXT_vertex_attribute_divisor", "instanced step rates emulated");
   soft(has_ext("VK_EXT_custom_border_color"), "VK_EXT_custom_border_color", "custom border colors approximated");
   soft(has_ext("VK_EXT_border_color_swizzle"), "VK_EXT_border_color_swizzle", "border color swizzle ignored");
   soft(has_ext("VK_EXT_extended_dynamic_state3"), "VK_EXT_extended_dynamic_state3", "more pipeline variants");
   soft(has_ext("VK_EXT_graphics_pipeline_library") && has_ext("VK_KHR_pipeline_library"),
        "VK_EXT_graphics_pipeline_library", "shader compile stutter");
   soft(has_ext("VK_EXT_line_rasterization") || has_ext("VK_KHR_line_rasterization"), "VK_EXT_line_rasterization",
        "D3D9 line modes approximated");
   soft(has_ext("VK_EXT_depth_bias_control"), "VK_EXT_depth_bias_control", "D3D depth bias representation approximated");
   soft(has_ext("VK_EXT_non_seamless_cube_map"), "VK_EXT_non_seamless_cube_map", "D3D9 non-seamless cubes seamless");
   soft(has_ext("VK_EXT_memory_priority"), "VK_EXT_memory_priority", "no allocation priorities");
   soft(has_ext("VK_EXT_memory_budget"), "VK_EXT_memory_budget", "no memory budget tracking");
   soft(has_ext("VK_EXT_multi_draw"), "VK_EXT_multi_draw", "multi-draw batching off");
   soft(has_ext("VK_EXT_attachment_feedback_loop_layout"), "VK_EXT_attachment_feedback_loop_layout",
        "feedback loops use GENERAL layout");
   soft(has_ext("VK_EXT_shader_stencil_export"), "VK_EXT_shader_stencil_export", "PS stencil ref unsupported");
   soft(has_ext("VK_EXT_sample_locations"), "VK_EXT_sample_locations", "custom sample positions ignored");

   printf("INFO hard %d/%d available, soft %d/%d available\n", hard_total - hard_missing, hard_total,
          soft_total - soft_missing, soft_total);
   p_DestroyInstance(inst, NULL);
   free(exts);
   printf("RESULT %s\n", hard_missing ? "FAIL" : "PASS");
   return hard_missing ? 1 : 0;
}

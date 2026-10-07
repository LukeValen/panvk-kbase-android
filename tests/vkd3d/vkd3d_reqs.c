/* vkd3d (D3D12 on Vulkan) device requirement gate (query only, no rendering).
 *
 * PanPlay bundles Proton 11.0-2 (GameNative proton-wine), whose d3d12.dll uses the
 * Wine-bundled upstream vkd3d 1.18 (exported from wined3d.dll). No vkd3d-proton is
 * shipped. So:
 *   HARD = what makes Wine vkd3d 1.18 device creation fail (libs/vkd3d/device.c, tag vkd3d-1.18).
 *   SOFT = Wine vkd3d 1.18 feature-level / capability gates, plus every vkd3d-proton
 *          (HEAD 2230755) hard requirement as "proton: ..." items, so the gap to
 *          vkd3d-proton is visible without failing the report.
 * Line refs: docs/vkd3d-vulkan-requirements.md.
 *
 * Output (same format as tests/dxvk/vulkan/dxvk-reqs/dxvk_reqs.c, parsed by PanProbe):
 *   HARD ok <name> | FAIL hard <name>
 *   SOFT ok <name> | SOFT missing <name> -- <effect>
 *   INFO <key> <value>
 *   RESULT PASS|FAIL   (FAIL when any hard requirement is missing)
 *
 * Usage: vkd3d_reqs <libvulkan_panfrost.so>
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

/* vkd3d-proton hard requirement, reported as soft (PanPlay does not ship vkd3d-proton). */
static int proton_fail = 0;
static void
proton(int ok, const char *item)
{
   char name[192];
   snprintf(name, sizeof(name), "proton: %s", item);
   soft(ok, name, "vkd3d-proton device creation fails");
   if (!ok)
      proton_fail++;
}

static VkExtensionProperties *exts;
static uint32_t ext_count;

static uint32_t
ext_version(const char *name)
{
   for (uint32_t i = 0; i < ext_count; i++)
      if (!strcmp(exts[i].extensionName, name))
         return exts[i].specVersion ? exts[i].specVersion : 1;
   return 0;
}
#define has_ext(n) (ext_version(n) != 0)

static PFN_vkGetPhysicalDeviceFeatures2 p_GetPhysicalDeviceFeatures2;
static PFN_vkGetPhysicalDeviceProperties2 p_GetPhysicalDeviceProperties2;
static PFN_vkGetPhysicalDeviceFormatProperties2 p_GetPhysicalDeviceFormatProperties2;
static VkPhysicalDevice phys;

/* Chains one extension struct, only when the extension is advertised (as vkd3d does). */
static void
query_ext_features(const char *ext, void *s)
{
   if (!has_ext(ext))
      return;
   VkPhysicalDeviceFeatures2 f = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = s };
   p_GetPhysicalDeviceFeatures2(phys, &f);
}

static void
query_ext_props(const char *ext, void *s)
{
   if (!has_ext(ext))
      return;
   VkPhysicalDeviceProperties2 p = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = s };
   p_GetPhysicalDeviceProperties2(phys, &p);
}

static VkFormatFeatureFlags2
fmt_features(VkFormat format, int optimal_only)
{
   VkFormatProperties3 fp3 = { .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3 };
   VkFormatProperties2 fp2 = { .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, .pNext = &fp3 };
   p_GetPhysicalDeviceFormatProperties2(phys, format, &fp2);
   return optimal_only ? fp3.optimalTilingFeatures : (fp3.optimalTilingFeatures | fp3.linearTilingFeatures);
}

static const char *
fl_name(int fl)
{
   switch (fl) {
   case 110: return "11_0";
   case 111: return "11_1";
   case 120: return "12_0";
   case 121: return "12_1";
   default: return "12_2";
   }
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
   GI(GetPhysicalDeviceQueueFamilyProperties)
   GI(EnumerateDeviceExtensionProperties)
   GI(DestroyInstance)
#undef GI
   p_GetPhysicalDeviceProperties2 = (PFN_vkGetPhysicalDeviceProperties2)gipa(inst, "vkGetPhysicalDeviceProperties2");
   p_GetPhysicalDeviceFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2)gipa(inst, "vkGetPhysicalDeviceFeatures2");
   p_GetPhysicalDeviceFormatProperties2 =
      (PFN_vkGetPhysicalDeviceFormatProperties2)gipa(inst, "vkGetPhysicalDeviceFormatProperties2");
   if (!p_GetPhysicalDeviceProperties2 || !p_GetPhysicalDeviceFeatures2 || !p_GetPhysicalDeviceFormatProperties2) {
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

   /* ---- properties ---- */
   VkPhysicalDeviceVulkan13Properties p13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES };
   VkPhysicalDeviceVulkan12Properties p12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES, .pNext = &p13 };
   VkPhysicalDeviceVulkan11Properties p11 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES, .pNext = &p12 };
   VkPhysicalDeviceProperties2 props2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &p11 };
   p_GetPhysicalDeviceProperties2(phys, &props2);
   const VkPhysicalDeviceProperties *p = &props2.properties;
   const VkPhysicalDeviceLimits *lim = &p->limits;
   printf("INFO device %s\n", p->deviceName);
   printf("INFO apiVersion %u.%u.%u\n", VK_API_VERSION_MAJOR(p->apiVersion),
          VK_API_VERSION_MINOR(p->apiVersion), VK_API_VERSION_PATCH(p->apiVersion));
   printf("INFO variant wine-vkd3d-1.18 (PanPlay Proton 11.0-2 d3d12.dll); proton items = vkd3d-proton 2230755\n");

   VkPhysicalDeviceTransformFeedbackPropertiesEXT xfbp = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT };
   VkPhysicalDeviceConservativeRasterizationPropertiesEXT crp = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT };
   VkPhysicalDeviceDescriptorBufferPropertiesEXT dbp = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT };
   query_ext_props("VK_EXT_transform_feedback", &xfbp);
   query_ext_props("VK_EXT_conservative_rasterization", &crp);
   query_ext_props("VK_EXT_descriptor_buffer", &dbp);

   /* ---- features ---- */
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
   VkPhysicalDeviceConditionalRenderingFeaturesEXT cond = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT };
   VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT vad = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT };
   VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT mut = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT };
   VkPhysicalDeviceDescriptorBufferFeaturesEXT dbf = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT };
   VkPhysicalDeviceMaintenance5FeaturesKHR m5 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR };
   VkPhysicalDeviceMaintenance6FeaturesKHR m6 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_FEATURES_KHR };
   VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR csd = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR };
   VkPhysicalDeviceShaderMaximalReconvergenceFeaturesKHR mrc = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MAXIMAL_RECONVERGENCE_FEATURES_KHR };
   VkPhysicalDeviceShaderQuadControlFeaturesKHR qc = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_QUAD_CONTROL_FEATURES_KHR };
   VkPhysicalDeviceMeshShaderFeaturesEXT mesh = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT };
   VkPhysicalDeviceFragmentShadingRateFeaturesKHR fsr = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR };
   VkPhysicalDeviceRayQueryFeaturesKHR rq = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
   VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT ia64 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT };
   query_ext_features("VK_EXT_robustness2", &rob2);
   query_ext_features("VK_EXT_depth_clip_enable", &dce);
   query_ext_features("VK_EXT_fragment_shader_interlock", &fsi);
   query_ext_features("VK_EXT_transform_feedback", &xfb);
   query_ext_features("VK_EXT_conditional_rendering", &cond);
   query_ext_features("VK_EXT_vertex_attribute_divisor", &vad);
   query_ext_features("VK_EXT_mutable_descriptor_type", &mut);
   query_ext_features("VK_EXT_descriptor_buffer", &dbf);
   query_ext_features("VK_KHR_maintenance5", &m5);
   query_ext_features("VK_KHR_maintenance6", &m6);
   query_ext_features("VK_KHR_compute_shader_derivatives", &csd);
   query_ext_features("VK_KHR_shader_maximal_reconvergence", &mrc);
   query_ext_features("VK_KHR_shader_quad_control", &qc);
   query_ext_features("VK_EXT_mesh_shader", &mesh);
   query_ext_features("VK_KHR_fragment_shading_rate", &fsr);
   query_ext_features("VK_KHR_ray_query", &rq);
   query_ext_features("VK_EXT_shader_image_atomic_int64", &ia64);

   uint32_t qn = 0;
   p_GetPhysicalDeviceQueueFamilyProperties(phys, &qn, NULL);
   VkQueueFamilyProperties *qf = calloc(qn ? qn : 1, sizeof(*qf));
   if (!qf)
      return 1;
   p_GetPhysicalDeviceQueueFamilyProperties(phys, &qn, qf);
   int gfx_compute = 0, sparse_queue = 0;
   for (uint32_t i = 0; i < qn; i++) {
      if ((qf[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
          (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
         gfx_compute = 1;
      if ((qf[i].queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) && qf[i].queueCount)
         sparse_queue = 1;
   }
   free(qf);

   /* ================= Wine vkd3d 1.18: HARD (device creation fails) ================= */
   /* device.c:73-78 required_device_extensions[]; device.c:2153-2180 direct queue. */
   hard(has_ext("VK_KHR_maintenance1"), "VK_KHR_maintenance1");
   hard(has_ext("VK_KHR_maintenance2"), "VK_KHR_maintenance2");
   hard(has_ext("VK_KHR_shader_draw_parameters"), "VK_KHR_shader_draw_parameters");
   hard(gfx_compute, "graphics+compute queue");
   /* Internal UAV-clear pipelines use StorageImageWriteWithoutFormat (state.c:4319-4325). */
   hard(f->shaderStorageImageWriteWithoutFormat, "shaderStorageImageWriteWithoutFormat (UAV clear pipelines)");
   /* Without robustness2.nullDescriptor, null views need RGBA8 sampled+storage images (resource.c:4890-4945). */
   const VkFormatFeatureFlags2 rgba8 = fmt_features(VK_FORMAT_R8G8B8A8_UNORM, 1);
   hard(rob2.nullDescriptor ||
           ((rgba8 & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT) && (rgba8 & VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT)),
        "nullDescriptor or RGBA8 sampled+storage (null resources)");

   /* ================= Wine vkd3d 1.18: SOFT ================= */
   /* FL11_0 checklist (device.c:1414-1433): any missing bit blocks FL11_1 promotion. */
   static const char *fl11_eff = "Wine vkd3d stays at FL11_0 (no 11_1)";
   int have_11_0 = 1;
#define FL110(cond, name)                                                      \
   do {                                                                        \
      int ok_ = !!(cond);                                                      \
      have_11_0 &= ok_;                                                        \
      soft(ok_, name " (FL11_0 checklist)", fl11_eff);                        \
   } while (0)
   FL110(f->depthBiasClamp, "depthBiasClamp");
   FL110(f->depthClamp, "depthClamp");
   FL110(f->drawIndirectFirstInstance, "drawIndirectFirstInstance");
   FL110(f->dualSrcBlend, "dualSrcBlend");
   FL110(f->fragmentStoresAndAtomics, "fragmentStoresAndAtomics");
   FL110(f->fullDrawIndexUint32, "fullDrawIndexUint32");
   FL110(f->geometryShader, "geometryShader");
   FL110(f->imageCubeArray, "imageCubeArray");
   FL110(f->independentBlend, "independentBlend");
   FL110(f->multiDrawIndirect, "multiDrawIndirect");
   FL110(f->multiViewport, "multiViewport");
   FL110(f->occlusionQueryPrecise, "occlusionQueryPrecise");
   FL110(f->pipelineStatisticsQuery, "pipelineStatisticsQuery");
   FL110(f->samplerAnisotropy, "samplerAnisotropy");
   FL110(f->sampleRateShading, "sampleRateShading");
   FL110(f->shaderClipDistance, "shaderClipDistance");
   FL110(f->shaderCullDistance, "shaderCullDistance");
   FL110(f->shaderImageGatherExtended, "shaderImageGatherExtended");
   FL110(f->tessellationShader, "tessellationShader");
#undef FL110
   /* Limit warnings only (device.c:1388-1412). */
   soft(lim->maxPushConstantsSize >= 256, "maxPushConstantsSize >= 256", "warning only; large root signatures fail");
   soft(lim->maxComputeSharedMemorySize >= 32768, "maxComputeSharedMemorySize >= 32768", "warning only; big groupshared fails");
   soft(lim->viewportBoundsRange[0] <= -32768 && lim->viewportBoundsRange[1] >= 32767, "viewportBoundsRange +-32768",
        "warning only");
   soft(lim->viewportSubPixelBits >= 8, "viewportSubPixelBits >= 8", "warning only");
   soft(lim->maxPerStageDescriptorUniformBuffers >= 15, "maxPerStageDescriptorUniformBuffers >= 15",
        "warning only; 14 CBV root tables");

   /* FL11_1 (device.c:1454-1459). */
   int fl11_1_bits = f->logicOp && f->vertexPipelineStoresAndAtomics && lim->maxPerStageDescriptorStorageBuffers >= 64 &&
                     lim->maxPerStageDescriptorStorageImages >= 64;
   soft(f->logicOp, "logicOp (FL11_1)", "capped at FL11_0");
   soft(f->vertexPipelineStoresAndAtomics, "vertexPipelineStoresAndAtomics (FL11_1)", "capped at FL11_0");
   soft(lim->maxPerStageDescriptorStorageBuffers >= 64 && lim->maxPerStageDescriptorStorageImages >= 64,
        "64 per-stage storage buffers+images (FL11_1)", "capped at FL11_0");

   /* Typed UAV additional formats, Wine: ReadWithoutFormat + STORAGE_IMAGE on 15 formats (device.c:1528-1554, 1804). */
   static const struct { VkFormat fmt; const char *name; } uav[18] = {
      { VK_FORMAT_R32G32B32A32_SFLOAT, "R32G32B32A32_SFLOAT" }, { VK_FORMAT_R32G32B32A32_UINT, "R32G32B32A32_UINT" },
      { VK_FORMAT_R32G32B32A32_SINT, "R32G32B32A32_SINT" }, { VK_FORMAT_R16G16B16A16_SFLOAT, "R16G16B16A16_SFLOAT" },
      { VK_FORMAT_R16G16B16A16_UINT, "R16G16B16A16_UINT" }, { VK_FORMAT_R16G16B16A16_SINT, "R16G16B16A16_SINT" },
      { VK_FORMAT_R8G8B8A8_UNORM, "R8G8B8A8_UNORM" }, { VK_FORMAT_R8G8B8A8_UINT, "R8G8B8A8_UINT" },
      { VK_FORMAT_R8G8B8A8_SINT, "R8G8B8A8_SINT" }, { VK_FORMAT_R16_SFLOAT, "R16_SFLOAT" },
      { VK_FORMAT_R16_UINT, "R16_UINT" }, { VK_FORMAT_R16_SINT, "R16_SINT" },
      { VK_FORMAT_R8_UNORM, "R8_UNORM" }, { VK_FORMAT_R8_UINT, "R8_UINT" }, { VK_FORMAT_R8_SINT, "R8_SINT" },
      { VK_FORMAT_R32_SFLOAT, "R32_SFLOAT" }, { VK_FORMAT_R32_UINT, "R32_UINT" }, { VK_FORMAT_R32_SINT, "R32_SINT" },
   };
   int wine_uav = 0, proton_uav = 0;
   char uav_missing[512] = "";
   for (int i = 0; i < 18; i++) {
      VkFormatFeatureFlags2 ff = fmt_features(uav[i].fmt, 0);
      if (i < 15 && (ff & VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT))
         wine_uav++;
      if ((ff & VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT) && (ff & VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT)) {
         proton_uav++;
      } else {
         if (uav_missing[0])
            strcat(uav_missing, ",");
         strcat(uav_missing, uav[i].name);
      }
   }
   int wine_typed_uav = f->shaderStorageImageReadWithoutFormat && wine_uav == 15;
   printf("INFO typed_uav_load %d/18%s%s\n", proton_uav, uav_missing[0] ? " missing=" : "", uav_missing);
   soft(wine_typed_uav, "TypedUAVLoadAdditionalFormats (ReadWithoutFormat + 15 storage formats)",
        "typed UAV loads limited to R32; FL12_0 blocked");

   /* Wine binding tier (device.c:1797-1802). */
   int wine_tier = lim->maxPerStageDescriptorSamplers <= 16 ? 1 : lim->maxPerStageDescriptorUniformBuffers <= 14 ? 2 : 3;
   soft(wine_tier == 3, "ResourceBindingTier 3 (samplers > 16, UBOs > 14)", "lower D3D12 binding tier");

   /* Vulkan descriptor heaps instead of virtual heaps (device.c:1954-1960). */
   int wine_vk_heaps = has_ext("VK_EXT_descriptor_indexing") && f12.descriptorBindingUniformBufferUpdateAfterBind &&
                       f12.descriptorBindingSampledImageUpdateAfterBind && f12.descriptorBindingStorageImageUpdateAfterBind &&
                       f12.descriptorBindingUniformTexelBufferUpdateAfterBind &&
                       f12.descriptorBindingStorageTexelBufferUpdateAfterBind;
   soft(has_ext("VK_EXT_descriptor_indexing"), "VK_EXT_descriptor_indexing", "virtual heaps, no bindless shaders");
   soft(wine_vk_heaps, "update-after-bind UBO/sampled/storage image/texel buffers (Vulkan heaps)",
        "virtual descriptor heaps (CPU copies, small tables)");
   soft(p12.robustBufferAccessUpdateAfterBind, "robustBufferAccessUpdateAfterBind",
        "Wine disables robustBufferAccess with UAB heaps");
   soft(has_ext("VK_EXT_mutable_descriptor_type") && mut.mutableDescriptorType, "VK_EXT_mutable_descriptor_type",
        "5 descriptor sets per heap instead of 1");
   soft(has_ext("VK_KHR_push_descriptor"), "VK_KHR_push_descriptor", "root descriptors via regular sets");
   soft(has_ext("VK_EXT_robustness2") && rob2.nullDescriptor, "VK_EXT_robustness2.nullDescriptor",
        "fallback null resources allocated");
   soft(has_ext("VK_KHR_timeline_semaphore") && f12.timelineSemaphore, "VK_KHR_timeline_semaphore",
        "D3D12 fences emulated with VkFence + binary semaphores");
   soft(has_ext("VK_EXT_transform_feedback") && xfb.transformFeedback, "VK_EXT_transform_feedback",
        "stream output PSOs fail (E_NOTIMPL)");
   soft(has_ext("VK_EXT_conditional_rendering") && cond.conditionalRendering, "VK_EXT_conditional_rendering",
        "SetPredication ignored");
   soft(ext_version("VK_EXT_vertex_attribute_divisor") >= 3 && vad.vertexAttributeInstanceRateDivisor,
        "VK_EXT_vertex_attribute_divisor (spec >= 3)", "instance step rates replaced by 1");
   soft(has_ext("VK_EXT_fragment_shader_interlock") && fsi.fragmentShaderPixelInterlock && fsi.fragmentShaderSampleInterlock,
        "VK_EXT_fragment_shader_interlock pixel+sample (ROVs)", "ROVsSupported false; FL12_1 blocked");
   soft(has_ext("VK_EXT_depth_clip_enable") && dce.depthClipEnable, "VK_EXT_depth_clip_enable", "DepthClipEnable=FALSE ignored");
   soft(has_ext("VK_EXT_shader_stencil_export"), "VK_EXT_shader_stencil_export", "PSSpecifiedStencilRef unsupported");
   soft(has_ext("VK_EXT_shader_viewport_index_layer"), "VK_EXT_shader_viewport_index_layer",
        "VS viewport/RT index needs GS emulation");
   soft(has_ext("VK_EXT_shader_demote_to_helper_invocation") && f13.shaderDemoteToHelperInvocation,
        "VK_EXT_shader_demote_to_helper_invocation", "discard not demote");
   soft(has_ext("VK_KHR_zero_initialize_workgroup_memory") && f13.shaderZeroInitializeWorkgroupMemory,
        "VK_KHR_zero_initialize_workgroup_memory", "groupshared not zeroed");
   soft(has_ext("VK_EXT_texel_buffer_alignment"), "VK_EXT_texel_buffer_alignment",
        "core texel buffer offset alignment");
   soft(has_ext("VK_KHR_draw_indirect_count"), "VK_KHR_draw_indirect_count", "ExecuteIndirect count buffer ignored");
   soft(has_ext("VK_KHR_sampler_mirror_clamp_to_edge"), "VK_KHR_sampler_mirror_clamp_to_edge",
        "MIRROR_ONCE becomes CLAMP");
   soft(has_ext("VK_EXT_4444_formats"), "VK_EXT_4444_formats", "B4G4R4A4 unsupported");
   soft(has_ext("VK_EXT_calibrated_timestamps") || has_ext("VK_KHR_calibrated_timestamps"), "VK_EXT_calibrated_timestamps",
        "GetClockCalibration returns 0");
   soft(f->depthBounds, "depthBounds", "DepthBoundsTestSupported false; PSOs using it fail");
   soft(f->shaderInt64, "shaderInt64", "Int64ShaderOps false");
   soft(f->shaderFloat64, "shaderFloat64", "DoublePrecisionFloatShaderOps false");
   const VkFormatFeatureFlags2 dsa = VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT;
   soft(fmt_features(VK_FORMAT_D24_UNORM_S8_UINT, 1) & dsa, "D24_UNORM_S8_UINT depth-stencil",
        "remapped to D32_SFLOAT_S8_UINT");
   int wave_ops = p11.subgroupSize >= 4 &&
                  (p11.subgroupSupportedOperations & (VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_VOTE_BIT |
                                                      VK_SUBGROUP_FEATURE_ARITHMETIC_BIT | VK_SUBGROUP_FEATURE_BALLOT_BIT |
                                                      VK_SUBGROUP_FEATURE_SHUFFLE_BIT | VK_SUBGROUP_FEATURE_QUAD_BIT)) ==
                     (VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_VOTE_BIT | VK_SUBGROUP_FEATURE_ARITHMETIC_BIT |
                      VK_SUBGROUP_FEATURE_BALLOT_BIT | VK_SUBGROUP_FEATURE_SHUFFLE_BIT | VK_SUBGROUP_FEATURE_QUAD_BIT) &&
                  (p11.subgroupSupportedStages & (VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)) ==
                     (VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
   soft(wave_ops, "WaveOps (subgroup basic/vote/arith/ballot/shuffle/quad in CS+FS)", "WaveOps false; SM6 wave intrinsics fail");

   /* Wine FL: 11_0 baseline; 11_1; 12_0 never automatic (tiled tier forced 0, device.c:1790-1795). */
   int wine_fl = (have_11_0 && fl11_1_bits) ? 111 : 110;
   printf("INFO wine_feature_level %s (12_0+ only via VKD3D_CAPS_OVERRIDE; tiled tier forced 0)\n", fl_name(wine_fl));
   printf("INFO wine_shader_model 6_0 (fixed cap, device.c:3637-3647)\n");
   printf("INFO wine_binding_tier %d\n", wine_tier);
   printf("INFO wine_descriptor_heaps %s\n", wine_vk_heaps ? "vulkan (update-after-bind)" : "virtual");

   /* ================= vkd3d-proton: hard requirements (as soft) ================= */
   proton(p->apiVersion >= VK_API_VERSION_1_3, "apiVersion >= 1.3");
   proton(f11.shaderDrawParameters, "shaderDrawParameters");
   proton(f12.samplerMirrorClampToEdge, "samplerMirrorClampToEdge");
   proton(rob2.robustBufferAccess2, "robustBufferAccess2");
   proton(rob2.robustImageAccess2, "robustImageAccess2");
   proton(rob2.nullDescriptor, "nullDescriptor");
   proton(has_ext("VK_KHR_push_descriptor"), "VK_KHR_push_descriptor");
   proton(m5.maintenance5, "maintenance5");
   proton(m6.maintenance6, "maintenance6");
   proton(vad.vertexAttributeInstanceRateDivisor && vad.vertexAttributeInstanceRateZeroDivisor &&
             ext_version("VK_EXT_vertex_attribute_divisor") >= 3,
          "EXT_vertex_attribute_divisor rate+zero divisor");
   proton(xfbp.transformFeedbackQueries, "transformFeedbackQueries");
   proton(p13.storageTexelBufferOffsetSingleTexelAlignment || p13.storageTexelBufferOffsetAlignmentBytes == 1,
          "storage texel buffer single-texel alignment");
   proton(p13.uniformTexelBufferOffsetSingleTexelAlignment || p13.uniformTexelBufferOffsetAlignmentBytes == 1,
          "uniform texel buffer single-texel alignment");
   proton(f12.descriptorIndexing, "descriptorIndexing");
   proton(f12.shaderStorageTexelBufferArrayNonUniformIndexing, "shaderStorageTexelBufferArrayNonUniformIndexing");
   proton(f12.shaderStorageImageArrayNonUniformIndexing, "shaderStorageImageArrayNonUniformIndexing");
   proton(f12.descriptorBindingVariableDescriptorCount, "descriptorBindingVariableDescriptorCount");
   /* Descriptor buffer path skips the 1M per-stage limit check (resource.c:11355, state.c:8201). */
   uint32_t ssbo_align = (uint32_t)lim->minStorageBufferOffsetAlignment;
   int db_path = dbf.descriptorBuffer && dbf.descriptorBufferPushDescriptors &&
                 f12.shaderUniformBufferArrayNonUniformIndexing && !(ssbo_align > 4 && ssbo_align <= 16);
   int limits_1m = p12.maxPerStageDescriptorUpdateAfterBindSampledImages >= 1000000 &&
                   p12.maxPerStageDescriptorUpdateAfterBindStorageImages >= 1000000 &&
                   p12.maxPerStageDescriptorUpdateAfterBindStorageBuffers >= 1000000;
   printf("INFO uab_per_stage sampled=%u storage_img=%u ssbo=%u ubo=%u samplers=%u\n",
          p12.maxPerStageDescriptorUpdateAfterBindSampledImages, p12.maxPerStageDescriptorUpdateAfterBindStorageImages,
          p12.maxPerStageDescriptorUpdateAfterBindStorageBuffers, p12.maxPerStageDescriptorUpdateAfterBindUniformBuffers,
          p12.maxPerStageDescriptorUpdateAfterBindSamplers);
   proton(db_path || limits_1m, "1M per-stage UAB sampled/storage images + SSBOs (or descriptor buffer)");
   /* Used unconditionally, no explicit check: Vulkan errors at use. */
   proton(gfx_compute, "graphics+compute queue");
   proton(f12.timelineSemaphore, "timelineSemaphore (used, unchecked)");
   proton(f12.bufferDeviceAddress, "bufferDeviceAddress (used, unchecked)");
   proton(f13.synchronization2, "synchronization2 (used, unchecked)");
   proton(f13.dynamicRendering, "dynamicRendering (used, unchecked)");

   /* ================= vkd3d-proton: capability gates ================= */
   int proton_uav_ok = proton_uav == 18;
   int tiled = 0; /* 0 none, 1, 2, 4 (device.c:8936-8960) */
   if (f->sparseBinding && f->sparseResidencyAliased && f->sparseResidencyBuffer && f->sparseResidencyImage2D &&
       p->sparseProperties.residencyStandard2DBlockShape && sparse_queue) {
      tiled = 1;
      if (f->shaderResourceResidency && f->shaderResourceMinLod && !p->sparseProperties.residencyAlignedMipSize &&
          p->sparseProperties.residencyNonResidentStrict && p12.filterMinmaxSingleComponentFormats)
         tiled = (f->sparseResidencyImage3D && p->sparseProperties.residencyStandard3DBlockShape) ? 4 : 2;
   }
   int cons = !has_ext("VK_EXT_conservative_rasterization") ? 0 : !crp.degenerateTrianglesRasterized ? 1
              : !crp.fullyCoveredFragmentShaderInputVariable ? 2 : 3;
   int rov = fsi.fragmentShaderPixelInterlock && fsi.fragmentShaderSampleInterlock;
   soft(tiled >= 2, "proton: tiled resources tier 2 (sparse residency + sparse queue)", "vkd3d-proton FL12_0 blocked");
   soft(proton_uav_ok, "proton: typed UAV load on 18 formats (STORAGE_READ_WITHOUT_FORMAT)", "vkd3d-proton FL12_0 blocked");
   soft(cons >= 1, "proton: VK_EXT_conservative_rasterization", "vkd3d-proton FL12_1 blocked");
   soft(rov, "proton: ROVs (pixel+sample interlock)", "vkd3d-proton FL12_1 blocked");
   int proton_fl = 110;
   if (f->logicOp && f->vertexPipelineStoresAndAtomics && lim->maxPerStageDescriptorStorageBuffers >= 64 &&
       lim->maxPerStageDescriptorStorageImages >= 64) {
      proton_fl = 111;
      if (tiled >= 2 && proton_uav_ok) { /* binding tier is always 3 */
         proton_fl = 120;
         if (rov && cons >= 1)
            proton_fl = 121; /* 12_2 needs RT 1.1, mesh, VRS 2, sampler feedback: not reported */
      }
   }

   /* Shader model (device.c:9736-9940). */
   const char *sm = "5_1";
   int sm60 = wave_ops && (f12.scalarBlockLayout || f12.uniformBufferStandardLayout) && f->shaderInt16;
   int denorm = p12.denormBehaviorIndependence != VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE &&
                p12.shaderDenormFlushToZeroFloat32 && p12.shaderDenormPreserveFloat32;
   int sm66 = csd.computeDerivativeGroupLinear && f12.shaderBufferInt64Atomics && f12.shaderInt8 &&
              (p13.minSubgroupSize == p13.maxSubgroupSize ||
               (p13.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT));
   int sm67 = mrc.shaderMaximalReconvergence && qc.shaderQuadControl;
   soft(sm60, "proton: SM6.0 (wave ops + scalar/std UBO layout + shaderInt16)", "vkd3d-proton SM5.1 only (no DXIL)");
   soft(denorm, "proton: SM6.2 (denorm independence + FP32 FTZ/preserve)", "vkd3d-proton SM6.0 max");
   soft(sm66, "proton: SM6.6 (compute derivatives linear + int64 buffer atomics + int8 + fixed wave)", "vkd3d-proton SM6.5 max");
   soft(sm67, "proton: SM6.7 (maximal reconvergence + quad control)", "vkd3d-proton SM6.6 max");
   if (sm60) {
      sm = "6_0";
      if (denorm) {
         sm = "6_5";
         if (sm66) {
            sm = "6_6";
            if (sm67)
               sm = "6_8";
         }
      }
   }
   soft(has_ext("VK_EXT_mutable_descriptor_type") && mut.mutableDescriptorType, "proton: VK_EXT_mutable_descriptor_type",
        "separate per-type descriptor sets (more memory)");
   soft(db_path, "proton: VK_EXT_descriptor_buffer path", "descriptor-set heap path (slower copies)");
   soft(f->depthBounds, "proton: depthBounds", "DepthBoundsTestSupported false");
   soft(f12.shaderOutputViewportIndex && f12.shaderOutputLayer, "proton: shaderOutputViewportIndex+Layer",
        "VPAndRTArrayIndex from VS unsupported (FL12_2 gate)");
   soft(fsr.pipelineFragmentShadingRate, "proton: VK_KHR_fragment_shading_rate", "VRS tier 0");
   soft(mesh.meshShader && mesh.taskShader, "proton: VK_EXT_mesh_shader", "mesh shader tier 0 (FL12_2 gate)");
   soft(rq.rayQuery, "proton: VK_KHR_ray_query + acceleration_structure", "DXR tier 0 (FL12_2 gate)");
   soft(f->shaderInt64 && ia64.shaderImageInt64Atomics, "proton: shaderImageInt64Atomics (sampler feedback 0.9)",
        "sampler feedback tier 0");

   printf("INFO proton_device_create %s (%d hard gaps)\n", proton_fail ? "fails" : "ok", proton_fail);
   printf("INFO proton_feature_level %s%s\n", fl_name(proton_fl), proton_fail ? " (if device creation succeeded)" : "");
   printf("INFO proton_shader_model %s\n", sm);
   printf("INFO proton_binding_tier 3\n");
   printf("INFO proton_tiled_tier %d conservative_tier %d\n", tiled, cons);
   printf("INFO proton_descriptor_model %s\n",
          db_path ? "descriptor_buffer" : (mut.mutableDescriptorType ? "mutable_sets" : "typed_sets"));
   printf("INFO hard %d/%d available, soft %d/%d available\n", hard_total - hard_missing, hard_total,
          soft_total - soft_missing, soft_total);
   p_DestroyInstance(inst, NULL);
   free(exts);
   printf("RESULT %s\n", hard_missing ? "FAIL" : "PASS");
   return hard_missing ? 1 : 0;
}

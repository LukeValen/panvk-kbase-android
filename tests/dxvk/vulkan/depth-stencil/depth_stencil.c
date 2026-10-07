/* Standalone Vulkan test program for depth and stencil operations.
 *
 * Covers:
 *  0) <tag>_viewport_zero_depth_range: minDepth == maxDepth == 0.5 must give
 *     a flat 0.5 (D3D MinZ == MaxZ).
 *  1) <tag>_depth: depth gradient stored with Draw A, conditional Draw B
 *     overwrites pixels with LESS compare.
 *  2) <tag>_stencil_ops: multi-draw pipeline sequence testing stencil ops
 *     (REPLACE, INVERT, INCREMENT_AND_CLAMP, INCREMENT_AND_WRAP, ZERO,
 *      DECREMENT_AND_WRAP, compare masks, write masks).
 *  3) <tag>_depth_bias_clamp: dynamic and static depth bias and clamping
 *     against nominal r.
 *  4) <tag>_depth_bias_control: VK_EXT_depth_bias_control representation
 *     and exact bias testing.
 *
 * Formats: D24S8, D32S8, D16S8.
 * Target: 32x32.
 * Usage: depth_stencil <libvulkan_panfrost.so>
 * SPIR-V regen: ./build_spv.sh
 */
#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "depth_stencil_spv.h"

#define W 32u
#define H 32u

typedef PFN_vkVoidFunction (*icd_gipa_fn)(VkInstance, const char *);

#define CK(expr, what)                                                         \
   do {                                                                        \
      VkResult _r = (expr);                                                    \
      if (_r != VK_SUCCESS) {                                                  \
         printf("FAIL %s r=%d line=%d\n", what, (int)_r, __LINE__);            \
         return 1;                                                             \
      }                                                                        \
   } while (0)

#define DEV_FNS(X)                                                             \
   X(CreateBuffer) X(GetBufferMemoryRequirements) X(AllocateMemory)            \
   X(BindBufferMemory) X(MapMemory) X(CreateImage)                             \
   X(GetImageMemoryRequirements) X(BindImageMemory) X(CreateImageView)         \
   X(CreateRenderPass) X(CreateFramebuffer) X(CreateShaderModule)              \
   X(CreatePipelineLayout) X(CreateGraphicsPipelines) X(CreateCommandPool)     \
   X(AllocateCommandBuffers) X(BeginCommandBuffer) X(EndCommandBuffer)         \
   X(CmdBeginRenderPass) X(CmdEndRenderPass) X(CmdBindPipeline)                \
   X(CmdSetScissor) X(CmdSetDepthBias) X(CmdDraw) X(CmdCopyImageToBuffer)      \
   X(CmdPipelineBarrier)                                                       \
   X(CreateFence) X(WaitForFences) X(ResetFences) X(QueueSubmit)               \
   X(ResetCommandBuffer) X(GetDeviceQueue)                                     \
   X(DestroyPipeline) X(DestroyShaderModule) X(DestroyPipelineLayout)          \
   X(DestroyRenderPass) X(DestroyFramebuffer) X(DestroyImageView)              \
   X(DestroyImage) X(DestroyBuffer) X(FreeMemory)                              \
   X(DestroyFence) X(DestroyCommandPool) X(DestroyDevice)

#define DECL(n) static PFN_vk##n p_##n;
DEV_FNS(DECL)
#undef DECL

static PFN_vkCmdSetDepthBias2EXT p_CmdSetDepthBias2EXT;

static VkDevice dev;
static VkQueue queue;
static VkPhysicalDeviceMemoryProperties mp;
static VkPipelineLayout pl;
static VkShaderModule sm_vs, sm_vs_flat, sm_fs;
static VkCommandBuffer cmd;
static VkFence fence;

static VkBuffer rb_depth, rb_stencil, rb_color;
static VkDeviceMemory rb_depth_mem, rb_stencil_mem, rb_color_mem;
static void *dmap, *smap, *cmap;

struct format_info {
   VkFormat format;
   const char *tag;
   uint32_t n;
   bool is_float;
   bool usable;
};

struct pipe_params {
   VkBool32 depthTestEnable;
   VkBool32 depthWriteEnable;
   VkCompareOp depthCompareOp;

   VkBool32 stencilTestEnable;
   VkStencilOpState stencil;

   VkColorComponentFlags colorWriteMask;

   float minDepth;
   float maxDepth;

   VkBool32 depthBiasEnable;
   float depthBiasConstantFactor;
   float depthBiasClamp;
   float depthBiasSlopeFactor;

   VkBool32 dynamicDepthBias;

   /* Use ds_flat.vert (constant z = 0.5) instead of the x depth gradient. */
   bool flat_z;
};

/* Pick a memory type matching bits and required flags. */
static int
pick_type(uint32_t bits, VkMemoryPropertyFlags want, uint32_t *out)
{
   for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
      if ((bits & (1u << i)) &&
          (mp.memoryTypes[i].propertyFlags & want) == want) {
         *out = i;
         return 0;
      }
   }
   for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
      if (bits & (1u << i)) {
         *out = i;
         return 0;
      }
   }
   printf("FAIL no memory type bits=0x%x\n", bits);
   return 1;
}

static int
alloc_bind(const VkMemoryRequirements *mr, VkMemoryPropertyFlags want,
           VkDeviceMemory *m, uint32_t *ti)
{
   if (pick_type(mr->memoryTypeBits, want, ti))
      return 1;
   VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                               .allocationSize = mr->size,
                               .memoryTypeIndex = *ti};
   CK(p_AllocateMemory(dev, &mai, NULL, m), "AllocateMemory");
   return 0;
}

static int
make_buf(VkDeviceSize sz, VkBufferUsageFlags usage, VkBuffer *b,
         VkDeviceMemory *m, void **map)
{
   VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                            .size = sz,
                            .usage = usage,
                            .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
   CK(p_CreateBuffer(dev, &bi, NULL, b), "CreateBuffer");
   VkMemoryRequirements mr;
   p_GetBufferMemoryRequirements(dev, *b, &mr);
   uint32_t ti;
   if (alloc_bind(&mr, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  m, &ti))
      return 1;
   CK(p_BindBufferMemory(dev, *b, *m, 0), "BindBufferMemory");
   CK(p_MapMemory(dev, *m, 0, sz, 0, map), "MapMemory");
   return 0;
}

static int
make_image(VkFormat fmt, VkSampleCountFlagBits samples, VkImageUsageFlags usage,
           VkImageAspectFlags aspect, VkImage *img, VkDeviceMemory *mem,
           VkImageView *view)
{
   VkImageCreateInfo ii = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = fmt,
      .extent = {W, H, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = samples,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
   CK(p_CreateImage(dev, &ii, NULL, img), "CreateImage");
   VkMemoryRequirements mr;
   p_GetImageMemoryRequirements(dev, *img, &mr);
   uint32_t ti;
   if (alloc_bind(&mr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mem, &ti))
      return 1;
   CK(p_BindImageMemory(dev, *img, *mem, 0), "BindImageMemory");
   VkImageViewCreateInfo vi = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = *img,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = fmt,
      .subresourceRange = {aspect, 0, 1, 0, 1}};
   CK(p_CreateImageView(dev, &vi, NULL, view), "CreateImageView");
   return 0;
}

/* Copies depth, stencil, and color aspects to their respective host buffers. */
static void
record_copy_all(VkCommandBuffer c, VkImage depth_img, VkImage color_img)
{
   VkBufferImageCopy bc_depth = {
      .imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1},
      .imageExtent = {W, H, 1}};
   p_CmdCopyImageToBuffer(c, depth_img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          rb_depth, 1, &bc_depth);

   VkBufferImageCopy bc_stencil = {
      .imageSubresource = {VK_IMAGE_ASPECT_STENCIL_BIT, 0, 0, 1},
      .imageExtent = {W, H, 1}};
   p_CmdCopyImageToBuffer(c, depth_img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          rb_stencil, 1, &bc_stencil);

   VkBufferImageCopy bc_color = {
      .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
      .imageExtent = {W, H, 1}};
   p_CmdCopyImageToBuffer(c, color_img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          rb_color, 1, &bc_color);

   VkMemoryBarrier mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                         .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                         .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
   p_CmdPipelineBarrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
}

static int
submit_and_wait(void)
{
   CK(p_EndCommandBuffer(cmd), "EndCommandBuffer");
   CK(p_ResetFences(dev, 1, &fence), "ResetFences");
   VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                      .commandBufferCount = 1,
                      .pCommandBuffers = &cmd};
   CK(p_QueueSubmit(queue, 1, &si, fence), "QueueSubmit");
   VkResult wr = VK_TIMEOUT;
   for (int k = 0; k < 40 && wr == VK_TIMEOUT; k++)
      wr = p_WaitForFences(dev, 1, &fence, VK_TRUE, 500000000ull);
   if (wr != VK_SUCCESS) {
      printf("FAIL fence wait r=%d\n", (int)wr);
      return 1;
   }
   return 0;
}

static int
make_pipe(const struct pipe_params *p, VkRenderPass r, VkPipeline *out)
{
   VkPipelineShaderStageCreateInfo st[2] = {
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_VERTEX_BIT,
       .module = p->flat_z ? sm_vs_flat : sm_vs,
       .pName = "main"},
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
       .module = sm_fs,
       .pName = "main"}};
   VkPipelineVertexInputStateCreateInfo vis = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
   VkPipelineInputAssemblyStateCreateInfo ias = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP};
   VkViewport vp = {0.0f, 0.0f, (float)W, (float)H, p->minDepth, p->maxDepth};
   VkRect2D sc = {{0, 0}, {W, H}};
   VkPipelineViewportStateCreateInfo vs = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .pViewports = &vp,
      .scissorCount = 1,
      .pScissors = &sc};
   VkPipelineRasterizationStateCreateInfo rs = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_NONE,
      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .lineWidth = 1.0f,
      .depthBiasEnable = p->depthBiasEnable,
      .depthBiasConstantFactor = p->depthBiasConstantFactor,
      .depthBiasClamp = p->depthBiasClamp,
      .depthBiasSlopeFactor = p->depthBiasSlopeFactor};
   VkPipelineMultisampleStateCreateInfo ms = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
   VkPipelineDepthStencilStateCreateInfo ds = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = p->depthTestEnable,
      .depthWriteEnable = p->depthWriteEnable,
      .depthCompareOp = p->depthCompareOp,
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable = p->stencilTestEnable,
      .front = p->stencil,
      .back = p->stencil,
      .minDepthBounds = 0.0f,
      .maxDepthBounds = 1.0f};
   VkPipelineColorBlendAttachmentState cba = {
      .colorWriteMask = p->colorWriteMask};
   VkPipelineColorBlendStateCreateInfo cbs = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &cba};
   VkDynamicState dyn[4];
   uint32_t nd = 0;
   dyn[nd++] = VK_DYNAMIC_STATE_SCISSOR;
   if (p->dynamicDepthBias)
      dyn[nd++] = VK_DYNAMIC_STATE_DEPTH_BIAS;
   VkPipelineDynamicStateCreateInfo dsi = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = nd,
      .pDynamicStates = dyn};
   VkGraphicsPipelineCreateInfo gpi = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount = 2,
      .pStages = st,
      .pVertexInputState = &vis,
      .pInputAssemblyState = &ias,
      .pViewportState = &vs,
      .pRasterizationState = &rs,
      .pMultisampleState = &ms,
      .pDepthStencilState = &ds,
      .pColorBlendState = &cbs,
      .pDynamicState = &dsi,
      .layout = pl,
      .renderPass = r,
      .subpass = 0};
   CK(p_CreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpi, NULL, out),
      "CreateGraphicsPipelines");
   return 0;
}

/* Decodes normalized or float depth from the readback buffer. */
static double
decode_depth(const void *depth_data, VkFormat fmt, uint32_t x, uint32_t y)
{
   uint32_t idx = y * W + x;
   if (fmt == VK_FORMAT_D24_UNORM_S8_UINT) {
      const uint32_t *p = (const uint32_t *)depth_data;
      return (double)(p[idx] & 0xFFFFFFu) / 16777215.0;
   } else if (fmt == VK_FORMAT_D32_SFLOAT_S8_UINT) {
      const float *p = (const float *)depth_data;
      return (double)p[idx];
   } else if (fmt == VK_FORMAT_D16_UNORM_S8_UINT) {
      const uint16_t *p = (const uint16_t *)depth_data;
      return (double)p[idx] / 65535.0;
   }
   return 0.0;
}

static double
get_tolerance(const struct format_info *f)
{
   if (f->is_float)
      return 1e-6;
   return 3.0 / ((double)((1u << f->n) - 1u));
}

static double
get_r_nominal(const struct format_info *f)
{
   if (f->is_float)
      return ldexp(1.0, -24);
   return ldexp(1.0, -(int)f->n);
}

/* Subcase 1: <tag>_depth */
static int
test_depth(const struct format_info *f, VkRenderPass rp, VkFramebuffer fb,
           VkImage depth_img, VkImage color_img, uint32_t *passes,
           uint32_t *fails)
{
   struct pipe_params pa = {
      .depthTestEnable = VK_TRUE,
      .depthWriteEnable = VK_TRUE,
      .depthCompareOp = VK_COMPARE_OP_ALWAYS,
      .stencilTestEnable = VK_FALSE,
      .colorWriteMask = 0,
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
   };
   struct pipe_params pb = {
      .depthTestEnable = VK_TRUE,
      .depthWriteEnable = VK_TRUE,
      .depthCompareOp = VK_COMPARE_OP_LESS,
      .stencilTestEnable = VK_FALSE,
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
      .flat_z = true,
   };
   VkPipeline pipeA, pipeB;
   if (make_pipe(&pa, rp, &pipeA) || make_pipe(&pb, rp, &pipeB))
      return 1;

   CK(p_ResetCommandBuffer(cmd, 0), "ResetCommandBuffer");
   VkCommandBufferBeginInfo bi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
   CK(p_BeginCommandBuffer(cmd, &bi), "BeginCommandBuffer");

   VkClearValue cv[2];
   memset(cv, 0, sizeof(cv));
   cv[1].depthStencil.depth = 1.0f;
   cv[1].depthStencil.stencil = 0x10;
   VkRenderPassBeginInfo rbi = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = rp,
      .framebuffer = fb,
      .renderArea = {{0, 0}, {W, H}},
      .clearValueCount = 2,
      .pClearValues = cv};
   p_CmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);

   VkRect2D sc_full = {{0, 0}, {W, H}};
   p_CmdSetScissor(cmd, 0, 1, &sc_full);

   p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeA);
   p_CmdDraw(cmd, 4, 1, 0, 0);

   p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeB);
   p_CmdDraw(cmd, 4, 1, 0, 0);

   p_CmdEndRenderPass(cmd);
   record_copy_all(cmd, depth_img, color_img);

   if (submit_and_wait()) {
      p_DestroyPipeline(dev, pipeA, NULL);
      p_DestroyPipeline(dev, pipeB, NULL);
      return 1;
   }

   p_DestroyPipeline(dev, pipeA, NULL);
   p_DestroyPipeline(dev, pipeB, NULL);

   double tol = get_tolerance(f);
   uint32_t bad_depth = 0;
   uint32_t bad_color = 0;
   const uint8_t *cbuf = (const uint8_t *)cmap;

   for (uint32_t y = 0; y < H; y++) {
      for (uint32_t x = 0; x < W; x++) {
         double exp_d = (x + 0.5) / 32.0;
         if (exp_d > 0.5)
            exp_d = 0.5;
         double act_d = decode_depth(dmap, f->format, x, y);
         if (fabs(act_d - exp_d) > tol) {
            if (bad_depth < 4)
               printf("BAD %s_depth x=%u y=%u got=%.9f want=%.9f\n", f->tag, x,
                      y, act_d, exp_d);
            bad_depth++;
         }

         const uint8_t *c = cbuf + (y * W + x) * 4;
         bool exp_green = (x >= 16);
         bool is_green =
            (c[0] == 0 && c[1] == 255 && c[2] == 0 && c[3] == 255);
         bool is_black =
            (c[0] == 0 && c[1] == 0 && c[2] == 0 && c[3] == 0);
         if (exp_green ? !is_green : !is_black)
            bad_color++;
      }
   }

   bool ok = (bad_depth == 0 && bad_color == 0);
   printf("%s case %s_depth badDepth=%u badColor=%u\n",
          ok ? "PASS" : "FAIL", f->tag, bad_depth, bad_color);
   if (ok)
      (*passes)++;
   else
      (*fails)++;
   return 0;
}

/* Subcase 2: <tag>_stencil_ops */
static int
test_stencil_ops(const struct format_info *f, VkRenderPass rp, VkFramebuffer fb,
                 VkImage depth_img, VkImage color_img, uint32_t *passes,
                 uint32_t *fails)
{
   VkStencilOpState base_st = {
      .failOp = VK_STENCIL_OP_KEEP,
      .passOp = VK_STENCIL_OP_KEEP,
      .depthFailOp = VK_STENCIL_OP_KEEP,
      .compareOp = VK_COMPARE_OP_ALWAYS,
      .compareMask = 0xFF,
      .writeMask = 0xFF,
      .reference = 0,
   };

   /* P1: scissor x<16, ALWAYS, ref 0x55, passOp REPLACE */
   struct pipe_params pp1 = {
      .stencilTestEnable = VK_TRUE,
      .stencil = base_st,
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
   };
   pp1.stencil.passOp = VK_STENCIL_OP_REPLACE;
   pp1.stencil.reference = 0x55;

   /* P2: scissor full, EQUAL ref 0x55, passOp INCREMENT_AND_CLAMP, failOp INVERT */
   struct pipe_params pp2 = {
      .stencilTestEnable = VK_TRUE,
      .stencil = base_st,
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
   };
   pp2.stencil.compareOp = VK_COMPARE_OP_EQUAL;
   pp2.stencil.passOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
   pp2.stencil.failOp = VK_STENCIL_OP_INVERT;
   pp2.stencil.reference = 0x55;

   /* P3: stencil ALWAYS, depth test on with compare NEVER, depthFailOp INCREMENT_AND_WRAP */
   struct pipe_params pp3 = {
      .depthTestEnable = VK_TRUE,
      .depthWriteEnable = VK_FALSE,
      .depthCompareOp = VK_COMPARE_OP_NEVER,
      .stencilTestEnable = VK_TRUE,
      .stencil = base_st,
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
   };
   pp3.stencil.depthFailOp = VK_STENCIL_OP_INCREMENT_AND_WRAP;

   /* P4: ALWAYS, passOp ZERO, writeMask 0x0F */
   struct pipe_params pp4 = {
      .stencilTestEnable = VK_TRUE,
      .stencil = base_st,
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
   };
   pp4.stencil.passOp = VK_STENCIL_OP_ZERO;
   pp4.stencil.writeMask = 0x0F;

   /* P5: EQUAL ref 0x50 compareMask 0xF0, color writes on (green) */
   struct pipe_params pp5 = {
      .stencilTestEnable = VK_TRUE,
      .stencil = base_st,
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
   };
   pp5.stencil.compareOp = VK_COMPARE_OP_EQUAL;
   pp5.stencil.compareMask = 0xF0;
   pp5.stencil.reference = 0x50;

   /* P6: NOT_EQUAL ref 0x50 compareMask 0xFF, passOp DECREMENT_AND_WRAP */
   struct pipe_params pp6 = {
      .stencilTestEnable = VK_TRUE,
      .stencil = base_st,
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
   };
   pp6.stencil.compareOp = VK_COMPARE_OP_NOT_EQUAL;
   pp6.stencil.passOp = VK_STENCIL_OP_DECREMENT_AND_WRAP;
   pp6.stencil.reference = 0x50;

   VkPipeline p1, p2, p3, p4, p5, p6;
   if (make_pipe(&pp1, rp, &p1) || make_pipe(&pp2, rp, &p2) ||
       make_pipe(&pp3, rp, &p3) || make_pipe(&pp4, rp, &p4) ||
       make_pipe(&pp5, rp, &p5) || make_pipe(&pp6, rp, &p6))
      return 1;

   CK(p_ResetCommandBuffer(cmd, 0), "ResetCommandBuffer");
   VkCommandBufferBeginInfo bi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
   CK(p_BeginCommandBuffer(cmd, &bi), "BeginCommandBuffer");

   VkClearValue cv[2];
   memset(cv, 0, sizeof(cv));
   cv[1].depthStencil.depth = 1.0f;
   cv[1].depthStencil.stencil = 0x10;
   VkRenderPassBeginInfo rbi = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = rp,
      .framebuffer = fb,
      .renderArea = {{0, 0}, {W, H}},
      .clearValueCount = 2,
      .pClearValues = cv};
   p_CmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);

   VkRect2D sc_half = {{0, 0}, {16, 32}};
   VkRect2D sc_full = {{0, 0}, {32, 32}};

   p_CmdSetScissor(cmd, 0, 1, &sc_half);
   p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p1);
   p_CmdDraw(cmd, 4, 1, 0, 0);

   p_CmdSetScissor(cmd, 0, 1, &sc_full);
   p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p2);
   p_CmdDraw(cmd, 4, 1, 0, 0);

   p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p3);
   p_CmdDraw(cmd, 4, 1, 0, 0);

   p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p4);
   p_CmdDraw(cmd, 4, 1, 0, 0);

   p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p5);
   p_CmdDraw(cmd, 4, 1, 0, 0);

   p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p6);
   p_CmdDraw(cmd, 4, 1, 0, 0);

   p_CmdEndRenderPass(cmd);
   record_copy_all(cmd, depth_img, color_img);

   if (submit_and_wait()) {
      p_DestroyPipeline(dev, p1, NULL);
      p_DestroyPipeline(dev, p2, NULL);
      p_DestroyPipeline(dev, p3, NULL);
      p_DestroyPipeline(dev, p4, NULL);
      p_DestroyPipeline(dev, p5, NULL);
      p_DestroyPipeline(dev, p6, NULL);
      return 1;
   }

   p_DestroyPipeline(dev, p1, NULL);
   p_DestroyPipeline(dev, p2, NULL);
   p_DestroyPipeline(dev, p3, NULL);
   p_DestroyPipeline(dev, p4, NULL);
   p_DestroyPipeline(dev, p5, NULL);
   p_DestroyPipeline(dev, p6, NULL);

   uint32_t bad_stencil = 0;
   uint32_t bad_color = 0;
   const uint8_t *sbuf = (const uint8_t *)smap;
   const uint8_t *cbuf = (const uint8_t *)cmap;

   for (uint32_t y = 0; y < H; y++) {
      for (uint32_t x = 0; x < W; x++) {
         uint8_t act_s = sbuf[y * W + x];
         uint8_t exp_s = (x < 16) ? 0x50 : 0xEF;
         if (act_s != exp_s)
            bad_stencil++;

         const uint8_t *c = cbuf + (y * W + x) * 4;
         bool exp_green = (x < 16);
         bool is_green =
            (c[0] == 0 && c[1] == 255 && c[2] == 0 && c[3] == 255);
         bool is_black =
            (c[0] == 0 && c[1] == 0 && c[2] == 0 && c[3] == 0);
         if (exp_green ? !is_green : !is_black)
            bad_color++;
      }
   }

   bool ok = (bad_stencil == 0 && bad_color == 0);
   printf("%s case %s_stencil_ops badStencil=%u badColor=%u\n",
          ok ? "PASS" : "FAIL", f->tag, bad_stencil, bad_color);
   if (ok)
      (*passes)++;
   else
      (*fails)++;
   return 0;
}

/* Runs a single render pass with specified depth bias state and reads back. */
static int
run_depth_bias_single(VkRenderPass rp, VkFramebuffer fb, VkImage depth_img,
                      VkImage color_img, VkPipeline pipe, bool is_dyn,
                      float const_factor, float clamp, float slope,
                      const VkDepthBiasInfoEXT *ext_info, double *out_b,
                      double tol, VkFormat fmt)
{
   CK(p_ResetCommandBuffer(cmd, 0), "ResetCommandBuffer");
   VkCommandBufferBeginInfo bi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
   CK(p_BeginCommandBuffer(cmd, &bi), "BeginCommandBuffer");

   VkClearValue cv[2];
   memset(cv, 0, sizeof(cv));
   cv[1].depthStencil.depth = 1.0f;
   cv[1].depthStencil.stencil = 0x10;
   VkRenderPassBeginInfo rbi = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = rp,
      .framebuffer = fb,
      .renderArea = {{0, 0}, {W, H}},
      .clearValueCount = 2,
      .pClearValues = cv};
   p_CmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);

   VkRect2D sc = {{0, 0}, {W, H}};
   p_CmdSetScissor(cmd, 0, 1, &sc);
   p_CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);

   if (ext_info)
      p_CmdSetDepthBias2EXT(cmd, ext_info);
   else if (is_dyn)
      p_CmdSetDepthBias(cmd, const_factor, clamp, slope);

   p_CmdDraw(cmd, 4, 1, 0, 0);
   p_CmdEndRenderPass(cmd);

   record_copy_all(cmd, depth_img, color_img);

   if (submit_and_wait())
      return 1;

   double d0 = decode_depth(dmap, fmt, 0, 0);
   *out_b = d0 - 0.5;

   double lo = d0, hi = d0;
   for (uint32_t y = 0; y < H; y++) {
      for (uint32_t x = 0; x < W; x++) {
         double d = decode_depth(dmap, fmt, x, y);
         lo = fmin(lo, d);
         hi = fmax(hi, d);
      }
   }
   if (hi - lo > tol) {
      /* Flat z (or a zero viewport depth range): depth must be uniform. */
      printf("BAD flat depth non-uniform min=%.9f max=%.9f x0=%.9f x31=%.9f\n",
             lo, hi, d0, decode_depth(dmap, fmt, W - 1, 0));
      return 2;
   }
   return 0;
}

/* Subcase 3: <tag>_depth_bias_clamp */
static int
test_depth_bias_clamp(const struct format_info *f, VkRenderPass rp,
                      VkFramebuffer fb, VkImage depth_img, VkImage color_img,
                      VkPipeline dyn_pipe, bool has_clamp, uint32_t *passes,
                      uint32_t *fails, uint32_t *skips)
{
   if (!has_clamp) {
      printf("SKIP case %s_depth_bias_clamp depthBiasClamp unsupported\n",
             f->tag);
      (*skips)++;
      return 0;
   }

   double tol = get_tolerance(f);
   double r_nominal = get_r_nominal(f);

   struct pipe_params pe = {
      .depthTestEnable = VK_TRUE,
      .depthWriteEnable = VK_TRUE,
      .depthCompareOp = VK_COMPARE_OP_ALWAYS,
      .stencilTestEnable = VK_FALSE,
      .colorWriteMask = 0,
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
      .flat_z = true,
      .depthBiasEnable = VK_TRUE,
      .depthBiasConstantFactor = 256.0f,
      .depthBiasClamp = (float)(128.0 * r_nominal),
      .depthBiasSlopeFactor = 0.0f,
      .dynamicDepthBias = VK_FALSE,
   };
   VkPipeline static_pipe;
   if (make_pipe(&pe, rp, &static_pipe))
      return 1;

   double ba = 0.0, bb = 0.0, bc = 0.0, bd = 0.0, be = 0.0;

   /* Run a: dynamic, bias 0, clamp 0, slope 0 */
   int ra = run_depth_bias_single(rp, fb, depth_img, color_img, dyn_pipe, true,
                                  0.0f, 0.0f, 0.0f, NULL, &ba, tol, f->format);
   bool ok_a = (ra == 0) && (fabs(ba) <= tol);

   /* Run b: dynamic, bias 256, clamp 0, slope 0 */
   int rb = run_depth_bias_single(rp, fb, depth_img, color_img, dyn_pipe, true,
                                  256.0f, 0.0f, 0.0f, NULL, &bb, tol, f->format);
   bool ok_b = (rb == 0);
   if (f->is_float) {
      ok_b = ok_b && (fabs(bb - 256.0 * r_nominal) <= tol + 2.0 * r_nominal);
   } else {
      ok_b = ok_b && (bb >= 256.0 * r_nominal * 0.98 - tol &&
                      bb <= 512.0 * r_nominal * 1.02 + tol);
   }

   /* Run c: dynamic, bias 256, clamp 128*r_nominal, slope 0 */
   int rc = run_depth_bias_single(rp, fb, depth_img, color_img, dyn_pipe, true,
                                  256.0f, (float)(128.0 * r_nominal), 0.0f,
                                  NULL, &bc, tol, f->format);
   bool ok_c = (rc == 0) && (fabs(bc - 128.0 * r_nominal) <= tol);

   /* Run d: dynamic, bias -256, clamp -128*r_nominal, slope 0 */
   int rd = run_depth_bias_single(rp, fb, depth_img, color_img, dyn_pipe, true,
                                  -256.0f, (float)(-128.0 * r_nominal), 0.0f,
                                  NULL, &bd, tol, f->format);
   bool ok_d = (rd == 0) && (fabs(bd + 128.0 * r_nominal) <= tol);

   /* Run e: static pipeline with constant 256, clamp 128*r_nominal */
   int re = run_depth_bias_single(rp, fb, depth_img, color_img, static_pipe, false,
                                  0.0f, 0.0f, 0.0f, NULL, &be, tol, f->format);
   bool ok_e = (re == 0) && (fabs(be - 128.0 * r_nominal) <= tol);

   p_DestroyPipeline(dev, static_pipe, NULL);

   bool ok = ok_a && ok_b && ok_c && ok_d && ok_e;
   printf("%s case %s_depth_bias_clamp a=%.2f b=%.2f c=%.2f d=%.2f e=%.2f\n",
          ok ? "PASS" : "FAIL", f->tag,
          ba / r_nominal, bb / r_nominal, bc / r_nominal,
          bd / r_nominal, be / r_nominal);
   if (ok)
      (*passes)++;
   else
      (*fails)++;
   return 0;
}

/* Subcase 4: <tag>_depth_bias_control */
static int
test_depth_bias_control(const struct format_info *f, VkRenderPass rp,
                        VkFramebuffer fb, VkImage depth_img, VkImage color_img,
                        VkPipeline dyn_pipe, bool has_ext,
                        const VkPhysicalDeviceDepthBiasControlFeaturesEXT *dbc,
                        uint32_t *passes, uint32_t *fails, uint32_t *skips)
{
   if (!has_ext) {
      printf("SKIP case %s_depth_bias_control extension unsupported\n", f->tag);
      (*skips)++;
      return 0;
   }
   if (!dbc->depthBiasControl) {
      printf("SKIP case %s_depth_bias_control depthBiasControl unsupported\n",
             f->tag);
      (*skips)++;
      return 0;
   }
   if (!p_CmdSetDepthBias2EXT) {
      printf("SKIP case %s_depth_bias_control vkCmdSetDepthBias2EXT NULL\n",
             f->tag);
      (*skips)++;
      return 0;
   }

   double tol = get_tolerance(f);
   double r_nominal = get_r_nominal(f);

   /* Run a: LEAST_REPRESENTABLE_VALUE_FORMAT */
   VkDepthBiasRepresentationInfoEXT rep_a = {
      .sType = VK_STRUCTURE_TYPE_DEPTH_BIAS_REPRESENTATION_INFO_EXT,
      .depthBiasRepresentation =
         VK_DEPTH_BIAS_REPRESENTATION_LEAST_REPRESENTABLE_VALUE_FORMAT_EXT,
      .depthBiasExact = dbc->depthBiasExact,
   };
   VkDepthBiasInfoEXT info_a = {
      .sType = VK_STRUCTURE_TYPE_DEPTH_BIAS_INFO_EXT,
      .pNext = &rep_a,
      .depthBiasConstantFactor = 256.0f,
      .depthBiasClamp = 0.0f,
      .depthBiasSlopeFactor = 0.0f,
   };
   double ba = 0.0;
   int ra = run_depth_bias_single(rp, fb, depth_img, color_img, dyn_pipe, true,
                                  0.0f, 0.0f, 0.0f, &info_a, &ba, tol, f->format);
   bool ok_a = (ra == 0);
   if (dbc->depthBiasExact) {
      ok_a = ok_a && (fabs(ba - 256.0 * r_nominal) <= tol);
   } else {
      if (f->is_float) {
         ok_a = ok_a && (fabs(ba - 256.0 * r_nominal) <= tol + 2.0 * r_nominal);
      } else {
         ok_a = ok_a && (ba >= 256.0 * r_nominal * 0.98 - tol &&
                         ba <= 512.0 * r_nominal * 1.02 + tol);
      }
   }

   /* Run b: FORCE_UNORM (if supported) */
   double bb = 0.0;
   bool ran_b = dbc->leastRepresentableValueForceUnormRepresentation != 0;
   bool ok_b = true;
   if (ran_b) {
      VkDepthBiasRepresentationInfoEXT rep_b = {
         .sType = VK_STRUCTURE_TYPE_DEPTH_BIAS_REPRESENTATION_INFO_EXT,
         .depthBiasRepresentation =
            VK_DEPTH_BIAS_REPRESENTATION_LEAST_REPRESENTABLE_VALUE_FORCE_UNORM_EXT,
         .depthBiasExact = dbc->depthBiasExact,
      };
      VkDepthBiasInfoEXT info_b = {
         .sType = VK_STRUCTURE_TYPE_DEPTH_BIAS_INFO_EXT,
         .pNext = &rep_b,
         .depthBiasConstantFactor = 256.0f,
         .depthBiasClamp = 0.0f,
         .depthBiasSlopeFactor = 0.0f,
      };
      int rb = run_depth_bias_single(rp, fb, depth_img, color_img, dyn_pipe,
                                     true, 0.0f, 0.0f, 0.0f, &info_b, &bb, tol,
                                     f->format);
      ok_b = (rb == 0);
      if (!f->is_float) {
         if (dbc->depthBiasExact) {
            ok_b = ok_b && (fabs(bb - 256.0 * r_nominal) <= tol);
         } else {
            ok_b = ok_b && (bb >= 256.0 * r_nominal * 0.98 - tol &&
                            bb <= 512.0 * r_nominal * 1.02 + tol);
         }
      } else {
         double r24 = ldexp(1.0, -24);
         if (dbc->depthBiasExact) {
            ok_b = ok_b && (fabs(bb - 256.0 * r24) <= tol);
         } else {
            /* Without depthBiasExact r may be up to 2 * 2^-24. */
            ok_b = ok_b && (bb >= 256.0 * r24 * 0.98 - tol &&
                            bb <= 512.0 * r24 * 1.02 + tol);
         }
      }
   }

   /* Run c: FLOAT (if supported) */
   double bc = 0.0;
   bool ran_c = dbc->floatRepresentation != 0;
   bool ok_c = true;
   if (ran_c) {
      VkDepthBiasRepresentationInfoEXT rep_c = {
         .sType = VK_STRUCTURE_TYPE_DEPTH_BIAS_REPRESENTATION_INFO_EXT,
         .depthBiasRepresentation =
            VK_DEPTH_BIAS_REPRESENTATION_FLOAT_EXT,
         .depthBiasExact = dbc->depthBiasExact,
      };
      VkDepthBiasInfoEXT info_c = {
         .sType = VK_STRUCTURE_TYPE_DEPTH_BIAS_INFO_EXT,
         .pNext = &rep_c,
         .depthBiasConstantFactor = 0.0625f,
         .depthBiasClamp = 0.0f,
         .depthBiasSlopeFactor = 0.0f,
      };
      int rc = run_depth_bias_single(rp, fb, depth_img, color_img, dyn_pipe,
                                     true, 0.0f, 0.0f, 0.0f, &info_c, &bc, tol,
                                     f->format);
      ok_c = (rc == 0) && (fabs(bc - 0.0625) <= tol);
   }

   bool ok = ok_a && ok_b && ok_c;
   char details[128];
   /* a and b in units of r_nominal (expect 256), c absolute (expect 0.0625). */
   int pos = snprintf(details, sizeof(details), "exact=%u a=%.2f",
                      dbc->depthBiasExact, ba / r_nominal);
   if (ran_b)
      pos += snprintf(details + pos, sizeof(details) - pos, " b=%.2f",
                      bb / r_nominal);
   if (ran_c)
      pos += snprintf(details + pos, sizeof(details) - pos, " c=%.8g", bc);

   printf("%s case %s_depth_bias_control %s\n",
          ok ? "PASS" : "FAIL", f->tag, details);
   if (ok)
      (*passes)++;
   else
      (*fails)++;
   return 0;
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
   if (!gipa) {
      printf("FAIL gipa\n");
      return 1;
   }

   PFN_vkCreateInstance CreateInstance =
      (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
   const char *iexts[] = {"VK_KHR_get_physical_device_properties2"};
   VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                            .apiVersion = VK_API_VERSION_1_3};
   VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                               .pApplicationInfo = &app,
                               .enabledExtensionCount = 1,
                               .ppEnabledExtensionNames = iexts};
   VkInstance inst;
   CK(CreateInstance(&ici, NULL, &inst), "CreateInstance");

#define GI(n)                                                                  \
   PFN_vk##n n = (PFN_vk##n)gipa(inst, "vk" #n);                               \
   if (!n) {                                                                   \
      printf("FAIL missing vk" #n "\n");                                       \
      return 1;                                                                \
   }
   GI(EnumeratePhysicalDevices)
   GI(GetPhysicalDeviceProperties)
   GI(GetPhysicalDeviceFeatures2)
   GI(GetPhysicalDeviceQueueFamilyProperties)
   GI(GetPhysicalDeviceMemoryProperties)
   GI(GetPhysicalDeviceFormatProperties)
   GI(EnumerateDeviceExtensionProperties)
   GI(CreateDevice)
   GI(GetDeviceProcAddr)
   GI(DestroyInstance)
#undef GI

   uint32_t nd = 0;
   CK(EnumeratePhysicalDevices(inst, &nd, NULL), "count");
   VkPhysicalDevice *devs = calloc(nd, sizeof(*devs));
   if (!devs)
      return 1;
   CK(EnumeratePhysicalDevices(inst, &nd, devs), "enum");
   VkPhysicalDevice phys = VK_NULL_HANDLE;
   for (uint32_t i = 0; i < nd; i++) {
      VkPhysicalDeviceProperties p;
      GetPhysicalDeviceProperties(devs[i], &p);
      printf("PHYS %u %s vendor=0x%x device=0x%x\n", i, p.deviceName,
             p.vendorID, p.deviceID);
      if (strstr(p.deviceName, "Mali"))
         phys = devs[i];
   }
   free(devs);
   if (!phys) {
      printf("FAIL no Mali\n");
      return 1;
   }

   /* Check for VK_EXT_depth_bias_control */
   uint32_t ext_count = 0;
   CK(EnumerateDeviceExtensionProperties(phys, NULL, &ext_count, NULL),
      "enum ext count");
   VkExtensionProperties *ext_props = calloc(ext_count, sizeof(*ext_props));
   if (!ext_props && ext_count > 0)
      return 1;
   if (ext_count > 0)
      CK(EnumerateDeviceExtensionProperties(phys, NULL, &ext_count, ext_props),
         "enum exts");

   bool has_depth_bias_control = false;
   for (uint32_t i = 0; i < ext_count; i++) {
      if (strcmp(ext_props[i].extensionName,
                 VK_EXT_DEPTH_BIAS_CONTROL_EXTENSION_NAME) == 0) {
         has_depth_bias_control = true;
         break;
      }
   }
   free(ext_props);

   VkPhysicalDeviceDepthBiasControlFeaturesEXT dbc_features = {
      .sType =
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_BIAS_CONTROL_FEATURES_EXT,
   };
   VkPhysicalDeviceFeatures2 feat2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
   };
   if (has_depth_bias_control)
      feat2.pNext = &dbc_features;

   GetPhysicalDeviceFeatures2(phys, &feat2);

   printf("FEATURE depthBiasClamp=%u\n", feat2.features.depthBiasClamp);
   if (has_depth_bias_control) {
      printf("FEATURE depthBiasControl=%u exact=%u forceUnorm=%u float=%u\n",
             dbc_features.depthBiasControl, dbc_features.depthBiasExact,
             dbc_features.leastRepresentableValueForceUnormRepresentation,
             dbc_features.floatRepresentation);
   } else {
      printf("FEATURE depthBiasControl=0 (extension unsupported)\n");
   }

   /* Select graphics queue family */
   uint32_t qn = 0;
   GetPhysicalDeviceQueueFamilyProperties(phys, &qn, NULL);
   VkQueueFamilyProperties *qp = calloc(qn, sizeof(*qp));
   if (!qp)
      return 1;
   GetPhysicalDeviceQueueFamilyProperties(phys, &qn, qp);
   uint32_t qi = ~0u;
   for (uint32_t i = 0; i < qn && qi == ~0u; i++)
      if (qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
         qi = i;
   free(qp);
   if (qi == ~0u) {
      printf("FAIL no graphics queue\n");
      return 1;
   }
   GetPhysicalDeviceMemoryProperties(phys, &mp);

   const char *dev_exts[4];
   uint32_t dev_ext_count = 0;
   if (has_depth_bias_control)
      dev_exts[dev_ext_count++] = VK_EXT_DEPTH_BIAS_CONTROL_EXTENSION_NAME;

   VkPhysicalDeviceFeatures2 dev_feat2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
   };
   if (feat2.features.depthBiasClamp)
      dev_feat2.features.depthBiasClamp = VK_TRUE;

   VkPhysicalDeviceDepthBiasControlFeaturesEXT enable_dbc = {
      .sType =
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_BIAS_CONTROL_FEATURES_EXT,
   };
   if (has_depth_bias_control && dbc_features.depthBiasControl) {
      enable_dbc.depthBiasControl = VK_TRUE;
      enable_dbc.depthBiasExact = dbc_features.depthBiasExact;
      enable_dbc.leastRepresentableValueForceUnormRepresentation =
         dbc_features.leastRepresentableValueForceUnormRepresentation;
      enable_dbc.floatRepresentation = dbc_features.floatRepresentation;
      dev_feat2.pNext = &enable_dbc;
   }

   float prio = 1.0f;
   VkDeviceQueueCreateInfo qci = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = qi,
      .queueCount = 1,
      .pQueuePriorities = &prio};
   VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                             .pNext = &dev_feat2,
                             .queueCreateInfoCount = 1,
                             .pQueueCreateInfos = &qci,
                             .enabledExtensionCount = dev_ext_count,
                             .ppEnabledExtensionNames = dev_exts};
   CK(CreateDevice(phys, &dci, NULL, &dev), "CreateDevice");

#define LP(n)                                                                  \
   do {                                                                        \
      PFN_vkVoidFunction _p = GetDeviceProcAddr(dev, "vk" #n);                 \
      if (!_p) {                                                               \
         printf("FAIL missing vk" #n "\n");                                    \
         return 1;                                                             \
      }                                                                        \
      memcpy(&p_##n, &_p, sizeof(_p));                                         \
   } while (0);
   DEV_FNS(LP)
#undef LP

   if (has_depth_bias_control && dbc_features.depthBiasControl) {
      PFN_vkVoidFunction _p = GetDeviceProcAddr(dev, "vkCmdSetDepthBias2EXT");
      memcpy(&p_CmdSetDepthBias2EXT, &_p, sizeof(_p));
   }

   p_GetDeviceQueue(dev, qi, 0, &queue);

   VkCommandPool pool;
   VkCommandPoolCreateInfo pci = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = qi};
   CK(p_CreateCommandPool(dev, &pci, NULL, &pool), "Pool");
   VkCommandBufferAllocateInfo cai = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1};
   CK(p_AllocateCommandBuffers(dev, &cai, &cmd), "AllocateCommandBuffers");
   VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   CK(p_CreateFence(dev, &fci, NULL, &fence), "CreateFence");

   VkPipelineLayoutCreateInfo pli = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
   CK(p_CreatePipelineLayout(dev, &pli, NULL, &pl), "CreatePipelineLayout");

   VkShaderModuleCreateInfo smi = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
   smi.codeSize = sizeof(ds_vert_spv);
   smi.pCode = ds_vert_spv;
   CK(p_CreateShaderModule(dev, &smi, NULL, &sm_vs), "CreateShaderModule VS");
   smi.codeSize = sizeof(ds_flat_vert_spv);
   smi.pCode = ds_flat_vert_spv;
   CK(p_CreateShaderModule(dev, &smi, NULL, &sm_vs_flat),
      "CreateShaderModule flat VS");
   smi.codeSize = sizeof(ds_frag_spv);
   smi.pCode = ds_frag_spv;
   CK(p_CreateShaderModule(dev, &smi, NULL, &sm_fs), "CreateShaderModule FS");

   /* Readback buffers (4096 bytes each is sufficient for 32x32) */
   if (make_buf(W * H * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &rb_depth,
                &rb_depth_mem, &dmap) ||
       make_buf(W * H * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &rb_stencil,
                &rb_stencil_mem, &smap) ||
       make_buf(W * H * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &rb_color,
                &rb_color_mem, &cmap))
      return 1;

   struct format_info formats[3] = {
      {VK_FORMAT_D24_UNORM_S8_UINT, "d24s8", 24, false, false},
      {VK_FORMAT_D32_SFLOAT_S8_UINT, "d32s8", 0, true, false},
      {VK_FORMAT_D16_UNORM_S8_UINT, "d16s8", 16, false, false},
   };

   for (int i = 0; i < 3; i++) {
      VkFormatProperties fp;
      GetPhysicalDeviceFormatProperties(phys, formats[i].format, &fp);
      VkFormatFeatureFlags req =
         VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
         VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
      if ((fp.optimalTilingFeatures & req) == req)
         formats[i].usable = true;
   }

   uint32_t passes = 0, fails = 0, skips = 0;

   if (!formats[0].usable && !formats[1].usable) {
      printf("FAIL case ds_format neither D24S8 nor D32S8 usable\n");
      fails++;
   }

   for (int i = 0; i < 3; i++) {
      const struct format_info *f = &formats[i];
      if (!f->usable) {
         if (i < 2 && !formats[0].usable && !formats[1].usable)
            continue;
         printf("SKIP case %s_all format unsupported\n", f->tag);
         skips++;
         continue;
      }

      /* Create per-format images, render pass, framebuffer, pipelines */
      VkImage depth_img, color_img;
      VkDeviceMemory depth_mem, color_mem;
      VkImageView dview, cview;

      if (make_image(f->format, VK_SAMPLE_COUNT_1_BIT,
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                     VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                     &depth_img, &depth_mem, &dview) ||
          make_image(VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT, &color_img, &color_mem,
                     &cview))
         return 1;

      VkAttachmentDescription ad[2] = {
         {.format = VK_FORMAT_R8G8B8A8_UNORM,
          .samples = VK_SAMPLE_COUNT_1_BIT,
          .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
          .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
          .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL},
         {.format = f->format,
          .samples = VK_SAMPLE_COUNT_1_BIT,
          .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
          .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
          .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL}};
      VkAttachmentReference cref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
      VkAttachmentReference dref = {
         1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
      VkSubpassDescription sub = {
         .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
         .colorAttachmentCount = 1,
         .pColorAttachments = &cref,
         .pDepthStencilAttachment = &dref};
      VkSubpassDependency dep = {
         .srcSubpass = 0,
         .dstSubpass = VK_SUBPASS_EXTERNAL,
         .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                         VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
         .dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
         .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT};
      VkRenderPassCreateInfo rpi = {
         .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
         .attachmentCount = 2,
         .pAttachments = ad,
         .subpassCount = 1,
         .pSubpasses = &sub,
         .dependencyCount = 1,
         .pDependencies = &dep};
      VkRenderPass rp;
      CK(p_CreateRenderPass(dev, &rpi, NULL, &rp), "CreateRenderPass");

      VkImageView atts[2] = {cview, dview};
      VkFramebufferCreateInfo fbi = {
         .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
         .renderPass = rp,
         .attachmentCount = 2,
         .pAttachments = atts,
         .width = W,
         .height = H,
         .layers = 1};
      VkFramebuffer fb;
      CK(p_CreateFramebuffer(dev, &fbi, NULL, &fb), "CreateFramebuffer");

      /* Pipeline with dynamic depth bias for subcases 3 & 4 */
      struct pipe_params pdyn = {
         .depthTestEnable = VK_TRUE,
         .depthWriteEnable = VK_TRUE,
         .depthCompareOp = VK_COMPARE_OP_ALWAYS,
         .stencilTestEnable = VK_FALSE,
         .colorWriteMask = 0,
         .minDepth = 0.0f,
         .maxDepth = 1.0f,
         .flat_z = true,
         .depthBiasEnable = VK_TRUE,
         .dynamicDepthBias = VK_TRUE,
      };
      VkPipeline dyn_pipe;
      if (make_pipe(&pdyn, rp, &dyn_pipe))
         return 1;

      /* Subcase 0: <tag>_viewport_zero_depth_range. D3D MinZ == MaxZ (sky
       * boxes, HUD at a fixed depth): with minDepth == maxDepth == 0.5 the
       * x depth gradient must collapse to exactly 0.5 for every pixel. */
      {
         struct pipe_params pz = {
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_ALWAYS,
            .minDepth = 0.5f,
            .maxDepth = 0.5f,
         };
         VkPipeline zpipe;
         if (make_pipe(&pz, rp, &zpipe))
            return 1;
         double b = 0.0;
         int rz = run_depth_bias_single(rp, fb, depth_img, color_img, zpipe,
                                        false, 0.0f, 0.0f, 0.0f, NULL, &b,
                                        get_tolerance(f), f->format);
         p_DestroyPipeline(dev, zpipe, NULL);
         int ok = rz == 0 && fabs(b) <= get_tolerance(f);
         printf("%s case %s_viewport_zero_depth_range x0_minus_0.5=%.9f%s\n",
                ok ? "PASS" : "FAIL", f->tag, b,
                rz == 2 ? " depth not flat" : "");
         if (ok)
            passes++;
         else
            fails++;
      }

      /* Run Subcase 1: <tag>_depth */
      if (test_depth(f, rp, fb, depth_img, color_img, &passes, &fails))
         return 1;

      /* Run Subcase 2: <tag>_stencil_ops */
      if (test_stencil_ops(f, rp, fb, depth_img, color_img, &passes, &fails))
         return 1;

      /* Run Subcase 3: <tag>_depth_bias_clamp */
      if (test_depth_bias_clamp(f, rp, fb, depth_img, color_img, dyn_pipe,
                                feat2.features.depthBiasClamp != 0, &passes,
                                &fails, &skips))
         return 1;

      /* Run Subcase 4: <tag>_depth_bias_control */
      if (test_depth_bias_control(f, rp, fb, depth_img, color_img, dyn_pipe,
                                  has_depth_bias_control, &dbc_features,
                                  &passes, &fails, &skips))
         return 1;

      p_DestroyPipeline(dev, dyn_pipe, NULL);
      p_DestroyFramebuffer(dev, fb, NULL);
      p_DestroyRenderPass(dev, rp, NULL);
      p_DestroyImageView(dev, cview, NULL);
      p_DestroyImage(dev, color_img, NULL);
      p_FreeMemory(dev, color_mem, NULL);
      p_DestroyImageView(dev, dview, NULL);
      p_DestroyImage(dev, depth_img, NULL);
      p_FreeMemory(dev, depth_mem, NULL);
   }

   printf("SUMMARY: passed=%u failed=%u skipped=%u\n", passes, fails, skips);

   p_DestroyBuffer(dev, rb_depth, NULL);
   p_FreeMemory(dev, rb_depth_mem, NULL);
   p_DestroyBuffer(dev, rb_stencil, NULL);
   p_FreeMemory(dev, rb_stencil_mem, NULL);
   p_DestroyBuffer(dev, rb_color, NULL);
   p_FreeMemory(dev, rb_color_mem, NULL);

   p_DestroyShaderModule(dev, sm_vs, NULL);
   p_DestroyShaderModule(dev, sm_vs_flat, NULL);
   p_DestroyShaderModule(dev, sm_fs, NULL);
   p_DestroyPipelineLayout(dev, pl, NULL);
   p_DestroyFence(dev, fence, NULL);
   p_DestroyCommandPool(dev, pool, NULL);
   p_DestroyDevice(dev, NULL);
   DestroyInstance(inst, NULL);
   dlclose(h);

   if (fails > 0) {
      printf("RESULT FAIL\n");
      return 1;
   }
   if (passes == 0) {
      printf("RESULT SKIP\n");
      return 0;
   }
   printf("RESULT PASS\n");
   return 0;
}

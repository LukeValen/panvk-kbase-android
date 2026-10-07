/* Vulkan occlusion query test.
 * Tests precise and non-precise queries, copy results, and command/host query resets.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if __has_include("../dx7_harness.h")
#include "../dx7_harness.h"
#else
#include "dx7_harness.h"
#endif

#if __has_include("occlusion_query_spv.h")
#include "occlusion_query_spv.h"
#else
#include "occlusion-query/occlusion_query_spv.h"
#endif

#define OQ_FUNCS(X)                                                            \
   X(CreateQueryPool) X(DestroyQueryPool) X(CmdResetQueryPool)                 \
   X(CmdBeginQuery) X(CmdEndQuery) X(GetQueryPoolResults)                      \
   X(CmdCopyQueryPoolResults)

OQ_FUNCS(DX7_DECL)

static PFN_vkResetQueryPool vkResetQueryPool;

static const char *g_icd_path;
static int has_occlusion_query_precise = 0;
static int has_host_query_reset = 0;

static void
device_hook(struct dx7 *t, VkDeviceCreateInfo *dci)
{
   void *h = dlopen(g_icd_path, RTLD_NOW | RTLD_LOCAL);
   if (!h) {
      printf("FAIL dlopen %s\n", dlerror());
      exit(1);
   }
   PFN_vkGetInstanceProcAddr gipa =
      (PFN_vkGetInstanceProcAddr)dlsym(h, "vk_icdGetInstanceProcAddr");
   if (!gipa) {
      printf("FAIL missing gipa\n");
      exit(1);
   }
   PFN_vkGetPhysicalDeviceFeatures2 get_feats2 =
      (PFN_vkGetPhysicalDeviceFeatures2)gipa(t->inst, "vkGetPhysicalDeviceFeatures2");
   if (!get_feats2)
      get_feats2 = (PFN_vkGetPhysicalDeviceFeatures2)gipa(t->inst, "vkGetPhysicalDeviceFeatures2KHR");

   static VkPhysicalDeviceVulkan12Features v12_feats;
   static VkPhysicalDeviceFeatures2 feats2;
   memset(&v12_feats, 0, sizeof(v12_feats));
   memset(&feats2, 0, sizeof(feats2));
   v12_feats.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
   feats2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
   feats2.pNext = &v12_feats;

   if (get_feats2)
      get_feats2(t->phys, &feats2);

   has_host_query_reset = v12_feats.hostQueryReset ? 1 : 0;
   has_occlusion_query_precise = t->feats.occlusionQueryPrecise ? 1 : 0;

   printf("INFO occlusionQueryPrecise=%d hostQueryReset=%d\n",
          has_occlusion_query_precise, has_host_query_reset);

   static VkPhysicalDeviceVulkan12Features enable_v12;
   static VkPhysicalDeviceFeatures2 enable_f2;
   memset(&enable_v12, 0, sizeof(enable_v12));
   memset(&enable_f2, 0, sizeof(enable_f2));

   enable_v12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
   enable_v12.pNext = (void *)dci->pNext;
   if (has_host_query_reset)
      enable_v12.hostQueryReset = VK_TRUE;

   enable_f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
   enable_f2.pNext = &enable_v12;
   if (has_occlusion_query_precise)
      enable_f2.features.occlusionQueryPrecise = VK_TRUE;

   dci->pEnabledFeatures = NULL;
   dci->pNext = &enable_f2;
}

static void
load_extra_funcs(struct dx7 *t, const char *icd)
{
   void *h = dlopen(icd, RTLD_NOW | RTLD_LOCAL);
   if (!h) {
      printf("FAIL dlopen %s\n", dlerror());
      exit(1);
   }
   PFN_vkGetInstanceProcAddr gipa =
      (PFN_vkGetInstanceProcAddr)dlsym(h, "vk_icdGetInstanceProcAddr");
   if (!gipa) {
      printf("FAIL missing gipa\n");
      exit(1);
   }
#define LOAD_EXTRA(n)                                                          \
   vk##n = (PFN_vk##n)gipa(t->inst, "vk" #n);                                  \
   if (!vk##n) {                                                               \
      printf("FAIL missing vk" #n "\n");                                       \
      exit(1);                                                                 \
   }
   OQ_FUNCS(LOAD_EXTRA)
#undef LOAD_EXTRA

   PFN_vkGetDeviceProcAddr gdpa =
      (PFN_vkGetDeviceProcAddr)gipa(t->inst, "vkGetDeviceProcAddr");

   vkResetQueryPool = (PFN_vkResetQueryPool)gipa(t->inst, "vkResetQueryPool");
   if (!vkResetQueryPool)
      vkResetQueryPool = (PFN_vkResetQueryPool)gipa(t->inst, "vkResetQueryPoolEXT");
   if (!vkResetQueryPool && gdpa)
      vkResetQueryPool = (PFN_vkResetQueryPool)gdpa(t->dev, "vkResetQueryPool");
   if (!vkResetQueryPool && gdpa)
      vkResetQueryPool = (PFN_vkResetQueryPool)gdpa(t->dev, "vkResetQueryPoolEXT");

   if (!vkResetQueryPool)
      has_host_query_reset = 0;
}

static void
cmd_begin(struct dx7 *t)
{
   CK(vkResetFences(t->dev, 1, &t->fence), "ResetFence");
   CK(vkResetCommandBuffer(t->cmd, 0), "ResetCmd");
   VkCommandBufferBeginInfo bbi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
   CK(vkBeginCommandBuffer(t->cmd, &bbi), "Begin");
}

static void
cmd_submit(struct dx7 *t)
{
   VkMemoryBarrier bar = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
   };
   vkCmdPipelineBarrier(t->cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &bar, 0, NULL, 0,
                        NULL);
   CK(vkEndCommandBuffer(t->cmd), "End");
   VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                      .commandBufferCount = 1,
                      .pCommandBuffers = &t->cmd};
   CK(vkQueueSubmit(t->queue, 1, &si, t->fence), "Submit");
   CK(vkWaitForFences(t->dev, 1, &t->fence, VK_TRUE, 30ull * 1000000000ull),
      "Wait");
}

/* Standard quad covering pixels x in [8, 40), y in [16, 48) on 64x64 target.
 * Exactly 1024 covered pixels at 1 sample.
 * NDC x in [-0.75, 0.25], y in [-0.5, 0.5]. */
static const float quad_verts[6][4] = {
   {-0.75f, -0.50f, 0.0f, 1.0f},
   { 0.25f, -0.50f, 0.0f, 1.0f},
   {-0.75f,  0.50f, 0.0f, 1.0f},
   { 0.25f, -0.50f, 0.0f, 1.0f},
   { 0.25f,  0.50f, 0.0f, 1.0f},
   {-0.75f,  0.50f, 0.0f, 1.0f},
};

/* Fullscreen quad covering all 4096 pixels of the 64x64 target. */
static const float fullscreen_verts[6][4] = {
   {-1.0f, -1.0f, 0.0f, 1.0f},
   { 1.0f, -1.0f, 0.0f, 1.0f},
   {-1.0f,  1.0f, 0.0f, 1.0f},
   { 1.0f, -1.0f, 0.0f, 1.0f},
   { 1.0f,  1.0f, 0.0f, 1.0f},
   {-1.0f,  1.0f, 0.0f, 1.0f},
};

/* Quad entirely off-screen (NDC x in [2, 3], y in [0, 1]). Exactly 0 pixels. */
static const float empty_verts[6][4] = {
   {2.0f, 0.0f, 0.0f, 1.0f},
   {3.0f, 0.0f, 0.0f, 1.0f},
   {2.0f, 1.0f, 0.0f, 1.0f},
   {3.0f, 0.0f, 0.0f, 1.0f},
   {3.0f, 1.0f, 0.0f, 1.0f},
   {2.0f, 1.0f, 0.0f, 1.0f},
};

/* Partial quad half off left edge, covering pixels x in [0, 16), y in [0, 64) visible.
 * Exactly 1024 (16*64) covered pixels.
 * Full quad NDC x in [-1.5, -0.5], y in [-1.0, 1.0]. */
static const float partial_verts[6][4] = {
   {-1.50f, -1.00f, 0.0f, 1.0f},
   {-0.50f, -1.00f, 0.0f, 1.0f},
   {-1.50f,  1.00f, 0.0f, 1.0f},
   {-0.50f, -1.00f, 0.0f, 1.0f},
   {-0.50f,  1.00f, 0.0f, 1.0f},
   {-1.50f,  1.00f, 0.0f, 1.0f},
};

static VkPipeline
create_quad_pipeline(struct dx7 *t)
{
   struct dx7_pipe_desc d = {
      .vs = vs_oq,
      .vs_size = sizeof(vs_oq),
      .fs = fs_oq,
      .fs_size = sizeof(fs_oq),
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .polygon_mode = VK_POLYGON_MODE_FILL,
      .cull_mode = VK_CULL_MODE_NONE,
      .vertex_stride = 16,
   };
   return dx7_pipeline(t, &d);
}

static void
copy_image_to_rbuf(struct dx7 *t)
{
   VkImageMemoryBarrier bar = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = t->img,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
   };
   vkCmdPipelineBarrier(t->cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
                        &bar);
   VkBufferImageCopy region = {
      .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
      .imageExtent = {RT_W, RT_H, 1},
   };
   vkCmdCopyImageToBuffer(t->cmd, t->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          t->rbuf, 1, &region);
}

static int
check_precise_quad_pixels(struct dx7 *t)
{
   int mismatches = 0;
   int first_x = 0, first_y = 0;
   const uint8_t *first = NULL;
   for (int y = 0; y < RT_H; y++) {
      for (int x = 0; x < RT_W; x++) {
         const uint8_t *px = t->px + (y * RT_W + x) * 4;
         int red = x >= 8 && x < 40 && y >= 16 && y < 48;
         if (px[0] != (red ? 255 : 0) || px[1] != 0 ||
             px[2] != (red ? 0 : 255) || px[3] != 255) {
            if (mismatches == 0) {
               first_x = x;
               first_y = y;
               first = px;
            }
            mismatches++;
         }
      }
   }
   if (mismatches != 0) {
      printf("FAIL case precise_quad_pixels mismatch=%d first=(%d,%d) RGBA=%u %u %u %u\n",
             mismatches, first_x, first_y,
             first[0], first[1], first[2], first[3]);
   } else {
      printf("PASS case precise_quad_pixels\n");
   }
   return mismatches;
}

int
main(int argc, char **argv)
{
   if (argc < 2) {
      printf("usage: %s <icd>\n", argv[0]);
      return 2;
   }
   g_icd_path = argv[1];
   dx7_device_hook = device_hook;

   struct dx7 t;
   dx7_init(&t, argv[1], NULL);
   load_extra_funcs(&t, argv[1]);

   VkQueryPoolCreateInfo qpci = {
      .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
      .queryType = VK_QUERY_TYPE_OCCLUSION,
      .queryCount = 8,
   };
   VkQueryPool pool;
   CK(vkCreateQueryPool(t.dev, &qpci, NULL, &pool), "CreateQueryPool");

   VkBuffer vb_quad, vb_empty, vb_partial, vb_fullscreen;
   VkDeviceMemory mem_quad, mem_empty, mem_partial, mem_fullscreen;
   dx7_buffer(&t, sizeof(quad_verts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
              quad_verts, &vb_quad, &mem_quad);
   dx7_buffer(&t, sizeof(empty_verts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
              empty_verts, &vb_empty, &mem_empty);
   dx7_buffer(&t, sizeof(partial_verts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
              partial_verts, &vb_partial, &mem_partial);
   dx7_buffer(&t, sizeof(fullscreen_verts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
              fullscreen_verts, &vb_fullscreen, &mem_fullscreen);

   VkBuffer copy_buf;
   VkDeviceMemory copy_mem;
   uint64_t *copy_mapped = NULL;
   dx7_buffer(&t, sizeof(uint64_t) * 2, VK_BUFFER_USAGE_TRANSFER_DST_BIT, NULL,
              &copy_buf, &copy_mem);
   CK(vkMapMemory(t.dev, copy_mem, 0, sizeof(uint64_t) * 2, 0,
                  (void **)&copy_mapped),
      "MapCopyBuf");

   VkPipeline pipe = create_quad_pipeline(&t);

   int fails = 0;
   int passes = 0;
   int skips = 0;

   /* Case a: precise_quad */
   if (!has_occlusion_query_precise) {
      printf("SKIP case precise_quad occlusionQueryPrecise not supported\n");
      skips++;
   } else {
      copy_mapped[0] = 0xdeadbeef;
      copy_mapped[1] = 0xdeadbeef;

      cmd_begin(&t);
      vkCmdResetQueryPool(t.cmd, pool, 0, 1);

      VkClearValue clear = {.color = {{0.0f, 0.0f, 1.0f, 1.0f}}};
      VkRenderPassBeginInfo rpbi = {
         .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
         .renderPass = t.rp,
         .framebuffer = t.fb,
         .renderArea = {{0, 0}, {RT_W, RT_H}},
         .clearValueCount = 1,
         .pClearValues = &clear,
      };
      vkCmdBeginRenderPass(t.cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
      vkCmdBindPipeline(t.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
      VkDeviceSize off = 0;
      vkCmdBindVertexBuffers(t.cmd, 0, 1, &vb_quad, &off);
      vkCmdBeginQuery(t.cmd, pool, 0, VK_QUERY_CONTROL_PRECISE_BIT);
      vkCmdDraw(t.cmd, 6, 1, 0, 0);
      vkCmdEndQuery(t.cmd, pool, 0);
      vkCmdEndRenderPass(t.cmd);

      copy_image_to_rbuf(&t);

      vkCmdCopyQueryPoolResults(t.cmd, pool, 0, 1, copy_buf, 0,
                                sizeof(uint64_t) * 2,
                                VK_QUERY_RESULT_64_BIT |
                                   VK_QUERY_RESULT_WAIT_BIT |
                                   VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
      cmd_submit(&t);

      uint64_t res = 0;
      CK(vkGetQueryPoolResults(t.dev, pool, 0, 1, sizeof(res), &res,
                               sizeof(res),
                               VK_QUERY_RESULT_64_BIT |
                                  VK_QUERY_RESULT_WAIT_BIT),
         "GetResults0");
      if (res == 1024) {
         printf("PASS case precise_quad got=1024 exp=1024\n");
         passes++;
      } else {
         printf("FAIL case precise_quad got=%llu exp=1024\n",
                (unsigned long long)res);
         fails++;
      }

      if (check_precise_quad_pixels(&t) != 0) {
         fails++;
      } else {
         passes++;
      }
   }

   /* Case b: precise_two_draws */
   if (!has_occlusion_query_precise) {
      printf("SKIP case precise_two_draws occlusionQueryPrecise not supported\n");
      skips++;
   } else {
      cmd_begin(&t);
      vkCmdResetQueryPool(t.cmd, pool, 1, 1);

      VkClearValue clear = {.color = {{0.0f, 0.0f, 1.0f, 1.0f}}};
      VkRenderPassBeginInfo rpbi = {
         .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
         .renderPass = t.rp,
         .framebuffer = t.fb,
         .renderArea = {{0, 0}, {RT_W, RT_H}},
         .clearValueCount = 1,
         .pClearValues = &clear,
      };
      vkCmdBeginRenderPass(t.cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
      vkCmdBindPipeline(t.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
      VkDeviceSize off = 0;
      vkCmdBindVertexBuffers(t.cmd, 0, 1, &vb_quad, &off);
      vkCmdBeginQuery(t.cmd, pool, 1, VK_QUERY_CONTROL_PRECISE_BIT);
      vkCmdDraw(t.cmd, 6, 1, 0, 0);
      vkCmdDraw(t.cmd, 6, 1, 0, 0);
      vkCmdEndQuery(t.cmd, pool, 1);
      vkCmdEndRenderPass(t.cmd);
      cmd_submit(&t);

      uint64_t res = 0;
      CK(vkGetQueryPoolResults(t.dev, pool, 1, 1, sizeof(res), &res,
                               sizeof(res),
                               VK_QUERY_RESULT_64_BIT |
                                  VK_QUERY_RESULT_WAIT_BIT),
         "GetResults1");
      if (res == 2048) {
         printf("PASS case precise_two_draws got=2048 exp=2048\n");
         passes++;
      } else {
         printf("FAIL case precise_two_draws got=%llu exp=2048\n",
                (unsigned long long)res);
         fails++;
      }
   }

   /* Case c: precise_empty */
   if (!has_occlusion_query_precise) {
      printf("SKIP case precise_empty occlusionQueryPrecise not supported\n");
      skips++;
   } else {
      cmd_begin(&t);
      vkCmdResetQueryPool(t.cmd, pool, 2, 1);

      VkClearValue clear = {.color = {{0.0f, 0.0f, 1.0f, 1.0f}}};
      VkRenderPassBeginInfo rpbi = {
         .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
         .renderPass = t.rp,
         .framebuffer = t.fb,
         .renderArea = {{0, 0}, {RT_W, RT_H}},
         .clearValueCount = 1,
         .pClearValues = &clear,
      };
      vkCmdBeginRenderPass(t.cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
      vkCmdBindPipeline(t.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
      VkDeviceSize off = 0;
      vkCmdBindVertexBuffers(t.cmd, 0, 1, &vb_empty, &off);
      vkCmdBeginQuery(t.cmd, pool, 2, VK_QUERY_CONTROL_PRECISE_BIT);
      vkCmdDraw(t.cmd, 6, 1, 0, 0);
      vkCmdEndQuery(t.cmd, pool, 2);
      vkCmdEndRenderPass(t.cmd);
      cmd_submit(&t);

      uint64_t res = 0;
      CK(vkGetQueryPoolResults(t.dev, pool, 2, 1, sizeof(res), &res,
                               sizeof(res),
                               VK_QUERY_RESULT_64_BIT |
                                  VK_QUERY_RESULT_WAIT_BIT),
         "GetResults2");
      if (res == 0) {
         printf("PASS case precise_empty got=0 exp=0\n");
         passes++;
      } else {
         printf("FAIL case precise_empty got=%llu exp=0\n",
                (unsigned long long)res);
         fails++;
      }
   }

   /* Case d: precise_partial */
   if (!has_occlusion_query_precise) {
      printf("SKIP case precise_partial occlusionQueryPrecise not supported\n");
      skips++;
   } else {
      cmd_begin(&t);
      vkCmdResetQueryPool(t.cmd, pool, 3, 1);

      VkClearValue clear = {.color = {{0.0f, 0.0f, 1.0f, 1.0f}}};
      VkRenderPassBeginInfo rpbi = {
         .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
         .renderPass = t.rp,
         .framebuffer = t.fb,
         .renderArea = {{0, 0}, {RT_W, RT_H}},
         .clearValueCount = 1,
         .pClearValues = &clear,
      };
      vkCmdBeginRenderPass(t.cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
      vkCmdBindPipeline(t.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
      VkDeviceSize off = 0;
      vkCmdBindVertexBuffers(t.cmd, 0, 1, &vb_partial, &off);
      vkCmdBeginQuery(t.cmd, pool, 3, VK_QUERY_CONTROL_PRECISE_BIT);
      vkCmdDraw(t.cmd, 6, 1, 0, 0);
      vkCmdEndQuery(t.cmd, pool, 3);
      vkCmdEndRenderPass(t.cmd);
      cmd_submit(&t);

      uint64_t res = 0;
      CK(vkGetQueryPoolResults(t.dev, pool, 3, 1, sizeof(res), &res,
                               sizeof(res),
                               VK_QUERY_RESULT_64_BIT |
                                  VK_QUERY_RESULT_WAIT_BIT),
         "GetResults3");
      if (res == 1024) {
         printf("PASS case precise_partial got=1024 exp=1024\n");
         passes++;
      } else {
         printf("FAIL case precise_partial got=%llu exp=1024\n",
                (unsigned long long)res);
         fails++;
      }
   }

   /* Case e: nonprecise */
   {
      cmd_begin(&t);
      vkCmdResetQueryPool(t.cmd, pool, 4, 2);

      VkClearValue clear = {.color = {{0.0f, 0.0f, 1.0f, 1.0f}}};
      VkRenderPassBeginInfo rpbi = {
         .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
         .renderPass = t.rp,
         .framebuffer = t.fb,
         .renderArea = {{0, 0}, {RT_W, RT_H}},
         .clearValueCount = 1,
         .pClearValues = &clear,
      };
      vkCmdBeginRenderPass(t.cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
      vkCmdBindPipeline(t.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
      VkDeviceSize off = 0;
      vkCmdBindVertexBuffers(t.cmd, 0, 1, &vb_quad, &off);
      vkCmdBeginQuery(t.cmd, pool, 4, 0);
      vkCmdDraw(t.cmd, 6, 1, 0, 0);
      vkCmdEndQuery(t.cmd, pool, 4);

      vkCmdBindVertexBuffers(t.cmd, 0, 1, &vb_empty, &off);
      vkCmdBeginQuery(t.cmd, pool, 5, 0);
      vkCmdDraw(t.cmd, 6, 1, 0, 0);
      vkCmdEndQuery(t.cmd, pool, 5);
      vkCmdEndRenderPass(t.cmd);
      cmd_submit(&t);

      uint64_t res_draw = 0, res_empty = 0;
      CK(vkGetQueryPoolResults(t.dev, pool, 4, 1, sizeof(res_draw), &res_draw,
                               sizeof(res_draw),
                               VK_QUERY_RESULT_64_BIT |
                                  VK_QUERY_RESULT_WAIT_BIT),
         "GetResults4");
      CK(vkGetQueryPoolResults(t.dev, pool, 5, 1, sizeof(res_empty), &res_empty,
                               sizeof(res_empty),
                               VK_QUERY_RESULT_64_BIT |
                                  VK_QUERY_RESULT_WAIT_BIT),
         "GetResults5");
      if (res_draw != 0 && res_empty == 0) {
         printf("PASS case nonprecise draw=%llu empty=%llu\n",
                (unsigned long long)res_draw, (unsigned long long)res_empty);
         passes++;
      } else {
         printf("FAIL case nonprecise got_draw=%llu got_empty=%llu exp_draw!=0 exp_empty=0\n",
                (unsigned long long)res_draw, (unsigned long long)res_empty);
         fails++;
      }
   }

   /* Case f: copy_results */
   if (!has_occlusion_query_precise) {
      printf("SKIP case copy_results occlusionQueryPrecise not supported\n");
      skips++;
   } else {
      uint64_t val = copy_mapped[0];
      uint64_t avail = copy_mapped[1];
      if (val == 1024 && avail != 0) {
         printf("PASS case copy_results val=1024 avail=%llu\n",
                (unsigned long long)avail);
         passes++;
      } else {
         printf("FAIL case copy_results got_val=%llu got_avail=%llu exp_val=1024 exp_avail!=0\n",
                (unsigned long long)val, (unsigned long long)avail);
         fails++;
      }
   }

   /* Case g: cmd_reset_reuse */
   if (!has_occlusion_query_precise) {
      printf("SKIP case cmd_reset_reuse occlusionQueryPrecise not supported\n");
      skips++;
   } else {
      cmd_begin(&t);
      vkCmdResetQueryPool(t.cmd, pool, 0, 1);

      VkClearValue clear = {.color = {{0.0f, 0.0f, 1.0f, 1.0f}}};
      VkRenderPassBeginInfo rpbi = {
         .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
         .renderPass = t.rp,
         .framebuffer = t.fb,
         .renderArea = {{0, 0}, {RT_W, RT_H}},
         .clearValueCount = 1,
         .pClearValues = &clear,
      };
      vkCmdBeginRenderPass(t.cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
      vkCmdBindPipeline(t.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
      VkDeviceSize off = 0;
      vkCmdBindVertexBuffers(t.cmd, 0, 1, &vb_fullscreen, &off);
      vkCmdBeginQuery(t.cmd, pool, 0, VK_QUERY_CONTROL_PRECISE_BIT);
      vkCmdDraw(t.cmd, 6, 1, 0, 0);
      vkCmdEndQuery(t.cmd, pool, 0);
      vkCmdEndRenderPass(t.cmd);
      cmd_submit(&t);

      uint64_t res = 0;
      CK(vkGetQueryPoolResults(t.dev, pool, 0, 1, sizeof(res), &res,
                               sizeof(res),
                               VK_QUERY_RESULT_64_BIT |
                                  VK_QUERY_RESULT_WAIT_BIT),
         "GetResults0_reuse");
      if (res == 4096) {
         printf("PASS case cmd_reset_reuse got=%llu exp=4096\n",
                (unsigned long long)res);
         passes++;
      } else {
         printf("FAIL case cmd_reset_reuse got=%llu exp=4096\n",
                (unsigned long long)res);
         fails++;
      }
   }

   /* Case h: host_reset */
   if (!has_host_query_reset) {
      printf("SKIP case host_reset hostQueryReset not supported\n");
      skips++;
   } else if (!has_occlusion_query_precise) {
      printf("SKIP case host_reset occlusionQueryPrecise not supported\n");
      skips++;
   } else {
      vkResetQueryPool(t.dev, pool, 0, 8);

      uint64_t host_res[2] = {~0ull, ~0ull};
      VkResult r = vkGetQueryPoolResults(t.dev, pool, 0, 1,
                                         sizeof(host_res), host_res,
                                         sizeof(uint64_t) * 2,
                                         VK_QUERY_RESULT_64_BIT |
                                            VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

      cmd_begin(&t);
      VkClearValue clear = {.color = {{0.0f, 0.0f, 1.0f, 1.0f}}};
      VkRenderPassBeginInfo rpbi = {
         .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
         .renderPass = t.rp,
         .framebuffer = t.fb,
         .renderArea = {{0, 0}, {RT_W, RT_H}},
         .clearValueCount = 1,
         .pClearValues = &clear,
      };
      vkCmdBeginRenderPass(t.cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
      vkCmdBindPipeline(t.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
      VkDeviceSize off = 0;
      vkCmdBindVertexBuffers(t.cmd, 0, 1, &vb_quad, &off);
      vkCmdBeginQuery(t.cmd, pool, 0, VK_QUERY_CONTROL_PRECISE_BIT);
      vkCmdDraw(t.cmd, 6, 1, 0, 0);
      vkCmdEndQuery(t.cmd, pool, 0);
      vkCmdEndRenderPass(t.cmd);
      cmd_submit(&t);

      uint64_t final_res = 0;
      CK(vkGetQueryPoolResults(t.dev, pool, 0, 1, sizeof(final_res),
                               &final_res, sizeof(final_res),
                               VK_QUERY_RESULT_64_BIT |
                                  VK_QUERY_RESULT_WAIT_BIT),
         "GetResults0_host_draw");

      if (r == VK_NOT_READY && host_res[1] == 0 && final_res == 1024) {
         printf("PASS case host_reset got=1024 exp=1024\n");
         passes++;
      } else {
         printf("FAIL case host_reset r=%d avail=%llu got=%llu exp=1024\n",
                r, (unsigned long long)host_res[1],
                (unsigned long long)final_res);
         fails++;
      }
   }

   vkDestroyPipeline(t.dev, pipe, NULL);
   vkDestroyQueryPool(t.dev, pool, NULL);

   if (fails > 0) {
      printf("RESULT FAIL\n");
      return 1;
   } else if (passes == 0 && skips > 0) {
      printf("RESULT SKIP\n");
      return 0;
   } else {
      printf("RESULT PASS\n");
      return 0;
   }
}

/* Standalone Vulkan blend probe.
 * Tests independent blend, dual source blending, and logic ops. */
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../dx7_harness.h"
#include "blend_spv.h"

#define FB_W 16
#define FB_H 16

#define EXTRA_FUNCS(X)        \
   X(DestroyFramebuffer)     \
   X(DestroyImageView)       \
   X(DestroyImage)           \
   X(DestroyRenderPass)      \
   X(DestroyBuffer)          \
   X(FreeMemory)

EXTRA_FUNCS(DX7_DECL)

static VkPhysicalDeviceFeatures enabled_features;

static void
device_hook(struct dx7 *t, VkDeviceCreateInfo *dci)
{
   memset(&enabled_features, 0, sizeof(enabled_features));
   if (t->feats.dualSrcBlend)
      enabled_features.dualSrcBlend = VK_TRUE;
   if (t->feats.independentBlend)
      enabled_features.independentBlend = VK_TRUE;
   if (t->feats.logicOp)
      enabled_features.logicOp = VK_TRUE;
   dci->pEnabledFeatures = &enabled_features;
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
   EXTRA_FUNCS(LOAD_EXTRA)
#undef LOAD_EXTRA
}

struct rt_image {
   VkImage img;
   VkDeviceMemory mem;
   VkImageView view;
};

static void
create_rt_image(struct dx7 *t, struct rt_image *rt)
{
   VkImageCreateInfo ici = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = VK_FORMAT_R8G8B8A8_UNORM,
      .extent = {FB_W, FB_H, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
               VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };
   CK(vkCreateImage(t->dev, &ici, NULL, &rt->img), "CreateImage");

   VkMemoryRequirements mr;
   vkGetImageMemoryRequirements(t->dev, rt->img, &mr);
   uint32_t mi = 0;
   while (!(mr.memoryTypeBits & (1u << mi)))
      mi++;
   VkMemoryAllocateInfo mai = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mr.size,
      .memoryTypeIndex = mi,
   };
   CK(vkAllocateMemory(t->dev, &mai, NULL, &rt->mem), "AllocImgMem");
   CK(vkBindImageMemory(t->dev, rt->img, rt->mem, 0), "BindImgMem");

   VkImageViewCreateInfo vci = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = rt->img,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = VK_FORMAT_R8G8B8A8_UNORM,
      .subresourceRange = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .baseMipLevel = 0,
         .levelCount = 1,
         .baseArrayLayer = 0,
         .layerCount = 1,
      },
   };
   CK(vkCreateImageView(t->dev, &vci, NULL, &rt->view), "CreateImageView");
}

static void
destroy_rt_image(struct dx7 *t, struct rt_image *rt)
{
   vkDestroyImageView(t->dev, rt->view, NULL);
   vkDestroyImage(t->dev, rt->img, NULL);
   vkFreeMemory(t->dev, rt->mem, NULL);
}

static VkPipeline
create_pipeline(struct dx7 *t, VkRenderPass rp,
                const uint32_t *vs_spv, size_t vs_size,
                const uint32_t *fs_spv, size_t fs_size,
                VkPipelineLayout layout,
                const VkPipelineColorBlendStateCreateInfo *cb)
{
   VkShaderModule vs = dx7_module(t, vs_spv, vs_size);
   VkShaderModule fs = dx7_module(t, fs_spv, fs_size);

   VkPipelineShaderStageCreateInfo stages[2] = {
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_VERTEX_BIT,
       .module = vs,
       .pName = "main"},
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
       .module = fs,
       .pName = "main"},
   };

   VkPipelineVertexInputStateCreateInfo vi = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
   };

   VkPipelineInputAssemblyStateCreateInfo ia = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
   };

   VkViewport vp = {0.0f, 0.0f, (float)FB_W, (float)FB_H, 0.0f, 1.0f};
   VkRect2D sc = {{0, 0}, {FB_W, FB_H}};
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

   VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
   VkPipelineDynamicStateCreateInfo ds = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = 2,
      .pDynamicStates = dyn,
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
      .pColorBlendState = cb,
      .pDynamicState = &ds,
      .layout = layout,
      .renderPass = rp,
      .subpass = 0,
   };

   VkPipeline pipe;
   CK(vkCreateGraphicsPipelines(t->dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipe),
      "CreateGraphicsPipelines");

   vkDestroyShaderModule(t->dev, vs, NULL);
   vkDestroyShaderModule(t->dev, fs, NULL);
   return pipe;
}

static void
cmd_begin(struct dx7 *t)
{
   CK(vkResetFences(t->dev, 1, &t->fence), "ResetFence");
   CK(vkResetCommandBuffer(t->cmd, 0), "ResetCmd");
   VkCommandBufferBeginInfo bbi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
   };
   CK(vkBeginCommandBuffer(t->cmd, &bbi), "BeginCmd");
}

static void
cmd_submit_wait(struct dx7 *t)
{
   VkMemoryBarrier bar = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
   };
   vkCmdPipelineBarrier(t->cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &bar, 0, NULL, 0,
                        NULL);
   CK(vkEndCommandBuffer(t->cmd), "EndCmd");
   VkSubmitInfo si = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers = &t->cmd,
   };
   CK(vkQueueSubmit(t->queue, 1, &si, t->fence), "QueueSubmit");
   CK(vkWaitForFences(t->dev, 1, &t->fence, VK_TRUE, 30ull * 1000000000ull),
      "WaitForFences");
}

static void
cmd_copy_images_to_buffer(struct dx7 *t, uint32_t count,
                          const struct rt_image *imgs, VkBuffer dst_buf)
{
   VkImageMemoryBarrier bars[4];
   for (uint32_t i = 0; i < count; i++) {
      bars[i] = (VkImageMemoryBarrier){
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
         .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
         .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .image = imgs[i].img,
         .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
      };
   }
   vkCmdPipelineBarrier(t->cmd,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, 0, NULL, 0, NULL, count, bars);

   for (uint32_t i = 0; i < count; i++) {
      VkBufferImageCopy region = {
         .bufferOffset = (VkDeviceSize)i * (FB_W * FB_H * 4),
         .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
         .imageExtent = {FB_W, FB_H, 1},
      };
      vkCmdCopyImageToBuffer(t->cmd, imgs[i].img,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             dst_buf, 1, &region);
   }
}

static inline uint8_t
clamp_u8(int v)
{
   if (v < 0)
      return 0;
   if (v > 255)
      return 255;
   return (uint8_t)v;
}

static int
test_independent_blend(struct dx7 *t)
{
   if (!t->feats.independentBlend || t->props.limits.maxColorAttachments < 4) {
      printf("SKIP case independent_blend independentBlend not supported or maxColorAttachments < 4\n");
      return -1;
   }

   struct rt_image rt[4];
   for (int i = 0; i < 4; i++)
      create_rt_image(t, &rt[i]);

   VkAttachmentDescription atts[4];
   VkAttachmentReference refs[4];
   for (uint32_t i = 0; i < 4; i++) {
      atts[i] = (VkAttachmentDescription){
         .format = VK_FORMAT_R8G8B8A8_UNORM,
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
         .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      };
      refs[i] = (VkAttachmentReference){
         .attachment = i,
         .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      };
   }
   VkSubpassDescription sub = {
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount = 4,
      .pColorAttachments = refs,
   };
   VkRenderPassCreateInfo rpci = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = 4,
      .pAttachments = atts,
      .subpassCount = 1,
      .pSubpasses = &sub,
   };
   VkRenderPass rp;
   CK(vkCreateRenderPass(t->dev, &rpci, NULL, &rp), "CreateRenderPass_mrt");

   VkImageView views[4] = {rt[0].view, rt[1].view, rt[2].view, rt[3].view};
   VkFramebufferCreateInfo fbci = {
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass = rp,
      .attachmentCount = 4,
      .pAttachments = views,
      .width = FB_W,
      .height = FB_H,
      .layers = 1,
   };
   VkFramebuffer fb;
   CK(vkCreateFramebuffer(t->dev, &fbci, NULL, &fb), "CreateFramebuffer_mrt");

   VkPipelineColorBlendAttachmentState bas[4] = {
      /* att0: blendEnable false, writeMask RGBA */
      {
         .blendEnable = VK_FALSE,
         .colorWriteMask = 0xF,
      },
      /* att1: ADD, src ONE, dst ONE (color and alpha) -> clamp(src+dst) */
      {
         .blendEnable = VK_TRUE,
         .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
         .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
         .colorBlendOp = VK_BLEND_OP_ADD,
         .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
         .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
         .alphaBlendOp = VK_BLEND_OP_ADD,
         .colorWriteMask = 0xF,
      },
      /* att2: REVERSE_SUBTRACT, src ONE, dst ONE -> clamp(dst-src) */
      {
         .blendEnable = VK_TRUE,
         .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
         .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
         .colorBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT,
         .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
         .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
         .alphaBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT,
         .colorWriteMask = 0xF,
      },
      /* att3: MAX op (factors ignored), colorWriteMask R|A only (G,B keep clear values) */
      {
         .blendEnable = VK_TRUE,
         .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
         .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
         .colorBlendOp = VK_BLEND_OP_MAX,
         .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
         .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
         .alphaBlendOp = VK_BLEND_OP_MAX,
         .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_A_BIT,
      },
   };

   VkPipelineColorBlendStateCreateInfo cb = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 4,
      .pAttachments = bas,
   };

   VkPipeline pipe = create_pipeline(t, rp, vs_blend, sizeof(vs_blend),
                                     fs_mrt, sizeof(fs_mrt), t->layout, &cb);

   VkBuffer rbuf;
   VkDeviceMemory rmem;
   dx7_buffer(t, 4 * FB_W * FB_H * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, NULL,
              &rbuf, &rmem);

   cmd_begin(t);

   const uint8_t src_colors[4][4] = {
      {11, 22, 33, 44},
      {30, 40, 50, 60},
      {20, 30, 40, 50},
      {210, 80, 90, 250},
   };

   const uint8_t dst_colors[4][4] = {
      {10, 20, 30, 40},
      {50, 60, 70, 80},
      {90, 100, 110, 120},
      {130, 140, 150, 160},
   };

   VkClearValue clears[4];
   for (int i = 0; i < 4; i++) {
      clears[i].color.float32[0] = (float)dst_colors[i][0] / 255.0f;
      clears[i].color.float32[1] = (float)dst_colors[i][1] / 255.0f;
      clears[i].color.float32[2] = (float)dst_colors[i][2] / 255.0f;
      clears[i].color.float32[3] = (float)dst_colors[i][3] / 255.0f;
   }

   VkRenderPassBeginInfo rpbi = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = rp,
      .framebuffer = fb,
      .renderArea = {{0, 0}, {FB_W, FB_H}},
      .clearValueCount = 4,
      .pClearValues = clears,
   };
   vkCmdBeginRenderPass(t->cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
   vkCmdBindPipeline(t->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);

   VkViewport vp = {0.0f, 0.0f, (float)FB_W, (float)FB_H, 0.0f, 1.0f};
   VkRect2D sc = {{0, 0}, {FB_W, FB_H}};
   vkCmdSetViewport(t->cmd, 0, 1, &vp);
   vkCmdSetScissor(t->cmd, 0, 1, &sc);

   vkCmdDraw(t->cmd, 3, 1, 0, 0);
   vkCmdEndRenderPass(t->cmd);

   cmd_copy_images_to_buffer(t, 4, rt, rbuf);
   cmd_submit_wait(t);

   void *map_ptr = NULL;
   CK(vkMapMemory(t->dev, rmem, 0, 4 * FB_W * FB_H * 4, 0, &map_ptr),
      "MapMemory_mrt");

   uint8_t exp[4][4];
   for (int c = 0; c < 4; c++)
      exp[0][c] = src_colors[0][c];
   for (int c = 0; c < 4; c++)
      exp[1][c] = clamp_u8((int)src_colors[1][c] + (int)dst_colors[1][c]);
   for (int c = 0; c < 4; c++)
      exp[2][c] = clamp_u8((int)dst_colors[2][c] - (int)src_colors[2][c]);
   exp[3][0] = src_colors[3][0] > dst_colors[3][0] ? src_colors[3][0] : dst_colors[3][0];
   exp[3][1] = dst_colors[3][1];
   exp[3][2] = dst_colors[3][2];
   exp[3][3] = src_colors[3][3] > dst_colors[3][3] ? src_colors[3][3] : dst_colors[3][3];

   uint32_t mismatches = 0;
   uint32_t first_att = 0;
   uint8_t first_got[4] = {0};
   uint8_t first_exp[4] = {0};

   for (uint32_t a = 0; a < 4; a++) {
      const uint8_t *apx = (const uint8_t *)map_ptr + a * (FB_W * FB_H * 4);
      for (uint32_t p = 0; p < FB_W * FB_H; p++) {
         const uint8_t *got = apx + p * 4;
         if (got[0] != exp[a][0] || got[1] != exp[a][1] ||
             got[2] != exp[a][2] || got[3] != exp[a][3]) {
            if (mismatches == 0) {
               first_att = a;
               memcpy(first_got, got, 4);
               memcpy(first_exp, exp[a], 4);
            }
            mismatches++;
         }
      }
   }

   vkUnmapMemory(t->dev, rmem);

   vkDestroyPipeline(t->dev, pipe, NULL);
   vkDestroyFramebuffer(t->dev, fb, NULL);
   vkDestroyRenderPass(t->dev, rp, NULL);
   for (int i = 0; i < 4; i++)
      destroy_rt_image(t, &rt[i]);
   vkDestroyBuffer(t->dev, rbuf, NULL);
   vkFreeMemory(t->dev, rmem, NULL);

   if (mismatches > 0) {
      printf("FAIL case independent_blend att=%u got=%u %u %u %u exp=%u %u %u %u mismatches=%u\n",
             first_att, first_got[0], first_got[1], first_got[2], first_got[3],
             first_exp[0], first_exp[1], first_exp[2], first_exp[3], mismatches);
      return 1;
   }

   printf("PASS case independent_blend\n");
   return 0;
}

static int
test_dual_source(struct dx7 *t)
{
   if (!t->feats.dualSrcBlend || t->props.limits.maxFragmentDualSrcAttachments < 1) {
      printf("SKIP case dual_source dualSrcBlend not supported or maxFragmentDualSrcAttachments < 1\n");
      return -1;
   }

   struct rt_image rt;
   create_rt_image(t, &rt);

   VkAttachmentDescription att = {
      .format = VK_FORMAT_R8G8B8A8_UNORM,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
   };
   VkAttachmentReference ref = {
      .attachment = 0,
      .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
   };
   VkSubpassDescription sub = {
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount = 1,
      .pColorAttachments = &ref,
   };
   VkRenderPassCreateInfo rpci = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &att,
      .subpassCount = 1,
      .pSubpasses = &sub,
   };
   VkRenderPass rp;
   CK(vkCreateRenderPass(t->dev, &rpci, NULL, &rp), "CreateRenderPass_dual");

   VkFramebufferCreateInfo fbci = {
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass = rp,
      .attachmentCount = 1,
      .pAttachments = &rt.view,
      .width = FB_W,
      .height = FB_H,
      .layers = 1,
   };
   VkFramebuffer fb;
   CK(vkCreateFramebuffer(t->dev, &fbci, NULL, &fb), "CreateFramebuffer_dual");

   VkPipelineColorBlendAttachmentState ba = {
      .blendEnable = VK_TRUE,
      .srcColorBlendFactor = VK_BLEND_FACTOR_SRC1_COLOR,
      .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR,
      .colorBlendOp = VK_BLEND_OP_ADD,
      .srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC1_ALPHA,
      .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA,
      .alphaBlendOp = VK_BLEND_OP_ADD,
      .colorWriteMask = 0xF,
   };
   VkPipelineColorBlendStateCreateInfo cb = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &ba,
   };

   VkPipeline pipe = create_pipeline(t, rp, vs_blend, sizeof(vs_blend),
                                     fs_dual, sizeof(fs_dual), t->layout, &cb);

   VkBuffer rbuf;
   VkDeviceMemory rmem;
   dx7_buffer(t, FB_W * FB_H * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, NULL,
              &rbuf, &rmem);

   const struct {
      float src1[4];
      uint8_t exp[4];
   } sub_checks[2] = {
      {{1.0f, 0.0f, 1.0f, 0.0f}, {200, 20, 50, 40}},
      {{0.0f, 1.0f, 0.0f, 1.0f}, {10, 100, 30, 255}},
   };

   uint32_t total_mismatches = 0;
   uint8_t first_got[4] = {0};
   uint8_t first_exp[4] = {0};

   for (int sc_idx = 0; sc_idx < 2; sc_idx++) {
      cmd_begin(t);

      VkClearValue clear = {
         .color = {.float32 = {10.0f / 255.0f, 20.0f / 255.0f,
                               30.0f / 255.0f, 40.0f / 255.0f}},
      };
      VkRenderPassBeginInfo rpbi = {
         .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
         .renderPass = rp,
         .framebuffer = fb,
         .renderArea = {{0, 0}, {FB_W, FB_H}},
         .clearValueCount = 1,
         .pClearValues = &clear,
      };
      vkCmdBeginRenderPass(t->cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
      vkCmdBindPipeline(t->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);

      VkViewport vp = {0.0f, 0.0f, (float)FB_W, (float)FB_H, 0.0f, 1.0f};
      VkRect2D sc_rect = {{0, 0}, {FB_W, FB_H}};
      vkCmdSetViewport(t->cmd, 0, 1, &vp);
      vkCmdSetScissor(t->cmd, 0, 1, &sc_rect);

      vkCmdPushConstants(t->cmd, t->layout,
                         VK_SHADER_STAGE_VERTEX_BIT |
                            VK_SHADER_STAGE_FRAGMENT_BIT,
                         0, 16, sub_checks[sc_idx].src1);

      vkCmdDraw(t->cmd, 3, 1, 0, 0);
      vkCmdEndRenderPass(t->cmd);

      cmd_copy_images_to_buffer(t, 1, &rt, rbuf);
      cmd_submit_wait(t);

      void *map_ptr = NULL;
      CK(vkMapMemory(t->dev, rmem, 0, FB_W * FB_H * 4, 0, &map_ptr),
         "MapMemory_dual");

      for (uint32_t p = 0; p < FB_W * FB_H; p++) {
         const uint8_t *got = (const uint8_t *)map_ptr + p * 4;
         const uint8_t *exp = sub_checks[sc_idx].exp;
         if (got[0] != exp[0] || got[1] != exp[1] ||
             got[2] != exp[2] || got[3] != exp[3]) {
            if (total_mismatches == 0) {
               memcpy(first_got, got, 4);
               memcpy(first_exp, exp, 4);
            }
            total_mismatches++;
         }
      }

      vkUnmapMemory(t->dev, rmem);
   }

   vkDestroyPipeline(t->dev, pipe, NULL);
   vkDestroyFramebuffer(t->dev, fb, NULL);
   vkDestroyRenderPass(t->dev, rp, NULL);
   destroy_rt_image(t, &rt);
   vkDestroyBuffer(t->dev, rbuf, NULL);
   vkFreeMemory(t->dev, rmem, NULL);

   if (total_mismatches > 0) {
      printf("FAIL case dual_source att=0 got=%u %u %u %u exp=%u %u %u %u mismatches=%u\n",
             first_got[0], first_got[1], first_got[2], first_got[3],
             first_exp[0], first_exp[1], first_exp[2], first_exp[3],
             total_mismatches);
      return 1;
   }

   printf("PASS case dual_source\n");
   return 0;
}

static int
test_logic_op(struct dx7 *t)
{
   if (!t->feats.logicOp) {
      printf("SKIP case logic_op logicOp not supported\n");
      return -1;
   }

   struct rt_image rt;
   create_rt_image(t, &rt);

   VkAttachmentDescription att = {
      .format = VK_FORMAT_R8G8B8A8_UNORM,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
   };
   VkAttachmentReference ref = {
      .attachment = 0,
      .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
   };
   VkSubpassDescription sub = {
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount = 1,
      .pColorAttachments = &ref,
   };
   VkRenderPassCreateInfo rpci = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &att,
      .subpassCount = 1,
      .pSubpasses = &sub,
   };
   VkRenderPass rp;
   CK(vkCreateRenderPass(t->dev, &rpci, NULL, &rp), "CreateRenderPass_logic");

   VkFramebufferCreateInfo fbci = {
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass = rp,
      .attachmentCount = 1,
      .pAttachments = &rt.view,
      .width = FB_W,
      .height = FB_H,
      .layers = 1,
   };
   VkFramebuffer fb;
   CK(vkCreateFramebuffer(t->dev, &fbci, NULL, &fb), "CreateFramebuffer_logic");

   VkBuffer rbuf;
   VkDeviceMemory rmem;
   dx7_buffer(t, FB_W * FB_H * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, NULL,
              &rbuf, &rmem);

   const VkLogicOp ops[5] = {
      VK_LOGIC_OP_XOR,
      VK_LOGIC_OP_AND,
      VK_LOGIC_OP_OR,
      VK_LOGIC_OP_INVERT,
      VK_LOGIC_OP_COPY_INVERTED,
   };

   const uint8_t s_bytes[4] = {0xF0, 0x0F, 0xAA, 0x55};
   const uint8_t d_bytes[4] = {0xCC, 0x33, 0x5A, 0xA5};

   uint32_t total_mismatches = 0;
   uint8_t first_got[4] = {0};
   uint8_t first_exp[4] = {0};

   for (int i = 0; i < 5; i++) {
      VkLogicOp op = ops[i];

      VkPipelineColorBlendAttachmentState ba = {
         .blendEnable = VK_FALSE,
         .colorWriteMask = 0xF,
      };
      VkPipelineColorBlendStateCreateInfo cb = {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
         .logicOpEnable = VK_TRUE,
         .logicOp = op,
         .attachmentCount = 1,
         .pAttachments = &ba,
      };

      VkPipeline pipe = create_pipeline(t, rp, vs_blend, sizeof(vs_blend),
                                        fs_logic, sizeof(fs_logic),
                                        t->layout, &cb);

      cmd_begin(t);

      VkClearValue clear = {
         .color = {.float32 = {(float)0xCC / 255.0f, (float)0x33 / 255.0f,
                               (float)0x5A / 255.0f, (float)0xA5 / 255.0f}},
      };
      VkRenderPassBeginInfo rpbi = {
         .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
         .renderPass = rp,
         .framebuffer = fb,
         .renderArea = {{0, 0}, {FB_W, FB_H}},
         .clearValueCount = 1,
         .pClearValues = &clear,
      };
      vkCmdBeginRenderPass(t->cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
      vkCmdBindPipeline(t->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);

      VkViewport vp = {0.0f, 0.0f, (float)FB_W, (float)FB_H, 0.0f, 1.0f};
      VkRect2D sc_rect = {{0, 0}, {FB_W, FB_H}};
      vkCmdSetViewport(t->cmd, 0, 1, &vp);
      vkCmdSetScissor(t->cmd, 0, 1, &sc_rect);

      vkCmdDraw(t->cmd, 3, 1, 0, 0);
      vkCmdEndRenderPass(t->cmd);

      cmd_copy_images_to_buffer(t, 1, &rt, rbuf);
      cmd_submit_wait(t);

      vkDestroyPipeline(t->dev, pipe, NULL);

      uint8_t exp[4];
      for (int c = 0; c < 4; c++) {
         switch (op) {
         case VK_LOGIC_OP_XOR: exp[c] = s_bytes[c] ^ d_bytes[c]; break;
         case VK_LOGIC_OP_AND: exp[c] = s_bytes[c] & d_bytes[c]; break;
         case VK_LOGIC_OP_OR: exp[c] = s_bytes[c] | d_bytes[c]; break;
         case VK_LOGIC_OP_INVERT: exp[c] = (uint8_t)(~d_bytes[c]); break;
         case VK_LOGIC_OP_COPY_INVERTED: exp[c] = (uint8_t)(~s_bytes[c]); break;
         default: exp[c] = 0; break;
         }
      }

      void *map_ptr = NULL;
      CK(vkMapMemory(t->dev, rmem, 0, FB_W * FB_H * 4, 0, &map_ptr),
         "MapMemory_logic");

      for (uint32_t p = 0; p < FB_W * FB_H; p++) {
         const uint8_t *got = (const uint8_t *)map_ptr + p * 4;
         if (got[0] != exp[0] || got[1] != exp[1] ||
             got[2] != exp[2] || got[3] != exp[3]) {
            if (total_mismatches == 0) {
               memcpy(first_got, got, 4);
               memcpy(first_exp, exp, 4);
            }
            total_mismatches++;
         }
      }

      vkUnmapMemory(t->dev, rmem);
   }

   vkDestroyFramebuffer(t->dev, fb, NULL);
   vkDestroyRenderPass(t->dev, rp, NULL);
   destroy_rt_image(t, &rt);
   vkDestroyBuffer(t->dev, rbuf, NULL);
   vkFreeMemory(t->dev, rmem, NULL);

   if (total_mismatches > 0) {
      printf("FAIL case logic_op att=0 got=%u %u %u %u exp=%u %u %u %u mismatches=%u\n",
             first_got[0], first_got[1], first_got[2], first_got[3],
             first_exp[0], first_exp[1], first_exp[2], first_exp[3],
             total_mismatches);
      return 1;
   }

   printf("PASS case logic_op\n");
   return 0;
}

int
main(int argc, char **argv)
{
   if (argc < 2) {
      printf("usage: %s <icd>\n", argv[0]);
      return 2;
   }

   dx7_device_hook = device_hook;

   struct dx7 t;
   dx7_init(&t, argv[1], NULL);

   load_extra_funcs(&t, argv[1]);

   printf("INFO dualSrcBlend=%d independentBlend=%d logicOp=%d maxColorAttachments=%u maxFragmentDualSrcAttachments=%u\n",
          t.feats.dualSrcBlend,
          t.feats.independentBlend,
          t.feats.logicOp,
          t.props.limits.maxColorAttachments,
          t.props.limits.maxFragmentDualSrcAttachments);

   int passes = 0, fails = 0, skips = 0;

   int r1 = test_independent_blend(&t);
   if (r1 < 0)
      skips++;
   else if (r1 > 0)
      fails++;
   else
      passes++;

   int r2 = test_dual_source(&t);
   if (r2 < 0)
      skips++;
   else if (r2 > 0)
      fails++;
   else
      passes++;

   int r3 = test_logic_op(&t);
   if (r3 < 0)
      skips++;
   else if (r3 > 0)
      fails++;
   else
      passes++;

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

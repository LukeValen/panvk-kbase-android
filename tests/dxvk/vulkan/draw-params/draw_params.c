/* DXVK draw-parameter execution test for PanVK (PanProbe).
 * Records vertex-shader outputs into an SSBO and checks them as an
 * unordered multiset. One command-buffer submit and a 10 s fence wait
 * per case.
 *
 * Cases:
 *   1. base_vertex_instance  vkCmdDrawIndexed base vertex/instance
 *   2. draw_nonindexed       vkCmdDraw base vertex/instance
 *   3. divisor               instance-rate divisor 2 (sub-case divisor0)
 *   4. maintenance5          null shader module + vkCmdBindIndexBuffer2KHR
 *   5. maintenance6          vkCmdBindDescriptorSets2KHR + vkCmdPushConstants2KHR
 *   6. load_store_op_none    LOAD/STORE NONE keeps a prior clear
 *
 * Usage: draw_params <libvulkan_panfrost.so>
 * Regenerate SPIR-V: ./build_spv.sh
 */
#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "draw_params_spv.h"

typedef PFN_vkVoidFunction (*icd_gipa_fn)(VkInstance, const char *);

enum {
   REC_CAP = 64,
   FB_W = 64,
   FB_H = 64
};

struct rec {
   uint32_t v[4];
};

/* std430: count at 0, rec[] at 16. */
struct ssbo_layout {
   uint32_t count;
   uint32_t pad[3];
   uint32_t rec[REC_CAP][4];
};

_Static_assert(offsetof(struct ssbo_layout, rec) == 16, "ssbo rec offset");

struct gpu_buf {
   VkBuffer buffer;
   VkDeviceMemory memory;
   void *map;
   VkDeviceSize size;
};

struct job {
   const char *name;
   VkPipeline pipe;
   int indexed;
   int index2;
   int desc2;
   int none_load;
   uint32_t count;
   uint32_t instances;
   uint32_t first;
   int32_t vertex_offset;
   uint32_t first_instance;
};

static struct {
   void *lib;
   icd_gipa_fn gipa;
   VkInstance inst;
   VkDevice dev;
   VkQueue queue;
   VkPhysicalDeviceMemoryProperties mp;
   VkCommandBuffer cmd;
   VkCommandPool pool;
   VkImage image;
   VkImageView view;
   VkDeviceMemory image_mem;
   VkImageLayout layout;
   struct gpu_buf ssbo, inst_buf, index_buf, pix;
   VkDescriptorSetLayout dsl;
   VkPipelineLayout pl;
   VkDescriptorPool dpool;
   VkDescriptorSet set;
   VkShaderModule vs, fs;
   VkPipeline pipe, pipe_div, pipe_div0, pipe_m5, pipe_mask0;
   PFN_vkGetDeviceProcAddr GetDeviceProcAddr;
   PFN_vkDestroyDevice DestroyDevice;
   PFN_vkDestroyInstance DestroyInstance;
   PFN_vkCreateBuffer CreateBuffer;
   PFN_vkDestroyBuffer DestroyBuffer;
   PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements;
   PFN_vkAllocateMemory AllocateMemory;
   PFN_vkFreeMemory FreeMemory;
   PFN_vkBindBufferMemory BindBufferMemory;
   PFN_vkMapMemory MapMemory;
   PFN_vkUnmapMemory UnmapMemory;
   PFN_vkCreateImage CreateImage;
   PFN_vkDestroyImage DestroyImage;
   PFN_vkGetImageMemoryRequirements GetImageMemoryRequirements;
   PFN_vkBindImageMemory BindImageMemory;
   PFN_vkCreateImageView CreateImageView;
   PFN_vkDestroyImageView DestroyImageView;
   PFN_vkCreateShaderModule CreateShaderModule;
   PFN_vkDestroyShaderModule DestroyShaderModule;
   PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout;
   PFN_vkDestroyDescriptorSetLayout DestroyDescriptorSetLayout;
   PFN_vkCreatePipelineLayout CreatePipelineLayout;
   PFN_vkDestroyPipelineLayout DestroyPipelineLayout;
   PFN_vkCreateGraphicsPipelines CreateGraphicsPipelines;
   PFN_vkDestroyPipeline DestroyPipeline;
   PFN_vkCreateDescriptorPool CreateDescriptorPool;
   PFN_vkDestroyDescriptorPool DestroyDescriptorPool;
   PFN_vkAllocateDescriptorSets AllocateDescriptorSets;
   PFN_vkUpdateDescriptorSets UpdateDescriptorSets;
   PFN_vkCreateCommandPool CreateCommandPool;
   PFN_vkDestroyCommandPool DestroyCommandPool;
   PFN_vkAllocateCommandBuffers AllocateCommandBuffers;
   PFN_vkBeginCommandBuffer BeginCommandBuffer;
   PFN_vkEndCommandBuffer EndCommandBuffer;
   PFN_vkResetCommandBuffer ResetCommandBuffer;
   PFN_vkCmdBindPipeline CmdBindPipeline;
   PFN_vkCmdBindDescriptorSets CmdBindDescriptorSets;
   PFN_vkCmdPushConstants CmdPushConstants;
   PFN_vkCmdBindVertexBuffers CmdBindVertexBuffers;
   PFN_vkCmdBindIndexBuffer CmdBindIndexBuffer;
   PFN_vkCmdDraw CmdDraw;
   PFN_vkCmdDrawIndexed CmdDrawIndexed;
   PFN_vkCmdBeginRendering CmdBeginRendering;
   PFN_vkCmdEndRendering CmdEndRendering;
   PFN_vkCmdPipelineBarrier CmdPipelineBarrier;
   PFN_vkCmdClearColorImage CmdClearColorImage;
   PFN_vkCmdCopyImageToBuffer CmdCopyImageToBuffer;
   PFN_vkCreateFence CreateFence;
   PFN_vkDestroyFence DestroyFence;
   PFN_vkQueueSubmit QueueSubmit;
   PFN_vkWaitForFences WaitForFences;
   PFN_vkCmdBindIndexBuffer2KHR CmdBindIndexBuffer2KHR;
   PFN_vkCmdBindDescriptorSets2KHR CmdBindDescriptorSets2KHR;
   PFN_vkCmdPushConstants2KHR CmdPushConstants2KHR;
} A;

static int
has_extension(const VkExtensionProperties *exts, uint32_t count, const char *name)
{
   for (uint32_t i = 0; i < count; i++) {
      if (!strcmp(exts[i].extensionName, name))
         return 1;
   }
   return 0;
}

static PFN_vkVoidFunction
devfn(const char *name)
{
   PFN_vkVoidFunction f = A.GetDeviceProcAddr(A.dev, name);
   if (!f)
      f = A.gipa(A.inst, name);
   return f;
}

static int
pick_mem(uint32_t bits, int host, uint32_t *out)
{
   uint32_t any = ~0u;
   for (uint32_t i = 0; i < A.mp.memoryTypeCount; i++) {
      if (!(bits & (1u << i)))
         continue;
      VkMemoryPropertyFlags f = A.mp.memoryTypes[i].propertyFlags;
      if (host) {
         VkMemoryPropertyFlags need = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
         if ((f & need) == need) {
            *out = i;
            return 0;
         }
      } else {
         if (any == ~0u)
            any = i;
         if (f & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
            *out = i;
            return 0;
         }
      }
   }
   if (!host && any != ~0u) {
      *out = any;
      return 0;
   }
   return 1;
}

static int
make_buf(VkDeviceSize size, VkBufferUsageFlags usage, struct gpu_buf *b)
{
   memset(b, 0, sizeof(*b));
   b->size = size;
   VkBufferCreateInfo ci = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = size,
      .usage = usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   };
   if (A.CreateBuffer(A.dev, &ci, NULL, &b->buffer) != VK_SUCCESS)
      return 1;
   VkMemoryRequirements mr;
   A.GetBufferMemoryRequirements(A.dev, b->buffer, &mr);
   uint32_t mi;
   if (pick_mem(mr.memoryTypeBits, 1, &mi))
      return 1;
   VkMemoryAllocateInfo mai = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mr.size,
      .memoryTypeIndex = mi,
   };
   if (A.AllocateMemory(A.dev, &mai, NULL, &b->memory) != VK_SUCCESS)
      return 1;
   if (A.BindBufferMemory(A.dev, b->buffer, b->memory, 0) != VK_SUCCESS)
      return 1;
   if (A.MapMemory(A.dev, b->memory, 0, size, 0, &b->map) != VK_SUCCESS)
      return 1;
   return 0;
}

static void
free_buf(struct gpu_buf *b)
{
   if (b->map && A.UnmapMemory) {
      A.UnmapMemory(A.dev, b->memory);
      b->map = NULL;
   }
   if (b->buffer && A.DestroyBuffer) {
      A.DestroyBuffer(A.dev, b->buffer, NULL);
      b->buffer = VK_NULL_HANDLE;
   }
   if (b->memory && A.FreeMemory) {
      A.FreeMemory(A.dev, b->memory, NULL);
      b->memory = VK_NULL_HANDLE;
   }
}

static void
img_barrier(VkCommandBuffer cmd, VkPipelineStageFlags src_stage,
            VkPipelineStageFlags dst_stage, VkAccessFlags src_access,
            VkAccessFlags dst_access, VkImageLayout old_l, VkImageLayout new_l)
{
   VkImageMemoryBarrier ib = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = src_access,
      .dstAccessMask = dst_access,
      .oldLayout = old_l,
      .newLayout = new_l,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = A.image,
      .subresourceRange = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .levelCount = 1,
         .layerCount = 1,
      },
   };
   A.CmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &ib);
   A.layout = new_l;
}

static void
buf_barrier(VkCommandBuffer cmd, VkBuffer buffer, VkPipelineStageFlags src_stage,
            VkPipelineStageFlags dst_stage, VkAccessFlags src_access,
            VkAccessFlags dst_access)
{
   VkBufferMemoryBarrier bb = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .srcAccessMask = src_access,
      .dstAccessMask = dst_access,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .buffer = buffer,
      .size = VK_WHOLE_SIZE,
   };
   A.CmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, NULL, 1, &bb, 0, NULL);
}

static int
make_pipe(uint32_t divisor, int use_div, VkColorComponentFlags mask, int null_mod,
          VkPipeline *out)
{
   VkShaderModuleCreateInfo smv = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = sizeof(draw_params_vert_spv),
      .pCode = draw_params_vert_spv,
   };
   VkShaderModuleCreateInfo smf = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = sizeof(draw_params_frag_spv),
      .pCode = draw_params_frag_spv,
   };
   VkPipelineShaderStageCreateInfo stages[2] = {
      {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .pNext = null_mod ? &smv : NULL,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = null_mod ? VK_NULL_HANDLE : A.vs,
         .pName = "main",
      },
      {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .pNext = null_mod ? &smf : NULL,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = null_mod ? VK_NULL_HANDLE : A.fs,
         .pName = "main",
      },
   };
   VkVertexInputBindingDescription bind = {
      .binding = 0,
      .stride = 4,
      .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
   };
   VkVertexInputAttributeDescription attr = {
      .location = 0,
      .binding = 0,
      .format = VK_FORMAT_R32_UINT,
      .offset = 0,
   };
   VkVertexInputBindingDivisorDescriptionKHR div_desc = {
      .binding = 0,
      .divisor = divisor,
   };
   VkPipelineVertexInputDivisorStateCreateInfoKHR div_ci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_KHR,
      .vertexBindingDivisorCount = 1,
      .pVertexBindingDivisors = &div_desc,
   };
   VkPipelineVertexInputStateCreateInfo vis = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .pNext = use_div ? &div_ci : NULL,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &bind,
      .vertexAttributeDescriptionCount = 1,
      .pVertexAttributeDescriptions = &attr,
   };
   VkPipelineInputAssemblyStateCreateInfo ia = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
   };
   VkViewport vp = { 0.0f, 0.0f, (float)FB_W, (float)FB_H, 0.0f, 1.0f };
   VkRect2D sc = { { 0, 0 }, { FB_W, FB_H } };
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
   VkPipelineColorBlendAttachmentState cba = { .colorWriteMask = mask };
   VkPipelineColorBlendStateCreateInfo cb = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &cba,
   };
   VkFormat fmt = VK_FORMAT_R8G8B8A8_UNORM;
   VkPipelineRenderingCreateInfo rend = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &fmt,
   };
   VkGraphicsPipelineCreateInfo gp = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &rend,
      .stageCount = 2,
      .pStages = stages,
      .pVertexInputState = &vis,
      .pInputAssemblyState = &ia,
      .pViewportState = &vps,
      .pRasterizationState = &rs,
      .pMultisampleState = &ms,
      .pColorBlendState = &cb,
      .layout = A.pl,
      .renderPass = VK_NULL_HANDLE,
   };
   return A.CreateGraphicsPipelines(A.dev, VK_NULL_HANDLE, 1, &gp, NULL, out) != VK_SUCCESS;
}

static void
bind_and_draw(VkCommandBuffer cmd, const struct job *j)
{
   VkDeviceSize off = 0;
   uint32_t tag = 1;
   A.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, j->pipe);
   A.CmdBindVertexBuffers(cmd, 0, 1, &A.inst_buf.buffer, &off);
   if (j->desc2) {
      VkBindDescriptorSetsInfoKHR bi = {
         .sType = VK_STRUCTURE_TYPE_BIND_DESCRIPTOR_SETS_INFO_KHR,
         .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
         .layout = A.pl,
         .descriptorSetCount = 1,
         .pDescriptorSets = &A.set,
      };
      VkPushConstantsInfoKHR pi = {
         .sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO_KHR,
         .layout = A.pl,
         .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
         .size = sizeof(tag),
         .pValues = &tag,
      };
      A.CmdBindDescriptorSets2KHR(cmd, &bi);
      A.CmdPushConstants2KHR(cmd, &pi);
   } else {
      A.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, A.pl, 0, 1, &A.set, 0, NULL);
      A.CmdPushConstants(cmd, A.pl, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(tag), &tag);
   }
   if (j->indexed) {
      if (j->index2)
         A.CmdBindIndexBuffer2KHR(cmd, A.index_buf.buffer, 0, 8, VK_INDEX_TYPE_UINT16);
      else
         A.CmdBindIndexBuffer(cmd, A.index_buf.buffer, 0, VK_INDEX_TYPE_UINT16);
      A.CmdDrawIndexed(cmd, j->count, j->instances, j->first, j->vertex_offset, j->first_instance);
   } else {
      A.CmdDraw(cmd, j->count, j->instances, j->first, j->first_instance);
   }
}

static void
begin_rendering(VkCommandBuffer cmd, VkAttachmentLoadOp load_op, VkAttachmentStoreOp store_op)
{
   VkClearValue clear = { 0 };
   clear.color.float32[0] = 0.0f;
   clear.color.float32[1] = 0.0f;
   clear.color.float32[2] = 0.0f;
   clear.color.float32[3] = 1.0f;
   VkRenderingAttachmentInfo att = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = A.view,
      .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .loadOp = load_op,
      .storeOp = store_op,
      .clearValue = clear,
   };
   VkRenderingInfo ri = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = { { 0, 0 }, { FB_W, FB_H } },
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = &att,
   };
   A.CmdBeginRendering(cmd, &ri);
}

/* Record, submit, and fence-wait one case. API failure stops the test. */
static int
record_submit_wait(const struct job *j, struct rec *got, uint32_t *ngot, uint8_t *pix)
{
   volatile struct ssbo_layout *s = A.ssbo.map;
   VkResult r;

   if (j->index2 && !A.CmdBindIndexBuffer2KHR) {
      printf("FAIL case %s vkCmdBindIndexBuffer2KHR missing\n", j->name);
      return 1;
   }
   if (j->desc2 && (!A.CmdBindDescriptorSets2KHR || !A.CmdPushConstants2KHR)) {
      printf("FAIL case %s maintenance6 command missing\n", j->name);
      return 1;
   }

   s->count = 0;
   r = A.ResetCommandBuffer(A.cmd, 0);
   if (r != VK_SUCCESS) {
      printf("FAIL case %s reset r=%d\n", j->name, (int)r);
      return 1;
   }
   VkCommandBufferBeginInfo bi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
   };
   r = A.BeginCommandBuffer(A.cmd, &bi);
   if (r != VK_SUCCESS) {
      printf("FAIL case %s begin r=%d\n", j->name, (int)r);
      return 1;
   }

   if (j->none_load) {
      VkPipelineStageFlags src_stage = A.layout == VK_IMAGE_LAYOUT_UNDEFINED
                                          ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                          : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      VkAccessFlags src_access = A.layout == VK_IMAGE_LAYOUT_UNDEFINED
                                    ? 0
                                    : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      img_barrier(A.cmd, src_stage, VK_PIPELINE_STAGE_TRANSFER_BIT, src_access,
                  VK_ACCESS_TRANSFER_WRITE_BIT, A.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
      VkClearColorValue col;
      col.float32[0] = 1.0f;
      col.float32[1] = 0.0f;
      col.float32[2] = 1.0f;
      col.float32[3] = 1.0f;
      VkImageSubresourceRange range = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .levelCount = 1,
         .layerCount = 1,
      };
      A.CmdClearColorImage(A.cmd, A.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &col, 1, &range);
      img_barrier(A.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
   } else if (A.layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
      img_barrier(A.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, A.layout,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
   }

   buf_barrier(A.cmd, A.ssbo.buffer, VK_PIPELINE_STAGE_HOST_BIT,
               VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_ACCESS_HOST_WRITE_BIT,
               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
   buf_barrier(A.cmd, A.inst_buf.buffer, VK_PIPELINE_STAGE_HOST_BIT,
               VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_HOST_WRITE_BIT,
               VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);
   if (j->indexed)
      buf_barrier(A.cmd, A.index_buf.buffer, VK_PIPELINE_STAGE_HOST_BIT,
                  VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_HOST_WRITE_BIT,
                  VK_ACCESS_INDEX_READ_BIT);

   if (j->none_load)
      begin_rendering(A.cmd, VK_ATTACHMENT_LOAD_OP_NONE_KHR, VK_ATTACHMENT_STORE_OP_NONE);
   else
      begin_rendering(A.cmd, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE);
   bind_and_draw(A.cmd, j);
   A.CmdEndRendering(A.cmd);

   buf_barrier(A.cmd, A.ssbo.buffer, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
               VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_SHADER_WRITE_BIT,
               VK_ACCESS_HOST_READ_BIT);

   if (j->none_load) {
      img_barrier(A.cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
      VkBufferImageCopy region = {
         .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
         },
         .imageExtent = { FB_W, FB_H, 1 },
      };
      A.CmdCopyImageToBuffer(A.cmd, A.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             A.pix.buffer, 1, &region);
      buf_barrier(A.cmd, A.pix.buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_ACCESS_HOST_READ_BIT);
   }

   r = A.EndCommandBuffer(A.cmd);
   if (r != VK_SUCCESS) {
      printf("FAIL case %s end r=%d\n", j->name, (int)r);
      return 1;
   }

   VkFence fence = VK_NULL_HANDLE;
   VkFenceCreateInfo fi = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
   r = A.CreateFence(A.dev, &fi, NULL, &fence);
   if (r != VK_SUCCESS) {
      printf("FAIL case %s fence r=%d\n", j->name, (int)r);
      return 1;
   }
   VkSubmitInfo si = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers = &A.cmd,
   };
   r = A.QueueSubmit(A.queue, 1, &si, fence);
   if (r != VK_SUCCESS) {
      A.DestroyFence(A.dev, fence, NULL);
      printf("FAIL case %s submit r=%d\n", j->name, (int)r);
      return 1;
   }
   r = A.WaitForFences(A.dev, 1, &fence, VK_TRUE, 10000000000ull); /* 10 s */
   if (r != VK_SUCCESS) {
      printf("FAIL case %s wait r=%d\n", j->name, (int)r);
      printf("RESULT FAIL\n");
      exit(1);
   }
   A.DestroyFence(A.dev, fence, NULL);

   __asm__ __volatile__("" ::: "memory");
   *ngot = s->count;
   uint32_t ncopy = *ngot > REC_CAP ? REC_CAP : *ngot;
   for (uint32_t i = 0; i < ncopy; i++) {
      for (int k = 0; k < 4; k++)
         got[i].v[k] = s->rec[i][k];
   }
   if (pix) {
      uint8_t *src = A.pix.map;
      memcpy(pix, src, (size_t)FB_W * FB_H * 4u);
   }
   return 0;
}

static void
check_recs(const char *name, const struct rec *got, uint32_t ng, const struct rec *exp,
           uint32_t ne, const uint8_t *pix, int *passes, int *fails)
{
   uint8_t used[REC_CAP];
   struct rec miss;
   uint32_t nseen = ng > REC_CAP ? REC_CAP : ng;
   int missing = 0;
   int badpix = 0;
   uint32_t bx = 0, by = 0, br = 0, bg = 0, bb = 0, ba = 0;

   memset(used, 0, sizeof(used));
   memset(&miss, 0, sizeof(miss));
   for (uint32_t i = 0; i < ne; i++) {
      int found = 0;
      for (uint32_t g = 0; g < nseen; g++) {
         if (used[g])
            continue;
         if (got[g].v[0] == exp[i].v[0] && got[g].v[1] == exp[i].v[1] &&
             got[g].v[2] == exp[i].v[2] && got[g].v[3] == exp[i].v[3]) {
            used[g] = 1;
            found = 1;
            break;
         }
      }
      if (!found && !missing) {
         missing = 1;
         miss = exp[i];
      }
   }
   if (!missing && ng != ne) {
      for (uint32_t g = 0; g < nseen; g++) {
         if (!used[g]) {
            missing = 1;
            miss = got[g];
            break;
         }
      }
   }
   if (pix) {
      for (uint32_t y = 0; y < FB_H && !badpix; y++) {
         for (uint32_t x = 0; x < FB_W; x++) {
            const uint8_t *p = pix + ((size_t)y * FB_W + x) * 4u;
            if (p[0] != 255 || p[1] != 0 || p[2] != 255 || p[3] != 255) {
               badpix = 1;
               bx = x;
               by = y;
               br = p[0];
               bg = p[1];
               bb = p[2];
               ba = p[3];
               break;
            }
         }
      }
   }
   if (!missing && ng == ne && !badpix) {
      printf("PASS case %s\n", name);
      (*passes)++;
      return;
   }
   if (pix && badpix && (missing || ng != ne)) {
      printf("FAIL case %s got_count=%u exp_count=%u first_missing=(%u,%u,%u,%u) "
             "bad_pixel=(%u,%u,%u,%u,%u,%u)\n",
             name, ng, ne, miss.v[0], miss.v[1], miss.v[2], miss.v[3], bx, by, br, bg, bb, ba);
   } else if (pix && badpix) {
      printf("FAIL case %s got_count=%u exp_count=%u bad_pixel=(%u,%u,%u,%u,%u,%u)\n",
             name, ng, ne, bx, by, br, bg, bb, ba);
   } else {
      printf("FAIL case %s got_count=%u exp_count=%u first_missing=(%u,%u,%u,%u)\n",
             name, ng, ne, miss.v[0], miss.v[1], miss.v[2], miss.v[3]);
   }
   (*fails)++;
}

static uint32_t
pack_base(uint32_t base_vertex, uint32_t base_instance)
{
   return (base_vertex & 0xffffu) | (base_instance << 16);
}

static void
fill_indexed(struct rec *e, uint32_t *n)
{
   uint32_t c = 0;
   for (uint32_t i = 7; i <= 9; i++) {
      for (uint32_t k = 0; k < 4; k++) {
         e[c].v[0] = 100 + k;
         e[c].v[1] = i;
         e[c].v[2] = pack_base(100, 7);
         e[c].v[3] = 1000 + i;
         c++;
      }
   }
   *n = c;
}

static void
fill_draw(struct rec *e, uint32_t *n)
{
   uint32_t c = 0;
   for (uint32_t i = 2; i <= 3; i++) {
      for (uint32_t k = 0; k < 3; k++) {
         e[c].v[0] = 5 + k;
         e[c].v[1] = i;
         e[c].v[2] = pack_base(5, 2);
         e[c].v[3] = 1000 + i;
         c++;
      }
   }
   *n = c;
}

static void
fill_div(struct rec *e, uint32_t *n, uint32_t divisor)
{
   uint32_t c = 0;
   for (uint32_t i = 4; i <= 9; i++) {
      uint32_t attr = divisor == 0 ? 1004u : 1004u + (i - 4u) / divisor;
      e[c].v[0] = 0;
      e[c].v[1] = i;
      e[c].v[2] = pack_base(0, 4);
      e[c].v[3] = attr;
      c++;
   }
   *n = c;
}

static int
run_job(const struct job *j, struct rec *exp, uint32_t ne, int want_pix, int *passes, int *fails)
{
   struct rec got[REC_CAP];
   uint32_t ng = 0;
   static uint8_t pix[FB_W * FB_H * 4];
   if (record_submit_wait(j, got, &ng, want_pix ? pix : NULL))
      return 1;
   check_recs(j->name, got, ng, exp, ne, want_pix ? pix : NULL, passes, fails);
   return 0;
}

int
main(int argc, char **argv)
{
   if (argc < 2) {
      printf("usage: draw_params <libvulkan_panfrost.so>\n");
      return 2;
   }
   setvbuf(stdout, NULL, _IONBF, 0);

   A.lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
   if (!A.lib) {
      printf("FAIL dlopen %s\n", dlerror());
      printf("RESULT FAIL\n");
      return 1;
   }
   A.gipa = (icd_gipa_fn)dlsym(A.lib, "vk_icdGetInstanceProcAddr");
   if (!A.gipa)
      A.gipa = (icd_gipa_fn)dlsym(A.lib, "vkGetInstanceProcAddr");
   if (!A.gipa) {
      printf("FAIL gipa\n");
      printf("RESULT FAIL\n");
      dlclose(A.lib);
      return 1;
   }

   PFN_vkCreateInstance CreateInstance =
      (PFN_vkCreateInstance)A.gipa(NULL, "vkCreateInstance");
   if (!CreateInstance) {
      printf("FAIL missing vkCreateInstance\n");
      printf("RESULT FAIL\n");
      dlclose(A.lib);
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
   VkResult r = CreateInstance(&ici, NULL, &A.inst);
   if (r != VK_SUCCESS) {
      ici.enabledExtensionCount = 0;
      ici.ppEnabledExtensionNames = NULL;
      r = CreateInstance(&ici, NULL, &A.inst);
   }
   if (r != VK_SUCCESS) {
      printf("FAIL CreateInstance r=%d\n", (int)r);
      printf("RESULT FAIL\n");
      dlclose(A.lib);
      return 1;
   }

#define GI(n)                                                                  \
   PFN_vk##n p_##n = (PFN_vk##n)A.gipa(A.inst, "vk" #n);                      \
   if (!p_##n) {                                                               \
      printf("FAIL missing vk" #n "\n");                                       \
      printf("RESULT FAIL\n");                                                 \
      PFN_vkDestroyInstance destroy_inst =                                     \
         (PFN_vkDestroyInstance)A.gipa(A.inst, "vkDestroyInstance");           \
      if (destroy_inst)                                                        \
         destroy_inst(A.inst, NULL);                                           \
      dlclose(A.lib);                                                          \
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
   A.GetDeviceProcAddr = p_GetDeviceProcAddr;
   A.DestroyDevice = p_DestroyDevice;
   A.DestroyInstance = p_DestroyInstance;

   uint32_t nd = 0;
   if (p_EnumeratePhysicalDevices(A.inst, &nd, NULL) != VK_SUCCESS || nd == 0) {
      printf("FAIL EnumeratePhysicalDevices count=0\n");
      printf("RESULT FAIL\n");
      p_DestroyInstance(A.inst, NULL);
      dlclose(A.lib);
      return 1;
   }
   VkPhysicalDevice *devs = calloc(nd, sizeof(*devs));
   if (!devs) {
      printf("FAIL oom\n");
      printf("RESULT FAIL\n");
      p_DestroyInstance(A.inst, NULL);
      dlclose(A.lib);
      return 1;
   }
   p_EnumeratePhysicalDevices(A.inst, &nd, devs);
   VkPhysicalDevice phys = VK_NULL_HANDLE;
   for (uint32_t i = 0; i < nd; i++) {
      VkPhysicalDeviceProperties p;
      p_GetPhysicalDeviceProperties(devs[i], &p);
      printf("PHYS %u %s vendor=0x%x device=0x%x\n", i, p.deviceName, p.vendorID, p.deviceID);
      if (strstr(p.deviceName, "Mali"))
         phys = devs[i];
   }
   if (!phys)
      phys = devs[0];
   free(devs);

   uint32_t ext_count = 0;
   p_EnumerateDeviceExtensionProperties(phys, NULL, &ext_count, NULL);
   VkExtensionProperties *exts = NULL;
   if (ext_count) {
      exts = calloc(ext_count, sizeof(*exts));
      if (!exts) {
         printf("FAIL oom\n");
         printf("RESULT FAIL\n");
         p_DestroyInstance(A.inst, NULL);
         dlclose(A.lib);
         return 1;
      }
      p_EnumerateDeviceExtensionProperties(phys, NULL, &ext_count, exts);
   }
   int has_div_khr = has_extension(exts, ext_count, "VK_KHR_vertex_attribute_divisor");
   int has_div_ext = has_extension(exts, ext_count, "VK_EXT_vertex_attribute_divisor");
   int has_m5 = has_extension(exts, ext_count, "VK_KHR_maintenance5");
   int has_m6 = has_extension(exts, ext_count, "VK_KHR_maintenance6");
   int has_ls_khr = has_extension(exts, ext_count, "VK_KHR_load_store_op_none");
   int has_ls_ext = has_extension(exts, ext_count, "VK_EXT_load_store_op_none");
   free(exts);

   VkPhysicalDeviceVulkan11Features q11 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
   };
   VkPhysicalDeviceVulkan13Features q13 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
   };
   VkPhysicalDeviceVertexAttributeDivisorFeaturesKHR qdiv = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_KHR,
   };
   VkPhysicalDeviceMaintenance5FeaturesKHR qm5 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR,
   };
   VkPhysicalDeviceMaintenance6FeaturesKHR qm6 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_FEATURES_KHR,
   };
   void **qt = &q11.pNext;
   *qt = &q13;
   qt = &q13.pNext;
   if (has_div_khr || has_div_ext) {
      *qt = &qdiv;
      qt = &qdiv.pNext;
   }
   if (has_m5) {
      *qt = &qm5;
      qt = &qm5.pNext;
   }
   if (has_m6) {
      *qt = &qm6;
      qt = &qm6.pNext;
   }
   (void)qt;
   VkPhysicalDeviceFeatures2 qf = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &q11,
   };
   p_GetPhysicalDeviceFeatures2(phys, &qf);

   int skip_all = 0;
   if (!qf.features.vertexPipelineStoresAndAtomics) {
      printf("SKIP draw_params vertexPipelineStoresAndAtomics missing\n");
      skip_all = 1;
   }
   if (!q11.shaderDrawParameters) {
      printf("SKIP draw_params shaderDrawParameters missing\n");
      skip_all = 1;
   }
   if (skip_all) {
      printf("RESULT SKIP\n");
      p_DestroyInstance(A.inst, NULL);
      dlclose(A.lib);
      return 0;
   }
   if (!q13.dynamicRendering) {
      printf("FAIL dynamicRendering missing\n");
      printf("RESULT FAIL\n");
      p_DestroyInstance(A.inst, NULL);
      dlclose(A.lib);
      return 1;
   }

   int can_div = (has_div_khr || has_div_ext) && qdiv.vertexAttributeInstanceRateDivisor;
   int can_div0 = can_div && qdiv.vertexAttributeInstanceRateZeroDivisor;
   int can_m5 = has_m5 && qm5.maintenance5;
   int can_m6 = has_m6 && qm6.maintenance6;
   int can_ls = has_ls_khr || has_ls_ext;

   const char *dev_exts[4];
   uint32_t dev_ext_count = 0;
   if (can_div)
      dev_exts[dev_ext_count++] = has_div_khr ? "VK_KHR_vertex_attribute_divisor"
                                              : "VK_EXT_vertex_attribute_divisor";
   if (can_m5)
      dev_exts[dev_ext_count++] = "VK_KHR_maintenance5";
   if (can_m6)
      dev_exts[dev_ext_count++] = "VK_KHR_maintenance6";
   if (can_ls)
      dev_exts[dev_ext_count++] = has_ls_khr ? "VK_KHR_load_store_op_none"
                                             : "VK_EXT_load_store_op_none";

   VkPhysicalDeviceFeatures2 feat2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
   };
   feat2.features.vertexPipelineStoresAndAtomics = VK_TRUE;
   VkPhysicalDeviceVulkan11Features f11 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
      .shaderDrawParameters = VK_TRUE,
   };
   VkPhysicalDeviceVulkan13Features f13 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .dynamicRendering = VK_TRUE,
   };
   VkPhysicalDeviceVertexAttributeDivisorFeaturesKHR fdiv = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_KHR,
      .vertexAttributeInstanceRateDivisor = can_div ? VK_TRUE : VK_FALSE,
      .vertexAttributeInstanceRateZeroDivisor = can_div0 ? VK_TRUE : VK_FALSE,
   };
   VkPhysicalDeviceMaintenance5FeaturesKHR fm5 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR,
      .maintenance5 = VK_TRUE,
   };
   VkPhysicalDeviceMaintenance6FeaturesKHR fm6 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_FEATURES_KHR,
      .maintenance6 = VK_TRUE,
   };
   feat2.pNext = &f11;
   f11.pNext = &f13;
   void **dt = &f13.pNext;
   if (can_div) {
      *dt = &fdiv;
      dt = &fdiv.pNext;
   }
   if (can_m5) {
      *dt = &fm5;
      dt = &fm5.pNext;
   }
   if (can_m6) {
      *dt = &fm6;
      dt = &fm6.pNext;
   }
   (void)dt;

   uint32_t qn = 0;
   p_GetPhysicalDeviceQueueFamilyProperties(phys, &qn, NULL);
   VkQueueFamilyProperties *qp = calloc(qn ? qn : 1, sizeof(*qp));
   if (!qp) {
      printf("FAIL oom\n");
      printf("RESULT FAIL\n");
      p_DestroyInstance(A.inst, NULL);
      dlclose(A.lib);
      return 1;
   }
   p_GetPhysicalDeviceQueueFamilyProperties(phys, &qn, qp);
   uint32_t qi = ~0u;
   for (uint32_t i = 0; i < qn; i++) {
      if (qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
         qi = i;
         break;
      }
   }
   free(qp);
   if (qi == ~0u) {
      printf("FAIL no graphics queue\n");
      printf("RESULT FAIL\n");
      p_DestroyInstance(A.inst, NULL);
      dlclose(A.lib);
      return 1;
   }
   p_GetPhysicalDeviceMemoryProperties(phys, &A.mp);

   float prio = 1.0f;
   VkDeviceQueueCreateInfo qci = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = qi,
      .queueCount = 1,
      .pQueuePriorities = &prio,
   };
   VkDeviceCreateInfo dci = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &feat2,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &qci,
      .enabledExtensionCount = dev_ext_count,
      .ppEnabledExtensionNames = dev_ext_count ? dev_exts : NULL,
   };
   if (p_CreateDevice(phys, &dci, NULL, &A.dev) != VK_SUCCESS) {
      printf("FAIL CreateDevice\n");
      printf("RESULT FAIL\n");
      p_DestroyInstance(A.inst, NULL);
      dlclose(A.lib);
      return 1;
   }

   int passes = 0;
   int fails = 0;
   int hard = 0;

#define GD(n)                                                                  \
   do {                                                                        \
      A.n = (PFN_vk##n)devfn("vk" #n);                                        \
      if (!A.n) {                                                              \
         printf("FAIL missing device vk" #n "\n");                             \
         hard = 1;                                                             \
         goto fail;                                                            \
      }                                                                        \
   } while (0)
   GD(CreateBuffer);
   GD(DestroyBuffer);
   GD(GetBufferMemoryRequirements);
   GD(AllocateMemory);
   GD(FreeMemory);
   GD(BindBufferMemory);
   GD(MapMemory);
   GD(UnmapMemory);
   GD(CreateImage);
   GD(DestroyImage);
   GD(GetImageMemoryRequirements);
   GD(BindImageMemory);
   GD(CreateImageView);
   GD(DestroyImageView);
   GD(CreateShaderModule);
   GD(DestroyShaderModule);
   GD(CreateDescriptorSetLayout);
   GD(DestroyDescriptorSetLayout);
   GD(CreatePipelineLayout);
   GD(DestroyPipelineLayout);
   GD(CreateGraphicsPipelines);
   GD(DestroyPipeline);
   GD(CreateDescriptorPool);
   GD(DestroyDescriptorPool);
   GD(AllocateDescriptorSets);
   GD(UpdateDescriptorSets);
   GD(CreateCommandPool);
   GD(DestroyCommandPool);
   GD(AllocateCommandBuffers);
   GD(BeginCommandBuffer);
   GD(EndCommandBuffer);
   GD(ResetCommandBuffer);
   GD(CmdBindPipeline);
   GD(CmdBindDescriptorSets);
   GD(CmdPushConstants);
   GD(CmdBindVertexBuffers);
   GD(CmdBindIndexBuffer);
   GD(CmdDraw);
   GD(CmdDrawIndexed);
   GD(CmdPipelineBarrier);
   GD(CmdClearColorImage);
   GD(CmdCopyImageToBuffer);
   GD(CreateFence);
   GD(DestroyFence);
   GD(QueueSubmit);
   GD(WaitForFences);
#undef GD

   A.CmdBeginRendering = (PFN_vkCmdBeginRendering)devfn("vkCmdBeginRendering");
   if (!A.CmdBeginRendering)
      A.CmdBeginRendering = (PFN_vkCmdBeginRendering)devfn("vkCmdBeginRenderingKHR");
   A.CmdEndRendering = (PFN_vkCmdEndRendering)devfn("vkCmdEndRendering");
   if (!A.CmdEndRendering)
      A.CmdEndRendering = (PFN_vkCmdEndRendering)devfn("vkCmdEndRenderingKHR");
   if (!A.CmdBeginRendering || !A.CmdEndRendering) {
      printf("FAIL missing device vkCmdBeginRendering\n");
      hard = 1;
      goto fail;
   }
   A.CmdBindIndexBuffer2KHR =
      (PFN_vkCmdBindIndexBuffer2KHR)devfn("vkCmdBindIndexBuffer2KHR");
   if (!A.CmdBindIndexBuffer2KHR)
      A.CmdBindIndexBuffer2KHR =
         (PFN_vkCmdBindIndexBuffer2KHR)devfn("vkCmdBindIndexBuffer2");
   A.CmdBindDescriptorSets2KHR =
      (PFN_vkCmdBindDescriptorSets2KHR)devfn("vkCmdBindDescriptorSets2KHR");
   if (!A.CmdBindDescriptorSets2KHR)
      A.CmdBindDescriptorSets2KHR =
         (PFN_vkCmdBindDescriptorSets2KHR)devfn("vkCmdBindDescriptorSets2");
   A.CmdPushConstants2KHR = (PFN_vkCmdPushConstants2KHR)devfn("vkCmdPushConstants2KHR");
   if (!A.CmdPushConstants2KHR)
      A.CmdPushConstants2KHR = (PFN_vkCmdPushConstants2KHR)devfn("vkCmdPushConstants2");

   {
      PFN_vkGetDeviceQueue getq = (PFN_vkGetDeviceQueue)devfn("vkGetDeviceQueue");
      if (!getq) {
         printf("FAIL missing device vkGetDeviceQueue\n");
         hard = 1;
         goto fail;
      }
      getq(A.dev, qi, 0, &A.queue);
   }

   VkColorComponentFlags rgba = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

   if (make_buf(sizeof(struct ssbo_layout), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &A.ssbo) ||
       make_buf(64 * sizeof(uint32_t), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &A.inst_buf) ||
       make_buf(8, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &A.index_buf) ||
       make_buf((VkDeviceSize)FB_W * FB_H * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &A.pix)) {
      printf("FAIL buffer creation\n");
      hard = 1;
      goto fail;
   }
   {
      uint32_t *inst = A.inst_buf.map;
      uint16_t *idx = A.index_buf.map;
      for (uint32_t i = 0; i < 64; i++)
         inst[i] = 1000 + i;
      idx[0] = 0;
      idx[1] = 1;
      idx[2] = 2;
      idx[3] = 3;
      memset(A.ssbo.map, 0, sizeof(struct ssbo_layout));
   }

   VkImageCreateInfo imi = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = VK_FORMAT_R8G8B8A8_UNORM,
      .extent = { FB_W, FB_H, 1 },
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
               VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };
   if (A.CreateImage(A.dev, &imi, NULL, &A.image) != VK_SUCCESS) {
      printf("FAIL CreateImage\n");
      hard = 1;
      goto fail;
   }
   {
      VkMemoryRequirements mr;
      uint32_t mi;
      A.GetImageMemoryRequirements(A.dev, A.image, &mr);
      if (pick_mem(mr.memoryTypeBits, 0, &mi)) {
         printf("FAIL image memory type\n");
         hard = 1;
         goto fail;
      }
      VkMemoryAllocateInfo mai = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
         .allocationSize = mr.size,
         .memoryTypeIndex = mi,
      };
      if (A.AllocateMemory(A.dev, &mai, NULL, &A.image_mem) != VK_SUCCESS ||
          A.BindImageMemory(A.dev, A.image, A.image_mem, 0) != VK_SUCCESS) {
         printf("FAIL image memory\n");
         hard = 1;
         goto fail;
      }
   }
   VkImageViewCreateInfo ivi = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = A.image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = VK_FORMAT_R8G8B8A8_UNORM,
      .subresourceRange = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .levelCount = 1,
         .layerCount = 1,
      },
   };
   if (A.CreateImageView(A.dev, &ivi, NULL, &A.view) != VK_SUCCESS) {
      printf("FAIL CreateImageView\n");
      hard = 1;
      goto fail;
   }
   A.layout = VK_IMAGE_LAYOUT_UNDEFINED;

   VkDescriptorSetLayoutBinding binding = {
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
   };
   VkDescriptorSetLayoutCreateInfo dlci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 1,
      .pBindings = &binding,
   };
   if (A.CreateDescriptorSetLayout(A.dev, &dlci, NULL, &A.dsl) != VK_SUCCESS) {
      printf("FAIL CreateDescriptorSetLayout\n");
      hard = 1;
      goto fail;
   }
   VkPushConstantRange pcr = { VK_SHADER_STAGE_VERTEX_BIT, 0, 4 };
   VkPipelineLayoutCreateInfo plci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &A.dsl,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &pcr,
   };
   if (A.CreatePipelineLayout(A.dev, &plci, NULL, &A.pl) != VK_SUCCESS) {
      printf("FAIL CreatePipelineLayout\n");
      hard = 1;
      goto fail;
   }
   VkDescriptorPoolSize psz = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 };
   VkDescriptorPoolCreateInfo dpci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1,
      .poolSizeCount = 1,
      .pPoolSizes = &psz,
   };
   if (A.CreateDescriptorPool(A.dev, &dpci, NULL, &A.dpool) != VK_SUCCESS) {
      printf("FAIL CreateDescriptorPool\n");
      hard = 1;
      goto fail;
   }
   VkDescriptorSetAllocateInfo dsai = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = A.dpool,
      .descriptorSetCount = 1,
      .pSetLayouts = &A.dsl,
   };
   if (A.AllocateDescriptorSets(A.dev, &dsai, &A.set) != VK_SUCCESS) {
      printf("FAIL AllocateDescriptorSets\n");
      hard = 1;
      goto fail;
   }
   VkDescriptorBufferInfo dbi = {
      .buffer = A.ssbo.buffer,
      .range = VK_WHOLE_SIZE,
   };
   VkWriteDescriptorSet wr = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = A.set,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &dbi,
   };
   A.UpdateDescriptorSets(A.dev, 1, &wr, 0, NULL);

   VkShaderModuleCreateInfo smv = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = sizeof(draw_params_vert_spv),
      .pCode = draw_params_vert_spv,
   };
   VkShaderModuleCreateInfo smf = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = sizeof(draw_params_frag_spv),
      .pCode = draw_params_frag_spv,
   };
   if (A.CreateShaderModule(A.dev, &smv, NULL, &A.vs) != VK_SUCCESS ||
       A.CreateShaderModule(A.dev, &smf, NULL, &A.fs) != VK_SUCCESS) {
      printf("FAIL CreateShaderModule\n");
      hard = 1;
      goto fail;
   }
   if (make_pipe(1, 0, rgba, 0, &A.pipe)) {
      printf("FAIL pipeline base\n");
      hard = 1;
      goto fail;
   }

   VkCommandPoolCreateInfo cpci = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = qi,
   };
   if (A.CreateCommandPool(A.dev, &cpci, NULL, &A.pool) != VK_SUCCESS) {
      printf("FAIL CreateCommandPool\n");
      hard = 1;
      goto fail;
   }
   VkCommandBufferAllocateInfo cbai = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = A.pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
   };
   if (A.AllocateCommandBuffers(A.dev, &cbai, &A.cmd) != VK_SUCCESS) {
      printf("FAIL AllocateCommandBuffers\n");
      hard = 1;
      goto fail;
   }

   struct rec exp[16];
   uint32_t ne = 0;

   fill_indexed(exp, &ne);
   {
      struct job j = {
         .name = "base_vertex_instance",
         .pipe = A.pipe,
         .indexed = 1,
         .count = 4,
         .instances = 3,
         .first = 0,
         .vertex_offset = 100,
         .first_instance = 7,
      };
      if (run_job(&j, exp, ne, 0, &passes, &fails)) {
         hard = 1;
         goto fail;
      }
   }

   fill_draw(exp, &ne);
   {
      struct job j = {
         .name = "draw_nonindexed",
         .pipe = A.pipe,
         .count = 3,
         .instances = 2,
         .first = 5,
         .first_instance = 2,
      };
      if (run_job(&j, exp, ne, 0, &passes, &fails)) {
         hard = 1;
         goto fail;
      }
   }

   if (!can_div) {
      printf("SKIP case divisor vertexAttributeInstanceRateDivisor missing\n");
   } else if (make_pipe(2, 1, rgba, 0, &A.pipe_div)) {
      printf("FAIL case divisor pipeline\n");
      fails++;
   } else {
      struct job j = {
         .name = "divisor",
         .pipe = A.pipe_div,
         .count = 1,
         .instances = 6,
         .first = 0,
         .first_instance = 4,
      };
      fill_div(exp, &ne, 2);
      if (run_job(&j, exp, ne, 0, &passes, &fails)) {
         hard = 1;
         goto fail;
      }
   }
   if (!can_div0) {
      if (can_div)
         printf("SKIP case divisor0 vertexAttributeInstanceRateZeroDivisor missing\n");
   } else if (make_pipe(0, 1, rgba, 0, &A.pipe_div0)) {
      printf("FAIL case divisor0 pipeline\n");
      fails++;
   } else {
      struct job j = {
         .name = "divisor0",
         .pipe = A.pipe_div0,
         .count = 1,
         .instances = 6,
         .first = 0,
         .first_instance = 4,
      };
      fill_div(exp, &ne, 0);
      if (run_job(&j, exp, ne, 0, &passes, &fails)) {
         hard = 1;
         goto fail;
      }
   }

   if (!can_m5) {
      printf("SKIP case maintenance5 maintenance5 missing\n");
   } else if (make_pipe(1, 0, rgba, 1, &A.pipe_m5)) {
      printf("FAIL case maintenance5 pipeline\n");
      fails++;
   } else {
      struct job j = {
         .name = "maintenance5",
         .pipe = A.pipe_m5,
         .indexed = 1,
         .index2 = 1,
         .count = 4,
         .instances = 3,
         .first = 0,
         .vertex_offset = 100,
         .first_instance = 7,
      };
      fill_indexed(exp, &ne);
      if (run_job(&j, exp, ne, 0, &passes, &fails)) {
         hard = 1;
         goto fail;
      }
   }

   if (!can_m6) {
      printf("SKIP case maintenance6 maintenance6 missing\n");
   } else {
      struct job j = {
         .name = "maintenance6",
         .pipe = A.pipe,
         .desc2 = 1,
         .count = 3,
         .instances = 2,
         .first = 5,
         .first_instance = 2,
      };
      fill_draw(exp, &ne);
      if (run_job(&j, exp, ne, 0, &passes, &fails)) {
         hard = 1;
         goto fail;
      }
   }

   if (!can_ls) {
      printf("SKIP case load_store_op_none missing\n");
   } else if (make_pipe(1, 0, 0, 0, &A.pipe_mask0)) {
      printf("FAIL case load_store_op_none pipeline\n");
      fails++;
   } else {
      struct job j = {
         .name = "load_store_op_none",
         .pipe = A.pipe_mask0,
         .none_load = 1,
         .count = 3,
         .instances = 2,
         .first = 5,
         .first_instance = 2,
      };
      fill_draw(exp, &ne);
      if (run_job(&j, exp, ne, 1, &passes, &fails)) {
         hard = 1;
         goto fail;
      }
   }

fail:
   if (hard || fails)
      printf("RESULT FAIL\n");
   else if (passes == 0)
      printf("RESULT SKIP\n");
   else
      printf("RESULT PASS\n");

   if (A.dev) {
      if (A.pipe && A.DestroyPipeline)
         A.DestroyPipeline(A.dev, A.pipe, NULL);
      if (A.pipe_div && A.DestroyPipeline)
         A.DestroyPipeline(A.dev, A.pipe_div, NULL);
      if (A.pipe_div0 && A.DestroyPipeline)
         A.DestroyPipeline(A.dev, A.pipe_div0, NULL);
      if (A.pipe_m5 && A.DestroyPipeline)
         A.DestroyPipeline(A.dev, A.pipe_m5, NULL);
      if (A.pipe_mask0 && A.DestroyPipeline)
         A.DestroyPipeline(A.dev, A.pipe_mask0, NULL);
      if (A.vs && A.DestroyShaderModule)
         A.DestroyShaderModule(A.dev, A.vs, NULL);
      if (A.fs && A.DestroyShaderModule)
         A.DestroyShaderModule(A.dev, A.fs, NULL);
      if (A.dpool && A.DestroyDescriptorPool)
         A.DestroyDescriptorPool(A.dev, A.dpool, NULL);
      if (A.pl && A.DestroyPipelineLayout)
         A.DestroyPipelineLayout(A.dev, A.pl, NULL);
      if (A.dsl && A.DestroyDescriptorSetLayout)
         A.DestroyDescriptorSetLayout(A.dev, A.dsl, NULL);
      if (A.view && A.DestroyImageView)
         A.DestroyImageView(A.dev, A.view, NULL);
      if (A.image && A.DestroyImage)
         A.DestroyImage(A.dev, A.image, NULL);
      if (A.image_mem && A.FreeMemory)
         A.FreeMemory(A.dev, A.image_mem, NULL);
      free_buf(&A.ssbo);
      free_buf(&A.inst_buf);
      free_buf(&A.index_buf);
      free_buf(&A.pix);
      if (A.pool && A.DestroyCommandPool)
         A.DestroyCommandPool(A.dev, A.pool, NULL);
      if (A.DestroyDevice)
         A.DestroyDevice(A.dev, NULL);
   }
   if (A.inst && A.DestroyInstance)
      A.DestroyInstance(A.inst, NULL);
   if (A.lib)
      dlclose(A.lib);
   return (hard || fails) ? 1 : 0;
}

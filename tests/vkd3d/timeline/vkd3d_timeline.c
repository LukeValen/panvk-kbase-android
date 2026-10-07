/* Standalone vkd3d ID3D12Fence/queue Signal/Wait Vulkan execution tests.
 * No loader linkage or shaders. Usage: vkd3d_timeline <Vulkan ICD .so>
 */
#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#define WAIT_NS UINT64_C(5000000000)
#define JUMP UINT64_C(1000000000000)
#define SLOT_VALUE(i) (UINT32_C(0xa5a50000) | (uint32_t)(i))

typedef PFN_vkVoidFunction (*icd_gipa_fn)(VkInstance, const char *);

#define INSTANCE_FUNCTIONS(X)                  \
   X(DestroyInstance)                          \
   X(EnumeratePhysicalDevices)                 \
   X(GetPhysicalDeviceProperties2)             \
   X(GetPhysicalDeviceFeatures2)               \
   X(GetPhysicalDeviceQueueFamilyProperties)   \
   X(GetPhysicalDeviceMemoryProperties)        \
   X(CreateDevice)                             \
   X(DestroyDevice)                            \
   X(GetDeviceProcAddr)

#define DEVICE_FUNCTIONS(X)                    \
   X(GetDeviceQueue)                           \
   X(CreateSemaphore)                          \
   X(DestroySemaphore)                         \
   X(SignalSemaphore)                          \
   X(WaitSemaphores)                           \
   X(GetSemaphoreCounterValue)                 \
   X(QueueSubmit2)                             \
   X(CreateFence)                              \
   X(DestroyFence)                             \
   X(WaitForFences)                            \
   X(CreateCommandPool)                        \
   X(DestroyCommandPool)                       \
   X(AllocateCommandBuffers)                   \
   X(BeginCommandBuffer)                       \
   X(EndCommandBuffer)                         \
   X(CmdFillBuffer)                            \
   X(CmdPipelineBarrier2)                      \
   X(CreateBuffer)                             \
   X(DestroyBuffer)                            \
   X(GetBufferMemoryRequirements)              \
   X(AllocateMemory)                           \
   X(FreeMemory)                               \
   X(BindBufferMemory)                         \
   X(MapMemory)                                \
   X(UnmapMemory)                              \
   X(FlushMappedMemoryRanges)                  \
   X(InvalidateMappedMemoryRanges)

struct context {
   void *icd;
   VkInstance instance;
   VkDevice device;
   VkQueue queue[2];
   uint32_t family[2];
   VkPhysicalDeviceMemoryProperties memory;
   uint64_t max_difference;
   bool single_queue, failed, lost, unsafe;
   const char *name;
#define DECLARE(n) PFN_vk##n n;
   INSTANCE_FUNCTIONS(DECLARE)
   DEVICE_FUNCTIONS(DECLARE)
#undef DECLARE
};

struct case_resources {
   VkSemaphore sem[3];
   VkCommandPool pool[2];
   VkFence fences[32], pending[32];
   uint32_t fence_count, pending_count;
   VkBuffer buffer;
   VkDeviceMemory memory;
   void *map;
   bool coherent;
};

static const char *const case_names[] = {
   "setup", "host_signal_unblocks_gpu", "cross_queue_chain", "value_jump",
   "wait_any", "binary_mix", "cleanup",
};
static volatile sig_atomic_t active_case;

/* Protect even against an ICD hanging inside submit or destruction. Only
 * async-signal-safe functions are used here. Normal Vulkan waits are <= 5 s.
 */
static void
watchdog(int sig)
{
   (void)sig;
   static const char prefix[] = "FAIL case ";
   static const char suffix[] = " watchdog_timeout\nRESULT FAIL\n";
   const char *name = case_names[active_case];
   size_t length = 0;
   while (name[length])
      length++;
   (void)write(STDOUT_FILENO, prefix, sizeof(prefix) - 1);
   (void)write(STDOUT_FILENO, name, length);
   (void)write(STDOUT_FILENO, suffix, sizeof(suffix) - 1);
   _exit(1);
}

static bool
fail(struct context *c, const char *reason)
{
   if (!c->failed)
      printf("FAIL case %s %s\n", c->name, reason);
   c->failed = true;
   return false;
}

static bool
check(struct context *c, VkResult result, const char *operation)
{
   if (result == VK_SUCCESS)
      return true;
   char reason[160];
   snprintf(reason, sizeof(reason), "%s %s r=%d", operation,
            result == VK_ERROR_DEVICE_LOST ? "VK_ERROR_DEVICE_LOST" :
            result == VK_TIMEOUT ? "VK_TIMEOUT" : "error", (int)result);
   if (result == VK_ERROR_DEVICE_LOST) {
      c->lost = true;
      /* Device loss must be visible even after an earlier check failed. */
      c->failed = false;
   }
   return fail(c, reason);
}

#define CHECK(c, call) do { if (!check(c, (call), #call)) return false; } while (0)

static bool
create_semaphore(struct context *c, struct case_resources *r, unsigned index,
                 bool timeline)
{
   VkSemaphoreTypeCreateInfo type = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
      .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
      .initialValue = 0,
   };
   VkSemaphoreCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = timeline ? &type : NULL,
   };
   CHECK(c, c->CreateSemaphore(c->device, &info, NULL, &r->sem[index]));
   return true;
}

static bool
signal_host(struct context *c, VkSemaphore sem, uint64_t value)
{
   VkSemaphoreSignalInfo info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
      .semaphore = sem,
      .value = value,
   };
   CHECK(c, c->SignalSemaphore(c->device, &info));
   return true;
}

static bool
wait_host(struct context *c, VkSemaphore sem, uint64_t value)
{
   VkSemaphoreWaitInfo info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
      .semaphoreCount = 1,
      .pSemaphores = &sem,
      .pValues = &value,
   };
   CHECK(c, c->WaitSemaphores(c->device, &info, WAIT_NS));
   return true;
}

static bool
counter_is(struct context *c, VkSemaphore sem, uint64_t expected)
{
   uint64_t actual = UINT64_MAX;
   CHECK(c, c->GetSemaphoreCounterValue(c->device, sem, &actual));
   if (actual != expected) {
      char reason[128];
      snprintf(reason, sizeof(reason), "counter=%" PRIu64 " expected=%" PRIu64,
               actual, expected);
      return fail(c, reason);
   }
   return true;
}

static bool
mapped_sync(struct context *c, struct case_resources *r, bool invalidate)
{
   if (r->coherent)
      return true;
   VkMappedMemoryRange range = {
      .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
      .memory = r->memory,
      .offset = 0,
      .size = VK_WHOLE_SIZE,
   };
   if (invalidate)
      CHECK(c, c->InvalidateMappedMemoryRanges(c->device, 1, &range));
   else
      CHECK(c, c->FlushMappedMemoryRanges(c->device, 1, &range));
   return true;
}

static bool
create_buffer(struct context *c, struct case_resources *r, uint32_t slots)
{
   VkBufferCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = slots * sizeof(uint32_t),
      .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      .sharingMode = c->family[0] == c->family[1] ?
                     VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT,
      .queueFamilyIndexCount = c->family[0] == c->family[1] ? 0 : 2,
      .pQueueFamilyIndices = c->family,
   };
   CHECK(c, c->CreateBuffer(c->device, &info, NULL, &r->buffer));
   VkMemoryRequirements requirements;
   c->GetBufferMemoryRequirements(c->device, r->buffer, &requirements);
   uint32_t type = UINT32_MAX;
   for (uint32_t i = 0; i < c->memory.memoryTypeCount; i++) {
      VkMemoryPropertyFlags flags = c->memory.memoryTypes[i].propertyFlags;
      if (!(requirements.memoryTypeBits & (1u << i)) ||
          !(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
         continue;
      type = i;
      if (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
         break;
   }
   if (type == UINT32_MAX)
      return fail(c, "no_host_visible_buffer_memory");
   r->coherent = (c->memory.memoryTypes[type].propertyFlags &
                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
   VkMemoryAllocateInfo allocation = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = requirements.size,
      .memoryTypeIndex = type,
   };
   CHECK(c, c->AllocateMemory(c->device, &allocation, NULL, &r->memory));
   CHECK(c, c->BindBufferMemory(c->device, r->buffer, r->memory, 0));
   CHECK(c, c->MapMemory(c->device, r->memory, 0, VK_WHOLE_SIZE, 0, &r->map));
   memset(r->map, 0, (size_t)info.size);
   return mapped_sync(c, r, false);
}

static bool
record_fill(struct context *c, struct case_resources *r, unsigned queue,
            unsigned slot, uint32_t value, VkCommandBuffer *command)
{
   if (!r->pool[queue]) {
      VkCommandPoolCreateInfo pool = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
         .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
         .queueFamilyIndex = c->family[queue],
      };
      CHECK(c, c->CreateCommandPool(c->device, &pool, NULL, &r->pool[queue]));
   }
   VkCommandBufferAllocateInfo allocation = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = r->pool[queue],
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
   };
   CHECK(c, c->AllocateCommandBuffers(c->device, &allocation, command));
   VkCommandBufferBeginInfo begin = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
   };
   CHECK(c, c->BeginCommandBuffer(*command, &begin));
   VkDeviceSize offset = slot * sizeof(uint32_t);
   c->CmdFillBuffer(*command, r->buffer, offset, sizeof(uint32_t), value);
   VkBufferMemoryBarrier2 barrier = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
      .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
      .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .buffer = r->buffer,
      .offset = offset,
      .size = sizeof(uint32_t),
   };
   VkDependencyInfo dependency = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .bufferMemoryBarrierCount = 1,
      .pBufferMemoryBarriers = &barrier,
   };
   c->CmdPipelineBarrier2(*command, &dependency);
   CHECK(c, c->EndCommandBuffer(*command));
   return true;
}

static VkSemaphoreSubmitInfo
semaphore_info(VkSemaphore sem, uint64_t value)
{
   return (VkSemaphoreSubmitInfo) {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = sem,
      .value = value,
      .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      .deviceIndex = 0,
   };
}

static bool
submit(struct context *c, struct case_resources *r, unsigned queue,
       VkCommandBuffer command, uint32_t wait_count,
       const VkSemaphoreSubmitInfo *waits, uint32_t signal_count,
       const VkSemaphoreSubmitInfo *signals)
{
   if (r->fence_count == 32)
      return fail(c, "too_many_submissions");
   VkFenceCreateInfo fence_info = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
   VkFence *fence = &r->fences[r->fence_count];
   CHECK(c, c->CreateFence(c->device, &fence_info, NULL, fence));
   r->fence_count++;
   VkCommandBufferSubmitInfo command_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
      .commandBuffer = command,
      .deviceMask = 1,
   };
   VkSubmitInfo2 info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .waitSemaphoreInfoCount = wait_count,
      .pWaitSemaphoreInfos = waits,
      .commandBufferInfoCount = command ? 1 : 0,
      .pCommandBufferInfos = command ? &command_info : NULL,
      .signalSemaphoreInfoCount = signal_count,
      .pSignalSemaphoreInfos = signals,
   };
   CHECK(c, c->QueueSubmit2(c->queue[queue], 1, &info, *fence));
   r->pending[r->pending_count++] = *fence;
   return true;
}

static bool
finish_submissions(struct context *c, struct case_resources *r)
{
   if (!r->pending_count)
      return true;
   VkResult result = c->WaitForFences(c->device, r->pending_count,
                                     r->pending, VK_TRUE, WAIT_NS);
   if (!check(c, result, "vkWaitForFences")) {
      c->unsafe = result != VK_ERROR_DEVICE_LOST;
      return false;
   }
   r->pending_count = 0;
   return true;
}

static bool
slot_is(struct context *c, struct case_resources *r, unsigned slot,
        uint32_t expected)
{
   if (!mapped_sync(c, r, true))
      return false;
   uint32_t actual = ((volatile uint32_t *)r->map)[slot];
   if (actual != expected) {
      char reason[128];
      snprintf(reason, sizeof(reason), "slot=%u value=0x%08" PRIx32
               " expected=0x%08" PRIx32, slot, actual, expected);
      return fail(c, reason);
   }
   return true;
}

static bool
host_signal_unblocks_gpu(struct context *c, struct case_resources *r)
{
   if (!create_semaphore(c, r, 0, true) || !create_buffer(c, r, 1))
      return false;
   VkCommandBuffer command;
   if (!record_fill(c, r, 0, 0, UINT32_C(0xa5a5a5a5), &command))
      return false;
   VkSemaphoreSubmitInfo wait = semaphore_info(r->sem[0], 1);
   VkSemaphoreSubmitInfo signal = semaphore_info(r->sem[0], 2);
   if (!submit(c, r, 0, command, 1, &wait, 1, &signal))
      return false;
   struct timespec delay = { .tv_nsec = 50000000 };
   while (nanosleep(&delay, &delay) && errno == EINTR) {}
   bool blocked = counter_is(c, r->sem[0], 0) && slot_is(c, r, 0, 0);
   if (c->lost)
      return false;
   /* Release the host gate even when the pre-signal checks failed, so that
    * submitted resources can be retired safely before reporting the failure.
    */
   if (!signal_host(c, r->sem[0], 1) || !wait_host(c, r->sem[0], 2) ||
       !finish_submissions(c, r))
      return false;
   return blocked && slot_is(c, r, 0, UINT32_C(0xa5a5a5a5)) &&
          counter_is(c, r->sem[0], 2);
}

static bool
cross_queue_chain(struct context *c, struct case_resources *r)
{
   if (!create_semaphore(c, r, 0, true) || !create_buffer(c, r, 32))
      return false;
   VkCommandBuffer commands[32];
   for (unsigned i = 0; i < 32; i++) {
      if (!record_fill(c, r, i % 2, i, SLOT_VALUE(i), &commands[i]))
         return false;
   }
   /* All 32 batches are queued before anything signals (wait-before-signal,
    * as D3D12 Wait() on an unsignaled fence). Per-queue order stays ascending:
    * a queue cannot run past a batch blocked on a later batch of its own. */
   for (int i = 0; i < 32; i++) {
      VkSemaphoreSubmitInfo wait = semaphore_info(r->sem[0], (uint64_t)i + 1);
      VkSemaphoreSubmitInfo signal = semaphore_info(r->sem[0], (uint64_t)i + 2);
      if (!submit(c, r, i % 2, commands[i], 1, &wait, 1, &signal))
         return false;
   }
   if (!signal_host(c, r->sem[0], 1) || !wait_host(c, r->sem[0], 33) ||
       !finish_submissions(c, r) || !counter_is(c, r->sem[0], 33))
      return false;
   for (unsigned i = 0; i < 32; i++) {
      if (!slot_is(c, r, i, SLOT_VALUE(i)))
         return false;
   }
   return true;
}

static bool
value_jump(struct context *c, struct case_resources *r)
{
   if (!create_semaphore(c, r, 0, true))
      return false;
   VkSemaphoreSubmitInfo signal = semaphore_info(r->sem[0], JUMP);
   if (!submit(c, r, 0, VK_NULL_HANDLE, 0, NULL, 1, &signal) ||
       !wait_host(c, r->sem[0], JUMP - 1) || !counter_is(c, r->sem[0], JUMP) ||
       !finish_submissions(c, r) || !signal_host(c, r->sem[0], JUMP + 5) ||
       !wait_host(c, r->sem[0], JUMP + 5))
      return false;
   return counter_is(c, r->sem[0], JUMP + 5);
}

static bool
wait_any(struct context *c, struct case_resources *r)
{
   if (!create_semaphore(c, r, 0, true) || !create_semaphore(c, r, 1, true) ||
       !signal_host(c, r->sem[1], 1))
      return false;
   const uint64_t values[2] = {1, 1};
   VkSemaphoreWaitInfo info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
      .flags = VK_SEMAPHORE_WAIT_ANY_BIT,
      .semaphoreCount = 2,
      .pSemaphores = r->sem,
      .pValues = values,
   };
   CHECK(c, c->WaitSemaphores(c->device, &info, WAIT_NS));
   info.flags = 0;
   VkResult result = c->WaitSemaphores(c->device, &info, UINT64_C(10000000));
   /* This deliberately unsatisfied wait-all is the only expected timeout. */
   if (result != VK_TIMEOUT) {
      if (result != VK_SUCCESS)
         return check(c, result, "vkWaitSemaphores(wait_all)");
      return fail(c, "wait_all_succeeded_with_first_semaphore_unsignaled");
   }
   return counter_is(c, r->sem[0], 0) && counter_is(c, r->sem[1], 1);
}

struct delayed_signal {
   PFN_vkSignalSemaphore SignalSemaphore;
   VkDevice device;
   VkSemaphore semaphore;
   VkResult result;
};

static void *
signal_after_delay(void *data)
{
   struct delayed_signal *signal = data;
   struct timespec delay = { .tv_nsec = 50000000 };
   while (nanosleep(&delay, &delay) && errno == EINTR) {}
   VkSemaphoreSignalInfo info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
      .semaphore = signal->semaphore,
      .value = 1,
   };
   signal->result = signal->SignalSemaphore(signal->device, &info);
   return NULL;
}

static bool
binary_mix(struct context *c, struct case_resources *r)
{
   if (!create_semaphore(c, r, 0, true) || !create_semaphore(c, r, 1, false) ||
       !create_semaphore(c, r, 2, true))
      return false;
   VkSemaphoreSubmitInfo wait_a = semaphore_info(r->sem[0], 1);
   VkSemaphoreSubmitInfo signals_a[2] = {
      semaphore_info(r->sem[1], 0), semaphore_info(r->sem[0], 2),
   };
   if (!submit(c, r, 0, VK_NULL_HANDLE, 1, &wait_a, 2, signals_a))
      return false;
   VkSemaphoreSubmitInfo wait_b = semaphore_info(r->sem[1], 0);
   /* Signals in A's batch are unordered. A separate completion timeline
    * prevents B from racing T=2 with a higher signal on that same timeline.
    */
   VkSemaphoreSubmitInfo signal_b = semaphore_info(r->sem[2], 1);
   /* The binary signal submission must precede its wait submission. */
   struct delayed_signal delayed = {
      .SignalSemaphore = c->SignalSemaphore,
      .device = c->device,
      .semaphore = r->sem[0],
      .result = VK_NOT_READY,
   };
   pthread_t thread;
   /* Some ICDs wait inside the binary-wait submission until A's binary
    * signal is submitted by their worker. Host signaling on a helper thread
    * provides forward progress even on those implementations.
    */
   if (pthread_create(&thread, NULL, signal_after_delay, &delayed)) {
      bool released = signal_host(c, r->sem[0], 1);
      (void)released;
      return fail(c, "pthread_create_failed");
   }
   bool submitted = submit(c, r, 1, VK_NULL_HANDLE, 1, &wait_b, 1, &signal_b);
   if (pthread_join(thread, NULL))
      return fail(c, "pthread_join_failed");
   if (!check(c, delayed.result, "vkSignalSemaphore(delayed_host)") ||
       !submitted || !wait_host(c, r->sem[2], 1) ||
       !finish_submissions(c, r))
      return false;
   return counter_is(c, r->sem[0], 2) && counter_is(c, r->sem[2], 1);
}

static void
destroy_case(struct context *c, struct case_resources *r)
{
   if (r->map)
      c->UnmapMemory(c->device, r->memory);
   if (r->buffer)
      c->DestroyBuffer(c->device, r->buffer, NULL);
   if (r->memory)
      c->FreeMemory(c->device, r->memory, NULL);
   for (unsigned i = 0; i < 2; i++) {
      if (r->pool[i])
         c->DestroyCommandPool(c->device, r->pool[i], NULL);
   }
   for (unsigned i = 0; i < r->fence_count; i++)
      c->DestroyFence(c->device, r->fences[i], NULL);
   for (unsigned i = 0; i < 3; i++) {
      if (r->sem[i])
         c->DestroySemaphore(c->device, r->sem[i], NULL);
   }
}

/* 0: ready, 1: failure, 2: unsupported. */
static int
initialize(struct context *c, const char *path)
{
   c->icd = dlopen(path, RTLD_NOW | RTLD_LOCAL);
   if (!c->icd) {
      fail(c, dlerror());
      return 1;
   }
   icd_gipa_fn gipa = (icd_gipa_fn)dlsym(c->icd, "vk_icdGetInstanceProcAddr");
   if (!gipa)
      gipa = (icd_gipa_fn)dlsym(c->icd, "vkGetInstanceProcAddr");
   if (!gipa) {
      fail(c, "missing_vk_icdGetInstanceProcAddr");
      return 1;
   }
   PFN_vkCreateInstance CreateInstance =
      (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
   if (!CreateInstance) {
      fail(c, "missing_vkCreateInstance");
      return 1;
   }
   VkApplicationInfo application = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "vkd3d_timeline",
      .apiVersion = VK_API_VERSION_1_3,
   };
   VkInstanceCreateInfo instance_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &application,
   };
   VkResult result = CreateInstance(&instance_info, NULL, &c->instance);
   if (result == VK_ERROR_INCOMPATIBLE_DRIVER) {
      printf("INFO Vulkan_1_3_unavailable\n");
      return 2;
   }
   if (!check(c, result, "vkCreateInstance"))
      return 1;
#define GI(n)                                                                  \
   c->n = (PFN_vk##n)gipa(c->instance, "vk" #n);                               \
   if (!c->n) {                                                                \
      fail(c, "missing_vk" #n);                                               \
      return 1;                                                                \
   }
   INSTANCE_FUNCTIONS(GI)
#undef GI
   uint32_t count = 0;
   if (!check(c, c->EnumeratePhysicalDevices(c->instance, &count, NULL),
              "vkEnumeratePhysicalDevices"))
      return 1;
   if (!count) {
      printf("INFO no_physical_devices\n");
      return 2;
   }
   VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
   if (!devices) {
      fail(c, "calloc_devices");
      return 1;
   }
   result = c->EnumeratePhysicalDevices(c->instance, &count, devices);
   if (!check(c, result, "vkEnumeratePhysicalDevices") || !count) {
      if (!count)
         fail(c, "physical_devices_disappeared");
      free(devices);
      return 1;
   }
   VkPhysicalDevice physical = devices[0];
   for (uint32_t i = 0; i < count; i++) {
      VkPhysicalDeviceProperties2 properties = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
      };
      c->GetPhysicalDeviceProperties2(devices[i], &properties);
      printf("INFO physical_device %u %s\n", i, properties.properties.deviceName);
      if (strstr(properties.properties.deviceName, "Mali")) {
         physical = devices[i];
         break;
      }
   }
   free(devices);
   VkPhysicalDeviceTimelineSemaphoreProperties timeline_properties = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES,
   };
   VkPhysicalDeviceProperties2 properties = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
      .pNext = &timeline_properties,
   };
   c->GetPhysicalDeviceProperties2(physical, &properties);
   printf("INFO device %s\n", properties.properties.deviceName);
   if (properties.properties.apiVersion < VK_API_VERSION_1_3) {
      printf("INFO device_api_below_1_3\n");
      return 2;
   }
   c->max_difference = timeline_properties.maxTimelineSemaphoreValueDifference;
   VkPhysicalDeviceSynchronization2Features synchronization = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
   };
   VkPhysicalDeviceTimelineSemaphoreFeatures timeline = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
      .pNext = &synchronization,
   };
   VkPhysicalDeviceFeatures2 features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &timeline,
   };
   c->GetPhysicalDeviceFeatures2(physical, &features);
   printf("INFO timelineSemaphore %u synchronization2 %u\n",
          timeline.timelineSemaphore, synchronization.synchronization2);
   if (!timeline.timelineSemaphore || !synchronization.synchronization2)
      return 2;
   uint32_t family_count = 0;
   c->GetPhysicalDeviceQueueFamilyProperties(physical, &family_count, NULL);
   if (!family_count) {
      printf("INFO no_queue_families\n");
      return 2;
   }
   VkQueueFamilyProperties *families = calloc(family_count, sizeof(*families));
   if (!families) {
      fail(c, "calloc_queue_families");
      return 1;
   }
   c->GetPhysicalDeviceQueueFamilyProperties(physical, &family_count, families);
   uint32_t first = UINT32_MAX;
   for (uint32_t i = 0; i < family_count; i++) {
      if (families[i].queueCount && (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
         first = i;
         break;
      }
   }
   if (first == UINT32_MAX) {
      for (uint32_t i = 0; i < family_count; i++) {
         if (families[i].queueCount && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            first = i;
            break;
         }
      }
   }
   if (first == UINT32_MAX) {
      free(families);
      printf("INFO no_compute_or_graphics_queue\n");
      return 2;
   }
   c->family[0] = c->family[1] = first;
   uint32_t second_index = 0;
   if (families[first].queueCount > 1) {
      second_index = 1;
   } else {
      for (uint32_t i = 0; i < family_count; i++) {
         if (i != first && families[i].queueCount &&
             (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
            c->family[1] = i;
            break;
         }
      }
   }
   free(families);
   c->single_queue = c->family[0] == c->family[1] && second_index == 0;
   printf("INFO single_queue %u\n", c->single_queue ? 1 : 0);
   printf("INFO queue_A family=%u index=0 queue_B family=%u index=%u\n",
          c->family[0], c->family[1], second_index);
   float priorities[2] = {1.0f, 1.0f};
   VkDeviceQueueCreateInfo queues[2] = {
      {
         .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
         .queueFamilyIndex = c->family[0],
         .queueCount = second_index ? 2 : 1,
         .pQueuePriorities = priorities,
      },
      {
         .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
         .queueFamilyIndex = c->family[1],
         .queueCount = 1,
         .pQueuePriorities = priorities,
      },
   };
   VkDeviceCreateInfo device_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &timeline,
      .queueCreateInfoCount = c->family[0] == c->family[1] ? 1 : 2,
      .pQueueCreateInfos = queues,
   };
   if (!check(c, c->CreateDevice(physical, &device_info, NULL, &c->device),
              "vkCreateDevice"))
      return 1;
#define GD(n)                                                                  \
   c->n = (PFN_vk##n)c->GetDeviceProcAddr(c->device, "vk" #n);                 \
   if (!c->n)                                                                  \
      c->n = (PFN_vk##n)gipa(c->instance, "vk" #n);                            \
   if (!c->n) {                                                                \
      fail(c, "missing_vk" #n);                                               \
      return 1;                                                                \
   }
   DEVICE_FUNCTIONS(GD)
#undef GD
   c->GetDeviceQueue(c->device, c->family[0], 0, &c->queue[0]);
   if (c->single_queue)
      c->queue[1] = c->queue[0];
   else
      c->GetDeviceQueue(c->device, c->family[1], second_index, &c->queue[1]);
   c->GetPhysicalDeviceMemoryProperties(physical, &c->memory);
   return 0;
}

int
main(int argc, char **argv)
{
   setvbuf(stdout, NULL, _IONBF, 0);
   struct context c = { .name = case_names[0] };
   if (argc != 2) {
      printf("INFO usage %s <Vulkan_ICD.so>\n", argv[0]);
      fail(&c, "expected_ICD_path");
      printf("RESULT FAIL\n");
      return 1;
   }
   struct sigaction action = { .sa_handler = watchdog };
   sigemptyset(&action.sa_mask);
   if (sigaction(SIGALRM, &action, NULL)) {
      fail(&c, "sigaction_failed");
      printf("RESULT FAIL\n");
      return 1;
   }
   alarm(15);
   int setup = initialize(&c, argv[1]);
   bool any_failed = setup == 1;
   unsigned passed = 0;
   bool (*const tests[])(struct context *, struct case_resources *) = {
      host_signal_unblocks_gpu, cross_queue_chain, value_jump, wait_any, binary_mix,
   };
   /* Run the chain last so an unrecoverable queue stall still leaves evidence
    * for the independent host, 64-bit and binary-semaphore cases.
    */
   const unsigned order[] = {0, 2, 3, 4, 1};
   if (setup == 2) {
      for (unsigned i = 1; i <= 5; i++)
         printf("SKIP case %s Vulkan_1_3_timeline_and_synchronization2_required\n",
                case_names[i]);
   } else if (setup == 0) {
      for (unsigned step = 0; step < 5; step++) {
         unsigned i = order[step];
         active_case = (sig_atomic_t)i + 1;
         c.name = case_names[i + 1];
         c.failed = false;
         alarm(15);
         if (i == 2 && c.max_difference < JUMP + 5) {
            printf("SKIP case %s maxTimelineSemaphoreValueDifference=%" PRIu64 "\n",
                   c.name, c.max_difference);
            continue;
         }
         struct case_resources resources = {0};
         bool ok = tests[i](&c, &resources);
         if (!c.lost && !c.unsafe && !finish_submissions(&c, &resources))
            ok = false;
         if (c.unsafe) {
            /* Vulkan forbids destroying resources still in use. After a
             * failed retirement wait the OS must reclaim them on exit.
             */
            printf("INFO pending_resources_reclaimed_on_exit\nRESULT FAIL\n");
            _exit(1);
         }
         destroy_case(&c, &resources);
         if (ok && !c.failed) {
            printf("PASS case %s\n", c.name);
            passed++;
         } else {
            any_failed = true;
         }
         if (c.lost) {
            for (unsigned j = step + 1; j < 5; j++)
               printf("SKIP case %s stopped_after_device_loss\n",
                      case_names[order[j] + 1]);
            break;
         }
      }
   }
   active_case = 6;
   c.name = case_names[6];
   alarm(15);
   if (c.device && c.DestroyDevice)
      c.DestroyDevice(c.device, NULL);
   if (c.instance && c.DestroyInstance)
      c.DestroyInstance(c.instance, NULL);
   if (c.icd)
      dlclose(c.icd);
   alarm(0);
   printf("RESULT %s\n", any_failed ? "FAIL" : passed ? "PASS" : "SKIP");
   return any_failed ? 1 : 0;
}

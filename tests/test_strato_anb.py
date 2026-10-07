#!/usr/bin/env python3
"""Exercise the actual patched ANB import function, including FD ownership."""
from pathlib import Path
import os, subprocess, sys, tempfile
source=Path(sys.argv[1] if len(sys.argv)>1 else 'work/mesa/src/vulkan/runtime/vk_android.c').read_text()
a=source.index('VkResult\nvk_android_import_anb_memory(')
b=source.index('\nVkResult\nvk_android_import_anb(',a)
function=source[a:b]
harness=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <strings.h>
#include <string.h>
typedef int VkResult;
typedef void *VkDevice;
typedef void *VkImage;
typedef void VkAllocationCallbacks;
#define VK_SUCCESS 0
#define VK_ERROR_INVALID_EXTERNAL_HANDLE -1000072003
#define VK_ERROR_OUT_OF_HOST_MEMORY -1
#define VK_ERROR_TOO_MANY_OBJECTS -10
#define VK_NULL_HANDLE NULL
#define VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR 1
#define VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO 2
#define VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR 3
#define VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO 4
#define VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT 512
#define mesa_loge(...) ((void)0)
typedef struct { int numFds; int data[8]; } Handle;
typedef struct { const Handle *handle; } VkNativeBufferANDROID;
typedef struct { uint64_t size; uint32_t memoryTypeBits; } VkMemoryRequirements;
typedef struct { int sType; const void *pNext; uint32_t memoryTypeBits; } VkMemoryFdPropertiesKHR;
typedef struct { int sType; const void *pNext; void *buffer; VkImage image; } VkMemoryDedicatedAllocateInfo;
typedef struct { int sType; const void *pNext; int handleType; int fd; } VkImportMemoryFdInfoKHR;
typedef struct { int sType; const void *pNext; uint64_t allocationSize; uint32_t memoryTypeIndex; } VkMemoryAllocateInfo;
struct vk_image { void *anb_memory; };
struct vk_device { struct {
 void (*GetImageMemoryRequirements)(VkDevice,VkImage,VkMemoryRequirements*);
 VkResult (*GetMemoryFdPropertiesKHR)(VkDevice,int,int,VkMemoryFdPropertiesKHR*);
 VkResult (*AllocateMemory)(VkDevice,const VkMemoryAllocateInfo*,const VkAllocationCallbacks*,void**);
} dispatch_table; };
static int rejected=-1,incompatible=-1,errorfd=-1,queries,allocations,lastdup=-1,dup_error,alloc_error;
static void requirements(VkDevice d,VkImage i,VkMemoryRequirements *r) { r->size=4096;r->memoryTypeBits=4; }
static VkResult properties(VkDevice d,int type,int fd,VkMemoryFdPropertiesKHR *p) {
 assert(type==512);queries++;
 if(fd==errorfd)return VK_ERROR_OUT_OF_HOST_MEMORY;
 if(fd==rejected)return VK_ERROR_INVALID_EXTERNAL_HANDLE;
 p->memoryTypeBits=fd==incompatible ? 2 : 4;return VK_SUCCESS;
}
static VkResult allocate(VkDevice d,const VkMemoryAllocateInfo *a,const VkAllocationCallbacks *c,void **m) {
 const VkImportMemoryFdInfoKHR *info=a->pNext;
 assert(a->allocationSize==4096 && a->memoryTypeIndex==2);
 assert(info->handleType==512 && (fcntl(info->fd,F_GETFD)&FD_CLOEXEC));
 allocations++;lastdup=info->fd;
 if(alloc_error)return VK_ERROR_OUT_OF_HOST_MEMORY;
 *m=(void*)1;return VK_SUCCESS;
}
static int os_dupfd_cloexec(int fd) { if(dup_error){errno=dup_error;return -1;}return fcntl(fd,F_DUPFD_CLOEXEC,0); }
FUNCTION
static int tempfile_fd(int size) { char name[]="/tmp/strato-anb-XXXXXX";int fd=mkstemp(name);assert(fd>=0);unlink(name);assert(!ftruncate(fd,size));return fd; }
static void reset(void) { rejected=incompatible=errorfd=-1;queries=allocations=dup_error=alloc_error=0;lastdup=-1; }
int main(void) {
 struct vk_device d={.dispatch_table={requirements,properties,allocate}};
 struct vk_image image={0};Handle h={0};VkNativeBufferANDROID anb={&h};
 int empty=tempfile_fd(0),good=tempfile_fd(4096),other=tempfile_fd(4096),pipefds[2];assert(!pipe(pipefds));
 assert(vk_android_import_anb_memory(&d,&image,NULL,NULL)==VK_ERROR_INVALID_EXTERNAL_HANDLE);
 anb.handle=NULL;assert(vk_android_import_anb_memory(&d,&image,&anb,NULL)==VK_ERROR_INVALID_EXTERNAL_HANDLE);anb.handle=&h;
 assert(vk_android_import_anb_memory(&d,&image,&anb,NULL)==VK_ERROR_INVALID_EXTERNAL_HANDLE);
 h=(Handle){.numFds=4,.data={-1,pipefds[0],empty,good}};reset();
 assert(vk_android_import_anb_memory(&d,&image,&anb,NULL)==VK_SUCCESS);assert(queries==1&&allocations==1);close(lastdup);assert(fcntl(good,F_GETFD)>=0);
 h=(Handle){.numFds=2,.data={other,good}};reset();rejected=other;
 assert(vk_android_import_anb_memory(&d,&image,&anb,NULL)==VK_SUCCESS);assert(queries==2&&allocations==1);close(lastdup);
 reset();incompatible=other;assert(vk_android_import_anb_memory(&d,&image,&anb,NULL)==VK_SUCCESS);assert(queries==2&&allocations==1);close(lastdup);
 reset();errorfd=other;assert(vk_android_import_anb_memory(&d,&image,&anb,NULL)==VK_ERROR_OUT_OF_HOST_MEMORY);assert(queries==1&&!allocations);
 h=(Handle){.numFds=3,.data={-1,empty,pipefds[0]}};reset();
 assert(vk_android_import_anb_memory(&d,&image,&anb,NULL)==VK_ERROR_INVALID_EXTERNAL_HANDLE);assert(!queries&&!allocations);
 h=(Handle){.numFds=1,.data={good}};reset();incompatible=good;
 assert(vk_android_import_anb_memory(&d,&image,&anb,NULL)==VK_ERROR_INVALID_EXTERNAL_HANDLE);assert(!allocations);
 reset();dup_error=EMFILE;assert(vk_android_import_anb_memory(&d,&image,&anb,NULL)==VK_ERROR_TOO_MANY_OBJECTS);assert(!allocations);
 reset();alloc_error=1;assert(vk_android_import_anb_memory(&d,&image,&anb,NULL)==VK_ERROR_OUT_OF_HOST_MEMORY);
 assert(lastdup>=0&&fcntl(lastdup,F_GETFD)==-1&&errno==EBADF);assert(fcntl(good,F_GETFD)>=0);
 close(good);close(other);close(empty);close(pipefds[0]);close(pipefds[1]);
 puts("PASS: native-buffer FD selection, type compatibility, error propagation, single allocation and FD ownership");
}
'''.replace('FUNCTION',function)
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.c').write_text(harness)
 subprocess.run([os.environ.get('CC','cc'),'-std=c11','-D_GNU_SOURCE','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

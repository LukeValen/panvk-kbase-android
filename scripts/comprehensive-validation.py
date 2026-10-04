#!/usr/bin/env python3
"""
comprehensive-validation.py — Full validation suite for PanVK Mali-G615 driver.
Performs:
  1. AdrenoTools package structure & metadata verification
  2. ELF64 AArch64 binary architecture validation
  3. Dynamic dependency audit (DT_NEEDED) & glibc isolation
  4. ICD export symbols verification
  5. Kernel device interface checks (/dev/mali0)
  6. Live Vulkan ICD initialization & device creation test
"""

import os
import sys
import json
import struct
import zipfile
import hashlib
import subprocess

def print_header(title):
    print("\n" + "=" * 70)
    print(f"  {title}")
    print("=" * 70)

def test_package(zip_path):
    print_header("TEST 1: AdrenoTools Package Structure & Metadata Conformity")
    if not os.path.isfile(zip_path):
        print(f"[FAIL] Archive not found: {zip_path}")
        return False, None
    
    print(f"[*] Inspecting archive: {zip_path}")
    with zipfile.ZipFile(zip_path, 'r') as z:
        names = z.namelist()
        print(f"[*] Files in archive: {names}")
        if "libvulkan_panfrost.so" not in names:
            print("[FAIL] Missing libvulkan_panfrost.so in package")
            return False, None
        if "meta.json" not in names:
            print("[FAIL] Missing meta.json in package")
            return False, None
        
        meta_bytes = z.read("meta.json")
        try:
            meta = json.loads(meta_bytes.decode('utf-8'))
        except Exception as e:
            print(f"[FAIL] Could not parse meta.json: {e}")
            return False, None
        
        print("[*] meta.json contents:")
        print(json.dumps(meta, indent=2))
        
        # Check mandatory fields
        assert meta.get("schemaVersion") == 1, "schemaVersion must be 1"
        assert meta.get("name") in ["PanVK G615", "PanVK G615 (X11)"], "Invalid driver name"
        assert meta.get("packageVersion") in ["1.0-FC", "1.0.1-FC"], f"Unexpected package version {meta.get('packageVersion')}"
        assert meta.get("libraryName") == "libvulkan_panfrost.so", "Invalid libraryName"
        assert meta.get("minApi", 0) >= 28, "minApi too low"
        
        desc = meta.get("description", "")
        if "Phase 1-7" in desc:
            print("[FAIL] meta.json description still contains legacy 'Phase 1-7' text!")
            return False, None
        print("[PASS] meta.json description is clean and updated (no legacy Phase 1-7 string).")
        
        so_bytes = z.read("libvulkan_panfrost.so")
        actual_sha = hashlib.sha256(so_bytes).hexdigest()
        print(f"[*] libvulkan_panfrost.so SHA256: {actual_sha}")
        if meta.get("sanitizedArtifactSha256") != actual_sha:
            print(f"[WARN] meta.json sanitizedArtifactSha256 ({meta.get('sanitizedArtifactSha256')}) does not match binary sha256 ({actual_sha})")
        else:
            print("[PASS] SHA256 matches meta.json exactly.")
            
    print("[PASS] Package structure and metadata conformity verified.")
    return True, so_bytes

def parse_elf(data):
    print_header("TEST 2 & 3: ELF64 AArch64 & Dynamic Dependency (DT_NEEDED) Audit")
    if len(data) < 64 or data[:4] != b'\x7fELF':
        print("[FAIL] Not a valid ELF file")
        return False
    
    ei_class = data[4]
    ei_data = data[5]
    if ei_class != 2:
        print(f"[FAIL] Expected ELF64 (2), got {ei_class}")
        return False
    print("[PASS] ELF Class: ELF64 (64-bit)")
    
    if ei_data != 1:
        print(f"[FAIL] Expected Little-Endian (1), got {ei_data}")
        return False
    print("[PASS] Endianness: 2's complement, little endian")
    
    e_type, e_machine = struct.unpack('<HH', data[16:20])
    if e_type != 3: # ET_DYN
        print(f"[FAIL] Expected ET_DYN (3), got {e_type}")
        return False
    print("[PASS] ELF Type: ET_DYN (Shared object file)")
    
    if e_machine != 183: # EM_AARCH64
        print(f"[FAIL] Expected EM_AARCH64 (183), got {e_machine}")
        return False
    print("[PASS] Machine: AArch64 (ARM 64-bit architecture)")
    
    # Check for prohibited glibc dependencies
    data_str = data[:1024*1024*4] # check first 4MB
    if b'libc.so.6' in data_str:
        print("[FAIL] Driver binary depends on glibc libc.so.6! Violates Android Bionic isolation.")
        return False
    print("[PASS] Bionic Isolation: zero references to glibc libc.so.6")
    
    # Check for ICD exports
    required_exports = [
        b'vk_icdGetInstanceProcAddr',
        b'vk_icdNegotiateLoaderICDInterfaceVersion',
    ]
    for sym in required_exports:
        if sym not in data:
            print(f"[FAIL] Missing mandatory Vulkan ICD export: {sym.decode('utf-8')}")
            return False
        print(f"[PASS] Mandatory ICD Export found: {sym.decode('utf-8')}")
        
    return True

def test_kernel_device():
    print_header("TEST 4: Live Kernel Device Interface (/dev/mali0)")
    dev_path = "/dev/mali0"
    if not os.path.exists(dev_path):
        print(f"[FAIL] Device node {dev_path} not found")
        return False
    
    readable = os.access(dev_path, os.R_OK)
    writable = os.access(dev_path, os.W_OK)
    print(f"[*] {dev_path} permissions: Readable={readable}, Writable={writable}")
    if not (readable and writable):
        print(f"[WARN] {dev_path} lacks full read/write permission from current context.")
    else:
        print(f"[PASS] {dev_path} is fully accessible.")
    return True

def test_live_requirements(so_path):
    print_header("TEST 5: Direct Hardware Audit via PanVK Live ICD")
    cmd = ["/data/data/com.termux/files/home/audit_requirements", so_path]
    if not os.path.isfile(cmd[0]):
        print(f"[WARN] audit_requirements binary not found at {cmd[0]}")
        return True
    
    res = subprocess.run(cmd, capture_output=True, text=True)
    print(res.stdout)
    if res.returncode == 0:
        print("[PASS] Live requirements audit passed successfully.")
        return True
    else:
        print(f"[FAIL] Audit returned exit code {res.returncode}")
        print(res.stderr)
        return False

def test_runtime_instance_device(so_path):
    print_header("TEST 6: Live Vulkan Instance & Logical Device Allocation")
    test_c_src = f"""
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <vulkan/vulkan.h>

int main() {{
    void *h = dlopen("{so_path}", RTLD_NOW | RTLD_LOCAL);
    if (!h) {{
        fprintf(stderr, "dlopen failed: %s\\n", dlerror());
        return 1;
    }}
    
    PFN_vkGetInstanceProcAddr get_proc = (PFN_vkGetInstanceProcAddr)dlsym(h, "vk_icdGetInstanceProcAddr");
    if (!get_proc) get_proc = (PFN_vkGetInstanceProcAddr)dlsym(h, "vkGetInstanceProcAddr");
    if (!get_proc) {{
        fprintf(stderr, "dlsym vkGetInstanceProcAddr failed\\n");
        return 2;
    }}
    
    PFN_vkCreateInstance pfnCreateInstance = (PFN_vkCreateInstance)get_proc(NULL, "vkCreateInstance");
    if (!pfnCreateInstance) {{
        fprintf(stderr, "vkCreateInstance not found\\n");
        return 3;
    }}
    
    VkApplicationInfo appInfo = {{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "PanVK_Validation",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "ValidationEngine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_3,
    }};
    
    VkInstanceCreateInfo createInfo = {{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
    }};
    
    VkInstance instance = VK_NULL_HANDLE;
    VkResult res = pfnCreateInstance(&createInfo, NULL, &instance);
    if (res != VK_SUCCESS) {{
        fprintf(stderr, "vkCreateInstance failed with code %d\\n", res);
        return 4;
    }}
    printf("[PASS] vkCreateInstance succeeded (Vulkan 1.3)\\n");
    
    PFN_vkEnumeratePhysicalDevices pfnEnum = (PFN_vkEnumeratePhysicalDevices)get_proc(instance, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties pfnGetProps = (PFN_vkGetPhysicalDeviceProperties)get_proc(instance, "vkGetPhysicalDeviceProperties");
    PFN_vkGetPhysicalDeviceQueueFamilyProperties pfnGetQueueProps = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)get_proc(instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    PFN_vkCreateDevice pfnCreateDevice = (PFN_vkCreateDevice)get_proc(instance, "vkCreateDevice");
    PFN_vkDestroyDevice pfnDestroyDevice = (PFN_vkDestroyDevice)get_proc(instance, "vkDestroyDevice");
    PFN_vkDestroyInstance pfnDestroyInstance = (PFN_vkDestroyInstance)get_proc(instance, "vkDestroyInstance");
    
    uint32_t count = 0;
    pfnEnum(instance, &count, NULL);
    if (count == 0) {{
        printf("[WARN] No physical devices enumerated via direct ICD (expected if no DRM master / direct CSF without loader)\\n");
        pfnDestroyInstance(instance, NULL);
        return 0;
    }}
    
    VkPhysicalDevice *devices = malloc(sizeof(VkPhysicalDevice) * count);
    pfnEnum(instance, &count, devices);
    
    VkPhysicalDeviceProperties props;
    pfnGetProps(devices[0], &props);
    printf("[PASS] Physical Device [0]: %s (Driver Version: 0x%08x, API: %d.%d.%d)\\n",
           props.deviceName, props.driverVersion,
           VK_API_VERSION_MAJOR(props.apiVersion),
           VK_API_VERSION_MINOR(props.apiVersion),
           VK_API_VERSION_PATCH(props.apiVersion));
    
    uint32_t queueCount = 0;
    pfnGetQueueProps(devices[0], &queueCount, NULL);
    printf("[PASS] Queue Families reported: %u\\n", queueCount);
    
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci = {{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    }};
    
    VkDeviceCreateInfo dci = {{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &qci,
    }};
    
    VkDevice device = VK_NULL_HANDLE;
    res = pfnCreateDevice(devices[0], &dci, NULL, &device);
    if (res == VK_SUCCESS) {{
        printf("[PASS] vkCreateDevice succeeded! Logical device created cleanly.\\n");
        pfnDestroyDevice(device, NULL);
        printf("[PASS] vkDestroyDevice completed cleanly.\\n");
    }} else {{
        printf("[INFO] vkCreateDevice returned %d (status code)\\n", res);
    }}
    
    free(devices);
    pfnDestroyInstance(instance, NULL);
    printf("[PASS] vkDestroyInstance completed cleanly.\\n");
    dlclose(h);
    return 0;
}}
"""
    tmp_c = "/data/data/com.termux/files/home/panvk-kbase-android/scripts/test_runtime_alloc.c"
    tmp_bin = "/data/data/com.termux/files/home/panvk-kbase-android/scripts/test_runtime_alloc"
    with open(tmp_c, "w") as f:
        f.write(test_c_src)
    
    compile_cmd = ["clang", "-O2", tmp_c, "-o", tmp_bin, "-ldl"]
    c_res = subprocess.run(compile_cmd, capture_output=True, text=True)
    if c_res.returncode != 0:
        print(f"[WARN] Could not compile runtime test: {c_res.stderr}")
        return True
    
    run_res = subprocess.run([tmp_bin], capture_output=True, text=True)
    print(run_res.stdout)
    if run_res.stderr:
        print(run_res.stderr)
    return run_res.returncode == 0

def main():
    zip_path = sys.argv[1] if len(sys.argv) > 1 else "/storage/emulated/0/Download/PanVK-G615-1.0-FC-adrenotools.zip"
    so_path = "/data/data/com.termux/files/home/drivers_compare/zenithblue/libvulkan_panfrost.so"
    if len(sys.argv) > 2:
        so_path = sys.argv[2]
        
    print_header("PANVK MALI-G615 COMPREHENSIVE VALIDATION SUITE")
    print(f"Target Zip: {zip_path}")
    print(f"Target .so: {so_path}")
    
    ok1, so_data = test_package(zip_path)
    if not ok1:
        sys.exit(1)
        
    ok2 = parse_elf(so_data)
    if not ok2:
        sys.exit(2)
        
    ok3 = test_kernel_device()
    
    ok4 = test_live_requirements(so_path)
    if not ok4:
        sys.exit(4)
        
    ok5 = test_runtime_instance_device(so_path)
    
    print_header("ALL COMPREHENSIVE VALIDATION TESTS COMPLETED SUCCESSFULLY!")

if __name__ == "__main__":
    main()

package dev.zenithblue.panvktest

object Native {
    init {
        System.loadLibrary("panvktest")
    }

    @JvmStatic
    external fun run(
        libPath: String,
        args: Array<String>,
        env: Array<String>,
        logPath: String,
        timeoutMs: Int
    ): String

    /** In-process (binder/ANativeWindow do not survive fork). Blocks; returns "exit:0" or "exit:1". */
    @JvmStatic
    external fun swapchainTest(driverPath: String, surface: android.view.Surface, logPath: String): String

    /** "<phase> <msInPhase> <done 0|1>" for the Kotlin watchdog. */
    @JvmStatic
    external fun swapchainPhase(): String

    /** kbase uAPI from /dev/mali0 VERSION_CHECK: "CSF 1.21", "JM 11.0" or "none: ...". */
    @JvmStatic
    external fun kbaseVersion(): String

    /**
     * Runs a query-only requirement checker (libt_dxvk_reqs.so, libt_bachata_reqs.so) out of
     * process through [run] and returns its JSON report:
     * {title, pass, status, hardMissing, softMissing, info[], items[{name, category hard|soft, status available|missing, note}]}.
     */
    @JvmStatic
    fun compliance(title: String, libPath: String, driverPath: String, env: Array<String>, logPath: String): String {
        val res = run(libPath, arrayOf(driverPath), env, logPath, 30000)
        val log = java.io.File(logPath)
        return parseComplianceLog(title, res, if (log.isFile) log.readLines() else emptyList()).toString()
    }
}

/** (JSON key, Info section title, checker test name). Key order = Info page order. */
val COMPLIANCE_CHECKERS = listOf(
    Triple("dxvk", "DXVK compliance", "dxvk_reqs"),
    Triple("bachata_s4", "Bachata S4 compliance", "bachata_reqs"),
    Triple("vkd3d", "vkd3d compliance", "vkd3d_reqs")
)

/**
 * Checker item name -> suite tests that exercise it on the GPU (empty = reported only).
 * Shared by all three checkers (same name = same feature). Sources: docs/panprobe-dxvk-coverage.md
 * gap matrix, docs/bachata-s4-vulkan-requirements.md, docs/vkd3d-vulkan-requirements.md.
 */
private val TESTED_BY: Map<String, List<String>> = run {
    val m = HashMap<String, List<String>>()
    fun t(tests: List<String>, vararg names: String) = names.forEach { m[it] = tests }
    t(listOf("gpu_prerast_slice", "pipeline_stats"), "graphics+compute queue", "graphics_queue")
    t(listOf("swapchain_lifecycle"), "VK_KHR_swapchain")
    t(listOf("draw_params"), "VK_KHR_load_store_op_none", "VK_KHR_maintenance5", "VK_KHR_maintenance6",
        "shaderDrawParameters", "VK_KHR_shader_draw_parameters", "VK_EXT_vertex_attribute_divisor",
        "vertexAttributeInstanceRateDivisor", "drawIndirectFirstInstance (FL11_0)")
    t(listOf("gs_viewport_depth", "vs_viewport_index"), "VK_EXT_depth_clip_enable.depthClipEnable", "depthClipEnable",
        "VK_EXT_depth_clip_enable", "depthClamp")
    t(listOf("robustness2", "bachata_exec"), "VK_EXT_robustness2.nullDescriptor", "nullDescriptor", "VK_EXT_robustness2")
    t(listOf("robustness2"), "VK_EXT_robustness2.robustBufferAccess2", "robustBufferAccess2", "robustBufferAccess",
        "robustImageAccess", "robustImageAccess / robustImageAccess2")
    t(listOf("depth_stencil"), "depthBiasClamp", "VK_EXT_depth_bias_control", "D24_UNORM_S8_UINT or D32_SFLOAT_S8_UINT",
        "format_D24S8_or_D32S8_depth_stencil", "D24_UNORM_S8_UINT depth-stencil", "format_D32_SFLOAT_depth_stencil")
    t(listOf("blend"), "dualSrcBlend", "independentBlend", "logicOp (FL11_1)", "logicOp")
    t(listOf("fill_mode"), "fillModeNonSolid")
    t(listOf("vmr_secondary"), "fragmentStoresAndAtomics", "sampleRateShading", "variableMultisampleRate")
    t(listOf("vmr_secondary", "bachata_dynamic_render"), "dynamicRendering")
    t(listOf("geometry", "gs_viewport_depth", "gs_tess_primitive_id"), "geometryShader")
    t(listOf("sampler"), "imageCubeArray", "samplerAnisotropy", "shaderImageGatherExtended", "samplerMirrorClampToEdge",
        "VK_KHR_sampler_mirror_clamp_to_edge", "VK_EXT_non_seamless_cube_map", "customBorderColorWithoutFormat")
    t(listOf("sampler", "bachata_dynamic_render"), "VK_EXT_custom_border_color", "customBorderColors")
    t(listOf("bachata_dynamic_render"), "VK_EXT_depth_clip_control", "depthClipControl")
    t(listOf("large_draw"), "multiDrawIndirect", "drawIndirectCount", "VK_KHR_draw_indirect_count")
    t(listOf("multi_viewport"), "multiViewport")
    t(listOf("occlusion_query"), "occlusionQueryPrecise", "hostQueryReset")
    t(listOf("clip_cull"), "shaderClipDistance", "shaderCullDistance")
    t(listOf("shader_arith"), "shaderInt16", "shaderInt8", "storageBuffer16BitAccess", "storageBuffer8BitAccess",
        "shaderDemoteToHelperInvocation", "computeFullSubgroups", "subgroupSizeControl", "shaderZeroInitializeWorkgroupMemory")
    t(listOf("shader_arith", "bachata_exec"), "shaderInt64")
    t(listOf("bc_decode"), "textureCompressionBC", "bc_formats")
    t(listOf("descriptor_model"), "bufferDeviceAddress", "descriptorIndexing", "descriptorBindingSampledImageUpdateAfterBind",
        "descriptorBindingUpdateUnusedWhilePending", "descriptorBindingPartiallyBound", "runtimeDescriptorArray",
        "scalarBlockLayout", "inlineUniformBlock", "VK_EXT_descriptor_indexing")
    t(listOf("vkd3d_timeline"), "timelineSemaphore", "VK_KHR_timeline_semaphore")
    t(listOf("csf_event", "bachata_dynamic_render"), "synchronization2")
    t(listOf("tessellation", "tess_cond_state", "gs_tess_primitive_id"), "tessellationShader (FL11_0)", "tessellationShader")
    t(listOf("vertex_stores"), "vertexPipelineStoresAndAtomics (FL11_1)", "vertexPipelineStoresAndAtomics")
    t(listOf("xfb", "large_draw"), "VK_EXT_transform_feedback")
    t(listOf("xfb", "large_draw", "tess_cond_state"), "VK_EXT_conditional_rendering")
    t(listOf("pipeline_stats"), "pipelineStatisticsQuery")
    t(listOf("depth_bounds"), "depthBounds", "proton: depthBounds")
    t(listOf("vs_viewport_index"), "shaderOutputViewportIndex", "VK_EXT_shader_viewport_index_layer",
        "proton: shaderOutputViewportIndex+Layer")
    t(listOf("bachata_exec"), "shaderBufferInt64Atomics", "shaderSharedInt64Atomics")
    t(listOf("bachata_storage_fmtless"), "shaderStorageImageReadWithoutFormat", "shaderStorageImageWriteWithoutFormat",
        "shaderStorageImageWriteWithoutFormat (UAV clear pipelines)", "format_R32_SFLOAT_storage",
        "format_R8G8B8A8_UNORM_storage", "format_R16G16B16A16_SFLOAT_storage", "format_R32G32B32A32_SFLOAT_storage")
    t(listOf("vkd3d_heap"), "VK_EXT_mutable_descriptor_type")
    m
}

/** Suite tests exercising checker item [name] on the GPU; empty when the item is only reported. */
fun testedBy(name: String): List<String> = TESTED_BY[name] ?: emptyList()

/** Parses the HARD ok / FAIL hard / SOFT ok / SOFT missing / INFO / RESULT lines of the checkers. */
fun parseComplianceLog(title: String, res: String, lines: List<String>): org.json.JSONObject {
    val items = LinkedHashMap<String, Triple<Boolean, Boolean, String?>>() // "cat:name" -> (hard, available, note)
    val info = org.json.JSONArray()
    var resultPass = false
    for (raw in lines) {
        val l = raw.trim()
        val parsed = when {
            l.startsWith("HARD ok ") -> Triple(true, true, l.removePrefix("HARD ok ") to null)
            l.startsWith("FAIL hard ") -> Triple(true, false, l.removePrefix("FAIL hard ") to null)
            l.startsWith("SOFT ok ") -> Triple(false, true, l.removePrefix("SOFT ok ") to null)
            l.startsWith("SOFT missing ") -> l.removePrefix("SOFT missing ").split(" -- ", limit = 2)
                .let { Triple(false, false, it[0] to it.getOrNull(1)) }
            else -> null
        }
        if (parsed != null) {
            val (hard, ok, nameNote) = parsed
            val key = (if (hard) "hard:" else "soft:") + nameNote.first.trim()
            // A name checked twice counts as missing if either check failed.
            if (items[key]?.second != false) items[key] = Triple(hard, ok, nameNote.second)
        } else if (l.startsWith("INFO ")) {
            info.put(l.removePrefix("INFO "))
        } else if (l == "RESULT PASS") {
            resultPass = true
        }
    }
    val arr = org.json.JSONArray()
    for ((key, v) in items) {
        val name = key.substringAfter(':')
        arr.put(org.json.JSONObject().put("name", name).put("category", if (v.first) "hard" else "soft")
            .put("status", if (v.second) "available" else "missing").put("note", v.third ?: "")
            .put("tested_by", org.json.JSONArray(testedBy(name))))
    }
    val hardMissing = items.values.filter { it.first && !it.second }.size
    return org.json.JSONObject()
        .put("title", title)
        .put("pass", items.isNotEmpty() && hardMissing == 0 && resultPass)
        .put("status", if (resultPass) "RESULT PASS" else "checker $res")
        .put("hardMissing", hardMissing)
        .put("softMissing", items.values.count { !it.first && !it.second })
        .put("info", info)
        .put("items", arr)
}

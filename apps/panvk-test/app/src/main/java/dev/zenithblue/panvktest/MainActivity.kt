package dev.zenithblue.panvktest

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.content.pm.ApplicationInfo
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.OpenableColumns
import android.util.Log
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.compose.ui.window.DialogProperties
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.net.URL
import java.text.SimpleDateFormat
import java.util.ArrayDeque
import java.util.Date
import java.util.Locale
import java.util.concurrent.CancellationException
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

data class RunItem(
    val folder: File,
    val name: String,
    val passCount: Int,
    val failCount: Int = 0,
    val skipCount: Int = 0,
    val totalCount: Int,
    val compliance: String = "" // "DXVK PASS · S4 FAIL · vkd3d FAIL" from summary.json
)

enum class DriverType(val label: String) {
    BUNDLED("Bundled PanVK"),
    SYSTEM("System Vulkan"),
    IMPORTED("Imported .so")
}

data class TestCase(
    val name: String,
    val isDraw: Boolean,
    val timeoutMs: Int = 60_000,
    val extraArgsProvider: (Context) -> List<String> = { emptyList() }
)

data class TestResult(
    val name: String,
    val status: String = "IDLE", // IDLE, RUNNING, PASS, FAIL, SKIP, CRASH, TIMEOUT
    val mismatch: Long = 0,
    val fps: String? = null,
    val durationMs: Long = 0,
    val logFile: File? = null,
    val lastLines: List<String> = emptyList(),
    val isExpanded: Boolean = false,
    val extra: String? = null
)

class MainActivity : ComponentActivity() {

    private val testCases = listOf(
        TestCase("gpu_prerast_slice", isDraw = false),
        TestCase("clip_cull", isDraw = true),
        TestCase("multi_viewport", isDraw = true),
        TestCase("fill_mode", isDraw = true),
        TestCase("bc_decode", isDraw = false) { ctx -> listOf(ctx.cacheDir.absolutePath) },
        TestCase("geometry", isDraw = true),
        TestCase("tessellation", isDraw = true),
        TestCase("xfb", isDraw = true),
        TestCase("pipeline_stats", isDraw = true),
        TestCase("vertex_stores", isDraw = false),
        TestCase("gs_viewport_depth", isDraw = false),
        TestCase("vs_viewport_index", isDraw = false),
        TestCase("depth_bounds", isDraw = false),
        TestCase("large_draw", isDraw = false),
        TestCase("vmr_secondary", isDraw = false),
        TestCase("tess_cond_state", isDraw = false),
        TestCase("bachata_reqs", isDraw = false),
        TestCase("bachata_exec", isDraw = false),
        TestCase("robustness2", isDraw = false),
        TestCase("blend", isDraw = false),
        TestCase("occlusion_query", isDraw = false),
        TestCase("descriptor_model", isDraw = false),
        TestCase("shader_arith", isDraw = false),
        TestCase("depth_stencil", isDraw = false),
        TestCase("sampler", isDraw = false),
        TestCase("draw_params", isDraw = false),
        // 30 s cap (argv[4]) + device init: needs more than the 60 s default.
        TestCase("submit_stress", isDraw = false, timeoutMs = 120_000) { listOf("1000000", "1", "30") },
        TestCase("csf_event", isDraw = false),
        TestCase("gs_tess_primitive_id", isDraw = false),
        TestCase("bachata_storage_fmtless", isDraw = false),
        TestCase("bachata_dynamic_render", isDraw = false),
        // Requirement checkers: FAIL only when a hard requirement is missing (soft gaps still PASS).
        TestCase("dxvk_reqs", isDraw = false),
        TestCase("vkd3d_reqs", isDraw = false),
        TestCase("vkd3d_heap", isDraw = false),
        TestCase("vkd3d_timeline", isDraw = false),
        TestCase("swapchain_lifecycle", isDraw = true)
    )

    private var driverTypeState = mutableStateOf(DriverType.BUNDLED)
    private var mesaDebugEnabledState = mutableStateOf(false)
    private var mesaDebugStrState = mutableStateOf("MESA_DEBUG=1 PANVK_DEBUG=trace")
    private var importedFileNameState = mutableStateOf<String?>(null)
    private val driverUpdate by lazy { DriverUpdate(this) }

    // Info tab state
    private var infoLoadingState = mutableStateOf(false)
    private var infoRawTextState = mutableStateOf<String?>(null)
    private var infoRawJsonState = mutableStateOf<String?>(null)
    private var infoParsedState = mutableStateOf<ParsedVulkanInfo?>(null)

    // Tests tab state
    private var testResultsState = mutableStateOf(
        testCases.map { TestResult(it.name) }
    )
    private var isRunningAllState = mutableStateOf(false)
    @Volatile private var swapSurface: android.view.Surface? = null
    // Live surface card is composed only while a surface test runs (name = test using it).
    private val surfaceTestState = mutableStateOf<String?>(null)
    @Volatile private var hungSwapThread: Thread? = null

    // Selected tab: 0=Driver, 1=Info, 2=Tests, 3=Logs
    private var selectedTabState = mutableIntStateOf(0)

    // Logs tab state
    private var selectedLogFileState = mutableStateOf<File?>(null)
    private var selectedLogTextState = mutableStateOf<String?>(null)
    private var logFilesListState = mutableStateOf<List<File>>(emptyList())
    private var runsListState = mutableStateOf<List<RunItem>>(emptyList())
    private val logsSeq = AtomicInteger(0)
    private val runsSeq = AtomicInteger(0)
    private var uploadEndpoint by mutableStateOf(PANVK_UPLOAD_ENDPOINT)

    // Auto-upload after "Run all": project endpoint only (never catbox/gofile). Toggle persisted in prefs "autoUpload".
    private var autoUploadOn by mutableStateOf(true)
    private var autoUploadStatus by mutableStateOf<String?>(null)
    private var autoUploadFailed by mutableStateOf(false)
    private val autoUploadBusy = AtomicBoolean(false)
    @Volatile private var lastAutoRun: File? = null
    // Debug builds only (intent extra --ez uploadDryRun true): build the payload, log it, skip the network.
    @Volatile private var uploadDryRun = false

    private fun saveDriverSelection(type: DriverType, importedName: String? = null) {
        val sp = getSharedPreferences("panprobe", Context.MODE_PRIVATE)
        val editor = sp.edit().putString("driver", type.name)
        if (importedName != null) {
            editor.putString("importedName", importedName)
        }
        editor.apply()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val sp = getSharedPreferences("panprobe", Context.MODE_PRIVATE)
        val savedImportedName = sp.getString("importedName", null)
        if (savedImportedName != null) {
            importedFileNameState.value = savedImportedName
        }
        val savedDriver = sp.getString("driver", null)
        if (savedDriver != null) {
            val loadedType = when (savedDriver.uppercase(Locale.US)) {
                "BUNDLED" -> DriverType.BUNDLED
                "SYSTEM" -> DriverType.SYSTEM
                "IMPORTED" -> {
                    val importedSo = File(filesDir, "imported/libimported.so")
                    if (importedSo.exists()) DriverType.IMPORTED else DriverType.BUNDLED
                }
                else -> DriverType.BUNDLED
            }
            driverTypeState.value = loadedType
        }

        // Read intent extras for headless testing
        val driverExtra = intent.getStringExtra("driver")
        if (driverExtra != null) {
            when (driverExtra.lowercase(Locale.US)) {
                "bundled" -> {
                    driverTypeState.value = DriverType.BUNDLED
                    saveDriverSelection(DriverType.BUNDLED)
                }
                "system" -> {
                    driverTypeState.value = DriverType.SYSTEM
                    saveDriverSelection(DriverType.SYSTEM)
                }
                "imported" -> {
                    driverTypeState.value = DriverType.IMPORTED
                    saveDriverSelection(DriverType.IMPORTED)
                }
            }
        }

        // Debug builds only (checked in DriverUpdate): --es updateAs 0.1.0-beta.15 makes an older release look newer.
        intent.getStringExtra("updateAs")?.let { DriverUpdate.debugInstalledAs = it.ifEmpty { null } }

        val extraEndpoint = intent.getStringExtra("uploadEndpoint")
        uploadEndpoint = if (extraEndpoint != null) {
            val parsed = try { URL(extraEndpoint) } catch (_: Exception) { null }
            val isValid = parsed != null &&
                (parsed.protocol.equals("http", ignoreCase = true) || parsed.protocol.equals("https", ignoreCase = true)) &&
                (parsed.host.equals("127.0.0.1", ignoreCase = true) || parsed.host.equals("localhost", ignoreCase = true))
            if (isValid) extraEndpoint else PANVK_UPLOAD_ENDPOINT
        } else {
            PANVK_UPLOAD_ENDPOINT
        }

        autoUploadOn = sp.getBoolean("autoUpload", true)
        uploadDryRun = intent.getBooleanExtra("uploadDryRun", false) &&
            applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0

        refreshLogsList()
        refreshRunsList()
        lifecycleScope.launch { driverUpdate.check(manual = false) }

        val autorunExtra = intent.getStringExtra("autorun")
        // One queued retry (set when a Run all finished offline), at the next normal start.
        val pending = sp.getString("pendingRun", null)
        if (pending != null) {
            sp.edit().remove("pendingRun").apply()
            val dir = File(File(filesDir, "runs"), pending)
            if (autorunExtra == null && autoUploadOn && dir.isDirectory) {
                lifecycleScope.launch(Dispatchers.IO) { autoUpload(dir, fromQueue = true) }
            }
        }
        // autorun = "all" or a single test name (e.g. gs_viewport_depth)
        if (autorunExtra != null && savedInstanceState == null) {
            selectedTabState.intValue = 2 // Switch UI to Tests tab
            lifecycleScope.launch(Dispatchers.IO) {
                runHeadlessAutorun(autorunExtra)
            }
        }

        setContent {
            PanvkTheme {
                Surface(
                    modifier = Modifier.fillMaxSize().safeDrawingPadding(),
                    color = MaterialTheme.colorScheme.background
                ) {
                    MainScreen()
                }
            }
        }
    }

    private fun refreshLogsList() {
        lifecycleScope.launch(Dispatchers.IO) {
            val seq = logsSeq.incrementAndGet()
            val logsDir = File(filesDir, "logs")
            val files = if (logsDir.exists()) {
                (logsDir.listFiles() ?: emptyArray())
                    .filter { it.isFile }
                    .sortedByDescending { it.lastModified() }
            } else {
                emptyList()
            }
            withContext(Dispatchers.Main) {
                if (seq == logsSeq.get()) {
                    logFilesListState.value = files
                }
            }
        }
    }

    private fun refreshRunsList() {
        lifecycleScope.launch(Dispatchers.IO) {
            val seq = runsSeq.incrementAndGet()
            val runsDir = File(filesDir, "runs")
            val items = if (runsDir.exists()) {
                val folders = (runsDir.listFiles() ?: emptyArray())
                    .filter { it.isDirectory }
                    .sortedByDescending { it.name }
                folders.map { folder ->
                    val summaryFile = File(folder, "summary.json")
                    var pass = 0
                    var fail = 0
                    var skip = 0
                    var total = 0
                    var compliance = ""
                    if (summaryFile.exists()) {
                        try {
                            val json = JSONObject(summaryFile.readText())
                            pass = json.optInt("pass", json.optInt("passCount", 0))
                            fail = json.optInt("fail", json.optInt("failCount", 0))
                            skip = json.optInt("skip", json.optInt("skipCount", 0))
                            total = json.optInt("total", 0)
                            if (!json.has("fail") && total > 0) {
                                fail = (total - pass - skip).coerceAtLeast(0)
                            }
                            json.optJSONObject("compliance")?.let { c ->
                                compliance = listOf("dxvk" to "DXVK", "bachata_s4" to "S4", "vkd3d" to "vkd3d").mapNotNull { (k, label) ->
                                    c.optJSONObject(k)?.let { "$label ${if (it.optBoolean("pass")) "PASS" else "FAIL"}" }
                                }.joinToString(" · ")
                            }
                        } catch (_: Exception) {}
                    }
                    RunItem(folder = folder, name = folder.name, passCount = pass, failCount = fail, skipCount = skip, totalCount = total, compliance = compliance)
                }
            } else {
                emptyList()
            }
            withContext(Dispatchers.Main) {
                if (seq == runsSeq.get()) {
                    runsListState.value = items
                }
            }
        }
    }

    @Suppress("DEPRECATION")
    private fun getAppVersion(): String {
        return try {
            packageManager.getPackageInfo(packageName, 0).versionName ?: "1.0.0"
        } catch (_: Exception) {
            "1.0.0"
        }
    }

    private fun loadBundledDriverJson(): JSONObject? {
        return try {
            assets.open("bundled-driver.json").bufferedReader().use { JSONObject(it.readText()) }
        } catch (_: Exception) {
            null
        }
    }

    private suspend fun saveRun(results: List<TestResult>): File? {
        return try { writeRun(results) } catch (e: Exception) { Log.e("PanVKTest", "saveRun failed", e); null }
    }

    private fun isOnline(): Boolean = try {
        val cm = getSystemService(ConnectivityManager::class.java)
        cm.getNetworkCapabilities(cm.activeNetwork)?.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) == true
    } catch (_: Exception) { false }

    /**
     * Upload the run zip to the project's own endpoint (never public hosts). Honors the toggle; offline queues
     * the run once for the next app start. Status shown on the Tests page. Dry run: build + log, no network.
     */
    private suspend fun autoUpload(run: File, fromQueue: Boolean = false) {
        if (!autoUploadOn) { say("AUTOUPLOAD skipped: toggle off"); return }
        if (!autoUploadBusy.compareAndSet(false, true)) return
        lastAutoRun = run
        autoUploadFailed = false
        try {
            val endpoint = uploadEndpoint
            if (!uploadDryRun && !isOnline()) {
                if (!fromQueue) getSharedPreferences("panprobe", Context.MODE_PRIVATE).edit().putString("pendingRun", run.name).apply()
                autoUploadStatus = if (fromQueue) "Offline: not uploaded" else "Offline: upload queued for next app start"
                say("AUTOUPLOAD offline fromQueue=$fromQueue")
                return
            }
            autoUploadStatus = "Uploading results..."
            val zip = buildRunZip(run)
            val sha = withContext(Dispatchers.IO) { sha256(zip) }
            val id = sha.take(8)
            if (uploadDryRun) {
                val entries = java.util.zip.ZipFile(zip).use { zf -> zf.entries().toList().map { "${it.name}:${it.size}" } }
                say("AUTOUPLOAD DRYRUN target=${endpoint.trimEnd('/')}/upload-url size=${zip.length()} sha256=$sha")
                say("AUTOUPLOAD DRYRUN manifest ${entries.joinToString(" ")}")
                withContext(Dispatchers.IO) { zip.copyTo(File(getExternalFilesDir(null), zip.name), overwrite = true) }
                autoUploadStatus = "Dry run, not sent (id $id)"
                return
            }
            withContext(Dispatchers.IO) {
                val res = uploadToR2(endpoint = endpoint, f = zip, sha256Hex = sha, version = getAppVersion()) { _, _ -> }
                val v = verifyUpload(res.directUrl, sha, getAppVersion())
                if (v != "Verified ✓") throw java.io.IOException(v)
                postRecord(endpoint, buildUploadRecord(zip, sha, endpoint, UploadPathState("-"),
                    UploadPathState("PanVK storage", url = res.url, verifyStatus = "✓")))
            }
            autoUploadStatus = "Uploaded (id $id)"
            say("AUTOUPLOAD OK id=$id size=${zip.length()} sha256=$sha")
        } catch (e: Exception) {
            autoUploadFailed = true
            val msg = if (e is ProjectStorageTooBigException) "zip over 25 MiB" else friendlyUploadError(e)
            autoUploadStatus = "Upload failed: $msg"
            say("AUTOUPLOAD FAIL $msg")
        } finally {
            autoUploadBusy.set(false)
        }
    }

    private suspend fun writeRun(results: List<TestResult>): File = withContext(Dispatchers.IO) {
        val timestamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(Date())
        val runsDir = File(filesDir, "runs").apply { mkdirs() }
        var runFolder = File(runsDir, timestamp)
        var n = 2
        while (runFolder.exists()) runFolder = File(runsDir, "$timestamp-${n++}")
        runFolder.mkdirs()

        for (res in results) {
            val testObj = JSONObject().apply {
                put("name", res.name)
                put("status", res.status)
                put("mismatch", res.mismatch)
                if (res.fps != null) put("fps", res.fps) else put("fps", JSONObject.NULL)
                put("durationMs", res.durationMs)
                if (res.extra != null) put("extra", res.extra) else put("extra", JSONObject.NULL)
                if (res.logFile != null) put("log", res.logFile.name) else put("log", JSONObject.NULL)
                put("lastLines", JSONArray(res.lastLines))
            }
            File(runFolder, "${res.name}.json").writeText(testObj.toString(2))
            res.logFile?.let { srcLog ->
                if (srcLog.exists()) {
                    try {
                        srcLog.copyTo(File(runFolder, srcLog.name), overwrite = true)
                    } catch (_: Exception) {}
                }
            }
        }

        // Always re-query: cached info may belong to a previously selected driver.
        val (rawJsonStr, parsedObj) = run {
            val (obj, raw) = runInfo()
            if (obj != null) {
                val s = obj.toString(2)
                withContext(Dispatchers.Main) {
                    infoRawJsonState.value = s
                    infoParsedState.value = parseVulkanInfo(obj)
                    infoRawTextState.value = null
                }
                Pair(s, obj)
            } else {
                val errObj = JSONObject().apply {
                    put("error", raw)
                    put("glInfo", glInfoJson())
                }
                Pair(errObj.toString(2), null)
            }
        }
        File(runFolder, "device_info.json").writeText(rawJsonStr)
        // vkinfo stderr: driver init lines (kbase uAPI, gpu_id, BC emulation) and VKDBG messenger output.
        File(filesDir, "logs").listFiles { f -> f.name.endsWith("-vkinfo.log") }?.maxByOrNull { it.lastModified() }
            ?.let { try { it.copyTo(File(runFolder, "vkinfo.log"), overwrite = true) } catch (_: Exception) {} }
        // Compliance reports (from the runInfo above) and their checker logs.
        val compliance = parsedObj?.optJSONObject("compliance")
        if (compliance != null) File(runFolder, "compliance.json").writeText(compliance.toString(2))
        for ((_, _, test) in COMPLIANCE_CHECKERS) {
            File(filesDir, "logs").listFiles { f -> f.name.endsWith("-compliance-$test.log") }?.maxByOrNull { it.lastModified() }
                ?.let { try { it.copyTo(File(runFolder, "compliance-$test.log"), overwrite = true) } catch (_: Exception) {} }
        }

        val summaryObj = JSONObject().apply {
            put("timestamp", timestamp)
            // Logcat window start for the upload zip: run duration plus margin.
            put("startMs", System.currentTimeMillis() - results.sumOf { it.durationMs } - 60_000L)
            put("appVersion", getAppVersion())
            put("driverType", driverTypeState.value.label)

            val firstDevProps = parsedObj?.optJSONArray("devices")?.optJSONObject(0)?.optJSONObject("properties")
            if (firstDevProps != null) {
                for (key in listOf("deviceName", "driverName", "driverInfo", "driverVersion", "apiVersion")) {
                    val v = firstDevProps.opt(key)
                    if (v != null && v != JSONObject.NULL) {
                        put(key, v)
                    }
                }
            }

            val passCount = results.count { it.status == "PASS" }
            val failCount = results.count { it.status in listOf("FAIL", "CRASH", "TIMEOUT") }
            val skipCount = results.count { it.status == "SKIP" }
            put("pass", passCount)
            put("fail", failCount)
            put("skip", skipCount)
            put("total", results.size)

            val testsObj = JSONObject()
            for (res in results) {
                testsObj.put(res.name, res.status)
            }
            put("tests", testsObj)
            // Explicit per-test list, driven by the run's results (new suite tests need no zip/upload change).
            put("results", JSONArray().apply {
                for (res in results) put(JSONObject().put("name", res.name).put("status", res.status)
                    .put("durationMs", res.durationMs).put("log", res.logFile?.name ?: JSONObject.NULL))
            })
            if (compliance != null) put("compliance", complianceSummary(compliance))
        }
        File(runFolder, "summary.json").writeText(summaryObj.toString(2))
        runFolder
    }

    /** Compact per-report verdict for summary.json / manifest.json: title, pass, missing hard and soft names. */
    private fun complianceSummary(reports: JSONObject): JSONObject = JSONObject().apply {
        for (key in reports.keys()) {
            val r = reports.optJSONObject(key) ?: continue
            val items = r.optJSONArray("items") ?: JSONArray()
            fun missing(cat: String) = JSONArray((0 until items.length()).map { items.getJSONObject(it) }
                .filter { it.optString("category") == cat && it.optString("status") == "missing" }.map { it.optString("name") })
            put(key, JSONObject().put("title", r.optString("title")).put("pass", r.optBoolean("pass"))
                .put("items", items.length()).put("missingHard", missing("hard")).put("missingSoft", missing("soft")))
        }
    }

    private suspend fun buildRunZip(runFolder: File): File = withContext(Dispatchers.IO) {
        val shareDir = File(cacheDir, "share").apply { mkdirs() }
        val stageDir = File(shareDir, "stage-${runFolder.name}-${System.nanoTime()}").apply {
            deleteRecursively()
            mkdirs()
        }
        val zipFile = File(shareDir, "panprobe-${runFolder.name}.zip")

        try {
            // 1. <run>/ folder
            val stagedRun = File(stageDir, runFolder.name)
            runFolder.copyRecursively(stagedRun, overwrite = true)

            // 2. vulkan-info.json: the run's device_info.json content (or infoRawJsonState if run has none)
            val runDevInfo = File(runFolder, "device_info.json")
            val vkJsonStr = when {
                runDevInfo.exists() -> runDevInfo.readText()
                infoRawJsonState.value != null -> infoRawJsonState.value
                else -> null
            }?.let { raw ->
                try {
                    JSONObject(raw).apply { put("glInfo", glInfoJson()) }.toString(2)
                } catch (_: Exception) {
                    raw
                }
            }
            if (vkJsonStr != null) {
                File(stageDir, "vulkan-info.json").writeText(vkJsonStr)
            }

            // 3. Cap staged run logs (head+tail), then logcat.txt: all tags/pids readable, bounded to the run window.
            val logsInfo = JSONArray()
            stagedRun.walkTopDown().filter { it.isFile && it.length() > UploadLogs.LOG_CAP }.toList().forEach { f ->
                val tmp = File(f.parentFile, f.name + ".full")
                f.renameTo(tmp)
                logsInfo.put(UploadLogs.capCopy(tmp, f).put("path", f.relativeTo(stageDir).path))
                tmp.delete()
            }
            val runStartMs = try { JSONObject(File(runFolder, "summary.json").readText()).optLong("startMs", 0L) } catch (_: Exception) { 0L }
                .takeIf { it > 0 } ?: (runFolder.lastModified() - 10 * 60_000L)
            val logcatFile = File(stageDir, "logcat.txt")
            val logcatInfo = try { UploadLogs.logcat(this@MainActivity, logcatFile, runStartMs) }
                catch (e: Exception) { JSONObject().put("error", e.toString()) }
            val driverLines = UploadLogs.driverLines(
                stagedRun.walkTopDown().filter { it.isFile && it.name.endsWith(".log") }.map { it.readText() } +
                    sequenceOf(if (logcatFile.isFile) logcatFile.readText() else "")
            )
            val deviceFacts = UploadLogs.deviceFacts(
                try { Native.kbaseVersion() } catch (t: Throwable) { "none: $t" }, driverLines
            )

            // 4. system/ extras
            val systemDir = File(stageDir, "system").apply { mkdirs() }

            var gpuinfoRaw: String? = null
            var gpuinfoUnavailableReason: String? = null
            try {
                val f = File("/sys/class/misc/mali0/device/gpuinfo")
                if (!f.exists()) {
                    gpuinfoUnavailableReason = "file missing"
                } else if (!f.canRead()) {
                    gpuinfoUnavailableReason = "not readable (SELinux/permissions)"
                } else {
                    val text = f.readText().trim()
                    if (text.isNotEmpty()) {
                        File(systemDir, "gpuinfo.txt").writeText(text)
                        gpuinfoRaw = text
                    } else {
                        gpuinfoUnavailableReason = "not readable (SELinux/permissions)"
                    }
                }
            } catch (e: Exception) {
                gpuinfoUnavailableReason = e.message ?: e.toString()
            }

            var procVersionText: String? = null
            try {
                val f = File("/proc/version")
                if (f.canRead()) {
                    val text = f.readText().trim()
                    if (text.isNotEmpty()) {
                        File(systemDir, "proc_version.txt").writeText(text)
                        procVersionText = text
                    }
                }
            } catch (_: Exception) {}

            try {
                val proc = Runtime.getRuntime().exec(arrayOf("getprop"))
                val lines = proc.inputStream.bufferedReader().use { it.readLines() }
                val keys = listOf("ro.product.", "ro.board.", "ro.soc.", "ro.hardware", "ro.build.version.", "ro.build.fingerprint")
                val filtered = lines.filter { line -> keys.any { line.contains(it) } }
                File(systemDir, "props.txt").writeText(filtered.joinToString("\n"))
            } catch (_: Exception) {}

            // 5. Driver resolution: read summary.json driverType if present; fall back to current driverTypeState
            val summaryFile = File(runFolder, "summary.json")
            val summaryDriverTypeLabel = if (summaryFile.exists()) {
                try { JSONObject(summaryFile.readText()).optString("driverType").takeIf { it.isNotEmpty() } } catch (_: Exception) { null }
            } else null

            val resolvedDriverType = DriverType.entries.firstOrNull { it.label == summaryDriverTypeLabel }
                ?: driverTypeState.value
            val driverPath = getDriverPath(resolvedDriverType)
            val driverFile = File(driverPath)
            val driverReadable = driverFile.exists() && driverFile.canRead()
            val runtimeDriverSha256 = driverSoSha256(driverPath)
            val driverBuildId = if (driverReadable) extractGnuBuildId(driverFile) else null

            // 6. Parse vulkan JSON for properties
            val vkJsonObj = try { vkJsonStr?.let { JSONObject(it) } } catch (_: Exception) { null }
            val firstDevProps = vkJsonObj?.optJSONArray("devices")?.optJSONObject(0)?.optJSONObject("properties")
            fun optVal(key: String): Any {
                val v = firstDevProps?.opt(key)
                return if (v == null || v == JSONObject.NULL || v == "null") JSONObject.NULL else v
            }

            // A failed PanVK probe must not turn the SoC's hardware name into a GPU model.
            // Keep the selected driver's properties separate from the system GPU fallback.
            val gpuProps = if (gpuName(firstDevProps?.opt("deviceName")) != null) {
                firstDevProps
            } else if (resolvedDriverType != DriverType.SYSTEM) {
                runInfo(DriverType.SYSTEM).first?.optJSONArray("devices")
                    ?.optJSONObject(0)?.optJSONObject("properties")
            } else null
            fun gpuVal(key: String): Any = gpuProps?.opt(key)
                ?.takeUnless { it == JSONObject.NULL || it == "null" } ?: JSONObject.NULL

            // 6b. driver-load.json: which driver was loaded, its identity, and whether loading worked.
            run {
                val bundledMeta = if (resolvedDriverType == DriverType.BUNDLED) loadBundledDriverJson() else null
                val importedName = importedFileNameState.value
                val source = when (resolvedDriverType) {
                    DriverType.BUNDLED -> "bundled"
                    DriverType.SYSTEM -> "system"
                    DriverType.IMPORTED -> if (importedName?.endsWith("GitHub)") == true) "downloaded" else "imported"
                }
                val infoError = vkJsonObj?.optString("error")?.takeIf { it.isNotEmpty() }
                val deviceCount = vkJsonObj?.optJSONArray("devices")?.length() ?: 0
                val loaded = vkJsonObj != null && infoError == null && deviceCount > 0
                val vkinfoTail = File(stagedRun, "vkinfo.log").takeIf { it.isFile }?.readLines()?.takeLast(20) ?: emptyList()
                File(stageDir, "driver-load.json").writeText(JSONObject().apply {
                    put("source", source)
                    put("driverType", resolvedDriverType.label)
                    if (resolvedDriverType == DriverType.IMPORTED) put("importedName", importedName ?: JSONObject.NULL)
                    put("path", driverPath)
                    put("readable", driverReadable)
                    put("soSha256", runtimeDriverSha256 ?: JSONObject.NULL)
                    put("buildId", driverBuildId ?: JSONObject.NULL)
                    put("driverName", optVal("driverName"))
                    put("driverVersion", optVal("driverVersion"))
                    put("driverInfo", optVal("driverInfo"))
                    if (bundledMeta != null) put("bundledRelease", JSONObject().apply {
                        for (k in listOf("id", "name", "packageVersion", "displayVersion", "sourceCommit", "buildId"))
                            put(k, bundledMeta.opt(k) ?: JSONObject.NULL)
                        put("expectedSha256", bundledMeta.optJSONObject("release")?.opt("sha256") ?: JSONObject.NULL)
                    })
                    put("loadSuccess", loaded)
                    put("deviceCount", deviceCount)
                    put("loadError", if (loaded) JSONObject.NULL else
                        (infoError ?: if (vkJsonObj == null) "no device info" else "no Vulkan device enumerated").take(2000))
                    put("vkinfoLogTail", JSONArray(vkinfoTail))
                    put("kbaseUapi", deviceFacts.opt("kbaseUapi") ?: JSONObject.NULL)
                    put("gpuId", deviceFacts.opt("gpuId") ?: JSONObject.NULL)
                    put("bcEmulation", deviceFacts.opt("bcEmulation") ?: JSONObject.NULL)
                    put("pageSize", deviceFacts.opt("pageSize") ?: JSONObject.NULL)
                    put("appVersion", getAppVersion())
                }.toString(2))
            }

            // 7. Files array of all staged files (excluding manifest.json)
            val filesArray = JSONArray()
            val stagedFiles = stageDir.walkTopDown().filter { it.isFile }.sortedBy { it.relativeTo(stageDir).path }
            for (f in stagedFiles) {
                val relPath = f.relativeTo(stageDir).path.replace('\\', '/')
                filesArray.put(JSONObject().apply {
                    put("path", relPath)
                    put("size", f.length())
                    put("sha256", sha256(f))
                })
            }

            // 8. manifest.json
            val pInfo = try { packageManager.getPackageInfo(packageName, 0) } catch (_: Exception) { null }
            val glInfo = queryGlInfo()
            val manifestObj = JSONObject().apply {
                put("timestamp", SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(Date()))
                put("app", JSONObject().apply {
                    put("versionName", pInfo?.versionName ?: getAppVersion())
                    put("versionCode", pInfo?.longVersionCode ?: 0L)
                })
                val bundledDriverMeta = if (resolvedDriverType == DriverType.BUNDLED) loadBundledDriverJson() else null
                // Bundled driver: always the pinned release metadata, so the record names it even when enumeration fails.
                val driverNameVal: Any = bundledDriverMeta?.optString("name")?.takeIf { it.isNotEmpty() }
                    ?: optVal("driverName").takeUnless { it == JSONObject.NULL }
                    ?: resolvedDriverType.label
                val driverVersionVal: Any = bundledDriverMeta?.let {
                    it.optString("displayVersion").takeIf { v -> v.isNotEmpty() } ?: it.optString("packageVersion").takeIf { v -> v.isNotEmpty() }
                } ?: optVal("driverVersion")

                put("driver", JSONObject().apply {
                    put("type", resolvedDriverType.label)
                    put("bundled", resolvedDriverType == DriverType.BUNDLED)
                    put("path", driverPath)
                    put("soSha256", runtimeDriverSha256 ?: JSONObject.NULL)
                    put("buildId", driverBuildId ?: JSONObject.NULL)
                    put("driverName", driverNameVal)
                    put("driverInfo", optVal("driverInfo"))
                    put("driverVersion", driverVersionVal)
                })
                put("device", JSONObject().apply {
                    put("manufacturer", android.os.Build.MANUFACTURER)
                    put("model", android.os.Build.MODEL)
                    put("board", android.os.Build.BOARD)
                    put("hardware", android.os.Build.HARDWARE)
                    if (android.os.Build.VERSION.SDK_INT >= 31) {
                        put("socManufacturer", android.os.Build.SOC_MANUFACTURER)
                        put("socModel", android.os.Build.SOC_MODEL)
                    } else {
                        put("socManufacturer", JSONObject.NULL)
                        put("socModel", JSONObject.NULL)
                    }
                })
                put("gpu", JSONObject().apply {
                    put("glRenderer", glInfo?.renderer ?: JSONObject.NULL)
                    put("glVendor", glInfo?.vendor ?: JSONObject.NULL)
                    put("glVersion", glInfo?.version ?: JSONObject.NULL)
                    val vkDeviceName = gpuName(gpuVal("deviceName"))
                    val gpuinfoModel = if (gpuinfoRaw != null) Regex("""Mali-[A-Za-z0-9]+""").find(gpuinfoRaw)?.value else null
                    val gpuModel = vkDeviceName ?: gpuinfoModel

                    put("deviceName", vkDeviceName ?: JSONObject.NULL)
                    put("deviceID", gpuVal("deviceID"))
                    put("vendorID", gpuVal("vendorID"))
                    put("apiVersion", gpuVal("apiVersion"))
                    put("gpuinfo", gpuinfoRaw ?: JSONObject.NULL)
                    if (gpuinfoRaw == null) {
                        put("gpuinfo_unavailable_reason", gpuinfoUnavailableReason ?: "not readable (SELinux/permissions)")
                    }
                    // gpuinfo sysfs is usually SELinux-blocked; fall back to the Vulkan deviceID,
                    // which is the Mali gpu_id on ARM (vendor 0x13B5). Arch major = gpu_id[31:28].
                    val vkId = (gpuVal("deviceID") as? Number)?.toLong()
                    val isArm = (gpuVal("vendorID") as? Number)?.toLong() == 0x13B5L
                    val gpuId = (if (gpuinfoRaw != null) Regex("""0x[0-9a-fA-F]+""").find(gpuinfoRaw)?.value else null)
                        ?: if (isArm && vkId != null) "0x%08x".format(vkId) else null
                    put("gpuId", gpuId ?: JSONObject.NULL)
                    val archMajor = gpuId?.removePrefix("0x")?.toLongOrNull(16)?.let { (it ushr 28) and 0xF }
                    put("arch", archMajor?.let { "v$it" } ?: JSONObject.NULL)
                    put("gpuModel", gpuModel ?: JSONObject.NULL)
                })
                put("android", JSONObject().apply {
                    put("release", android.os.Build.VERSION.RELEASE)
                    put("sdk", android.os.Build.VERSION.SDK_INT)
                    put("kernel", procVersionText ?: System.getProperty("os.version") ?: JSONObject.NULL)
                })
                val summaryFile = File(runFolder, "summary.json")
                val summaryObj = try {
                    if (summaryFile.exists()) JSONObject(summaryFile.readText()) else null
                } catch (_: Exception) { null }
                if (summaryObj != null) {
                    summaryObj.optJSONObject("tests")?.let { put("tests", it) }
                    put("pass", summaryObj.optInt("pass", 0))
                    put("fail", summaryObj.optInt("fail", 0))
                    put("skip", summaryObj.optInt("skip", 0))
                    put("total", summaryObj.optInt("total", 0))
                    summaryObj.optJSONArray("results")?.let { put("results", it) }
                    summaryObj.optJSONObject("compliance")?.let { put("compliance", it) }
                }
                put("deviceFacts", deviceFacts)
                put("logcat", logcatInfo)
                put("truncatedLogs", logsInfo)
                put("files", filesArray)
            }
            File(stageDir, "manifest.json").writeText(manifestObj.toString(2))

            // 9. Zip staged files
            zipFiles(zipFile, listOf(Pair("", stageDir)))

            // 10. Self-check
            verifyZip(zipFile)

            zipFile
        } finally {
            stageDir.deleteRecursively()
        }
    }

    private fun getDriverPath(type: DriverType): String {
        return when (type) {
            DriverType.BUNDLED -> File(applicationInfo.nativeLibraryDir, "libvulkan_panfrost.so").absolutePath
            DriverType.SYSTEM -> "/system/lib64/libvulkan.so"
            DriverType.IMPORTED -> File(filesDir, "imported/libimported.so").absolutePath
        }
    }

    private fun buildEnv(isDraw: Boolean): Array<String> {
        val envList = mutableListOf<String>()
        // Driver lines to the test log (stderr) and to logcat for the upload zip.
        envList.add("MESA_LOG=file,android")
        envList.add("TMPDIR=${cacheDir.absolutePath}")
        if (mesaDebugEnabledState.value) {
            mesaDebugStrState.value.trim().split(Regex("\\s+"))
                .filter { it.isNotEmpty() && it.contains("=") }
                .forEach { envList.add(it) }
            if (envList.none { it.startsWith("PANDECODE_DUMP_FILE=") }) {
                envList.add("PANDECODE_DUMP_FILE=stderr")
            }
        }
        if (isDraw) {
            envList.add("PT_FPS_MS=1000")
        }
        return envList.toTypedArray()
    }

    private fun createLogFile(name: String): File {
        val logsDir = File(filesDir, "logs").apply { mkdirs() }
        val timestamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(Date())
        return File(logsDir, "$timestamp-$name.log")
    }

    private suspend fun runInfo(type: DriverType = driverTypeState.value): Pair<JSONObject?, String> = withContext(Dispatchers.IO) {
        val logFile = createLogFile("vkinfo")
        val libPath = File(applicationInfo.nativeLibraryDir, "libt_vkinfo.so").absolutePath
        val driverPath = getDriverPath(type)
        val env = buildEnv(isDraw = false)
        val jsonFile = File(cacheDir, "vkinfo.json").apply { delete() }
        val res = Native.run(libPath, arrayOf(driverPath, jsonFile.absolutePath), env, logFile.absolutePath, 30000)

        val logContent = if (logFile.exists()) readLogCapped(logFile) else ""
        refreshLogsList()

        val jsonText = if (jsonFile.exists()) jsonFile.readText() else ""
        val jsonObject = try { JSONObject(jsonText) } catch (_: Exception) { null }
        jsonObject?.put("glInfo", glInfoJson())
        // Compliance evaluators: query-only, out of process like vkinfo; logs land next to vkinfo's.
        if (jsonObject != null) {
            // Keyed so backends/scripts can read compliance.dxvk / compliance.bachata_s4 directly.
            val reports = JSONObject()
            for ((key, title, test) in COMPLIANCE_CHECKERS) {
                val lib = File(applicationInfo.nativeLibraryDir, "libt_$test.so").absolutePath
                val log = createLogFile("compliance-$test")
                reports.put(key, try { JSONObject(Native.compliance(title, lib, driverPath, env, log.absolutePath)) }
                    catch (e: Exception) { JSONObject().put("title", title).put("pass", false).put("status", e.toString()) })
            }
            jsonObject.put("compliance", reports)
            refreshLogsList()
        }

        Pair(jsonObject, if (jsonObject != null) jsonObject.toString(2) else "Result: $res\n$logContent")
    }


    private suspend fun executeTest(test: TestCase): TestResult = withContext(Dispatchers.IO) {
        if (test.name == "swapchain_lifecycle") return@withContext executeSwapchainTest()
        val t0 = System.currentTimeMillis()
        val logFile = createLogFile(test.name)
        val libPath = File(applicationInfo.nativeLibraryDir, "libt_${test.name}.so").absolutePath
        val driverPath = getDriverPath(driverTypeState.value)
        val args = (listOf(driverPath) + test.extraArgsProvider(this@MainActivity)).toTypedArray()
        val env = buildEnv(test.isDraw)

        val res = Native.run(libPath, args, env, logFile.absolutePath, test.timeoutMs)
        val durationMs = System.currentTimeMillis() - t0

        var hasFail = false
        var hasSkip = false
        var mismatchSum = 0L
        var fps: String? = null
        val mismatchRegex = Regex("""mismatch=(\d+)""")
        val fpsRegex = Regex("""FPS ([0-9.]+)""")
        val last40 = ArrayDeque<String>(40)
        if (logFile.exists()) scanLog(logFile) { line ->
            val trimmed = line.trimStart()
            if (trimmed.startsWith("FAIL") || trimmed.startsWith("RESULT FAIL")) hasFail = true
            if (trimmed.startsWith("RESULT SKIP")) hasSkip = true
            mismatchSum += mismatchRegex.findAll(line)
                .sumOf { it.groupValues[1].toLongOrNull() ?: 0L }
            if (fps == null) fps = fpsRegex.find(line)?.groupValues?.get(1)
            if (last40.size == 40) last40.removeFirst()
            last40.addLast(line)
        }

        val status = when {
            res == "timeout" -> "TIMEOUT"
            res.startsWith("signal:") -> {
                try {
                    val crashFile = File(logFile.parentFile, "${logFile.nameWithoutExtension}.crash")
                    logFile.copyTo(crashFile, overwrite = true)
                } catch (_: Exception) {}
                "CRASH"
            }
            hasFail -> "FAIL"
            res == "exit:0" && hasSkip -> "SKIP"
            res == "exit:0" -> "PASS"
            else -> "FAIL"
        }

        refreshLogsList()

        TestResult(
            name = test.name,
            status = status,
            mismatch = mismatchSum,
            fps = fps,
            durationMs = durationMs,
            logFile = logFile,
            lastLines = last40.toList()
        )
    }

    private suspend fun executeSwapchainTest(): TestResult = withContext(Dispatchers.IO) {
        val t0 = System.currentTimeMillis()
        if (hungSwapThread?.isAlive == true) {
            return@withContext TestResult(
                name = "swapchain_lifecycle",
                status = "FAIL",
                durationMs = System.currentTimeMillis() - t0,
                lastLines = listOf("FAIL previous swapchain run still hung")
            )
        }

        // Show the card (creates the SurfaceView), wait for surfaceCreated, then let the expand animation settle.
        surfaceTestState.value = "swapchain_lifecycle"
        val surfaceWaitEnd = System.currentTimeMillis() + 5_000L
        var surface = swapSurface
        while (surface == null && System.currentTimeMillis() < surfaceWaitEnd) {
            Thread.sleep(50)
            surface = swapSurface
        }
        if (surface == null) {
            surfaceTestState.value = null
            return@withContext TestResult(
                name = "swapchain_lifecycle",
                status = "FAIL",
                durationMs = System.currentTimeMillis() - t0,
                lastLines = listOf("FAIL no surface")
            )
        }
        Thread.sleep(400)
        val boundSurface = swapSurface ?: surface

        val logFile = createLogFile("swapchain_lifecycle")
        val holder = object {
            @Volatile var result: String? = null
        }
        val thread = Thread({
            holder.result = Native.swapchainTest(
                getDriverPath(driverTypeState.value),
                boundSurface,
                logFile.absolutePath
            )
        }, "swapchain-test")
        thread.isDaemon = true
        thread.start()

        // g_done stays 1 until this run calls set_done(0). Ignore that stale sample.
        var sawRun = false
        var hangPhase: String? = null
        val runDeadline = System.currentTimeMillis() + 120_000L
        while (true) {
            Thread.sleep(100)
            val parts = Native.swapchainPhase().split(' ')
            val phase = parts.getOrElse(0) { "" }
            val ms = parts.getOrNull(1)?.toLongOrNull() ?: 0L
            val done = parts.getOrNull(2) == "1"
            if (!done) sawRun = true
            if (!thread.isAlive || (done && sawRun)) break
            if (System.currentTimeMillis() >= runDeadline || (sawRun && ms > 10_000L)) {
                logFile.appendText("FAIL hang in $phase (watchdog 10s)\n")
                hungSwapThread = thread
                hangPhase = phase
                break
            }
        }
        if (hangPhase == null) thread.join()
        surfaceTestState.value = null

        val result = holder.result ?: ""
        var hasFail = false
        var fps: String? = null
        var resized: String? = null
        val fpsRegex = Regex("""FPS ([0-9.]+)""")
        val resizedRegex = Regex("""FPS_RESIZED ([0-9.]+)""")
        val phaseRegex = Regex("""PHASE (\S+) ms=(\d+)""")
        val phases = mutableListOf<String>()
        val last40 = ArrayDeque<String>(40)
        if (logFile.exists()) scanLog(logFile) { line ->
            if (line.trimStart().startsWith("FAIL")) hasFail = true
            if (fps == null) fps = fpsRegex.find(line)?.groupValues?.get(1)
            if (resized == null) resized = resizedRegex.find(line)?.groupValues?.get(1)
            phaseRegex.findAll(line).forEach {
                phases.add("${it.groupValues[1]} ${it.groupValues[2]}ms")
            }
            if (last40.size == 40) last40.removeFirst()
            last40.addLast(line)
        }
        val status = if (hangPhase == null && result == "exit:0" && !hasFail) "PASS" else "FAIL"
        val extra = (if (hangPhase != null) "hang in $hangPhase | " else "") +
            "resized FPS ${resized ?: ""} | ${phases.joinToString(", ")}"

        refreshLogsList()
        TestResult(
            name = "swapchain_lifecycle",
            status = status,
            fps = fps,
            durationMs = System.currentTimeMillis() - t0,
            logFile = logFile,
            lastLines = last40.toList(),
            extra = extra
        )
    }

    // Autorun summary: logcat + files/autorun.txt (some devices drop app logcat over adb).
    private fun say(s: String) {
        Log.i("PanVKTest", s)
        java.io.File(filesDir, "autorun.txt").appendText(s + "\n")
    }

    private suspend fun runHeadlessAutorun(which: String) {
        java.io.File(filesDir, "autorun.txt").delete()
        val (json, _) = runInfo()
        val devicesArr = json?.optJSONArray("devices")
        val numDevices = devicesArr?.length() ?: 0
        val firstDev = if (numDevices > 0) devicesArr?.optJSONObject(0) else null
        val numExts = firstDev?.optJSONArray("extensions")?.length()
            ?: (json?.optJSONArray("instanceExtensions")?.length() ?: 0)
        val numFormats = firstDev?.optJSONObject("formats")?.length() ?: 0

        say("INFO devices=$numDevices exts=$numExts formats=$numFormats")
        json?.optJSONObject("compliance")?.let { reports ->
            val s = complianceSummary(reports)
            for (k in s.keys()) s.getJSONObject(k).let {
                say("COMPLIANCE ${it.optString("title")} ${if (it.optBoolean("pass")) "PASS" else "FAIL"} items=${it.optInt("items")}" +
                    " missingHard=${it.optJSONArray("missingHard")} missingSoft=${it.optJSONArray("missingSoft")}")
            }
        }

        val selected = if (which == "all") testCases else testCases.filter { it.name == which }
        val runResults = mutableListOf<TestResult>()
        for (test in selected) {
            withContext(Dispatchers.Main) {
                updateTestStatus(test.name, "RUNNING")
            }
            val res = executeTest(test)
            runResults.add(res)
            withContext(Dispatchers.Main) {
                updateTestResult(res)
            }
            say("RESULT ${test.name} ${res.status} mismatch=${res.mismatch} fps=${res.fps ?: "0"} ms=${res.durationMs}${if (res.extra != null) " extra=${res.extra}" else ""}")
        }
        val passCount = runResults.count { it.status == "PASS" }
        val failCount = runResults.count { it.status in listOf("FAIL", "CRASH", "TIMEOUT") }
        val skipCount = runResults.count { it.status == "SKIP" }
        say("AUTORUN DONE pass=$passCount fail=$failCount skip=$skipCount total=${selected.size} ($passCount pass / $failCount fail / $skipCount skip)")
        val savedRun = saveRun(runResults)
        // Only a full run uploads; a single named test never does.
        if (which == "all" && savedRun != null) autoUpload(savedRun)
        // --ez zip true: build the upload zip locally (no upload) and copy it to external files for adb pull.
        if (intent.getBooleanExtra("zip", false)) try {
            val run = File(filesDir, "runs").listFiles { f -> f.isDirectory }?.maxByOrNull { it.name }
            val zip = run?.let { buildRunZip(it) }
            val out = zip?.copyTo(File(getExternalFilesDir(null), zip.name), overwrite = true)
            say("AUTORUN ZIP ${out?.absolutePath} size=${out?.length()}")
        } catch (e: Exception) { say("AUTORUN ZIP FAIL $e") }
        withContext(Dispatchers.Main) {
            refreshRunsList()
        }
    }

    private fun updateTestStatus(testName: String, status: String) {
        testResultsState.value = testResultsState.value.map {
            if (it.name == testName) it.copy(status = status) else it
        }
    }

    private fun updateTestResult(res: TestResult) {
        testResultsState.value = testResultsState.value.map {
            if (it.name == res.name) res else it
        }
    }

    private fun startRunAll() {
        if (isRunningAllState.value) return
        lifecycleScope.launch(Dispatchers.Main) {
            isRunningAllState.value = true
            val runResults = mutableListOf<TestResult>()
            for (test in testCases) {
                updateTestStatus(test.name, "RUNNING")
                val res = executeTest(test)
                runResults.add(res)
                updateTestResult(res)
            }
            val savedRun = saveRun(runResults)
            refreshRunsList()
            isRunningAllState.value = false
            if (savedRun != null) lifecycleScope.launch(Dispatchers.IO) { autoUpload(savedRun) }
        }
    }

    @Composable
    fun MainScreen() {
        val currentTab = AppTab.entries.getOrElse(selectedTabState.intValue) { AppTab.Driver }
        val isAnyRunning = isRunningAllState.value || infoLoadingState.value || testResultsState.value.any { it.status == "RUNNING" }

        AppShell(
            tab = currentTab,
            onTab = { selectedTabState.intValue = it.ordinal },
            running = isAnyRunning
        ) { tab ->
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(horizontal = 16.dp, vertical = 8.dp),
                contentAlignment = Alignment.TopCenter
            ) {
                Box(
                    modifier = Modifier
                        .widthIn(max = 760.dp)
                        .fillMaxSize()
                ) {
                    when (tab) {
                        AppTab.Driver -> DriverTab()
                        AppTab.Info -> InfoTab()
                        AppTab.Tests -> TestsTab()
                        AppTab.Logs -> LogsTab()
                    }
                }
            }
        }
    }

    @Composable
    fun DriverTab() {
        val context = LocalContext.current
        val pickLauncher = rememberLauncherForActivityResult(
            contract = ActivityResultContracts.OpenDocument()
        ) { uri: Uri? ->
            if (uri != null) {
                try {
                    val importedDir = File(context.filesDir, "imported").apply { mkdirs() }
                    val destFile = File(importedDir, "libimported.so")
                    context.contentResolver.openInputStream(uri)?.use { input ->
                        destFile.outputStream().use { output ->
                            input.copyTo(output)
                        }
                    }
                    var name = uri.lastPathSegment ?: "libimported.so"
                    context.contentResolver.query(uri, null, null, null, null)?.use { cursor ->
                        val idx = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                        if (idx != -1 && cursor.moveToFirst()) {
                            name = cursor.getString(idx)
                        }
                    }
                    importedFileNameState.value = name
                    saveDriverSelection(driverTypeState.value, name)
                } catch (e: Exception) {
                    importedFileNameState.value = "Error: ${e.message}"
                }
            }
        }

        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            SectionTitle("Vulkan Driver Selection")

            val rel = driverUpdate.available
            val relName = rel?.let { "${DriverUpdate.ASSET} (${it.label}, GitHub)" }
            val relDownloaded = relName != null && importedFileNameState.value == relName && File(filesDir, "imported/libimported.so").exists()
            fun selectImported() {
                driverTypeState.value = DriverType.IMPORTED
                saveDriverSelection(DriverType.IMPORTED)
            }
            DriverUpdateCard(
                upd = driverUpdate,
                downloaded = relDownloaded,
                selected = relDownloaded && driverTypeState.value == DriverType.IMPORTED,
                onCheck = { lifecycleScope.launch { driverUpdate.check(manual = true) } },
                onDownload = {
                    lifecycleScope.launch {
                        val ok = driverUpdate.download { f, _ ->
                            // Same slot as "Imported .so": imported/libimported.so (replaced atomically).
                            val dir = File(filesDir, "imported").apply { mkdirs() }
                            val tmp = File(dir, "libimported.so.tmp")
                            f.copyTo(tmp, overwrite = true)
                            if (!tmp.renameTo(File(dir, "libimported.so"))) { tmp.delete(); throw java.io.IOException("Could not store driver") }
                        }
                        if (ok && relName != null) {
                            importedFileNameState.value = relName
                            saveDriverSelection(DriverType.IMPORTED, relName)
                            selectImported()
                        }
                    }
                },
                onSelect = { selectImported() },
                note = "Download replaces the Imported .so slot."
            )

            val bundledJson = remember { loadBundledDriverJson() }

            DriverType.entries.forEach { type ->
                val isSelected = driverTypeState.value == type
                OutlinedCard(
                    onClick = {
                        driverTypeState.value = type
                        saveDriverSelection(type)
                    },
                    modifier = Modifier.fillMaxWidth(),
                    colors = CardDefaults.outlinedCardColors(
                        containerColor = if (isSelected) MaterialTheme.colorScheme.surfaceContainerHigh else MaterialTheme.colorScheme.surface
                    )
                ) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(12.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        RadioButton(
                            selected = isSelected,
                            onClick = {
                                driverTypeState.value = type
                                saveDriverSelection(type)
                            }
                        )
                        Spacer(Modifier.width(8.dp))
                        Column(Modifier.weight(1f)) {
                            Text(type.label, style = MaterialTheme.typography.bodyLarge, fontWeight = FontWeight.SemiBold)
                            Text(
                                text = getDriverPath(type),
                                style = MaterialTheme.typography.bodySmall,
                                fontFamily = FontFamily.Monospace,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                            if (type == DriverType.BUNDLED && bundledJson != null && bundledJson.length() > 0) {
                                val name = bundledJson.optString("name").takeIf { it.isNotEmpty() }
                                val version = bundledJson.optString("displayVersion").takeIf { it.isNotEmpty() }
                                    ?: bundledJson.optString("packageVersion").takeIf { it.isNotEmpty() }
                                val text = when {
                                    name != null && version != null -> "$name  $version"
                                    name != null -> name
                                    version != null -> version
                                    else -> null
                                }
                                if (text != null) {
                                    Text(
                                        text = text,
                                        style = MaterialTheme.typography.bodySmall,
                                        fontFamily = FontFamily.Monospace,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                }
                            }
                        }
                    }
                }
            }

            if (driverTypeState.value == DriverType.IMPORTED) {
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainerLow)
                ) {
                    Column(
                        modifier = Modifier.padding(14.dp),
                        verticalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Button(onClick = { pickLauncher.launch(arrayOf("*/*")) }) {
                            Text("Select .so file")
                        }
                        importedFileNameState.value?.let {
                            Text("Imported file: $it", style = MaterialTheme.typography.bodyMedium)
                        }
                    }
                }
            }

            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)

            SectionTitle("Mesa Environment")

            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainerLow)
            ) {
                Column(
                    modifier = Modifier.padding(14.dp),
                    verticalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable { mesaDebugEnabledState.value = !mesaDebugEnabledState.value },
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Checkbox(
                            checked = mesaDebugEnabledState.value,
                            onCheckedChange = { mesaDebugEnabledState.value = it }
                        )
                        Spacer(Modifier.width(8.dp))
                        Text("Mesa debug env", style = MaterialTheme.typography.bodyMedium, fontWeight = FontWeight.Medium)
                    }

                    OutlinedTextField(
                        value = mesaDebugStrState.value,
                        onValueChange = { mesaDebugStrState.value = it },
                        label = { Text("Debug Variables (space-separated)") },
                        modifier = Modifier.fillMaxWidth()
                    )
                }
            }

            Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainer)) {
                Column(
                    modifier = Modifier.padding(14.dp),
                    verticalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    Text("Always passed:", fontWeight = FontWeight.SemiBold, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.primary)
                    Text("• MESA_LOG=file", style = MaterialTheme.typography.bodySmall, fontFamily = FontFamily.Monospace)
                    Text("• TMPDIR=${context.cacheDir.absolutePath}", style = MaterialTheme.typography.bodySmall, fontFamily = FontFamily.Monospace)
                }
            }
        }
    }

    @Composable
    fun InfoTab() {
        val coroutineScope = rememberCoroutineScope()
        InfoTabContent(
            isLoading = infoLoadingState.value,
            parsedInfo = infoParsedState.value,
            rawJson = infoRawJsonState.value,
            rawErrorText = infoRawTextState.value,
            hasRuns = {
                val runsDir = File(filesDir, "runs")
                runsDir.exists() && (runsDir.listFiles()?.any { it.isDirectory } == true)
            },
            onRunTests = {
                selectedTabState.intValue = 2
                startRunAll()
            },
            onLoadClick = {
                coroutineScope.launch {
                    infoLoadingState.value = true
                    val (json, raw) = runInfo()
                    if (json != null) {
                        infoParsedState.value = parseVulkanInfo(json)
                        infoRawJsonState.value = json.toString(2)
                        infoRawTextState.value = null
                    } else {
                        infoParsedState.value = null
                        infoRawJsonState.value = null
                        infoRawTextState.value = raw
                    }
                    infoLoadingState.value = false
                }
            }
        )
    }

    @Composable
    fun TestsTab() {
        val coroutineScope = rememberCoroutineScope()

        Column(modifier = Modifier.fillMaxSize()) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column {
                    SectionTitle("Tests")
                    val passCount = testResultsState.value.count { it.status == "PASS" }
                    val failCount = testResultsState.value.count { it.status in listOf("FAIL", "CRASH", "TIMEOUT") }
                    val skipCount = testResultsState.value.count { it.status == "SKIP" }
                    if (passCount > 0 || failCount > 0 || skipCount > 0) {
                        Text(
                            "$passCount pass / $failCount fail / $skipCount skip",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
                Button(
                    enabled = !isRunningAllState.value,
                    onClick = { startRunAll() }
                ) {
                    Text(if (isRunningAllState.value) "Running..." else "Run all")
                }
            }

            Spacer(Modifier.height(8.dp))

            var showSent by remember { mutableStateOf(false) }
            Card(
                modifier = Modifier.fillMaxWidth().padding(bottom = 8.dp),
                shape = MaterialTheme.shapes.large,
                colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainer)
            ) {
                Column(Modifier.padding(horizontal = 12.dp, vertical = 8.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Column(Modifier.weight(1f)) {
                            Text("Auto-upload after Run all", style = MaterialTheme.typography.labelLarge)
                            Text(
                                "Results are uploaded automatically after Run all to help driver development: anonymous device + driver logs, no personal data.",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                        Switch(
                            checked = autoUploadOn,
                            onCheckedChange = {
                                autoUploadOn = it
                                getSharedPreferences("panprobe", Context.MODE_PRIVATE).edit().putBoolean("autoUpload", it).apply()
                            }
                        )
                    }
                    TextButton(
                        onClick = { showSent = !showSent },
                        contentPadding = PaddingValues(0.dp),
                        modifier = Modifier.height(28.dp)
                    ) { Text(if (showSent) "Hide what is sent" else "What is sent?", style = MaterialTheme.typography.labelSmall) }
                    if (showSent) {
                        Text(
                            "A ZIP sent only to the PanVK project's own storage (not to public file hosts): " +
                                "test results and per-test logs, Vulkan info (properties, features, extensions, formats), " +
                                "DXVK / Bachata S4 / vkd3d compliance reports, driver load details (bundled, imported or downloaded; " +
                                "version, SHA-256, BuildID, load errors, kbase uAPI), a logcat excerpt of the run, device model, " +
                                "SoC/GPU and Android version. No accounts, serials, contacts or files. Single-test runs are never uploaded. " +
                                "Nothing is sent while the switch is off.",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    autoUploadStatus?.let { st ->
                        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            Text(
                                st,
                                style = MaterialTheme.typography.labelMedium,
                                color = if (autoUploadFailed) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary,
                                modifier = Modifier.weight(1f)
                            )
                            if (autoUploadFailed) TextButton(onClick = {
                                lastAutoRun?.let { r -> lifecycleScope.launch(Dispatchers.IO) { autoUpload(r) } }
                            }) { Text("Retry") }
                        }
                    }
                }
            }

            if (isRunningAllState.value) {
                BusyCard("Running test suite...", modifier = Modifier.padding(bottom = 8.dp))
            }

            AnimatedVisibility(
                visible = surfaceTestState.value != null,
                enter = expandVertically() + fadeIn(),
                exit = shrinkVertically() + fadeOut()
            ) {
                Card(
                    modifier = Modifier.padding(bottom = 8.dp),
                    shape = MaterialTheme.shapes.large,
                    colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainer)
                ) {
                    Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        Row(
                            Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(
                                surfaceTestState.value ?: "",
                                style = MaterialTheme.typography.labelLarge,
                                fontFamily = FontFamily.Monospace
                            )
                            Text(
                                "live surface",
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                        LinearProgressIndicator(Modifier.fillMaxWidth())
                        Surface(
                            modifier = Modifier.fillMaxWidth().height(160.dp),
                            shape = MaterialTheme.shapes.medium,
                            color = Color.Black
                        ) {
                            AndroidView(
                                factory = { ctx ->
                                    SurfaceView(ctx).apply {
                                        holder.addCallback(object : SurfaceHolder.Callback {
                                            override fun surfaceCreated(holder: SurfaceHolder) {
                                                swapSurface = holder.surface
                                            }
                                            override fun surfaceChanged(
                                                holder: SurfaceHolder, format: Int, width: Int, height: Int
                                            ) {
                                                swapSurface = holder.surface
                                            }
                                            override fun surfaceDestroyed(holder: SurfaceHolder) {
                                                swapSurface = null
                                            }
                                        })
                                    }
                                },
                                modifier = Modifier.fillMaxSize()
                            )
                        }
                    }
                }
            }

            Spacer(Modifier.height(8.dp))

            LazyColumn(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(8.dp),
                contentPadding = PaddingValues(bottom = 16.dp)
            ) {
                items(testCases) { test ->
                    val result = testResultsState.value.firstOrNull { it.name == test.name } ?: TestResult(test.name)
                    TestRow(
                        test = test,
                        result = result,
                        onRunClick = {
                            coroutineScope.launch {
                                updateTestStatus(test.name, "RUNNING")
                                val res = executeTest(test)
                                updateTestResult(res)
                                saveRun(listOf(res))
                                refreshRunsList()
                            }
                        },
                        onToggleExpand = {
                            testResultsState.value = testResultsState.value.map {
                                if (it.name == test.name) it.copy(isExpanded = !it.isExpanded) else it
                            }
                        }
                    )
                }
            }
        }
    }

    @OptIn(ExperimentalLayoutApi::class)
    @Composable
    fun TestRow(
        test: TestCase,
        result: TestResult,
        onRunClick: () -> Unit,
        onToggleExpand: () -> Unit
    ) {
        Card(
            modifier = Modifier
                .fillMaxWidth()
                .clickable { onToggleExpand() },
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainer)
        ) {
            Column(modifier = Modifier.padding(12.dp)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column(modifier = Modifier.weight(1f)) {
                        FlowRow(
                            horizontalArrangement = Arrangement.spacedBy(6.dp),
                            verticalArrangement = Arrangement.spacedBy(2.dp),
                            itemVerticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(test.name, fontWeight = FontWeight.Bold, style = MaterialTheme.typography.bodyLarge)
                            if (test.isDraw) {
                                StatusPill(
                                    text = "draw",
                                    tone = Tone.Accent
                                )
                            }
                            TARGET_TAGS[test.name]?.forEach { TargetTag(it) }
                        }
                        Spacer(Modifier.height(4.dp))
                        Row(
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            StatusPill(
                                text = result.status,
                                tone = testStatusTone(result.status)
                            )
                            if (result.status != "IDLE" && result.status != "RUNNING") {
                                Text("mismatch=${result.mismatch}", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                                if (result.fps != null) {
                                    Text("FPS ${result.fps}", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                                }
                                Text("${result.durationMs}ms", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                            }
                        }
                        result.extra?.let {
                            Spacer(Modifier.height(2.dp))
                            Text(it, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                    }

                    Spacer(Modifier.width(8.dp))

                    Button(
                        onClick = onRunClick,
                        enabled = result.status != "RUNNING" && !isRunningAllState.value,
                        contentPadding = PaddingValues(horizontal = 16.dp, vertical = 6.dp)
                    ) {
                        Text("Run")
                    }
                }

                if (result.isExpanded && result.lastLines.isNotEmpty()) {
                    Spacer(Modifier.height(8.dp))
                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                    Spacer(Modifier.height(6.dp))
                    Text(
                        "Last ${result.lastLines.size} lines:",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.primary,
                        fontWeight = FontWeight.SemiBold
                    )
                    Spacer(Modifier.height(4.dp))
                    Surface(
                        modifier = Modifier.fillMaxWidth(),
                        shape = MaterialTheme.shapes.small,
                        color = MaterialTheme.colorScheme.surfaceContainerLowest
                    ) {
                        SelectionContainer(modifier = Modifier.padding(8.dp)) {
                            Text(
                                text = result.lastLines.joinToString("\n"),
                                fontFamily = FontFamily.Monospace,
                                fontSize = 11.sp,
                                lineHeight = 14.sp
                            )
                        }
                    }
                }
            }
        }
    }

    @Composable
    fun LogsTab() {
        val context = LocalContext.current
        val coroutineScope = rememberCoroutineScope()

        var cloudConfirmRun by remember { mutableStateOf<RunItem?>(null) }
        var isUploading by remember { mutableStateOf(false) }
        var showLinkDialog by remember { mutableStateOf(false) }
        var showErrorDialog by remember { mutableStateOf(false) }
        var uploadZipFile by remember { mutableStateOf<File?>(null) }
        var uploadSha256 by remember { mutableStateOf<String?>(null) }
        var recordStatus by remember { mutableStateOf<String?>(null) }
        var pathAState by remember { mutableStateOf(UploadPathState(name = "catbox / gofile")) }
        var pathBState by remember { mutableStateOf(UploadPathState(name = "PanVK storage")) }
        var isRetryingA by remember { mutableStateOf(false) }
        var isRetryingB by remember { mutableStateOf(false) }
        val currentCancelFlag = remember { mutableStateOf(AtomicBoolean(false)) }
        val uploadGeneration = remember { AtomicInteger(0) }

        DisposableEffect(Unit) {
            onDispose {
                currentCancelFlag.value.set(true)
                uploadGeneration.incrementAndGet()
            }
        }

        fun startDualUpload(
            zip: File,
            localSha: String,
            existingGen: Int? = null,
            existingFlag: AtomicBoolean? = null
        ) {
            val gen = existingGen ?: uploadGeneration.incrementAndGet()
            val flag = existingFlag ?: AtomicBoolean(false).also {
                currentCancelFlag.value.set(true)
                currentCancelFlag.value = it
            }
            uploadZipFile = zip
            uploadSha256 = localSha
            recordStatus = null
            coroutineScope.launch {
                if (flag.get() || uploadGeneration.get() != gen) return@launch
                val endpoint = uploadEndpoint
                val bInitialStatus = if (endpoint.isEmpty()) "Skipped (not configured)" else "Uploading"
                val bInitialError = if (endpoint.isEmpty()) "Skipped (not configured)" else null
                pathAState = UploadPathState(name = "catbox / gofile", status = "Uploading")
                pathBState = UploadPathState(name = "PanVK storage", status = bInitialStatus, error = bInitialError)
                isUploading = true
                showLinkDialog = false
                showErrorDialog = false

                try {
                    pathAState = pathAState.copy(totalBytes = zip.length())
                    if (endpoint.isNotEmpty()) {
                        pathBState = pathBState.copy(totalBytes = zip.length())
                    }

                    coroutineScope {
                        val jobA = async(Dispatchers.IO) {
                            var loggedCancelA = false
                            fun logCancel() {
                                if (!loggedCancelA) {
                                    loggedCancelA = true
                                    Log.i("PanProbe", "upload cancelled")
                                }
                            }

                            if (flag.get() || uploadGeneration.get() != gen) {
                                if (flag.get()) logCancel()
                                return@async
                            }
                            var lastPercentA = -1
                            try {
                                val resA = uploadToCloud(zip, getAppVersion(), flag) { sent, total ->
                                    val pct = if (total > 0) ((sent * 100) / total).toInt() else 0
                                    if (pct != lastPercentA || sent == total) {
                                        lastPercentA = pct
                                        if (!flag.get() && uploadGeneration.get() == gen) {
                                            pathAState = pathAState.copy(bytesSent = sent, totalBytes = total)
                                        }
                                    }
                                }
                                if (flag.get() || uploadGeneration.get() != gen) {
                                    if (flag.get()) logCancel()
                                    return@async
                                }
                                if (resA.directUrl == null) {
                                    if (uploadGeneration.get() == gen) {
                                        pathAState = pathAState.copy(
                                            url = resA.url,
                                            directUrl = null,
                                            status = "Done (not verified, gofile)",
                                            verifyStatus = "– not verified",
                                            bytesSent = zip.length(),
                                            totalBytes = zip.length()
                                        )
                                    }
                                } else {
                                    if (uploadGeneration.get() == gen) {
                                        pathAState = pathAState.copy(
                                            url = resA.url,
                                            directUrl = resA.directUrl,
                                            status = "Verifying",
                                            bytesSent = zip.length(),
                                            totalBytes = zip.length()
                                        )
                                    }
                                    val vA = verifyUpload(resA.directUrl, localSha, getAppVersion(), flag)
                                    if (flag.get() || uploadGeneration.get() != gen) {
                                        if (flag.get()) logCancel()
                                        return@async
                                    }
                                    if (uploadGeneration.get() == gen) {
                                        if (vA == "Verified ✓") {
                                            pathAState = pathAState.copy(status = "Done ✓ verified", verifyStatus = "✓")
                                        } else {
                                            pathAState = pathAState.copy(status = "Failed: $vA", verifyStatus = "✗")
                                        }
                                    }
                                }
                            } catch (e: CancellationException) {
                                logCancel()
                                throw e
                            } catch (e: Exception) {
                                if (flag.get()) {
                                    logCancel()
                                    throw CancellationException("Upload cancelled")
                                }
                                if (!flag.get() && uploadGeneration.get() == gen) {
                                    val msg = friendlyUploadError(e)
                                    pathAState = pathAState.copy(status = "Failed: $msg", error = msg)
                                }
                            }
                        }

                        val jobB = async(Dispatchers.IO) {
                            var loggedCancelB = false
                            fun logCancel() {
                                if (!loggedCancelB) {
                                    loggedCancelB = true
                                    Log.i("PanProbe", "upload cancelled")
                                }
                            }

                            if (endpoint.isEmpty()) {
                                if (uploadGeneration.get() == gen) {
                                    pathBState = pathBState.copy(status = "Skipped (not configured)", error = "Skipped (not configured)")
                                }
                                return@async
                            }

                            if (flag.get() || uploadGeneration.get() != gen) {
                                if (flag.get()) logCancel()
                                return@async
                            }
                            var lastPercentB = -1
                            try {
                                val resB = uploadToR2(
                                    endpoint = endpoint,
                                    f = zip,
                                    sha256Hex = localSha,
                                    version = getAppVersion(),
                                    cancelled = flag
                                ) { sent, total ->
                                    val pct = if (total > 0) ((sent * 100) / total).toInt() else 0
                                    if (pct != lastPercentB || sent == total) {
                                        lastPercentB = pct
                                        if (!flag.get() && uploadGeneration.get() == gen) {
                                            pathBState = pathBState.copy(bytesSent = sent, totalBytes = total)
                                        }
                                    }
                                }
                                if (flag.get() || uploadGeneration.get() != gen) {
                                    if (flag.get()) logCancel()
                                    return@async
                                }
                                if (uploadGeneration.get() == gen) {
                                    pathBState = pathBState.copy(
                                        url = resB.url,
                                        directUrl = resB.directUrl,
                                        status = "Verifying",
                                        bytesSent = zip.length(),
                                        totalBytes = zip.length()
                                    )
                                }
                                val vB = verifyUpload(resB.directUrl, localSha, getAppVersion(), flag)
                                if (flag.get() || uploadGeneration.get() != gen) {
                                    if (flag.get()) logCancel()
                                    return@async
                                }
                                if (uploadGeneration.get() == gen) {
                                    if (vB == "Verified ✓") {
                                        pathBState = pathBState.copy(status = "Done ✓ verified", verifyStatus = "✓")
                                    } else {
                                        pathBState = pathBState.copy(status = "Failed: $vB", verifyStatus = "✗")
                                    }
                                }
                            } catch (e: CancellationException) {
                                logCancel()
                                throw e
                            } catch (e: Exception) {
                                if (flag.get()) {
                                    logCancel()
                                    throw CancellationException("Upload cancelled")
                                }
                                if (!flag.get() && uploadGeneration.get() == gen) {
                                    if (e is R2StorageNotConfiguredException) {
                                        pathBState = pathBState.copy(status = "Skipped (not configured)", error = "Skipped (not configured)")
                                    } else if (e is ProjectStorageTooBigException) {
                                        pathBState = pathBState.copy(status = "Too big for PanVK storage (zip over 25 MiB) - use the other link", error = null)
                                    } else {
                                        val msg = friendlyUploadError(e)
                                        pathBState = pathBState.copy(status = "Failed: $msg", error = msg)
                                    }
                                }
                            }
                        }

                        jobA.await()
                        jobB.await()
                    }

                    if (flag.get() || uploadGeneration.get() != gen) return@launch
                    isUploading = false
                    if (pathAState.url != null || pathBState.url != null) {
                        showLinkDialog = true
                    } else {
                        showErrorDialog = true
                    }
                    if (endpoint.isNotEmpty() && (localSha.isNotEmpty() || pathAState.url != null || pathBState.url != null)) {
                        val recordedA = pathAState
                        val recordedB = pathBState
                        recordStatus = "Recording…"
                        lifecycleScope.launch(Dispatchers.IO) {
                            val recorded = runCatching {
                                postRecord(endpoint, buildUploadRecord(zip, localSha, endpoint, recordedA, recordedB))
                            }.getOrDefault(false)
                            withContext(Dispatchers.Main) {
                                if (uploadGeneration.get() == gen) {
                                    recordStatus = if (recorded) "Recorded ✓" else "Record failed"
                                }
                            }
                        }
                    }
                } catch (_: CancellationException) {
                    if (flag.get() || uploadGeneration.get() != gen) return@launch
                    if (isUploading) {
                        isUploading = false
                        Toast.makeText(context, "Upload cancelled", Toast.LENGTH_SHORT).show()
                    }
                } catch (e: Exception) {
                    if (flag.get() || uploadGeneration.get() != gen) return@launch
                    isUploading = false
                    val msg = friendlyUploadError(e)
                    if (pathAState.url == null) pathAState = pathAState.copy(status = "Failed: $msg", error = msg)
                    if (pathBState.url == null) pathBState = pathBState.copy(status = "Failed: $msg", error = msg)
                    showErrorDialog = true
                }
            }
        }

        if (cloudConfirmRun != null) {
            AlertDialog(
                onDismissRequest = { cloudConfirmRun = null },
                title = { Text("Send to cloud?") },
                text = {
                    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        Text("This uploads a ZIP of logs to a public file host (catbox.moe, or gofile.io as fallback) and to the PanVK project's own storage (deleted after 30 days). Anyone with a link can download it. It may contain your device model, GPU info, Android version, app and package names and file paths. Upload details (device, GPU, driver, links) are also saved to the PanVK project database. It does not include accounts, contacts or personal files. Share the links only in the PanVK Telegram group. Files on catbox/gofile may not be deletable.")
                        if (uploadEndpoint != PANVK_UPLOAD_ENDPOINT) {
                            Text(
                                text = "Test upload endpoint override active: $uploadEndpoint",
                                color = MaterialTheme.colorScheme.error
                            )
                            TextButton(
                                onClick = { uploadEndpoint = PANVK_UPLOAD_ENDPOINT },
                                contentPadding = PaddingValues(horizontal = 8.dp, vertical = 2.dp),
                                modifier = Modifier.height(28.dp)
                            ) {
                                Text("Clear override", style = MaterialTheme.typography.labelSmall)
                            }
                        }
                    }
                },
                confirmButton = {
                    Button(
                        enabled = !isUploading,
                        onClick = {
                            val targetRun = cloudConfirmRun
                            cloudConfirmRun = null
                            if (targetRun != null && !isUploading) {
                                val gen = uploadGeneration.incrementAndGet()
                                val flag = AtomicBoolean(false)
                                currentCancelFlag.value.set(true)
                                currentCancelFlag.value = flag

                                val endpoint = uploadEndpoint
                                val bZipStatus = if (endpoint.isEmpty()) "Skipped (not configured)" else "Preparing ZIP..."
                                val bZipError = if (endpoint.isEmpty()) "Skipped (not configured)" else null
                                pathAState = UploadPathState(name = "catbox / gofile", status = "Preparing ZIP...")
                                pathBState = UploadPathState(name = "PanVK storage", status = bZipStatus, error = bZipError)
                                isUploading = true
                                showLinkDialog = false
                                showErrorDialog = false

                                coroutineScope.launch {
                                    try {
                                        val zip = buildRunZip(targetRun.folder)
                                        if (flag.get() || uploadGeneration.get() != gen) return@launch
                                        val localSha = withContext(Dispatchers.IO) { sha256(zip) }
                                        if (flag.get() || uploadGeneration.get() != gen) return@launch
                                        startDualUpload(zip, localSha, gen, flag)
                                    } catch (e: Exception) {
                                        if (flag.get() || uploadGeneration.get() != gen) return@launch
                                        isUploading = false
                                        Toast.makeText(context, "Zip failed: ${friendlyUploadError(e)}", Toast.LENGTH_SHORT).show()
                                    }
                                }
                            }
                        }
                    ) {
                        Text("Upload")
                    }
                },
                dismissButton = {
                    OutlinedButton(onClick = { cloudConfirmRun = null }) {
                        Text("Cancel")
                    }
                }
            )
        }

        if (isUploading) {
            AlertDialog(
                onDismissRequest = { /* non-dismissable */ },
                properties = DialogProperties(dismissOnBackPress = false, dismissOnClickOutside = false),
                title = { Text("Uploading...") },
                text = {
                    Column(
                        modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
                        verticalArrangement = Arrangement.spacedBy(16.dp)
                    ) {
                        for (path in listOf(pathAState, pathBState)) {
                            Column(
                                modifier = Modifier.fillMaxWidth(),
                                verticalArrangement = Arrangement.spacedBy(4.dp)
                            ) {
                                Text(path.name, fontWeight = FontWeight.SemiBold, style = MaterialTheme.typography.labelMedium)
                                val progress = if (path.totalBytes > 0) {
                                    (path.bytesSent.toFloat() / path.totalBytes.toFloat()).coerceIn(0f, 1f)
                                } else if (path.status.startsWith("Done") || path.status == "Verifying") {
                                    1f
                                } else {
                                    0f
                                }
                                LinearProgressIndicator(
                                    progress = { progress },
                                    modifier = Modifier.fillMaxWidth()
                                )
                                val progressText = if (path.status == "Uploading") {
                                    val percent = (progress * 100).toInt()
                                    val sentMb = path.bytesSent / (1024.0 * 1024.0)
                                    val totalMb = path.totalBytes / (1024.0 * 1024.0)
                                    String.format(Locale.US, "Uploading %.2f / %.2f MB (%d%%)", sentMb, totalMb, percent)
                                } else {
                                    path.status
                                }
                                Text(
                                    text = progressText,
                                    style = MaterialTheme.typography.bodySmall,
                                    modifier = Modifier.align(Alignment.CenterHorizontally)
                                )
                            }
                        }
                    }
                },
                confirmButton = {
                    OutlinedButton(
                        onClick = {
                            currentCancelFlag.value.set(true)
                            uploadGeneration.incrementAndGet()
                            isUploading = false
                            Toast.makeText(context, "Upload cancelled", Toast.LENGTH_SHORT).show()
                        }
                    ) {
                        Text("Cancel")
                    }
                }
            )
        }

        if (showErrorDialog) {
            val errA = pathAState.error ?: pathAState.status
            val errB = pathBState.error ?: pathBState.status
            AlertDialog(
                onDismissRequest = {
                    showErrorDialog = false
                    uploadGeneration.incrementAndGet()
                },
                title = { Text("Upload Failed") },
                text = {
                    SelectionContainer {
                        Text("${pathAState.name}: $errA\n\n${pathBState.name}: $errB")
                    }
                },
                confirmButton = {
                    Button(onClick = {
                        showErrorDialog = false
                        val zip = uploadZipFile
                        val sha = uploadSha256
                        if (zip != null && sha != null) {
                            startDualUpload(zip, sha)
                        }
                    }) {
                        Text("Retry")
                    }
                },
                dismissButton = {
                    OutlinedButton(onClick = {
                        showErrorDialog = false
                        uploadGeneration.incrementAndGet()
                    }) {
                        Text("Cancel")
                    }
                }
            )
        }

        if (showLinkDialog) {
            val hex = uploadSha256 ?: ""
            val shareText = buildString {
                append("PanProbe logs:\n")
                if (pathAState.url != null) append(pathAState.url).append("\n")
                if (pathBState.url != null) append(pathBState.url).append("\n")
                append("SHA-256: $hex")
            }

            AlertDialog(
                onDismissRequest = {
                    showLinkDialog = false
                    uploadGeneration.incrementAndGet()
                },
                title = { Text("Upload done") },
                text = {
                    Column(
                        modifier = Modifier.fillMaxWidth(),
                        verticalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        for (p in listOf(pathAState, pathBState)) {
                            val isPathA = p.name == pathAState.name
                            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                                Text(p.name, fontWeight = FontWeight.SemiBold, style = MaterialTheme.typography.labelLarge)
                                if (p.url != null) {
                                    SelectionContainer {
                                        Text(
                                            text = p.url,
                                            style = MaterialTheme.typography.bodyMedium,
                                            color = MaterialTheme.colorScheme.primary
                                        )
                                    }
                                    Row(
                                        verticalAlignment = Alignment.CenterVertically,
                                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                                    ) {
                                        val (statusText, statusTone) = when (p.verifyStatus) {
                                            "✓" -> Pair("✓", Tone.Ok)
                                            "– not verified" -> Pair("– not verified", Tone.Neutral)
                                            "✗" -> Pair("✗", Tone.Error)
                                            else -> Pair(p.verifyStatus ?: "", Tone.Neutral)
                                        }
                                        StatusPill(text = statusText, tone = statusTone)

                                        if (p.verifyStatus == "✗" && p.directUrl != null) {
                                            val isRetrying = if (isPathA) isRetryingA else isRetryingB
                                            OutlinedButton(
                                                enabled = !isRetrying,
                                                onClick = {
                                                    val retryGen = uploadGeneration.get()
                                                    coroutineScope.launch {
                                                        if (isPathA) isRetryingA = true else isRetryingB = true
                                                        val currentP = if (isPathA) pathAState else pathBState
                                                        if (isPathA) {
                                                            pathAState = pathAState.copy(status = "Verifying", verifyStatus = "Verifying...")
                                                        } else {
                                                            pathBState = pathBState.copy(status = "Verifying", verifyStatus = "Verifying...")
                                                        }
                                                        val newStatus = withContext(Dispatchers.IO) {
                                                            verifyUpload(currentP.directUrl, hex, getAppVersion(), currentCancelFlag.value)
                                                        }
                                                        if (uploadGeneration.get() != retryGen) return@launch
                                                        val isOk = newStatus == "Verified ✓"
                                                        val updated = currentP.copy(
                                                            status = if (isOk) "Done ✓ verified" else "Failed: $newStatus",
                                                            verifyStatus = if (isOk) "✓" else "✗"
                                                        )
                                                        if (isPathA) pathAState = updated else pathBState = updated
                                                        if (isPathA) isRetryingA = false else isRetryingB = false
                                                    }
                                                },
                                                contentPadding = PaddingValues(horizontal = 8.dp, vertical = 2.dp),
                                                modifier = Modifier.height(28.dp)
                                            ) {
                                                Text(if (isRetrying) "Retrying..." else "Retry", style = MaterialTheme.typography.labelSmall)
                                            }
                                        }
                                    }
                                } else {
                                    SelectionContainer {
                                        Text(
                                            text = p.error ?: p.status,
                                            style = MaterialTheme.typography.bodySmall,
                                            color = if (p.status == "Too big for project storage")
                                                MaterialTheme.colorScheme.onSurfaceVariant else MaterialTheme.colorScheme.error
                                        )
                                    }
                                }
                            }
                        }

                        SelectionContainer {
                            Text(
                                text = "SHA-256: $hex",
                                style = MaterialTheme.typography.bodySmall,
                                fontFamily = FontFamily.Monospace
                            )
                        }
                        recordStatus?.let {
                            Text(it, style = MaterialTheme.typography.bodySmall)
                        }
                    }
                },
                confirmButton = {
                    Column(
                        modifier = Modifier.fillMaxWidth(),
                        verticalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            Button(
                                onClick = {
                                    val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                                    val clip = ClipData.newPlainText("PanProbe Upload Link", shareText)
                                    clipboard.setPrimaryClip(clip)
                                    Toast.makeText(context, "Link copied to clipboard", Toast.LENGTH_SHORT).show()
                                },
                                modifier = Modifier.weight(1f)
                            ) {
                                Text("Copy")
                            }
                            Button(
                                onClick = {
                                    val sendIntent = Intent(Intent.ACTION_SEND).apply {
                                        type = "text/plain"
                                        putExtra(Intent.EXTRA_TEXT, shareText)
                                    }
                                    context.startActivity(Intent.createChooser(sendIntent, "Share Link"))
                                },
                                modifier = Modifier.weight(1f)
                            ) {
                                Text("Share")
                            }
                        }
                        Button(
                            onClick = {
                                val tgIntent = Intent(Intent.ACTION_VIEW, Uri.parse("https://t.me/+E-NhUATmkqE5ODg1"))
                                context.startActivity(tgIntent)
                            },
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text("Open Telegram group")
                        }
                        OutlinedButton(
                            onClick = {
                                showLinkDialog = false
                                uploadGeneration.incrementAndGet()
                            },
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text("Close")
                        }
                    }
                }
            )
        }

        var showClearConfirmDialog by remember { mutableStateOf(false) }

        if (showClearConfirmDialog) {
            AlertDialog(
                onDismissRequest = { showClearConfirmDialog = false },
                title = { Text("Clear all logs and runs?") },
                text = { Text("This deletes all saved test runs and log files on this device.") },
                confirmButton = {
                    Button(
                        onClick = {
                            showClearConfirmDialog = false
                            val logsDir = File(filesDir, "logs")
                            logsDir.listFiles()?.forEach { it.delete() }
                            val runsDir = File(filesDir, "runs")
                            runsDir.deleteRecursively()
                            selectedLogFileState.value = null
                            selectedLogTextState.value = null
                            refreshLogsList()
                            refreshRunsList()
                        }
                    ) {
                        Text("Clear")
                    }
                },
                dismissButton = {
                    OutlinedButton(onClick = { showClearConfirmDialog = false }) {
                        Text("Cancel")
                    }
                }
            )
        }

        Column(modifier = Modifier.fillMaxSize()) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                SectionTitle("Logs")
                Button(
                    onClick = {
                        showClearConfirmDialog = true
                    }
                ) {
                    Text("Clear")
                }
            }

            Spacer(Modifier.height(8.dp))

            if (selectedLogFileState.value != null) {
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainer)
                ) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(12.dp),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            selectedLogFileState.value?.name ?: "",
                            fontWeight = FontWeight.Bold,
                            style = MaterialTheme.typography.bodyMedium,
                            modifier = Modifier.weight(1f)
                        )
                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            Button(
                                onClick = {
                                    val textToShare = (selectedLogTextState.value ?: "").take(400_000)
                                    val sendIntent = Intent(Intent.ACTION_SEND).apply {
                                        type = "text/plain"
                                        putExtra(Intent.EXTRA_TEXT, textToShare)
                                    }
                                    context.startActivity(Intent.createChooser(sendIntent, "Share Log"))
                                }
                            ) {
                                Text("Share")
                            }
                            OutlinedButton(onClick = {
                                selectedLogFileState.value = null
                                selectedLogTextState.value = null
                            }) {
                                Text("Close")
                            }
                        }
                    }
                }

                Spacer(Modifier.height(8.dp))

                Surface(
                    modifier = Modifier.fillMaxSize(),
                    shape = MaterialTheme.shapes.small,
                    color = MaterialTheme.colorScheme.surfaceContainerLowest
                ) {
                    SelectionContainer(
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(10.dp)
                            .verticalScroll(rememberScrollState())
                    ) {
                        Text(
                            text = selectedLogTextState.value ?: "",
                            fontFamily = FontFamily.Monospace,
                            fontSize = 11.sp,
                            lineHeight = 14.sp
                        )
                    }
                }
            } else {
                if (runsListState.value.isEmpty() && logFilesListState.value.isEmpty()) {
                    EmptyState(
                        painter = painterResource(R.drawable.ic_tab_logs),
                        title = "No logs yet.",
                        body = "Send the ZIP or link to the PanVK Telegram group so we can check your results."
                    )
                } else {
                    LazyColumn(
                        modifier = Modifier.fillMaxSize(),
                        verticalArrangement = Arrangement.spacedBy(8.dp),
                        contentPadding = PaddingValues(bottom = 16.dp)
                    ) {
                        if (runsListState.value.isNotEmpty()) {
                            item(key = "runs_header") {
                                Column(modifier = Modifier.fillMaxWidth().padding(bottom = 4.dp)) {
                                    SectionTitle("Runs")
                                    Spacer(Modifier.height(2.dp))
                                    Text(
                                        "Send the ZIP or link to the PanVK Telegram group so we can check your results.",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                }
                            }
                            items(runsListState.value, key = { "run_${it.name}" }) { run ->
                                Card(
                                    modifier = Modifier.fillMaxWidth(),
                                    colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainer)
                                ) {
                                    Column(modifier = Modifier.padding(14.dp)) {
                                        Row(
                                            modifier = Modifier.fillMaxWidth(),
                                            horizontalArrangement = Arrangement.SpaceBetween,
                                            verticalAlignment = Alignment.CenterVertically
                                        ) {
                                            Text(run.name, fontWeight = FontWeight.Bold, style = MaterialTheme.typography.titleMedium)
                                            val runPassTone = if (run.failCount > 0) Tone.Error
                                                else if (run.passCount + run.skipCount == run.totalCount && run.totalCount > 0) Tone.Ok else Tone.Neutral
                                            StatusPill(
                                                text = "${run.passCount} pass / ${run.failCount} fail / ${run.skipCount} skip",
                                                tone = runPassTone
                                            )
                                        }
                                        if (run.compliance.isNotEmpty()) {
                                            Spacer(Modifier.height(6.dp))
                                            Text(
                                                run.compliance,
                                                style = MaterialTheme.typography.bodySmall,
                                                color = MaterialTheme.colorScheme.onSurfaceVariant
                                            )
                                        }
                                        Spacer(Modifier.height(10.dp))
                                        Row(
                                            modifier = Modifier.fillMaxWidth(),
                                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                                        ) {
                                            Button(
                                                onClick = { cloudConfirmRun = run },
                                                enabled = !isUploading,
                                                contentPadding = PaddingValues(horizontal = 14.dp, vertical = 6.dp)
                                            ) {
                                                Text("Send to cloud", maxLines = 1)
                                            }
                                            OutlinedButton(
                                                onClick = {
                                                    coroutineScope.launch {
                                                        try {
                                                            val zipFile = buildRunZip(run.folder)
                                                            shareFile(context, zipFile, "application/zip", "Share Run ZIP")
                                                        } catch (e: Exception) {
                                                            Toast.makeText(context, "Zip failed: ${e.message}", Toast.LENGTH_SHORT).show()
                                                        }
                                                    }
                                                },
                                                enabled = !isUploading,
                                                contentPadding = PaddingValues(horizontal = 14.dp, vertical = 6.dp)
                                            ) {
                                                Text("Zip & Share", maxLines = 1)
                                            }
                                        }
                                    }
                                }
                            }
                            if (logFilesListState.value.isNotEmpty()) {
                                item(key = "logs_header") {
                                    Spacer(Modifier.height(4.dp))
                                    SectionTitle("Log Files")
                                }
                            }
                        } else {
                            item(key = "no_runs_hint") {
                                Text(
                                    "Send the ZIP or link to the PanVK Telegram group so we can check your results.",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                        }

                        items(logFilesListState.value, key = { "log_${it.name}" }) { file ->
                            OutlinedCard(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clickable {
                                        selectedLogFileState.value = file
                                        selectedLogTextState.value = null
                                        coroutineScope.launch {
                                            val text = withContext(Dispatchers.IO) {
                                                if (file.exists()) readLogCapped(file) else ""
                                            }
                                            if (selectedLogFileState.value == file) {
                                                selectedLogTextState.value = text
                                            }
                                        }
                                    },
                                colors = CardDefaults.outlinedCardColors(containerColor = MaterialTheme.colorScheme.surface)
                            ) {
                                Row(
                                    modifier = Modifier.padding(14.dp).fillMaxWidth(),
                                    horizontalArrangement = Arrangement.SpaceBetween,
                                    verticalAlignment = Alignment.CenterVertically
                                ) {
                                    Column(modifier = Modifier.weight(1f)) {
                                        Text(file.name, fontWeight = FontWeight.SemiBold, style = MaterialTheme.typography.bodyMedium)
                                        Text("${file.length()} bytes", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

/**
 * Tests page only: which target each suite test was written for. Sources: CMakeLists source dirs
 * (tests/dxvk, tests/bachata, tests/vkd3d, device/ = DXVK), docs/panprobe-compliance-plan.md phases 2-3,
 * docs/bachata-s4-vulkan-requirements.md, docs/panprobe-vkd3d-compliance-plan.md (Phase 2 reuse list).
 */
private val TARGET_TAGS: Map<String, List<String>> = run {
    val d = "DXVK"; val s = "S4"; val v = "vkd3d"
    val m = HashMap<String, List<String>>()
    fun t(tags: List<String>, vararg tests: String) = tests.forEach { m[it] = tags }
    t(listOf(d), "gpu_prerast_slice", "clip_cull", "multi_viewport", "fill_mode", "bc_decode", "pipeline_stats",
        "vertex_stores", "gs_viewport_depth", "vs_viewport_index", "large_draw", "vmr_secondary", "csf_event",
        "gs_tess_primitive_id", "dxvk_reqs", "swapchain_lifecycle")
    t(listOf(d, s), "geometry", "tessellation", "tess_cond_state")
    t(listOf(d, v), "xfb", "depth_bounds", "blend", "occlusion_query", "descriptor_model", "shader_arith",
        "depth_stencil", "draw_params", "submit_stress")
    t(listOf(d, s, v), "robustness2", "sampler")
    t(listOf(s), "bachata_reqs")
    t(listOf(s, v), "bachata_exec", "bachata_storage_fmtless", "bachata_dynamic_render")
    t(listOf(v), "vkd3d_reqs", "vkd3d_heap", "vkd3d_timeline")
    m
}

@Composable
private fun TargetTag(tag: String) {
    val cs = MaterialTheme.colorScheme
    val (bg, fg) = when (tag) {
        "DXVK" -> cs.secondaryContainer to cs.onSecondaryContainer
        "S4" -> cs.tertiaryContainer to cs.onTertiaryContainer
        else -> cs.surfaceContainerHighest to cs.primary
    }
    val name = if (tag == "S4") "Bachata S4" else tag
    Surface(
        color = bg, contentColor = fg, shape = RoundedCornerShape(6.dp),
        modifier = Modifier.semantics { contentDescription = "Needed by $name" }
    ) {
        Text(tag, style = MaterialTheme.typography.labelSmall, modifier = Modifier.padding(horizontal = 5.dp, vertical = 1.dp))
    }
}

import java.security.MessageDigest

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.plugin.compose")
}

val panvkSoProp = providers.gradleProperty("panvkSo").orNull
val defaultPanvkPath = File(rootDir.parentFile.parentFile, "build/android-dxint-dist/libvulkan_panfrost.so")
val panvkSoFile = if (panvkSoProp != null) file(panvkSoProp) else defaultPanvkPath

fun sha256Of(f: File): String = MessageDigest.getInstance("SHA-256")
    .digest(f.readBytes()).joinToString("") { "%02x".format(it) }

val bundledDriverJsonFile = File(rootDir.parentFile.parentFile, "apps/panvk-launcher/app/src/main/assets/bundled-driver.json")

// The label comes from bundled-driver.json; refuse a .so that is not the pinned one (a stale default
// build/android-dxint-dist .so once shipped under a beta.17 label). -PpanvkSo with another .so only warns.
val checkPanvkSo = tasks.register("checkPanvkSo") {
    inputs.property("panvkSoPath", panvkSoFile.absolutePath)
    outputs.upToDateWhen { false }
    doLast {
        if (!panvkSoFile.exists()) {
            throw GradleException("Bundled driver panvkSo not found at: ${panvkSoFile.absolutePath}. Specify -PpanvkSo=<path> or ensure default path exists.")
        }
        @Suppress("UNCHECKED_CAST")
        val pin = ((groovy.json.JsonSlurper().parse(bundledDriverJsonFile) as Map<String, Any?>)["release"] as Map<String, Any?>)["sha256"]
        val got = sha256Of(panvkSoFile)
        if (got != pin) {
            val msg = "panvkSo ${panvkSoFile.absolutePath} sha256 $got != bundled-driver.json pin $pin"
            if (panvkSoProp == null) throw GradleException("$msg (stale default driver); pass -PpanvkSo=<pinned .so>")
            logger.warn("WARNING: $msg: APK label will not match the bundled .so")
        }
    }
}

val copyPanvkSo = tasks.register<Copy>("copyPanvkSo") {
    dependsOn(checkPanvkSo)
    from(panvkSoFile)
    into(file("build/generated/panvkJni/arm64-v8a"))
    rename { "libvulkan_panfrost.so" }
}

val checkBundledDriverJson = tasks.register("checkBundledDriverJson") {
    inputs.property("bundledDriverJsonPath", bundledDriverJsonFile.absolutePath)
    outputs.upToDateWhen { bundledDriverJsonFile.exists() }
    doLast {
        if (!bundledDriverJsonFile.exists()) {
            throw GradleException("Bundled driver JSON not found at: ${bundledDriverJsonFile.absolutePath}")
        }
    }
}

val copyBundledDriverJson = tasks.register<Copy>("copyBundledDriverJson") {
    dependsOn(checkBundledDriverJson)
    from(bundledDriverJsonFile)
    into(file("build/generated/panvkAssets"))
}

tasks.named("preBuild") {
    dependsOn(copyPanvkSo)
    dependsOn(copyBundledDriverJson)
}

android {
    namespace = "dev.zenithblue.panvktest"
    compileSdk = 36
    ndkVersion = "29.0.14206865"

    defaultConfig {
        applicationId = "dev.zenithblue.panvktest"
        minSdk = 29
        targetSdk = 36
        versionCode = 7
        versionName = "1.2.3"

        ndk {
            abiFilters.add("arm64-v8a")
        }
    }

    buildTypes {
        debug {
            isDebuggable = true
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        compose = true
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    sourceSets.getByName("main") {
        jniLibs.directories.add("build/generated/panvkJni")
        assets.directories.add("build/generated/panvkAssets")
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true
        }
    }
}

dependencies {
    val composeBom = platform("androidx.compose:compose-bom:2026.02.01")
    implementation(composeBom)
    implementation("androidx.compose.material3:material3")
    implementation("androidx.activity:activity-compose:1.13.0")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.foundation:foundation")
}

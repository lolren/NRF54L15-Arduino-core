import java.security.MessageDigest

plugins {
  id("com.android.application")
  kotlin("android")
}

val appVersionCode = 1
val appVersionName = "1.0.0"
val artifactName = "sense-voice-imu-${appVersionName}-debug.apk"
val debugApk = layout.buildDirectory.file("outputs/apk/debug/app-debug.apk")
val artifact = rootProject.projectDir.parentFile.resolve("apk/$artifactName")
val artifactChecksum = rootProject.projectDir.parentFile.resolve("apk/SHA256SUMS.txt")

android {
  namespace = "com.lolren.sensevoiceimu"
  compileSdk = 34

  defaultConfig {
    applicationId = "com.lolren.sensevoiceimu"
    minSdk = 26
    targetSdk = 34
    versionCode = appVersionCode
    versionName = appVersionName
  }

  buildTypes {
    debug {
      applicationIdSuffix = ".debug"
      versionNameSuffix = "-debug"
      isMinifyEnabled = false
    }
    release {
      isMinifyEnabled = true
      proguardFiles(
          getDefaultProguardFile("proguard-android-optimize.txt"),
          "proguard-rules.pro",
      )
    }
  }

  compileOptions {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
  }

  kotlinOptions {
    jvmTarget = "17"
  }
}

dependencies {
  implementation("androidx.core:core-ktx:1.13.1")
  implementation("androidx.appcompat:appcompat:1.7.0")
  implementation("androidx.activity:activity-ktx:1.9.1")
  implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.8.4")
  implementation("com.google.android.material:material:1.12.0")
  testImplementation("junit:junit:4.13.2")
}

val copyVersionedDebugApk = tasks.register("copyVersionedDebugApk") {
  inputs.file(debugApk)
  outputs.file(artifact)
  outputs.file(artifactChecksum)
  doLast {
    artifact.parentFile.mkdirs()
    copy {
      from(debugApk)
      into(artifact.parentFile)
      rename { artifactName }
    }
    val digest = MessageDigest.getInstance("SHA-256")
        .digest(artifact.readBytes())
        .joinToString("") { (it.toInt() and 0xff).toString(16).padStart(2, '0') }
    artifactChecksum.writeText("$digest  $artifactName\n")
  }
}

afterEvaluate {
  tasks.named("copyVersionedDebugApk").configure {
    mustRunAfter("createDebugApkListingFileRedirect")
  }
  tasks.named("assembleDebug").configure {
    finalizedBy(copyVersionedDebugApk)
  }
}

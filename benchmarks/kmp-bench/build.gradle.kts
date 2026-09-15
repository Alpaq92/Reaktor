// Compose Multiplatform on the desktop target, which is what the published
// comparison means by Kotlin Multiplatform: Skia through Skiko, on a JVM.
plugins {
    kotlin("jvm") version "2.4.20"
    id("org.jetbrains.kotlin.plugin.compose") version "2.4.20"
    id("org.jetbrains.compose") version "1.12.0"
}

repositories {
    google()
    mavenCentral()
    maven("https://maven.pkg.jetbrains.space/public/p/compose/dev")
}

dependencies {
    implementation(compose.desktop.currentOs)
}

// No toolchain is pinned. The JDK that runs Gradle is the one that compiles
// this and the one jpackage bundles into the distributable, so the arm is
// measured on the JVM the machine actually has - 17 or newer, which is what
// Compose needs. Pinning a version here would send Gradle looking for that
// exact JDK and fail on a machine that has a later one.

compose.desktop {
    application {
        mainClass = "MainKt"

        // No heap flags. A JVM sized by hand is a thumb on the memory bar,
        // and what this arm costs out of the box is the thing worth knowing.
        nativeDistributions {
            packageName = "reaktor-bench-kmp"
            packageVersion = "1.0.0"
        }
    }
}

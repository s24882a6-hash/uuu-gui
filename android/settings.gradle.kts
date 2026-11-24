pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "uuu-android"

// Этап 0: проверка USB-хоста на телефонах (без libuuu).
include(":probe")

// Этап 1: libuuu под NDK.
include(":app")

import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.smartheavy.uuuflash"
    compileSdk = 36
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "com.smartheavy.uuuflash"
        minSdk = 29
        targetSdk = 36
        versionCode = 11
        versionName = "0.9.2"

        // Все наши телефоны — arm64. x86_64 добавить, если понадобится эмулятор.
        ndk { abiFilters += listOf("arm64-v8a") }
        externalNativeBuild {
            cmake { arguments += listOf("-DANDROID_STL=c++_static") }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.31.6"
        }
    }

    // Релизный ключ — вне репозитория: путь и пароли в ~/.gradle/gradle.properties
    // (UUUFLASH_*). Обновление ставится поверх, только если подписано тем же ключом.
    signingConfigs {
        create("release") {
            val p = providers
            storeFile = p.gradleProperty("UUUFLASH_STORE_FILE").orNull?.let { file(it) }
            storePassword = p.gradleProperty("UUUFLASH_STORE_PASSWORD").orNull
            keyAlias = p.gradleProperty("UUUFLASH_KEY_ALIAS").orNull
            keyPassword = p.gradleProperty("UUUFLASH_KEY_PASSWORD").orNull
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("release")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(JvmTarget.JVM_17)
    }
}

// Универсальные загрузчики 2G/4G вшиваются в APK, но в git не хранятся
// (внутренние бинарники). Без них приложение соберётся, но упадёт при запуске —
// поэтому останавливаем сборку сразу и говорим, куда их положить.
val bootloaders = listOf("imx-boot_2G.bin", "imx-boot_4G.bin").map { file("src/main/assets/boot/$it") }
tasks.named("preBuild") {
    doFirst {
        val missing = bootloaders.filterNot { it.isFile }
        if (missing.isNotEmpty()) throw GradleException(
            "Нет универсальных загрузчиков: ${missing.joinToString { it.name }}. " +
                "Положи их в app/src/main/assets/boot/ (см. android/README.md, «Сборка»)."
        )
    }
}

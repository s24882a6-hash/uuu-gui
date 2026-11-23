// JNI для com.smartheavy.uuuflash.UuuNative.
//
// libuuu держит глобальное состояние, поэтому run() рассчитан на один запуск
// за жизнь процесса — как uuu-helper в настольном uuu-gui. Повторный запуск —
// через перезапуск процесса.

#include <jni.h>
#include <pthread.h>
#include <cstring>
#include <mutex>

#include "libuuu.h"
#include "uuu_android_usb.h"

namespace {

JavaVM *g_vm = nullptr;
pthread_key_t g_detach_key;

std::mutex g_listener_mtx;
jobject g_listener = nullptr;  // global ref, живёт на время run()
jmethodID g_on_event = nullptr;

// libuuu шлёт события из своих потоков. Такой поток подключаем к JVM при
// первом событии, а отключаем автоматически, когда он завершается: иначе ART
// аварийно останавливает процесс.
JNIEnv *env_for_this_thread() {
    JNIEnv *env = nullptr;
    if (g_vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) == JNI_OK) return env;
    if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
    pthread_setspecific(g_detach_key, env);
    return env;
}

int notify_cb(uuu_notify nt, void *) {
    std::lock_guard<std::mutex> lk(g_listener_mtx);
    if (!g_listener) return 0;
    JNIEnv *env = env_for_this_thread();
    if (!env) return 0;

    jbyteArray text = nullptr;
    jlong value = 0;
    switch (nt.type) {
    case uuu_notify::NOTIFY_CMD_START:
    case uuu_notify::NOTIFY_CMD_INFO:
    case uuu_notify::NOTIFY_WAIT_FOR:
    case uuu_notify::NOTIFY_DEV_ATTACH:
    case uuu_notify::NOTIFY_DECOMPRESS_START:
    case uuu_notify::NOTIFY_DOWNLOAD_START:
        // Байты, а не NewStringUTF: в путях может быть кириллица, а
        // NewStringUTF ждёт modified UTF-8 и на неверных данных роняет процесс.
        if (nt.str) {
            jsize n = static_cast<jsize>(strlen(nt.str));
            text = env->NewByteArray(n);
            env->SetByteArrayRegion(text, 0, n, reinterpret_cast<const jbyte *>(nt.str));
        }
        break;
    case uuu_notify::NOTIFY_CMD_END:
        value = nt.status;
        break;
    default:
        value = static_cast<jlong>(nt.index);  // index/total — одно поле union
        break;
    }
    env->CallVoidMethod(g_listener, g_on_event, static_cast<jint>(nt.type),
                        static_cast<jlong>(nt.id), text, value);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (text) env->DeleteLocalRef(text);
    return 0;
}

}  // namespace

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *) {
    g_vm = vm;
    pthread_key_create(&g_detach_key, [](void *) { g_vm->DetachCurrentThread(); });
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_smartheavy_uuuflash_UuuNative_version(JNIEnv *env, jclass) {
    return env->NewStringUTF(uuu_get_version_string());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_smartheavy_uuuflash_UuuNative_lastError(JNIEnv *env, jclass) {
    const char *e = uuu_get_last_err_string();
    jsize n = e ? static_cast<jsize>(strlen(e)) : 0;
    // Через байты по той же причине, что и в notify_cb.
    jbyteArray bytes = env->NewByteArray(n);
    if (n) env->SetByteArrayRegion(bytes, 0, n, reinterpret_cast<const jbyte *>(e));
    jclass str = env->FindClass("java/lang/String");
    jmethodID ctor = env->GetMethodID(str, "<init>", "([B)V");
    return static_cast<jstring>(env->NewObject(str, ctor, bytes));
}

extern "C" JNIEXPORT jint JNICALL
Java_com_smartheavy_uuuflash_UuuNative_addDevice(JNIEnv *, jclass, jint fd) {
    return uuu_android_add_fd(fd);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_smartheavy_uuuflash_UuuNative_removeDevice(JNIEnv *, jclass, jint fd) {
    return uuu_android_remove_fd(fd) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_smartheavy_uuuflash_UuuNative_run(JNIEnv *env, jclass, jstring script,
                                           jint wait_seconds, jobject listener) {
    {
        std::lock_guard<std::mutex> lk(g_listener_mtx);
        g_listener = env->NewGlobalRef(listener);
        jclass cls = env->GetObjectClass(listener);
        g_on_event = env->GetMethodID(cls, "onEvent", "(IJ[BJ)V");
    }
    uuu_register_notify_callback(notify_cb, nullptr);
    // Сколько ждать первое устройство и каждое следующее после переподключения
    // (ROM → U-Boot). Без этого libuuu ждёт бесконечно.
    uuu_set_wait_timeout(wait_seconds);
    uuu_set_wait_next_timeout(wait_seconds);

    const char *s = env->GetStringUTFChars(script, nullptr);
    int rc = uuu_run_cmd_script(s, 0);
    env->ReleaseStringUTFChars(script, s);
    if (rc == 0) rc = uuu_wait_uuu_finish(0, 0);

    uuu_unregister_notify_callback(notify_cb);
    {
        std::lock_guard<std::mutex> lk(g_listener_mtx);
        env->DeleteGlobalRef(g_listener);
        g_listener = nullptr;
    }
    return rc;
}

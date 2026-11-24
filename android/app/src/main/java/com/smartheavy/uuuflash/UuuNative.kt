package com.smartheavy.uuuflash

/** Обёртка над libuuu (src/main/cpp/uuu_jni.cpp). */
object UuuNative {
    init {
        System.loadLibrary("uuuflash")
    }

    /** События libuuu. text — UTF-8 байты строки, value — статус, размер или позиция. */
    fun interface Listener {
        fun onEvent(type: Int, thread: Long, text: ByteArray?, value: Long)
    }

    @JvmStatic external fun version(): String
    @JvmStatic external fun lastError(): String

    /** Передать libuuu открытое устройство. 0 — успех, иначе код ошибки libusb. */
    @JvmStatic external fun addDevice(fd: Int): Int

    /** Устройство отключено. true — libuuu его отпустил и fd можно закрывать. */
    @JvmStatic external fun removeDevice(fd: Int): Boolean

    /** Выполнить скрипт uuu до конца. Блокирует поток. Один раз за жизнь процесса. */
    @JvmStatic external fun run(script: String, waitSeconds: Int, listener: Listener): Int

    // uuu_notify::NOTIFY_TYPE из libuuu.h, в том же порядке.
    const val CMD_TOTAL = 0
    const val CMD_START = 1
    const val CMD_END = 2
    const val CMD_INDEX = 3
    const val CMD_INFO = 4
    const val PHASE_TOTAL = 5
    const val PHASE_INDEX = 6
    const val TRANS_SIZE = 7
    const val TRANS_POS = 8
    const val WAIT_FOR = 9
    const val DEV_ATTACH = 10
    const val DECOMPRESS_START = 11
    const val DECOMPRESS_SIZE = 12
    const val DECOMPRESS_POS = 13
    const val DOWNLOAD_START = 14
    const val DOWNLOAD_END = 15
    const val THREAD_EXIT = 16
    const val DONE = 17
}

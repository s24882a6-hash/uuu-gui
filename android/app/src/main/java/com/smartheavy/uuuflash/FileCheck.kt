package com.smartheavy.uuuflash

/**
 * Проверка выбранных файлов по содержимому, а не только по имени: чтобы
 * перепутанные загрузчик и прошивка или образ одного раздела не прошли.
 * Возвращает причину отказа (коротко, для показа под строкой файла) или null.
 */
object FileCheck {

    /** Заголовок IVT i.MX8M (barker 0x412000D1, little-endian) — начало imx-boot. */
    private val IVT = byteArrayOf(0xD1.toByte(), 0x00, 0x20, 0x41)
    private val ZSTD = byteArrayOf(0x28, 0xB5.toByte(), 0x2F, 0xFD.toByte())
    private val BZIP2 = "BZh".toByteArray()
    private val GZIP = byteArrayOf(0x1F, 0x8B.toByte())

    /** Типы для фильтра проводника: загрузчик — «двоичный файл без типа». */
    val BOOTLOADER_MIME = arrayOf("application/octet-stream")
    /** Прошивка: то же плюс сжатые архивы. */
    val FIRMWARE_MIME = arrayOf(
        "application/octet-stream",
        "application/zstd", "application/x-zstd",
        "application/x-bzip2", "application/gzip", "application/x-gzip",
    )

    fun bootloader(name: String, size: Long, head: ByteArray, n: Int): String? = when {
        !head.startsWith(IVT, n) && looksLikeImage(name, head, n) -> "Это файл прошивки, а не загрузчик."
        !head.startsWith(IVT, n) -> "Это не загрузчик панели."
        size > 16_000_000 -> "Слишком большой файл для загрузчика."
        else -> null
    }

    fun firmware(name: String, size: Long, head: ByteArray, n: Int): String? {
        val lower = name.lowercase()
        if (head.startsWith(IVT, n)) return "Это загрузчик, а не прошивка."
        if (".ext4" in lower || ("rootfs" in lower && ".wic" !in lower))
            return "Это образ раздела. Нужен полный образ .wic.zst."
        val ok = when {
            lower.endsWith(".wic.zst") -> head.startsWith(ZSTD, n)
            lower.endsWith(".wic.bz2") -> head.startsWith(BZIP2, n)
            lower.endsWith(".wic.gz") -> head.startsWith(GZIP, n)
            // Несжатый образ диска: подпись MBR 55 AA в конце первого сектора.
            lower.endsWith(".wic") -> n >= 512 && head[510] == 0x55.toByte() && head[511] == 0xAA.toByte()
            else -> return "Нужен файл .wic.zst."
        }
        if (!ok) return "Файл повреждён или переименован."
        if (size in 0 until 100_000_000) return "Слишком маленький файл для прошивки."
        return null
    }

    private fun looksLikeImage(name: String, head: ByteArray, n: Int) =
        ".wic" in name.lowercase() || head.startsWith(ZSTD, n)

    private fun ByteArray.startsWith(prefix: ByteArray, n: Int) =
        n >= prefix.size && prefix.indices.all { this[it] == prefix[it] }
}

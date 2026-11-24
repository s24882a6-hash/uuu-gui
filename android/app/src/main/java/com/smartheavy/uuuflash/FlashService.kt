package com.smartheavy.uuuflash

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.ServiceInfo
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.net.Uri
import android.os.Build
import android.os.ParcelFileDescriptor
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.PowerManager
import android.os.Process
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.concurrent.thread

/**
 * Прошивка в фоне: foreground-сервис в отдельном процессе ":flash".
 *
 * Отдельный процесс — потому что libuuu рассчитан на один запуск за жизнь
 * процесса (глобальное состояние, как uuu-helper в настольном uuu-gui). После
 * каждого запуска процесс завершается, следующий стартует с чистым libuuu.
 *
 * USB сервис открывает сам: разрешение UsbManager выдаётся приложению (uid),
 * а не процессу. Экран (MainActivity) получает события широковещательно и
 * читает лог из файла — так он показывает ход работы, даже если его открыли
 * посреди прошивки.
 */
class FlashService : Service() {

    private lateinit var usbLink: UsbLink
    private lateinit var notifications: NotificationManager
    private var wakeLock: PowerManager.WakeLock? = null
    private var started = false
    private val ui = Handler(Looper.getMainLooper())
    private val timeFmt = SimpleDateFormat("HH:mm:ss.SSS", Locale.US)

    private val permissionReceiver = object : BroadcastReceiver() {
        override fun onReceive(ctx: Context, intent: Intent) {
            val dev = intent.usbDevice() ?: return
            usbLink.onPermission(dev, intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false))
        }
    }

    private val hotplugReceiver = object : BroadcastReceiver() {
        override fun onReceive(ctx: Context, intent: Intent) {
            val dev = intent.usbDevice() ?: return
            when (intent.action) {
                UsbManager.ACTION_USB_DEVICE_ATTACHED -> usbLink.onAttached(dev, "broadcast")
                UsbManager.ACTION_USB_DEVICE_DETACHED -> usbLink.onDetached(dev)
            }
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        notifications = getSystemService(NotificationManager::class.java)
        createChannel()
        val title = intent?.getStringExtra(EXTRA_TITLE) ?: "Прошивка панели"
        val notif = progressNotification(title, "Жду панель в режиме SDP…", 0)
        if (Build.VERSION.SDK_INT >= 29) {
            startForeground(NOTIF_PROGRESS, notif, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
        } else {
            startForeground(NOTIF_PROGRESS, notif)
        }
        // Второй запуск в том же процессе libuuu не переживёт — игнорируем.
        if (started || intent == null) return START_NOT_STICKY
        started = true

        File(filesDir, RUNNING_FILE).writeText(Process.myPid().toString())
        File(filesDir, LOG_FILE).writeText("")
        wakeLock = getSystemService(PowerManager::class.java)
            .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "uuuflash:flash")
            .apply { acquire(60 * 60 * 1000L) }

        usbLink = UsbLink(this, ::line)
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(permissionReceiver, IntentFilter(UsbLink.ACTION_PERMISSION), RECEIVER_NOT_EXPORTED)
            registerReceiver(hotplugReceiver, hotplugFilter(), RECEIVER_EXPORTED)
        } else {
            registerReceiver(permissionReceiver, IntentFilter(UsbLink.ACTION_PERMISSION))
            registerReceiver(hotplugReceiver, hotplugFilter())
        }

        val job = Job.from(intent)
        line("══ $title ══")
        line("libuuu ${UuuNative.version()}, процесс ${Process.myPid()}")
        send(Intent(ACTION_EVENT).putExtra(EV_STATE, STATE_RUNNING))
        usbLink.scan()
        run(job, title)
        return START_NOT_STICKY
    }

    // ── Запуск libuuu ────────────────────────────────────────────────────────

    private fun run(job: Job, title: String) {
        var failed = false
        var sawSdp = false       // ROM нашёлся и начал принимать загрузчик
        var sawFastboot = false  // дошли до U-Boot — значит загрузчик в RAM запустился
        var uid: String? = null  // UID платы — экран запомнит по нему объём памяти
        var currentCmd = ""
        // Передача и распаковка — разные потоки libuuu со своими событиями.
        // Размер передачи при потоковой распаковке .zst сначала 0 и приходит
        // позже (fastboot.cpp, flash_raw2sparse), у распаковки размера нет вовсе.
        var transTotal = 0L
        var imageTotal = 0L      // сколько libuuu пишет на шаге образа (raw2sparse)
        var lastPct = -1
        var lastMb = -1L
        status("Жду панель в режиме SDP…", 0, title)

        thread(name = "uuu") {
            val script = try {
                job.script(open(job.boardUri, job.boardName), open(job.imageUri, job.imageName))
            } catch (e: Exception) {
                line("✗ Не открыть файл: ${e.message}")
                ui.post { done(job, title, false, "0:00", DIAG_FILE, e.message ?: "", null) }
                return@thread
            }
            val t0 = System.currentTimeMillis()
            val rc = UuuNative.run(script, WAIT_SECONDS) { type, _, bytes, value ->
                val text = bytes?.let { String(it, Charsets.UTF_8).trim() }
                when (type) {
                    UuuNative.DEV_ATTACH -> {
                        line("libuuu: устройство $text")
                        // «1:144-<UID>» — после дефиса UID платы.
                        if (uid == null) uid = text?.substringAfterLast('-', "")?.uppercase()?.ifEmpty { null }
                    }
                    UuuNative.CMD_START -> {
                        if (text?.startsWith("SDP") == true) sawSdp = true
                        if (text?.startsWith("FB") == true) sawFastboot = true
                        currentCmd = text ?: ""
                        transTotal = 0; lastPct = -1; lastMb = -1
                        line("$ $text")
                        status(shortCmd(text), null, title)
                    }
                    UuuNative.CMD_INFO -> if (!text.isNullOrEmpty()) line("  $text")
                    UuuNative.CMD_END -> if (value != 0L) {
                        failed = true
                        line("  ✗ код $value: ${UuuNative.lastError()}")
                    }
                    UuuNative.WAIT_FOR -> line("ожидание: $text")
                    UuuNative.TRANS_SIZE -> if (value > 0) {
                        transTotal = value
                        if ("raw2sparse" in currentCmd) imageTotal = value
                    }
                    UuuNative.TRANS_POS -> {
                        val mb = value / 1_000_000
                        if (transTotal > 0) {
                            val pct = (value * 100 / transTotal).toInt().coerceIn(0, 100)
                            if (pct != lastPct) {
                                // В лог — только запись образа; 3 МБ загрузчика не засоряют его.
                                if (transTotal > 50_000_000 && (pct / 10 != lastPct / 10 || pct == 100))
                                    line("  записано $pct% ($mb из ${transTotal / 1_000_000} МБ)")
                                lastPct = pct
                                status("Запись: $pct% ($mb из ${transTotal / 1_000_000} МБ)", pct, title)
                            }
                        } else if (mb / 256 != lastMb / 256) {
                            lastMb = mb
                            line("  записано $mb МБ")
                            status("Запись: $mb МБ", null, title)
                        }
                    }
                    UuuNative.DECOMPRESS_POS -> {}  // идёт впереди записи, отдельно не показываем
                }
            }
            // Страховка: сжатый образ, а записано ровно столько, сколько весит
            // файл, — значит, libuuu писал его без распаковки (так было в 0.6–0.8).
            val raw = compressedSize > 0 && imageTotal == compressedSize
            if (raw) line("✗ Образ записан без распаковки — eMMC испорчена. Это ошибка приложения, сообщи разработчику.")
            val ok = rc == 0 && !failed && !raw
            val error = UuuNative.lastError()
            val sec = (System.currentTimeMillis() - t0) / 1000
            val time = "%d:%02d".format(sec / 60, sec % 60)
            if (ok) line("✓ Успешно за $time")
            else line("✗ Ошибка через $time" + if (error.isNotEmpty()) ": $error" else "")

            ui.post {
                usbLink.closeZombies()
                if (ok && job.kind == KIND_RAM) readFastbootVars()
                // Галку смотрим в самом конце, а не при запуске: её можно
                // поставить и пока идёт запись (экран пишет файл-флаг).
                val rebooted = ok && job.kind == KIND_FLASH && File(filesDir, REBOOT_FLAG).exists() && rebootPanel()
                line(
                    when {
                        rebooted -> "Готово. Панель перезагружается в новую прошивку."
                        ok && job.kind == KIND_FLASH -> "Готово. Передёрни питание панели (без кнопки) — она загрузится в новую прошивку."
                        ok -> "Проверка закончена, eMMC не менялась. Передёрни питание панели."
                        else -> "Передёрни питание панели и снова переведи её в SDP."
                    }
                )
                done(job, title, ok, time, diagnose(ok, sawSdp, sawFastboot), error, uid)
            }
        }
    }

    private fun done(job: Job, title: String, ok: Boolean, time: String, diagnosis: String, error: String, uid: String?) {
        finishNotification(ok, title, time, diagnosis)
        send(
            Intent(ACTION_EVENT)
                .putExtra(EV_STATE, STATE_DONE)
                .putExtra(EV_OK, ok)
                .putExtra(EV_TIME, time)
                .putExtra(EV_DIAGNOSIS, diagnosis)
                .putExtra(EV_MEMORY, job.memory)
                .putExtra(EV_ERROR, error)
                .putExtra(EV_UID, uid)
        )
        shutdown()
    }

    /** Открытые файлы из проводника: держим до конца процесса, libuuu делает dup. */
    private val openFiles = ArrayList<ParcelFileDescriptor>()
    /** Размер сжатого образа (.zst/.bz2/.gz) — для проверки, что его распаковали. */
    private var compressedSize = -1L

    /**
     * Путь для libuuu к файлу, выбранному в проводнике. Обычный файл —
     * "/uuufd/<fd>/<имя>" без копии (cpp/uuu_android_file.h). Если провайдер
     * отдал поток, а не файл (statSize < 0) — копируем во временную папку.
     */
    private fun open(uri: String?, name: String?): String? {
        if (uri == null || name == null) return null
        val pfd = contentResolver.openFileDescriptor(Uri.parse(uri), "r")
            ?: throw java.io.IOException("$name недоступен — выбери файл заново")
        val safe = name.replace('/', '_').replace('"', '_')
        if (pfd.statSize >= 0) {
            openFiles += pfd
            if (listOf(".zst", ".bz2", ".gz", ".xz").any { safe.endsWith(it) }) compressedSize = pfd.statSize
            line("Файл $name: читаю напрямую, ${pfd.statSize / 1_000_000} МБ")
            return "/uuufd/${pfd.fd}/$safe"
        }
        line("Файл $name отдан потоком — копирую во временную папку…")
        val f = File(cacheDir, safe)
        ParcelFileDescriptor.AutoCloseInputStream(pfd).use { input -> f.outputStream().use { input.copyTo(it, 1 shl 20) } }
        return f.absolutePath
    }

    /**
     * Почему не вышло — по тому, докуда дошёл скрипт. Объём памяти панели по
     * USB не узнать, но если универсальный загрузчик до fastboot не дошёл —
     * почти наверняка выбран не тот объём (так 2G падает на 4-гигабайтной).
     */
    private fun diagnose(ok: Boolean, sawSdp: Boolean, sawFastboot: Boolean) = when {
        ok -> DIAG_OK
        !sawSdp -> DIAG_NO_PANEL
        !sawFastboot -> DIAG_WRONG_MEMORY
        else -> DIAG_FASTBOOT_FAILED
    }

    /**
     * «Перезагрузить после прошивки», как Reboot after flash в uuu-gui: команда
     * fastboot reboot в U-Boot. Ответа можно не дождаться — плата уже ушла в сброс.
     */
    private fun rebootPanel(): Boolean {
        val (dev, conn) = usbLink.fastboot() ?: run {
            line("Перезагрузить не удалось: fastboot-устройства уже нет")
            return false
        }
        line("$ fastboot reboot")
        line("  ${UsbLink.fastboot(dev, conn, "reboot")}")
        return true
    }

    private fun readFastbootVars() {
        val (dev, conn) = usbLink.fastboot() ?: return line("fastboot-устройства нет — переменные не прочитать")
        line("Переменные U-Boot (${UsbLink.tag(dev)}):")
        for (v in listOf("version", "version-bootloader", "product", "serialno", "max-download-size"))
            line("  $v = ${UsbLink.getvar(dev, conn, v)}")
    }

    /** Процесс завершаем целиком: следующий запуск — с чистым libuuu. */
    private fun shutdown() {
        unregisterReceiver(permissionReceiver)
        unregisterReceiver(hotplugReceiver)
        wakeLock?.takeIf { it.isHeld }?.release()
        File(filesDir, RUNNING_FILE).delete()
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
        ui.postDelayed({ Process.killProcess(Process.myPid()) }, 500)
    }

    // ── События для экрана и уведомления ─────────────────────────────────────

    private fun line(text: String) {
        val s = "${timeFmt.format(Date())} $text\n"
        android.util.Log.i("UuuFlash", text)
        synchronized(this) { File(filesDir, LOG_FILE).appendText(s) }
        send(Intent(ACTION_EVENT).putExtra(EV_LINE, s))
    }

    private var lastNotifPct = -2
    private var lastNotifText = ""

    private fun status(text: String, pct: Int?, title: String) {
        send(Intent(ACTION_EVENT).putExtra(EV_STATUS, text).putExtra(EV_PCT, pct ?: -1))
        // Уведомление обновляем только при смене процента или шага.
        val p = pct ?: lastNotifPct
        if (p == lastNotifPct && text == lastNotifText) return
        lastNotifPct = p; lastNotifText = text
        notifications.notify(NOTIF_PROGRESS, progressNotification(title, text, p))
    }

    private fun send(i: Intent) = sendBroadcast(i.setPackage(packageName))

    private fun createChannel() {
        notifications.createNotificationChannel(
            NotificationChannel(CHANNEL_PROGRESS, "Ход прошивки", NotificationManager.IMPORTANCE_LOW)
        )
        notifications.createNotificationChannel(
            NotificationChannel(CHANNEL_RESULT, "Результат прошивки", NotificationManager.IMPORTANCE_HIGH)
        )
    }

    private fun openApp() = PendingIntent.getActivity(
        this, 0, Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
        PendingIntent.FLAG_IMMUTABLE,
    )

    private fun progressNotification(title: String, text: String, pct: Int): Notification =
        Notification.Builder(this, CHANNEL_PROGRESS)
            .setSmallIcon(android.R.drawable.stat_sys_download)
            .setContentTitle(title)
            .setContentText(text)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setProgress(100, pct.coerceAtLeast(0), pct < 0)
            .setContentIntent(openApp())
            .build()

    private fun finishNotification(ok: Boolean, title: String, time: String, diagnosis: String) {
        val text = when (diagnosis) {
            DIAG_OK -> "Успешно за $time"
            DIAG_FILE -> "Файл прошивки недоступен — выбери его заново"
            DIAG_NO_PANEL -> "Панель не найдена — подключи её в режиме SDP"
            DIAG_WRONG_MEMORY -> "Похоже, выбран не тот объём памяти панели"
            else -> "Ошибка на шаге fastboot — подробности в приложении"
        }
        notifications.notify(
            NOTIF_RESULT,
            Notification.Builder(this, CHANNEL_RESULT)
                .setSmallIcon(if (ok) android.R.drawable.stat_sys_download_done else android.R.drawable.stat_notify_error)
                .setContentTitle((if (ok) "✓ " else "✗ ") + title)
                .setContentText(text)
                .setAutoCancel(true)
                .setContentIntent(openApp())
                .build()
        )
    }

    private fun shortCmd(cmd: String?): String = when {
        cmd == null -> ""
        cmd.startsWith("SDP") -> "Загружаю U-Boot в RAM панели…"
        cmd.contains("raw2sparse") -> "Пишу образ в eMMC…"
        cmd.contains(" bootloader ") -> "Пишу загрузчик платы…"
        cmd.startsWith("FB: getvar") -> "Читаю переменные U-Boot…"
        else -> cmd
    }

    private fun Intent.usbDevice(): UsbDevice? =
        if (Build.VERSION.SDK_INT >= 33) getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
        else @Suppress("DEPRECATION") getParcelableExtra(UsbManager.EXTRA_DEVICE)

    private fun hotplugFilter() = IntentFilter().apply {
        addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
        addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
    }

    // ── Что запускать ────────────────────────────────────────────────────────

    /**
     * Задание: проверка в RAM или прошивка eMMC. Скрипт — как в uuu-gui.
     * universal — путь к вшитому загрузчику; загрузчик платы и образ — URI из
     * проводника (с постоянным разрешением на чтение) и их имена.
     */
    class Job(
        val kind: String,
        val memory: String,
        val universal: String,
        val boardUri: String? = null,
        val boardName: String? = null,
        val imageUri: String? = null,
        val imageName: String? = null,
    ) {

        fun toIntent(ctx: Context, title: String) = Intent(ctx, FlashService::class.java)
            .putExtra(EXTRA_TITLE, title)
            .putExtra(EXTRA_KIND, kind)
            .putExtra(EXTRA_MEMORY, memory)
            .putExtra(EXTRA_UNIVERSAL, universal)
            .putExtra(EXTRA_BOARD_URI, boardUri)
            .putExtra(EXTRA_BOARD_NAME, boardName)
            .putExtra(EXTRA_IMAGE_URI, imageUri)
            .putExtra(EXTRA_IMAGE_NAME, imageName)

        /** board и image — уже пути для libuuu (см. FlashService.open). */
        fun script(board: String?, image: String?): String = if (kind == KIND_RAM) {
            """
                uuu_version 1.4.149
                SDPS: boot -f "$universal"
                FB: getvar version
                FB: getvar version-bootloader
                FB: done
            """.trimIndent() + "\n"
        } else {
            // Как пресет EmmcAll4G: универсальный загрузчик → fastboot → шаги
            // FB: из emmc_all. Сжатый образ libuuu распаковывает на лету, только
            // если к пути приписан «/*» — как substitute_tokens() у uuu-helper.
            val compressed = listOf(".zst", ".bz2", ".gz", ".xz").any { image!!.endsWith(it) }
            val img = if (compressed) "\"$image/*\"" else "\"$image\""
            val d = "$"  // переменные U-Boot ${emmc_dev} — не шаблоны Kotlin
            """
                uuu_version 1.4.149
                SDPS: boot -f "$universal"
                FB: ucmd setenv fastboot_dev mmc
                FB: ucmd setenv mmcdev $d{emmc_dev}
                FB: ucmd mmc dev $d{emmc_dev}
                FB: flash -raw2sparse all $img
                FB: flash -scanterm -scanlimited 0x800000 bootloader "$board"
                FB: ucmd if env exists emmc_ack; then ; else setenv emmc_ack 0; fi;
                FB: ucmd mmc partconf $d{emmc_dev} $d{emmc_ack} 1 0
                FB: done
            """.trimIndent() + "\n"
        }

        companion object {
            fun from(i: Intent) = Job(
                kind = i.getStringExtra(EXTRA_KIND)!!,
                memory = i.getStringExtra(EXTRA_MEMORY)!!,
                universal = i.getStringExtra(EXTRA_UNIVERSAL)!!,
                boardUri = i.getStringExtra(EXTRA_BOARD_URI),
                boardName = i.getStringExtra(EXTRA_BOARD_NAME),
                imageUri = i.getStringExtra(EXTRA_IMAGE_URI),
                imageName = i.getStringExtra(EXTRA_IMAGE_NAME),
            )
        }
    }

    companion object {
        const val KIND_FLASH = "flash"
        const val KIND_RAM = "ram"

        const val ACTION_EVENT = "com.smartheavy.uuuflash.EVENT"
        const val EV_LINE = "line"
        const val EV_STATUS = "status"
        const val EV_PCT = "pct"
        const val EV_STATE = "state"
        const val EV_OK = "ok"
        const val EV_TIME = "time"
        const val EV_DIAGNOSIS = "diagnosis"
        const val EV_MEMORY = "memory"
        const val EV_ERROR = "error"
        const val EV_UID = "uid"
        const val STATE_RUNNING = "running"
        const val STATE_DONE = "done"

        const val DIAG_OK = "ok"
        const val DIAG_FILE = "file"
        const val DIAG_NO_PANEL = "no_panel"
        const val DIAG_WRONG_MEMORY = "wrong_memory"
        const val DIAG_FASTBOOT_FAILED = "fastboot_failed"

        /** Лог последнего запуска и признак «идёт работа» — общие с экраном. */
        const val LOG_FILE = "flash.log"
        const val RUNNING_FILE = "flash.running"
        /** Есть файл — после прошивки перезагрузить панель. Пишет экран по галке. */
        const val REBOOT_FLAG = "reboot.on"

        private const val EXTRA_TITLE = "title"
        private const val EXTRA_KIND = "kind"
        private const val EXTRA_MEMORY = "memory"
        private const val EXTRA_UNIVERSAL = "universal"
        private const val EXTRA_BOARD_URI = "board_uri"
        private const val EXTRA_BOARD_NAME = "board_name"
        private const val EXTRA_IMAGE_URI = "image_uri"
        private const val EXTRA_IMAGE_NAME = "image_name"

        private const val CHANNEL_PROGRESS = "progress"
        private const val CHANNEL_RESULT = "result"
        private const val NOTIF_PROGRESS = 1
        private const val NOTIF_RESULT = 2
        /** Сколько libuuu ждёт панель и её появление после переподключения. */
        private const val WAIT_SECONDS = 60

        /** Идёт ли сейчас работа: файл с pid есть и процесс жив. */
        fun isRunning(ctx: Context): Boolean {
            val f = File(ctx.filesDir, RUNNING_FILE)
            val pid = f.takeIf { it.exists() }?.readText()?.trim()?.toIntOrNull() ?: return false
            if (File("/proc/$pid").exists()) return true
            f.delete()  // процесс умер, не убрав за собой
            return false
        }
    }
}

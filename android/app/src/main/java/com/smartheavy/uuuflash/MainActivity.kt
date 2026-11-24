package com.smartheavy.uuuflash

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.Drawable
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.hardware.usb.UsbManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import android.text.SpannableStringBuilder
import android.text.TextUtils
import android.text.style.ForegroundColorSpan
import android.text.style.StyleSpan
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import android.widget.Button
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.Switch
import android.widget.TextView
import java.io.File
import java.security.MessageDigest
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.concurrent.thread

/**
 * Экран: панель, файлы, прошивка, лог. Работу с USB и libuuu делает
 * FlashService в отдельном процессе — приложение можно свернуть.
 *
 * Порядок прошивки — как пресет EmmcAll4G в настольном uuu-gui: вшитый
 * универсальный загрузчик (2G/4G) → RAM → fastboot → образ и загрузчик платы.
 *
 * Оформление — палитра фронта панелей (colors.xml в res/values и
 * res/values-night), светлую и тёмную тему переключает Android.
 */
class MainActivity : Activity() {

    private lateinit var usb: UsbManager
    private lateinit var panelDot: View
    private lateinit var panelTitle: TextView
    private lateinit var panelSub: TextView
    private val fileName = HashMap<String, TextView>()
    private val fileNote = HashMap<String, TextView>()
    /** Почему последний выбранный файл не подошёл — показываем под строкой файла. */
    private val fileError = HashMap<String, String>()
    private val pickButtons = ArrayList<Button>()
    private val diagButtons = ArrayList<Button>()
    private lateinit var mem4: TextView
    private lateinit var mem2: TextView
    private lateinit var rebootSwitch: Switch
    private lateinit var flashButton: Button
    private lateinit var progress: ProgressBar
    private lateinit var statusView: TextView
    private lateinit var logBody: ScrollView
    private lateinit var logToggle: TextView
    private lateinit var logView: TextView

    private val log = StringBuilder()
    private val timeFmt = SimpleDateFormat("HH:mm:ss.SSS", Locale.US)
    /** Выбранные файлы, объём памяти, перезагрузка, объём по UID платы. */
    private val prefs by lazy { getSharedPreferences("uuuflash", MODE_PRIVATE) }
    private var panelUid: String? = null
    /** Итог последнего запуска: держим на экране, пока не сменится панель или файл. */
    private var result: String? = null

    private var memory: String
        get() = prefs.getString("memory", "4G")!!
        set(v) { prefs.edit().putString("memory", v).apply() }

    /** Файл из проводника: URI с постоянным разрешением на чтение, имя и размер. */
    private class Picked(val uri: String, val name: String, val size: Long)

    private fun picked(key: String): Picked? {
        val uri = prefs.getString("${key}_uri", null) ?: return null
        return Picked(uri, prefs.getString("${key}_name", "?")!!, prefs.getLong("${key}_size", -1))
    }

    /** События от FlashService (другой процесс). */
    private val eventReceiver = object : BroadcastReceiver() {
        override fun onReceive(ctx: Context, i: Intent) {
            i.getStringExtra(FlashService.EV_LINE)?.let { append(it) }
            i.getStringExtra(FlashService.EV_STATUS)?.let {
                val pct = i.getIntExtra(FlashService.EV_PCT, -1)
                status(it, pct.takeIf { p -> p >= 0 })
            }
            when (i.getStringExtra(FlashService.EV_STATE)) {
                FlashService.STATE_RUNNING -> refresh()
                FlashService.STATE_DONE -> onDone(i)
            }
        }
    }

    /** Подключение и отключение панели — чтобы показать, что видим. */
    private val hotplugReceiver = object : BroadcastReceiver() {
        override fun onReceive(ctx: Context, intent: Intent) = showPanel()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        usb = getSystemService(UsbManager::class.java)
        buildUi()

        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(eventReceiver, IntentFilter(FlashService.ACTION_EVENT), RECEIVER_NOT_EXPORTED)
            registerReceiver(hotplugReceiver, hotplugFilter(), RECEIVER_EXPORTED)
            // Без этого разрешения не видно уведомлений о ходе и результате.
            if (checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
                requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1)
        } else {
            registerReceiver(eventReceiver, IntentFilter(FlashService.ACTION_EVENT))
            registerReceiver(hotplugReceiver, hotplugFilter())
        }

        line("UUU Flash ${version()}, ${Build.MANUFACTURER} ${Build.MODEL}, Android ${Build.VERSION.RELEASE}")
        for (name in BUNDLED) line("Вшит $name: ${sha256(bundled(name)).take(16)}…")
        // До 0.6 образ копировался внутрь приложения — убираем старые копии (2+ ГБ).
        File(filesDir, "image").deleteRecursively()
        File(filesDir, "board").deleteRecursively()
        // Лог последнего запуска (или текущего, если он ещё идёт).
        File(filesDir, FlashService.LOG_FILE).takeIf { it.length() > 0 }?.let {
            line(if (FlashService.isRunning(this)) "── идёт работа ──" else "── последний запуск ──")
            append(it.readText())
        }
        if (FlashService.isRunning(this)) status("Идёт работа — ход в уведомлении и здесь", null)
        revalidateSaved()
        showFiles()
        showPanel()
    }

    override fun onResume() {
        super.onResume()
        showPanel()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        showPanel()  // плату подключили — Android открыл нас по intent-filter
    }

    override fun onDestroy() {
        unregisterReceiver(eventReceiver)
        unregisterReceiver(hotplugReceiver)
        super.onDestroy()
    }

    @Deprecated("Activity API")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        val uri = data?.data ?: return
        if (resultCode != RESULT_OK) return
        when (requestCode) {
            PICK_BOARD -> choose(uri, KEY_BOARD)
            PICK_IMAGE -> choose(uri, KEY_IMAGE)
        }
    }

    // ── Запуск ───────────────────────────────────────────────────────────────

    private fun start(job: FlashService.Job, title: String) {
        if (FlashService.isRunning(this)) return line("Уже идёт работа")
        log.setLength(0); logView.text = ""
        result = null
        progress.visibility = View.VISIBLE
        status("Запускаю…", 0)
        startForegroundService(job.toIntent(this, title))
        refresh(running = true)
    }

    private fun testInRam(mem: String) = start(
        FlashService.Job(FlashService.KIND_RAM, mem, bundled("imx-boot_$mem.bin").absolutePath),
        "Проверка ${memoryName(mem)}, без записи",
    )

    private fun confirmFlash() {
        val mem = memory
        val universal = bundled("imx-boot_$mem.bin")
        val board = picked(KEY_BOARD) ?: return
        val image = picked(KEY_IMAGE) ?: return
        val text = SpannableStringBuilder()
        // Объём памяти этой панели уже известен по прошлой прошивке — и он другой.
        val known = panelUid?.let { prefs.getString("mem_$it", null) }
        if (known != null && known != mem) {
            warn(text, "⚠ В прошлый раз эта панель прошилась с ${memoryName(known)}.\n\n")
        }
        // Машина в именах файлов: imx-boot-pm-<машина>-… и …-pm-<машина>.rootfs…
        val boardMachine = machineOf(board.name)
        val imageMachine = machineOf(image.name)
        val mismatch = boardMachine != null && imageMachine != null && boardMachine != imageMachine
        if (mismatch) {
            warn(text, "⚠ Загрузчик для $boardMachine, а прошивка для $imageMachine. Проверь, что выбрана нужная версия панели — с чужой прошивкой панель может не загрузиться.\n\n")
        }
        text.append(
            "Память панели: ${memoryName(mem)}\n\n" +
                "Прошивка: ${image.name} (${size(image.size)})\n" +
                "Загрузчик: ${board.name}\n" +
                (if (rebootSwitch.isChecked) "После прошивки — перезагрузка\n" else "") +
                "\nВсё, что сейчас на eMMC, будет стёрто. Прошивка идёт в фоне — приложение можно свернуть."
        )
        AlertDialog.Builder(this)
            .setTitle("Перезаписать eMMC панели?")
            .setMessage(text)
            .setPositiveButton(if (mismatch) "Всё равно прошить" else "Прошить") { _, _ ->
                start(
                    FlashService.Job(
                        FlashService.KIND_FLASH, mem, universal.absolutePath,
                        board.uri, board.name, image.uri, image.name,
                    ),
                    "Прошивка ${imageMachine ?: "панели"}",
                )
            }
            .setNegativeButton("Отмена", null)
            .show()
    }

    private fun onDone(i: Intent) {
        val ok = i.getBooleanExtra(FlashService.EV_OK, false)
        val time = i.getStringExtra(FlashService.EV_TIME) ?: ""
        val mem = i.getStringExtra(FlashService.EV_MEMORY) ?: ""
        val error = i.getStringExtra(FlashService.EV_ERROR) ?: ""
        result = if (ok) "✓ Успешно за $time" else "✗ Ошибка — подробности в логе"
        status(result, if (ok) 100 else null, error = !ok)
        // Загрузчик с этим объёмом на этой плате стартовал — запоминаем по UID.
        val uid = i.getStringExtra(FlashService.EV_UID)
        if (ok && uid != null) prefs.edit().putString("mem_$uid", mem).apply()
        // Проверка прошла — этот объём и выбираем: можно сразу шить
        // (U-Boot уже в RAM, панель в fastboot — переходить в SDP не нужно).
        if (ok && mem.isNotEmpty()) { memory = mem; showMemory(mem) }
        if (!ok) showLog(true)
        // Процесс сервиса завершается через полсекунды — кнопки включаем сразу.
        refresh(running = false)
        showPanel()
        when (i.getStringExtra(FlashService.EV_DIAGNOSIS)) {
            FlashService.DIAG_FILE -> alert(
                "Файл недоступен",
                "Не удалось открыть выбранный файл.",
                "Возможно, его переместили или удалили. Выбери загрузчик и прошивку заново.\n\n$error",
            )
            FlashService.DIAG_NO_PANEL -> alert(
                "Панель не найдена",
                "За минуту панель в режиме SDP так и не появилась.",
                "Проверь кабель и OTG-переходник, переведи панель в SDP и запусти снова.",
            )
            FlashService.DIAG_WRONG_MEMORY -> alert(
                "Панель не запустила загрузчик",
                "Похоже, выбран не тот объём памяти: загрузчик ${memoryName(mem)} на этой панели не стартует.",
                "1. Передёрни питание панели и снова переведи её в SDP.\n" +
                    "2. Выбери память ${memoryName(if (mem == "4G") "2G" else "4G")} и прошей снова.\n\n" +
                    "Ошибка libuuu: $error",
            )
            FlashService.DIAG_FASTBOOT_FAILED -> alert(
                "Прошивка прервалась",
                "Ошибка на шаге fastboot — eMMC могла записаться не полностью.",
                "Передёрни питание, снова SDP и прошей заново.\n\nОшибка libuuu: $error",
            )
        }
    }

    // ── Файлы ────────────────────────────────────────────────────────────────

    /**
     * Вшитые универсальные загрузчики: libuuu читает файлы по пути, поэтому
     * копируем из assets в память приложения. Копию обновляем, если APK
     * переустановлен позже, чем она сделана (assets сжаты, размер не узнать).
     */
    private fun bundled(name: String): File {
        val f = File(filesDir, "boot/$name")
        val installed = packageManager.getPackageInfo(packageName, 0).lastUpdateTime
        if (!f.exists() || f.lastModified() < installed) {
            f.parentFile?.mkdirs()
            assets.open("boot/$name").use { input -> f.outputStream().use { input.copyTo(it) } }
        }
        return f
    }

    /**
     * Проводник Android фильтрует только по типу файла, не по имени. Поэтому
     * прячем всё с известным типом (фото, музыка, видео, документы, APK):
     * у imx-boot и .wic типа нет, у .wic.zst — архив. Остальное отсекает FileCheck.
     */
    private fun pick(requestCode: Int) {
        val key = if (requestCode == PICK_BOARD) KEY_BOARD else KEY_IMAGE
        val i = Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
            .setType("*/*")
            .putExtra(Intent.EXTRA_MIME_TYPES, if (key == KEY_BOARD) FileCheck.BOOTLOADER_MIME else FileCheck.FIRMWARE_MIME)
            // Открываем там, где лежал прошлый файл, иначе — в Download/Telegram.
            .putExtra(DocumentsContract.EXTRA_INITIAL_URI, Uri.parse(picked(key)?.uri ?: TELEGRAM_DOWNLOADS))
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION)
        @Suppress("DEPRECATION") startActivityForResult(i, requestCode)
    }

    /**
     * Выбранный файл проверяем по содержимому и, если подходит, запоминаем URI
     * с постоянным разрешением на чтение. Не копируем: сервис откроет файл сам
     * и отдаст libuuu дескриптор (cpp/uuu_android_file.h).
     */
    private fun choose(uri: Uri, key: String) {
        val what = if (key == KEY_BOARD) "Загрузчик" else "Прошивка"
        var name = "file.bin"
        var size = -1L
        contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE), null, null, null)?.use {
            if (it.moveToFirst()) {
                name = it.getString(0) ?: name
                if (!it.isNull(1)) size = it.getLong(1)
            }
        }
        val error = validate(uri, key, name, size)
        result = null
        if (error != null) {
            fileError[key] = error
            line("$what не подходит: $name. $error")
            showFiles()
            return
        }
        fileError.remove(key)
        try {
            contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (e: SecurityException) {
            line("⚠ Проводник не дал постоянный доступ к файлу: ${e.message}")
        }
        // Прежний файл того же вида больше не нужен — отпускаем разрешение.
        picked(key)?.takeIf { it.uri != uri.toString() }?.let {
            runCatching { contentResolver.releasePersistableUriPermission(Uri.parse(it.uri), Intent.FLAG_GRANT_READ_URI_PERMISSION) }
        }
        prefs.edit()
            .putString("${key}_uri", uri.toString())
            .putString("${key}_name", name)
            .putLong("${key}_size", size)
            .apply()
        line("$what: $name (${size(size)})")
        showFiles()
    }

    /** Подходит ли файл на роль загрузчика или прошивки; причина отказа или null. */
    private fun validate(uri: Uri, key: String, name: String, size: Long): String? {
        val head = ByteArray(4096)
        val n = try {
            contentResolver.openInputStream(uri)?.use { s -> s.readNBytes(head, 0, head.size) } ?: -1
        } catch (e: Exception) {
            -1
        }
        return when {
            n < 0 -> "Файл не открывается. Возможно, его удалили или переместили."
            key == KEY_BOARD -> FileCheck.bootloader(name, size, head, n)
                ?: if (isBundled(uri, size)) "Это универсальный загрузчик, он уже есть в приложении. Нужен загрузчик панели imx-boot-pm-…" else null
            else -> FileCheck.firmware(name, size, head, n)
        }
    }

    /**
     * Файлы, выбранные раньше (в том числе версиями без проверки), проверяем
     * заново при запуске: неподходящий сбрасываем и пишем почему.
     */
    private fun revalidateSaved() {
        for (key in listOf(KEY_BOARD, KEY_IMAGE)) {
            val p = picked(key) ?: continue
            val error = validate(Uri.parse(p.uri), key, p.name, p.size) ?: continue
            fileError[key] = error
            line("${if (key == KEY_BOARD) "Загрузчик" else "Прошивка"} сброшен(а): ${p.name}. $error")
            runCatching { contentResolver.releasePersistableUriPermission(Uri.parse(p.uri), Intent.FLAG_GRANT_READ_URI_PERMISSION) }
            prefs.edit().remove("${key}_uri").remove("${key}_name").remove("${key}_size").apply()
        }
    }

    /**
     * Универсальный 2G/4G тоже начинается с IVT, но в eMMC его писать нельзя —
     * это U-Boot от EVK, панель с ним не загрузится. Узнаём по размеру и SHA-256.
     */
    private fun isBundled(uri: Uri, size: Long): Boolean {
        val same = BUNDLED.map { bundled(it) }.filter { it.length() == size }
        if (same.isEmpty()) return false
        val md = MessageDigest.getInstance("SHA-256")
        contentResolver.openInputStream(uri)?.use { s ->
            val buf = ByteArray(1 shl 16)
            while (true) { val k = s.read(buf); if (k <= 0) break; md.update(buf, 0, k) }
        } ?: return false
        val hash = md.digest().joinToString("") { "%02x".format(it) }
        return same.any { sha256(it) == hash }
    }

    private fun showFiles() {
        for (key in listOf(KEY_BOARD, KEY_IMAGE)) {
            val p = picked(key)
            val err = fileError[key]
            fileName.getValue(key).apply {
                text = p?.name ?: "не выбран"
                setTextColor(getColor(if (p != null) R.color.text else R.color.text2))
            }
            fileNote.getValue(key).apply {
                when {
                    err != null -> { text = err; setTextColor(getColor(R.color.error)) }
                    p != null -> { text = size(p.size); setTextColor(getColor(R.color.text2)) }
                    else -> text = if (key == KEY_BOARD) "imx-boot-…-flash_evk" else "образ диска .wic.zst"
                }
                if (err == null && p == null) setTextColor(getColor(R.color.text2))
            }
        }
        refresh()
    }

    private fun sha256(f: File): String {
        val md = MessageDigest.getInstance("SHA-256")
        f.inputStream().use { s ->
            val buf = ByteArray(1 shl 20)
            while (true) { val n = s.read(buf); if (n <= 0) break; md.update(buf, 0, n) }
        }
        return md.digest().joinToString("") { "%02x".format(it) }
    }

    // ── Панель ───────────────────────────────────────────────────────────────

    /** Что сейчас висит на USB из наших устройств. Без открытия — только список. */
    private fun panelState(): String? = usb.deviceList.values
        .firstOrNull { it.vendorId in UsbLink.OUR_VIDS }
        ?.let { UsbLink.known(it).ifEmpty { "устройство ${UsbLink.tag(it)}" } }

    private fun showPanel() {
        val state = panelState()
        panelUid = null
        if (state != null) {
            panelTitle.text = "Панель подключена"
            panelSub.text = state
        } else {
            panelTitle.text = "Панель не подключена"
            panelSub.text = "Зажми скрытую кнопку, включи питание и подключи панель к телефону"
        }
        (panelDot.background as GradientDrawable).setColor(getColor(if (state != null) R.color.ok else R.color.text2))
        showMemory(null)
        refresh()
        readUidAsync()
    }

    /**
     * UID платы из ROM — чтобы подсказать объём памяти, с которым она уже
     * прошивалась. Только чтение дескриптора; пока идёт работа, плату не трогаем.
     */
    private fun readUidAsync() {
        if (FlashService.isRunning(this)) return
        val dev = usb.deviceList.values.firstOrNull { it.vendorId == 0x1fc9 && it.productId == 0x0146 } ?: return
        if (!usb.hasPermission(dev)) return
        thread(name = "uid") {
            val uid = usb.openDevice(dev)?.let { conn -> try { UsbLink.romUid(conn) } finally { conn.close() } }
            runOnUiThread {
                if (uid == null || panelState() == null) return@runOnUiThread
                panelUid = uid
                val known = prefs.getString("mem_$uid", null)
                panelSub.text = "${UsbLink.known(dev)} · UID $uid" +
                    (known?.let { "\nВ прошлый раз прошита с памятью ${memoryName(it)}" } ?: "")
                // Объём, который этой панели уже подошёл, выбираем сразу.
                if (known != null && !FlashService.isRunning(this)) memory = known
                showMemory(known)
            }
        }
    }

    // ── UI ───────────────────────────────────────────────────────────────────

    private val density by lazy { resources.displayMetrics.density }
    private fun dp(v: Int) = (v * density).toInt()
    private val wrap = LinearLayout.LayoutParams.WRAP_CONTENT
    private val match = LinearLayout.LayoutParams.MATCH_PARENT

    private fun rounded(color: Int, radius: Int) = GradientDrawable().apply {
        cornerRadius = dp(radius).toFloat()
        setColor(color)
    }

    private fun pressable(content: Drawable, radius: Int) =
        RippleDrawable(ColorStateList.valueOf(0x33808080), content, rounded(Color.WHITE, radius))

    private fun label(text: String, size: Float, color: Int, bold: Boolean = false) = TextView(this).apply {
        this.text = text
        textSize = size
        setTextColor(getColor(color))
        if (bold) typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL)
    }

    private fun card(parent: LinearLayout, title: String?): LinearLayout = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        background = rounded(getColor(R.color.card), 12)
        setPadding(dp(14), dp(10), dp(14), dp(12))
        if (title != null) addView(cardTitle(title), LinearLayout.LayoutParams(match, wrap).apply { bottomMargin = dp(6) })
        parent.addView(this, LinearLayout.LayoutParams(match, wrap).apply { topMargin = dp(8) })
    }

    private fun cardTitle(text: String) = label(text.uppercase(), 11f, R.color.text2, bold = true).apply {
        letterSpacing = 0.08f
    }

    /** Вторичная кнопка: заливка цветом контролов, как кнопки на панели. */
    private fun tonalButton(text: String, onClick: () -> Unit) = Button(this).apply {
        this.text = text
        isAllCaps = false
        textSize = 13f
        setTextColor(getColor(R.color.text))
        typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL)
        background = pressable(rounded(getColor(R.color.control), 8), 8)
        stateListAnimator = null
        minHeight = dp(34); minimumHeight = dp(34)
        minWidth = 0; minimumWidth = 0
        setPadding(dp(14), 0, dp(14), 0)
        setOnClickListener { onClick() }
    }

    private fun divider() = View(this).apply { setBackgroundColor(getColor(R.color.divider)) }

    private fun buildUi() {
        val page = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(12), dp(8), dp(12), dp(16))
        }

        // Шапка: знак «Точмаш», название, версия.
        page.addView(LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            addView(ImageView(this@MainActivity).apply { setImageResource(R.drawable.logo_mark) },
                LinearLayout.LayoutParams(dp(28), dp(24)).apply { rightMargin = dp(10) })
            addView(LinearLayout(this@MainActivity).apply {
                orientation = LinearLayout.VERTICAL
                addView(label("UUU Flash", 17f, R.color.text, bold = true))
                addView(label("Прошивка панелей по USB", 12f, R.color.text2))
            }, LinearLayout.LayoutParams(0, wrap, 1f))
            addView(label(version(), 12f, R.color.text2))
            addView(ImageView(this@MainActivity).apply {
                setImageResource(R.drawable.ic_info)
                imageTintList = ColorStateList.valueOf(getColor(R.color.text2))
                contentDescription = Help.TITLE
                setPadding(dp(8), dp(8), dp(8), dp(8))
                background = pressable(rounded(Color.TRANSPARENT, 20), 20)
                setOnClickListener { showHelp() }
            }, LinearLayout.LayoutParams(dp(40), dp(40)).apply { leftMargin = dp(4) })
        }, LinearLayout.LayoutParams(match, wrap).apply { topMargin = dp(4) })

        // Панель: точка-индикатор, состояние, подсказка или UID.
        card(page, "Панель").apply {
            addView(LinearLayout(this@MainActivity).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                panelDot = View(this@MainActivity).apply {
                    background = GradientDrawable().apply { shape = GradientDrawable.OVAL; setColor(getColor(R.color.text2)) }
                }
                addView(panelDot, LinearLayout.LayoutParams(dp(8), dp(8)).apply { rightMargin = dp(8) })
                panelTitle = label("", 15f, R.color.text, bold = true)
                addView(panelTitle)
            })
            panelSub = label("", 12f, R.color.text2).apply { setPadding(dp(16), dp(1), 0, 0) }
            addView(panelSub)
        }

        // Файлы: загрузчик и прошивка, у каждого — имя, размер или ошибка.
        card(page, "Файлы").apply {
            addView(fileRow(KEY_BOARD, "Загрузчик") { pick(PICK_BOARD) })
            addView(divider(), LinearLayout.LayoutParams(match, dp(1)).apply { topMargin = dp(8); bottomMargin = dp(8) })
            addView(fileRow(KEY_IMAGE, "Прошивка") { pick(PICK_IMAGE) })
        }

        // Прошивка: память, перезагрузка, кнопка, прогресс.
        card(page, "Прошивка").apply {
            addView(LinearLayout(this@MainActivity).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                addView(label("Память панели", 14f, R.color.text), LinearLayout.LayoutParams(0, wrap, 1f))
                addView(memorySwitch())
            })
            addView(LinearLayout(this@MainActivity).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                addView(label("Перезагрузить после прошивки", 14f, R.color.text), LinearLayout.LayoutParams(0, wrap, 1f))
                // Как «Reboot after flash» в uuu-gui: по умолчанию выключено, запоминается.
                rebootSwitch = Switch(this@MainActivity).apply {
                    isChecked = prefs.getBoolean("reboot", false)
                    val states = arrayOf(intArrayOf(android.R.attr.state_checked), intArrayOf())
                    thumbTintList = ColorStateList(states, intArrayOf(getColor(R.color.accent), Color.WHITE))
                    trackTintList = ColorStateList(states, intArrayOf(getColor(R.color.accent), getColor(R.color.text2)))
                    setOnCheckedChangeListener { _, on -> prefs.edit().putBoolean("reboot", on).apply(); syncRebootFlag(on) }
                }
                syncRebootFlag(rebootSwitch.isChecked)
                addView(rebootSwitch)
            }, LinearLayout.LayoutParams(match, wrap).apply { topMargin = dp(4) })

            flashButton = Button(this@MainActivity).apply {
                text = "Прошить панель"
                isAllCaps = false
                textSize = 15f
                setTextColor(Color.WHITE)
                typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL)
                background = pressable(rounded(getColor(R.color.accent), 10), 10)
                stateListAnimator = null
                setOnClickListener { confirmFlash() }
            }
            addView(flashButton, LinearLayout.LayoutParams(match, dp(44)).apply { topMargin = dp(10) })

            progress = ProgressBar(this@MainActivity, null, android.R.attr.progressBarStyleHorizontal).apply {
                max = 100
                progressTintList = ColorStateList.valueOf(getColor(R.color.accent))
                progressBackgroundTintList = ColorStateList.valueOf(getColor(R.color.control))
                visibility = View.GONE
            }
            addView(progress, LinearLayout.LayoutParams(match, wrap).apply { topMargin = dp(8) })
            statusView = label("", 12f, R.color.text2).apply { gravity = Gravity.CENTER_HORIZONTAL }
            addView(statusView, LinearLayout.LayoutParams(match, wrap).apply { topMargin = dp(6) })

            // Диагностика без записи: дойдёт ли новый телефон до fastboot и
            // какой объём памяти у незнакомой панели. Обычно не нужна — свёрнута.
            val diagRow = LinearLayout(this@MainActivity).apply {
                orientation = LinearLayout.HORIZONTAL
                visibility = View.GONE
                for ((i, mem) in listOf("4G", "2G").withIndex()) {
                    val b = tonalButton("${memoryName(mem)} → RAM") { testInRam(mem) }
                    diagButtons += b
                    addView(b, LinearLayout.LayoutParams(0, dp(34), 1f).apply { if (i == 0) rightMargin = dp(8) })
                }
            }
            val diagToggle = label("Проверка без записи ▸", 12f, R.color.text2).apply {
                setPadding(0, dp(10), 0, dp(2))
                setOnClickListener {
                    val open = diagRow.visibility != View.VISIBLE
                    diagRow.visibility = if (open) View.VISIBLE else View.GONE
                    text = if (open) "Проверка без записи ▾" else "Проверка без записи ▸"
                }
            }
            addView(diagToggle)
            addView(diagRow, LinearLayout.LayoutParams(match, wrap).apply { topMargin = dp(4) })
        }

        // Лог: свёрнут, раскрывается по нажатию или сам — при ошибке.
        card(page, null).apply {
            addView(LinearLayout(this@MainActivity).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                logToggle = cardTitle("Лог ▸").apply { setOnClickListener { showLog(logBody.visibility != View.VISIBLE) } }
                addView(logToggle, LinearLayout.LayoutParams(0, wrap, 1f))
                addView(label("Поделиться", 13f, R.color.accent, bold = true).apply {
                    setPadding(dp(8), dp(4), 0, dp(4))
                    setOnClickListener { shareLog() }
                })
            })
            logView = TextView(this@MainActivity).apply {
                typeface = Typeface.MONOSPACE
                textSize = 11f
                setTextColor(getColor(R.color.text))
                setTextIsSelectable(true)
            }
            logBody = ScrollView(this@MainActivity).apply {
                addView(logView)
                visibility = View.GONE
                // Лог прокручивается сам, а не вместе со страницей: вложенная
                // прокрутка Android + запрет странице перехватывать касание.
                isNestedScrollingEnabled = true
            }
            // Касание забирает выделяемый текст лога, до контейнера оно не
            // доходит — поэтому запрет ставим здесь. Самому контейнеру лога
            // перехват оставляем: он и прокручивает.
            logView.setOnTouchListener { _, e ->
                if (e.action == MotionEvent.ACTION_DOWN) (logBody.parent as View).parent.requestDisallowInterceptTouchEvent(true)
                false
            }
            addView(logBody, LinearLayout.LayoutParams(match, dp(240)).apply { topMargin = dp(6) })
        }

        setContentView(ScrollView(this).apply {
            fitsSystemWindows = true
            isFillViewport = true
            addView(page)
        })
    }

    /** Строка файла: подпись, имя (с многоточием в середине), размер или ошибка, «Выбрать». */
    private fun fileRow(key: String, title: String, onPick: () -> Unit) = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
        addView(LinearLayout(this@MainActivity).apply {
            orientation = LinearLayout.VERTICAL
            addView(label(title, 12f, R.color.text2))
            val name = label("", 14f, R.color.text).apply {
                maxLines = 1
                ellipsize = TextUtils.TruncateAt.MIDDLE
            }
            fileName[key] = name
            addView(name, LinearLayout.LayoutParams(match, wrap).apply { topMargin = dp(1) })
            val note = label("", 12f, R.color.text2).apply { maxLines = 2 }
            fileNote[key] = note
            addView(note)
        }, LinearLayout.LayoutParams(0, wrap, 1f).apply { rightMargin = dp(12) })
        val b = tonalButton("Выбрать", onPick)
        pickButtons += b
        addView(b, LinearLayout.LayoutParams(wrap, dp(34)))
    }

    /** Переключатель «4 ГБ | 2 ГБ»: выбранный сегмент — на фоне карточки. */
    private fun memorySwitch() = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        background = rounded(getColor(R.color.control), 8)
        setPadding(dp(2), dp(2), dp(2), dp(2))
        fun segment(mem: String) = label(memoryName(mem), 13f, R.color.text, bold = true).apply {
            gravity = Gravity.CENTER
            setPadding(dp(12), dp(5), dp(12), dp(5))
            setOnClickListener { if (!FlashService.isRunning(this@MainActivity)) { memory = mem; showMemory(panelUid?.let { prefs.getString("mem_$it", null) }) } }
        }
        mem4 = segment("4G"); mem2 = segment("2G")
        addView(mem4); addView(mem2)
    }

    /** Выбранный объём и ✓ у того, что этой панели уже подходил. */
    private fun showMemory(known: String?) {
        for ((view, mem) in listOf(mem4 to "4G", mem2 to "2G")) {
            val selected = memory == mem
            view.text = memoryName(mem) + if (known == mem) " ✓" else ""
            view.background = if (selected) rounded(getColor(R.color.card), 6) else null
            view.setTextColor(getColor(if (selected) R.color.text else R.color.text2))
        }
    }

    /**
     * Галку сервис смотрит в самом конце прошивки (другой процесс — через файл),
     * поэтому её можно поставить или снять и во время записи.
     */
    private fun syncRebootFlag(on: Boolean) {
        val f = File(filesDir, FlashService.REBOOT_FLAG)
        if (on) f.writeText("1") else f.delete()
    }

    private fun showLog(open: Boolean) {
        logBody.visibility = if (open) View.VISIBLE else View.GONE
        logToggle.text = if (open) "ЛОГ ▾" else "ЛОГ ▸"
        if (open) logBody.post { logBody.fullScroll(ScrollView.FOCUS_DOWN) }
    }

    /**
     * Что можно нажимать. «Прошить» — только когда есть панель и оба файла;
     * пока нельзя, под кнопкой сказано почему.
     */
    private fun refresh(running: Boolean = FlashService.isRunning(this)) {
        val panel = panelState() != null
        val files = picked(KEY_BOARD) != null && picked(KEY_IMAGE) != null
        val canFlash = !running && panel && files
        flashButton.isEnabled = canFlash
        flashButton.alpha = if (canFlash) 1f else 0.4f
        pickButtons.forEach { it.isEnabled = !running; it.alpha = if (running) 0.4f else 1f }
        diagButtons.forEach { it.isEnabled = !running && panel; it.alpha = if (!running && panel) 1f else 0.4f }
        if (!running && result == null) {
            statusView.setTextColor(getColor(R.color.text2))
            statusView.text = when {
                !files -> "Выбери загрузчик и прошивку"
                !panel -> "Подключи панель в режиме SDP"
                else -> "Всё готово к прошивке"
            }
            progress.visibility = View.GONE
        }
    }

    /** text == null — не менять надпись; pct == null — не менять полосу. */
    private fun status(text: String?, pct: Int?, error: Boolean = false) = runOnUiThread {
        text?.let {
            statusView.text = it
            statusView.setTextColor(getColor(if (error) R.color.error else R.color.text2))
        }
        pct?.let { progress.visibility = View.VISIBLE; progress.progress = it }
    }

    private fun showHelp() {
        AlertDialog.Builder(this).setTitle(Help.TITLE).setMessage(Help.text()).setPositiveButton("Понятно", null).show()
    }

    /** Окно, которое трудно не заметить: первая строка красным. */
    private fun alert(title: String, headline: String, details: String) {
        val text = SpannableStringBuilder()
        warn(text, headline + "\n\n")
        text.append(details)
        AlertDialog.Builder(this).setTitle(title).setMessage(text).setPositiveButton("Понятно", null).show()
    }

    private fun warn(sb: SpannableStringBuilder, s: String) {
        val start = sb.length
        sb.append(s)
        sb.setSpan(ForegroundColorSpan(getColor(R.color.error)), start, sb.length, 0)
        sb.setSpan(StyleSpan(Typeface.BOLD), start, sb.length, 0)
    }

    private fun shareLog() {
        val send = Intent(Intent.ACTION_SEND).setType("text/plain")
            .putExtra(Intent.EXTRA_SUBJECT, "UUU Flash: ${Build.MANUFACTURER} ${Build.MODEL}")
            .putExtra(Intent.EXTRA_TEXT, log.toString())
        startActivity(Intent.createChooser(send, "Отправить лог"))
    }

    private fun line(text: String) = append("${timeFmt.format(Date())} $text\n")

    private fun append(s: String) = runOnUiThread {
        log.append(s)
        logView.append(s)
        if (logBody.visibility == View.VISIBLE) logBody.post { logBody.fullScroll(ScrollView.FOCUS_DOWN) }
    }

    private fun version() = packageManager.getPackageInfo(packageName, 0).versionName ?: ""
    private fun machineOf(fileName: String) = MACHINE.find(fileName)?.value
    private fun memoryName(m: String) = m.replace("G", " ГБ")
    private fun size(bytes: Long) = when {
        bytes < 0 -> "размер неизвестен"
        bytes >= 1_000_000_000 -> "%.1f ГБ".format(bytes / 1e9)
        else -> "%.1f МБ".format(bytes / 1e6)
    }

    private fun hotplugFilter() = IntentFilter().apply {
        addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
        addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
    }

    companion object {
        private const val PICK_BOARD = 1
        private const val PICK_IMAGE = 2
        private const val KEY_BOARD = "board"
        private const val KEY_IMAGE = "image"
        private val BUNDLED = listOf("imx-boot_4G.bin", "imx-boot_2G.bin")
        /** Куда Telegram сохраняет файлы — стартовая папка проводника в первый раз. */
        private const val TELEGRAM_DOWNLOADS = "content://com.android.externalstorage.documents/document/primary%3ADownload%2FTelegram"
        /** Машина в имени файла сборки: pm-<машина>. */
        private val MACHINE = Regex("pm-[a-z0-9_]+")
    }
}

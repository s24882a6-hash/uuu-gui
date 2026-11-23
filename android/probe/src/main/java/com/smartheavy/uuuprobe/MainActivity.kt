package com.smartheavy.uuuprobe

import android.app.Activity
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.graphics.Typeface
import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbEndpoint
import android.hardware.usb.UsbInterface
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import android.view.WindowManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.Executors

/**
 * Этап 0: проверка, что телефон может работать USB-хостом для i.MX8M Plus.
 *
 * Ничего не пишет ни в ROM, ни в eMMC. Для каждого устройства из device_filter:
 * дескрипторы → разрешение → claimInterface (сначала без force, потом с force,
 * то есть с отключением драйвера ядра) → чтение HID report descriptor или
 * fastboot getvar. Всё это — только чтение.
 */
class MainActivity : Activity() {

    private lateinit var usb: UsbManager
    private lateinit var logView: TextView
    private lateinit var scroll: ScrollView
    private val log = StringBuilder()
    private val io = Executors.newSingleThreadExecutor()
    private val timeFmt = SimpleDateFormat("HH:mm:ss.SSS", Locale.US)

    /** deviceName → момент подключения, чтобы мерить задержку до разрешения. */
    private val attachedAt = HashMap<String, Long>()
    /** Состояние по deviceName: описано в логе / спросили разрешение / проверено. */
    private val seen = HashSet<String>()
    private val asked = HashSet<String>()
    private val probed = HashSet<String>()
    private val ui = Handler(Looper.getMainLooper())

    private val permissionReceiver = object : BroadcastReceiver() {
        override fun onReceive(ctx: Context, intent: Intent) {
            val dev = intent.usbDevice() ?: return line("ответ о разрешении без устройства")
            val granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
            line("${tag(dev)} разрешение из диалога requestPermission: ${if (granted) "дано" else "ОТКАЗАНО"}${since(dev)}")
            if (granted) probeOnce(dev)
        }
    }

    private val hotplugReceiver = object : BroadcastReceiver() {
        override fun onReceive(ctx: Context, intent: Intent) {
            val dev = intent.usbDevice() ?: return
            when (intent.action) {
                UsbManager.ACTION_USB_DEVICE_ATTACHED -> onAttached(dev, "broadcast")
                UsbManager.ACTION_USB_DEVICE_DETACHED -> {
                    if (dev.deviceName in seen) line("${tag(dev)} отключено")
                    forget(dev.deviceName)
                }
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        usb = getSystemService(UsbManager::class.java)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        buildUi()

        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(permissionReceiver, IntentFilter(ACTION_PERMISSION), RECEIVER_NOT_EXPORTED)
            // Attach/detach — защищённые системные broadcast'ы, их шлёт только система.
            registerReceiver(hotplugReceiver, hotplugFilter(), RECEIVER_EXPORTED)
        } else {
            registerReceiver(permissionReceiver, IntentFilter(ACTION_PERMISSION))
            registerReceiver(hotplugReceiver, hotplugFilter())
        }

        line("UUU Probe ${packageManager.getPackageInfo(packageName, 0).versionName}")
        line("Телефон: ${Build.MANUFACTURER} ${Build.MODEL} (${Build.DEVICE})")
        line("Android ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT}), патч ${Build.VERSION.SECURITY_PATCH}")
        line("USB host: ${packageManager.hasSystemFeature(PackageManager.FEATURE_USB_HOST)}")
        line("Запуск: ${if (intent?.action == UsbManager.ACTION_USB_DEVICE_ATTACHED) "по подключению платы" else "вручную"}")
        handleIntent(intent)
        scan()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleIntent(intent)
    }

    override fun onDestroy() {
        unregisterReceiver(permissionReceiver)
        unregisterReceiver(hotplugReceiver)
        io.shutdown()
        super.onDestroy()
    }

    // ── Обнаружение ────────────────────────────────────────────────────────────

    private fun handleIntent(intent: Intent?) {
        if (intent?.action == UsbManager.ACTION_USB_DEVICE_ATTACHED) {
            intent.usbDevice()?.let { onAttached(it, "intent-filter") }
        }
    }

    private fun scan() {
        val list = usb.deviceList.values
        line("── сканирование: устройств ${list.size}")
        list.forEach { onAttached(it, "scan") }
    }

    private fun forget(name: String) {
        attachedAt.remove(name); seen.remove(name); asked.remove(name); probed.remove(name)
    }

    /**
     * Вызывается и из broadcast, и из intent-filter (onNewIntent), и при скане.
     * Порядок важен: если Android сам выдал разрешение через «Открыть UUU Probe?»,
     * это видно только при повторном заходе сюда из intent-filter.
     */
    private fun onAttached(dev: UsbDevice, via: String) {
        val name = dev.deviceName
        attachedAt.putIfAbsent(name, SystemClock.elapsedRealtime())
        if (seen.add(name)) {
            line("")
            if (dev.vendorId !in OUR_VIDS) {
                line("${tag(dev)} пропускаю ($via): не наше устройство, class=${hex2(dev.deviceClass)}")
                return
            }
            line("${tag(dev)} найдено ($via): ${known(dev)}")
            describe(dev)
        } else if (dev.vendorId !in OUR_VIDS) {
            return
        } else {
            line("${tag(dev)} снова пришло ($via)${since(dev)}")
        }

        if (usb.hasPermission(dev)) {
            line("${tag(dev)} разрешение есть (путь: $via)${since(dev)}")
            probeOnce(dev)
            return
        }
        if (via == "intent-filter") {
            line("${tag(dev)} пришло через intent-filter, но разрешения нет")
        }
        // Даём Android 3 с на собственный диалог «Открыть UUU Probe?», и только
        // потом спрашиваем явно — иначе два окна перебивают друг друга.
        if (asked.add(name)) {
            line("${tag(dev)} жду 3 с, выдаст ли Android разрешение сам")
            ui.postDelayed({ askIfStillNeeded(dev) }, 3000)
        }
    }

    private fun askIfStillNeeded(dev: UsbDevice) {
        if (dev.deviceName !in seen || dev.deviceName in probed) return
        if (usb.hasPermission(dev)) {
            line("${tag(dev)} разрешение появилось само${since(dev)}")
            probeOnce(dev)
            return
        }
        line("${tag(dev)} разрешения нет — вызываю requestPermission")
        val flags = if (Build.VERSION.SDK_INT >= 31) PendingIntent.FLAG_MUTABLE else 0
        val pi = PendingIntent.getBroadcast(
            this, dev.deviceId, Intent(ACTION_PERMISSION).setPackage(packageName), flags
        )
        usb.requestPermission(dev, pi)
    }

    private fun describe(dev: UsbDevice) {
        line("  путь ${dev.deviceName}, class=${hex2(dev.deviceClass)} bcdDevice=${dev.version} интерфейсов ${dev.interfaceCount}")
        for (i in 0 until dev.interfaceCount) {
            val intf = dev.getInterface(i)
            line("  intf#${intf.id} alt=${intf.alternateSetting} class=${hex2(intf.interfaceClass)}/${hex2(intf.interfaceSubclass)}/${hex2(intf.interfaceProtocol)} ${className(intf)}")
            for (e in 0 until intf.endpointCount) {
                val ep = intf.getEndpoint(e)
                line("    ep ${hex2(ep.address)} ${epType(ep)} ${if (ep.direction == UsbConstants.USB_DIR_IN) "IN" else "OUT"} maxPacket=${ep.maxPacketSize} interval=${ep.interval}")
            }
        }
    }

    // ── Проверка устройства (в фоне, чтобы таймауты USB не повесили UI) ─────────

    private fun probeOnce(dev: UsbDevice) {
        if (probed.add(dev.deviceName)) io.execute { probe(dev) }
    }

    private fun probe(dev: UsbDevice) {
        val t = tag(dev)
        val conn = usb.openDevice(dev)
        if (conn == null) {
            line("$t openDevice: null — FAIL")
            return
        }
        try {
            line("$t открыто: fd=${conn.fileDescriptor}, serial=${conn.serial ?: dev.serialNumber ?: "-"}")
            line("$t строки: \"${dev.manufacturerName ?: "-"}\" / \"${dev.productName ?: "-"}\", rawDescriptors=${conn.rawDescriptors?.size ?: -1} байт")
            if (dev.interfaceCount == 0) return line("$t нет интерфейсов")
            val intf = dev.getInterface(0)

            var claimed = conn.claimInterface(intf, false)
            line("$t claimInterface(force=false): $claimed")
            if (!claimed) {
                claimed = conn.claimInterface(intf, true)
                line("$t claimInterface(force=true, с отключением драйвера ядра): $claimed")
            }
            if (!claimed) return line("$t ИТОГ: FAIL — интерфейс не захвачен")

            when {
                intf.interfaceClass == UsbConstants.USB_CLASS_HID -> hidReportDescriptor(t, conn, intf)
                isFastboot(intf) -> fastbootGetvars(t, conn, intf)
                else -> line("$t протокол не проверяю (не HID и не fastboot)")
            }
            conn.releaseInterface(intf)
            line("$t ИТОГ: OK")
        } catch (e: Exception) {
            line("$t исключение: $e")
        } finally {
            conn.close()
        }
    }

    /** GET_DESCRIPTOR(HID report) на ep0 — только чтение, ROM на него просто отвечает. */
    private fun hidReportDescriptor(t: String, conn: UsbDeviceConnection, intf: UsbInterface) {
        val buf = ByteArray(1024)
        val n = conn.controlTransfer(0x81, 0x06, 0x2200, intf.id, buf, buf.size, 1000)
        if (n < 0) return line("$t HID report descriptor: ошибка $n")
        line("$t HID report descriptor: $n байт, report ID: ${reportIds(buf, n)}")
        line("$t   ${hex(buf, n)}")
        // У i.MX8M Plus ROM уникальный ID чипа лежит в строке №4, а не в
        // iSerialNumber (rominfo.cpp: MX815 serial_idx=4). По нему libuuu
        // узнаёт ту же плату после переподключения.
        for (idx in listOf(4)) line("$t строка №$idx (UID для libuuu): ${stringDescriptor(conn, idx) ?: "нет"}")
    }

    private fun stringDescriptor(conn: UsbDeviceConnection, idx: Int): String? {
        val buf = ByteArray(255)
        val n = conn.controlTransfer(0x80, 0x06, 0x0300 or idx, 0x0409, buf, buf.size, 1000)
        if (n < 2 || buf[1].toInt() != 0x03) return null
        return String(buf, 2, minOf(n, buf[0].toInt() and 0xff) - 2, Charsets.UTF_16LE).trim()
    }

    /** fastboot getvar — только чтение переменных U-Boot. */
    private fun fastbootGetvars(t: String, conn: UsbDeviceConnection, intf: UsbInterface) {
        var out: UsbEndpoint? = null
        var inp: UsbEndpoint? = null
        for (e in 0 until intf.endpointCount) {
            val ep = intf.getEndpoint(e)
            if (ep.type != UsbConstants.USB_ENDPOINT_XFER_BULK) continue
            if (ep.direction == UsbConstants.USB_DIR_OUT) out = ep else inp = ep
        }
        if (out == null || inp == null) return line("$t fastboot: нет bulk-endpoint'ов")
        for (v in listOf("version", "version-bootloader", "product", "serialno")) {
            val cmd = "getvar:$v".toByteArray()
            val w = conn.bulkTransfer(out, cmd, cmd.size, 1000)
            if (w < 0) { line("$t fastboot $v: ошибка записи $w"); continue }
            val resp = ByteArray(256)
            var answer = ""
            for (attempt in 0 until 8) { // INFO-строки, потом OKAY или FAIL
                val r = conn.bulkTransfer(inp, resp, resp.size, 1000)
                if (r < 0) { answer += " [таймаут $r]"; break }
                val s = String(resp, 0, r)
                answer += " $s"
                if (!s.startsWith("INFO")) break
            }
            line("$t fastboot $v:${answer}")
        }
    }

    // ── Мелочи ───────────────────────────────────────────────────────────────

    private fun buildUi() {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            fitsSystemWindows = true
        }
        val buttons = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        fun button(text: String, onClick: () -> Unit) = Button(this).apply {
            this.text = text
            setOnClickListener { onClick() }
            buttons.addView(this, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        }
        button("Сканировать") { seen.clear(); asked.clear(); probed.clear(); scan() }
        button("Поделиться") { shareLog() }
        button("Очистить") { log.setLength(0); logView.text = "" }
        logView = TextView(this).apply {
            typeface = Typeface.MONOSPACE
            textSize = 11f
            setTextIsSelectable(true)
            setPadding(24, 16, 24, 16)
        }
        scroll = ScrollView(this).apply { addView(logView) }
        root.addView(buttons)
        root.addView(scroll, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f))
        setContentView(root)
    }

    private fun shareLog() {
        val send = Intent(Intent.ACTION_SEND)
            .setType("text/plain")
            .putExtra(Intent.EXTRA_SUBJECT, "UUU Probe: ${Build.MANUFACTURER} ${Build.MODEL}")
            .putExtra(Intent.EXTRA_TEXT, log.toString())
        startActivity(Intent.createChooser(send, "Отправить лог"))
    }

    private fun line(text: String) {
        val s = if (text.isEmpty()) "\n" else "${timeFmt.format(Date())} $text\n"
        Log.i(TAG, text)
        runOnUiThread {
            log.append(s)
            logView.append(s)
            scroll.post { scroll.fullScroll(ScrollView.FOCUS_DOWN) }
        }
    }

    private fun since(dev: UsbDevice): String {
        val t0 = attachedAt[dev.deviceName] ?: return ""
        return " (+${SystemClock.elapsedRealtime() - t0} мс после подключения)"
    }

    private fun tag(dev: UsbDevice) = "[${hex4(dev.vendorId)}:${hex4(dev.productId)}]"

    private fun known(dev: UsbDevice): String = when (dev.vendorId to dev.productId) {
        0x1fc9 to 0x0146 -> "i.MX8M Plus ROM, режим SDPS"
        0x1fc9 to 0x0151, 0x0525 to 0xb4a4 -> "SPL (SDPV)"
        0x1fc9 to 0x0152, 0x0525 to 0xa4a5, 0x18d1 to 0x0d02 -> "U-Boot fastboot"
        else -> "неизвестное устройство"
    }

    private fun isFastboot(intf: UsbInterface) =
        intf.interfaceClass == 0xff && intf.interfaceSubclass == 0x42 && intf.interfaceProtocol == 0x03

    private fun className(intf: UsbInterface) = when {
        intf.interfaceClass == UsbConstants.USB_CLASS_HID -> "(HID)"
        isFastboot(intf) -> "(fastboot)"
        intf.interfaceClass == UsbConstants.USB_CLASS_VENDOR_SPEC -> "(vendor)"
        else -> ""
    }

    private fun epType(ep: UsbEndpoint) = when (ep.type) {
        UsbConstants.USB_ENDPOINT_XFER_CONTROL -> "CONTROL"
        UsbConstants.USB_ENDPOINT_XFER_ISOC -> "ISOC"
        UsbConstants.USB_ENDPOINT_XFER_BULK -> "BULK"
        UsbConstants.USB_ENDPOINT_XFER_INT -> "INT"
        else -> "?"
    }

    /** Report ID из короткого элемента HID 0x85 xx (Global, tag 8). */
    private fun reportIds(b: ByteArray, n: Int): List<Int> {
        val ids = ArrayList<Int>()
        var i = 0
        while (i < n) {
            val prefix = b[i].toInt() and 0xff
            if (prefix == 0xfe) { // long item
                if (i + 1 >= n) break
                i += 3 + (b[i + 1].toInt() and 0xff)
                continue
            }
            val size = when (prefix and 0x03) { 3 -> 4; else -> prefix and 0x03 }
            if ((prefix and 0xfc) == 0x84 && size >= 1 && i + 1 < n) ids.add(b[i + 1].toInt() and 0xff)
            i += 1 + size
        }
        return ids
    }

    private fun Intent.usbDevice(): UsbDevice? =
        if (Build.VERSION.SDK_INT >= 33) getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
        else @Suppress("DEPRECATION") getParcelableExtra(UsbManager.EXTRA_DEVICE)

    private fun hotplugFilter() = IntentFilter().apply {
        addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
        addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
    }

    companion object {
        private const val TAG = "UuuProbe"
        private const val ACTION_PERMISSION = "com.smartheavy.uuuprobe.USB_PERMISSION"
        /** Те же VID, что в res/xml/device_filter.xml. Остальное (хабы, картридеры) пропускаем. */
        private val OUR_VIDS = setOf(0x1fc9, 0x15a2, 0x0525, 0x18d1, 0x3016)
        private fun hex2(v: Int) = "%02x".format(v)
        private fun hex4(v: Int) = "%04x".format(v)
        private fun hex(b: ByteArray, n: Int) = (0 until n).joinToString(" ") { "%02x".format(b[it]) }
    }
}

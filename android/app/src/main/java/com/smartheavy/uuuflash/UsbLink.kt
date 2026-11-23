package com.smartheavy.uuuflash

import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbEndpoint
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.Handler
import android.os.Looper

/**
 * Доступ к платам через UsbManager и передача их в libuuu.
 *
 * Всё, что проверено пробой (этап 0): разрешение приходит через intent-filter
 * сразу при подключении; если нет — ждём 3 с и спрашиваем явно; драйвер ядра
 * (usbhid у ROM) снимаем claimInterface(force = true); openDevice сразу после
 * подключения иногда возвращает null — повторяем.
 *
 * Все методы — из главного потока.
 */
class UsbLink(private val ctx: Context, private val log: (String) -> Unit) {

    private val usb = ctx.getSystemService(UsbManager::class.java)
    private val ui = Handler(Looper.getMainLooper())

    private class Opened(val dev: UsbDevice, val conn: UsbDeviceConnection, val fd: Int)

    private val opened = HashMap<String, Opened>()
    /** Отключены, но libuuu ещё держал handle: закрываем после run(). */
    private val zombies = ArrayList<UsbDeviceConnection>()
    private val asked = HashSet<String>()

    fun scan() = usb.deviceList.values.forEach { onAttached(it, "scan") }

    fun onAttached(dev: UsbDevice, via: String) {
        val name = dev.deviceName
        if (dev.vendorId !in OUR_VIDS || name in opened) return
        if (usb.hasPermission(dev)) {
            open(dev, via, attempt = 0)
        } else if (asked.add(name)) {
            log("${tag(dev)} нет разрешения ($via), жду 3 с")
            ui.postDelayed({ askIfStillNeeded(dev) }, 3000)
        }
    }

    fun onDetached(dev: UsbDevice) {
        asked.remove(dev.deviceName)
        val o = opened.remove(dev.deviceName) ?: return
        val released = UuuNative.removeDevice(o.fd)
        log("${tag(dev)} отключено" + if (released) "" else " (libuuu ещё держит, закрою после)")
        if (released) o.conn.close() else zombies += o.conn
    }

    fun closeZombies() {
        zombies.forEach { it.close() }
        zombies.clear()
    }

    /** Открытое fastboot-устройство U-Boot, если есть. */
    fun fastboot(): Pair<UsbDevice, UsbDeviceConnection>? =
        opened.values.firstOrNull { isFastboot(it.dev) }?.let { it.dev to it.conn }

    private fun askIfStillNeeded(dev: UsbDevice) {
        if (dev.deviceName in opened || dev.deviceName !in asked) return
        if (usb.hasPermission(dev)) return open(dev, "после ожидания", 0)
        log("${tag(dev)} спрашиваю разрешение")
        val flags = if (Build.VERSION.SDK_INT >= 31) PendingIntent.FLAG_MUTABLE else 0
        val pi = PendingIntent.getBroadcast(
            ctx, dev.deviceId, Intent(ACTION_PERMISSION).setPackage(ctx.packageName), flags
        )
        usb.requestPermission(dev, pi)
    }

    fun onPermission(dev: UsbDevice, granted: Boolean) {
        log("${tag(dev)} разрешение: ${if (granted) "дано" else "ОТКАЗАНО"}")
        if (granted) open(dev, "диалог", 0)
    }

    private fun open(dev: UsbDevice, via: String, attempt: Int) {
        if (dev.deviceName in opened) return
        val conn = usb.openDevice(dev)
        if (conn == null) {
            if (attempt < 5) ui.postDelayed({ open(dev, via, attempt + 1) }, 150)
            else log("${tag(dev)} openDevice не удался после 6 попыток")
            return
        }
        // Захват с отключением драйвера ядра. Интерфейс не отпускаем: libusb
        // работает через тот же fd, и повторный claim на нём проходит.
        val intf = dev.getInterface(0)
        if (!conn.claimInterface(intf, true)) {
            log("${tag(dev)} claimInterface не удался")
            conn.close()
            return
        }
        val fd = conn.fileDescriptor
        val r = UuuNative.addDevice(fd)
        if (r != 0) {
            log("${tag(dev)} libuuu не принял устройство: $r")
            conn.close()
            return
        }
        opened[dev.deviceName] = Opened(dev, conn, fd)
        log("${tag(dev)} ${known(dev)} подключено к libuuu ($via${if (attempt > 0) ", попытка ${attempt + 1}" else ""})")
    }

    companion object {
        const val ACTION_PERMISSION = "com.smartheavy.uuuflash.USB_PERMISSION"

        /** Те же VID, что в res/xml/device_filter.xml. */
        val OUR_VIDS = setOf(0x1fc9, 0x15a2, 0x0525, 0x18d1, 0x3016)

        fun tag(dev: UsbDevice) = "[%04x:%04x]".format(dev.vendorId, dev.productId)

        fun known(dev: UsbDevice) = when {
            dev.vendorId == 0x1fc9 && dev.productId == 0x0146 -> "ROM i.MX8M Plus"
            isFastboot(dev) -> "U-Boot fastboot"
            else -> ""
        }

        fun isFastboot(dev: UsbDevice): Boolean {
            if (dev.interfaceCount == 0) return false
            val i = dev.getInterface(0)
            return i.interfaceClass == 0xff && i.interfaceSubclass == 0x42 && i.interfaceProtocol == 0x03
        }

        /**
         * UID платы из ROM i.MX8M Plus: у него нет iSerialNumber, ID лежит в
         * строке дескриптора №4 (rominfo.cpp: MX815 serial_idx = 4). Тот же
         * ID libuuu показывает в «устройство 1:144-<UID>».
         */
        fun romUid(conn: UsbDeviceConnection): String? {
            val buf = ByteArray(255)
            val n = conn.controlTransfer(0x80, 0x06, 0x0304, 0x0409, buf, buf.size, 1000)
            if (n < 2 || buf[1].toInt() != 0x03) return null
            return String(buf, 2, minOf(n, buf[0].toInt() and 0xff) - 2, Charsets.UTF_16LE).trim().uppercase()
        }

        /** fastboot getvar напрямую, без libuuu — только чтение. */
        fun getvar(dev: UsbDevice, conn: UsbDeviceConnection, name: String) = fastboot(dev, conn, "getvar:$name")

        /**
         * Команда fastboot напрямую, без libuuu: getvar, reboot. Ответ —
         * строка от U-Boot (OKAY…/FAIL…), INFO-строки склеиваются в начало.
         */
        fun fastboot(dev: UsbDevice, conn: UsbDeviceConnection, command: String): String {
            val intf = dev.getInterface(0)
            var out: UsbEndpoint? = null
            var inp: UsbEndpoint? = null
            for (e in 0 until intf.endpointCount) {
                val ep = intf.getEndpoint(e)
                if (ep.type != UsbConstants.USB_ENDPOINT_XFER_BULK) continue
                if (ep.direction == UsbConstants.USB_DIR_OUT) out = ep else inp = ep
            }
            if (out == null || inp == null) return "нет bulk-endpoint'ов"
            val cmd = command.toByteArray()
            if (conn.bulkTransfer(out, cmd, cmd.size, 1000) < 0) return "ошибка записи"
            val buf = ByteArray(256)
            val sb = StringBuilder()
            for (i in 0 until 8) {
                val n = conn.bulkTransfer(inp, buf, buf.size, 1000)
                if (n < 0) return "$sb [таймаут]"
                val s = String(buf, 0, n)
                if (s.startsWith("INFO")) { sb.append(s.removePrefix("INFO")).append(' '); continue }
                return sb.append(s).toString()
            }
            return sb.toString()
        }
    }
}

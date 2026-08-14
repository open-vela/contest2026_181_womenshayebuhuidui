package com.agent.coapp.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothProfile
import android.content.Context
import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import java.util.UUID

/**
 * BLE GATT tunnel manager (NUS data channel).
 * Frame protocol matches the device: 2-byte big-endian length prefix.
 */
@SuppressLint("MissingPermission")
class BleTunnelManager(private val context: Context) {

    companion object {
        private const val TAG = "BleTunnelManager"

        private val SERVICE_UUID: UUID =
            UUID.fromString("6e400001-b5a3-f393-e0a9-e50e24dcca9e")
        private val CHAR_RX: UUID =
            UUID.fromString("6e400002-b5a3-f393-e0a9-e50e24dcca9e")
        private val CHAR_TX: UUID =
            UUID.fromString("6e400003-b5a3-f393-e0a9-e50e24dcca9e")
        private val CCCD_UUID: UUID =
            UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

        /** 帧协议：大端 16 位长度前缀 */
        private const val FRAME_HEADER_SIZE = 2
        private const val MAX_FRAME_PAYLOAD = 1518
        private const val RX_BUFFER_SIZE = 4096
    }

    enum class TunnelState { DISCONNECTED, CONNECTING, CONNECTED, ERROR }

    /** 设备 → App：解帧后的完整 IP 包回调（在 GATT 回调线程） */
    var onFrameReceived: ((ByteArray) -> Unit)? = null
    var onStateChanged: ((TunnelState, String?) -> Unit)? = null

    private val _state = MutableStateFlow(TunnelState.DISCONNECTED)
    val state: StateFlow<TunnelState> = _state

    private val _statusMessage = MutableStateFlow("")
    val statusMessage: StateFlow<String> = _statusMessage

    private var gatt: BluetoothGatt? = null
    private var txChar: BluetoothGattCharacteristic? = null
    private var rxChar: BluetoothGattCharacteristic? = null

    /** 接收重组缓冲 */
    private val rxBuf = ByteArray(RX_BUFFER_SIZE)
    private var rxLen = 0

    /** 协商后的 ATT MTU（默认 23；requestMtu 后通常为 247） */
    private var attMtu = 23

    /** 写入串行化 */
    private val writeLock = Any()

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                /* Connection attempt failed/timed out (e.g. status=133
                 * after ~30s): go to ERROR so the service cleans up
                 * instead of hanging in CONNECTING. */
                Log.w(TAG, "connection state change with status=$status newState=$newState")
                fail("连接失败: $status")
                return
            }
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    _statusMessage.value = "GATT 已连接，发现服务..."
                    gatt.discoverServices()
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    Log.i(TAG, "disconnected")
                    resetToDisconnected()
                }
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                fail("服务发现失败: $status")
                return
            }
            val service = gatt.getService(SERVICE_UUID) ?: run {
                fail("未找到 NUS 服务")
                return
            }
            rxChar = service.getCharacteristic(CHAR_RX)
            txChar = service.getCharacteristic(CHAR_TX)
            if (rxChar == null || txChar == null) {
                fail("未找到 NUS 特征")
                return
            }

            // 协商更大的 ATT MTU（247）：单次写入/通知的载荷从 20 字节
            // 提升到 244 字节，蓝牙代理吞吐量提升约 12 倍。
            // 设备端 BLE 侧会按 (MTU-3) 分片，两端必须都按协商值分片。
            runCatching { gatt.requestMtu(247) }

            // 使能 TX 通知
            val enabled = gatt.setCharacteristicNotification(txChar, true)
            if (enabled) {
                val cccd = txChar?.getDescriptor(CCCD_UUID)
                if (cccd != null) {
                    cccd.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                    gatt.writeDescriptor(cccd)
                } else {
                    _state.value = TunnelState.CONNECTED
                    _statusMessage.value = "已连接（无 CCCD）"
                    onStateChanged?.invoke(TunnelState.CONNECTED, null)
                }
            } else {
                fail("通知使能失败")
            }
        }

        override fun onDescriptorWrite(
            gatt: BluetoothGatt,
            descriptor: BluetoothGattDescriptor,
            status: Int
        ) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                _state.value = TunnelState.CONNECTED
                _statusMessage.value = "隧道已建立"
                onStateChanged?.invoke(TunnelState.CONNECTED, null)
            } else {
                fail("CCCD 写入失败: $status")
            }
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            val data = characteristic.value ?: return
            handleRxStream(data)
        }

        override fun onCharacteristicWrite(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int
        ) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                Log.w(TAG, "write failed: $status")
            }
        }

        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                attMtu = mtu
                Log.i(TAG, "MTU negotiated: $mtu")
                _statusMessage.value = "隧道已建立（MTU=$mtu）"
            } else {
                Log.w(TAG, "MTU request failed: $status, staying at $attMtu")
            }
        }
    }

    /** 连接设备（BLE GATT） */
    fun connect(device: BluetoothDevice) {
        _state.value = TunnelState.CONNECTING
        _statusMessage.value = "正在连接 ${device.name ?: device.address} ..."
        rxLen = 0
        gatt = device.connectGatt(context, false, gattCallback)
    }

    /**
     * 发送一帧（帧封装 + 按 MTU 分片写 RX）。
     */
    fun sendFrame(payload: ByteArray): Boolean {
        val gatt = gatt ?: return false
        val rx = rxChar ?: return false
        if (payload.isEmpty() || payload.size > MAX_FRAME_PAYLOAD) return false

        val frame = ByteArray(FRAME_HEADER_SIZE + payload.size)
        frame[0] = ((payload.size shr 8) and 0xff).toByte()
        frame[1] = (payload.size and 0xff).toByte()
        System.arraycopy(payload, 0, frame, FRAME_HEADER_SIZE, payload.size)

        // 分片：BLE 单次写入受 MTU 限制，按协商后的 (MTU-3) 分片
        // （requestMtu 成功后一般为 244 字节；失败回退 20 字节）
        synchronized(writeLock) {
            val chunkLimit = maxOf(20, attMtu - 3)
            var offset = 0
            while (offset < frame.size) {
                val chunkLen = minOf(chunkLimit, frame.size - offset)
                val chunk = frame.copyOfRange(offset, offset + chunkLen)
                rx.value = chunk
                if (!gatt.writeCharacteristic(rx)) {
                    Log.w(TAG, "writeCharacteristic failed at offset $offset")
                    return false
                }
                offset += chunkLen
            }
        }
        return true
    }

    /** 断开连接 */
    fun disconnect() {
        runCatching { gatt?.disconnect() }
        runCatching { gatt?.close() }
        gatt = null
        rxChar = null
        txChar = null
        rxLen = 0
        _state.value = TunnelState.DISCONNECTED
        _statusMessage.value = "已断开"
    }

    private fun handleRxStream(data: ByteArray) {
        if (rxLen + data.size > rxBuf.size) {
            Log.w(TAG, "rx buffer overflow, resync")
            rxLen = 0
            return
        }
        System.arraycopy(data, 0, rxBuf, rxLen, data.size)
        rxLen += data.size

        var off = 0
        while (rxLen - off >= FRAME_HEADER_SIZE) {
            val plen = ((rxBuf[off].toInt() and 0xff) shl 8) or
                (rxBuf[off + 1].toInt() and 0xff)
            if (plen == 0 || plen > MAX_FRAME_PAYLOAD) {
                Log.w(TAG, "invalid frame length $plen, resync")
                rxLen = 0
                return
            }
            if (rxLen - off < FRAME_HEADER_SIZE + plen) break
            val payload = rxBuf.copyOfRange(off + FRAME_HEADER_SIZE, off + FRAME_HEADER_SIZE + plen)
            onFrameReceived?.invoke(payload)
            off += FRAME_HEADER_SIZE + plen
        }
        if (off > 0) {
            System.arraycopy(rxBuf, off, rxBuf, 0, rxLen - off)
            rxLen -= off
        }
    }

    private fun fail(message: String) {
        _statusMessage.value = message
        _state.value = TunnelState.ERROR
        onStateChanged?.invoke(TunnelState.ERROR, message)
        disconnect()
    }

    private fun resetToDisconnected() {
        gatt?.close()
        gatt = null
        rxChar = null
        txChar = null
        rxLen = 0
        _state.value = TunnelState.DISCONNECTED
        _statusMessage.value = "连接已断开"
        onStateChanged?.invoke(TunnelState.DISCONNECTED, null)
    }
}

package com.lolren.sensevoiceimu

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.View
import android.widget.ProgressBar
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.google.android.material.button.MaterialButton
import java.util.Locale

class MainActivity : AppCompatActivity() {
    private lateinit var bluetoothManager: BluetoothManager
    private lateinit var adapter: BluetoothAdapter
    private lateinit var connectButton: MaterialButton
    private lateinit var audioButton: MaterialButton
    private lateinit var deviceStatus: TextView
    private lateinit var audioStatus: TextView
    private lateinit var audioLevel: ProgressBar
    private lateinit var imuValues: TextView
    private lateinit var imuPlot: ImuPlotView

    private val handler = Handler(Looper.getMainLooper())
    private val audioEngine = AudioEngine()
    @Volatile private var gatt: BluetoothGatt? = null
    @Volatile private var listenMode = false
    @Volatile private var scanActive = false
    @Volatile private var connecting = false
    @Volatile private var subscriptionsReady = false
    private var subscriptionIndex = 0
    private var audioEnabled = true
    private var transaction = 0
    private var receivedAudioFrames = 0L
    private var receivedImuFrames = 0L
    private var lastUiAudioUpdate = 0L
    @Volatile private var pendingGattOperation = "GATT operation"

    private val retryScan = Runnable {
        if (listenMode && gatt == null && !connecting && !isFinishing && !isDestroyed) {
            startListening()
        }
    }

    private val gattTimeout = Runnable {
        val currentGatt = gatt ?: return@Runnable
        failGatt(currentGatt, "$pendingGattOperation timed out")
    }

    private val requiredPermissions: Array<String>
        get() = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        } else {
            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }

    private val permissionRequest =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { result ->
            if (result.values.all { it }) startListening() else showStatus("Bluetooth permission denied")
        }

    private val enableBluetooth =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            if (result.resultCode == Activity.RESULT_OK || adapter.isEnabled) startListening()
            else showStatus("Bluetooth is off")
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        bluetoothManager = getSystemService(BluetoothManager::class.java)
        adapter = bluetoothManager.adapter
        connectButton = findViewById(R.id.connectButton)
        audioButton = findViewById(R.id.audioButton)
        deviceStatus = findViewById(R.id.deviceStatus)
        audioStatus = findViewById(R.id.audioStatus)
        audioLevel = findViewById(R.id.audioLevel)
        imuValues = findViewById(R.id.imuValues)
        imuPlot = findViewById(R.id.imuPlot)

        connectButton.setOnClickListener {
            when {
                gatt != null -> stopListening()
                listenMode -> stopListening()
                else -> ensurePermissionsAndListen()
            }
        }
        audioButton.setOnClickListener {
            audioEnabled = !audioEnabled
            audioEngine.setMuted(!audioEnabled)
            audioButton.text = getString(if (audioEnabled) R.string.mute else R.string.listen)
            writeStreamControl()
        }
    }

    override fun onDestroy() {
        listenMode = false
        handler.removeCallbacks(retryScan)
        clearGattTimeout()
        stopScan()
        closeGatt()
        audioEngine.close()
        super.onDestroy()
    }

    private fun ensurePermissionsAndListen() {
        val missing = requiredPermissions.filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
        if (missing.isNotEmpty()) {
            permissionRequest.launch(missing.toTypedArray())
        } else if (!adapter.isEnabled) {
            enableBluetooth.launch(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
        } else {
            startListening()
        }
    }

    @SuppressLint("MissingPermission")
    private fun startListening() {
        if (!adapter.isEnabled || scanActive || gatt != null || connecting ||
            isFinishing || isDestroyed
        ) return
        listenMode = true
        handler.removeCallbacks(retryScan)
        scanActive = true
        connectButton.text = getString(R.string.stop)
        showStatus("Listening for the next board wake...")
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .setReportDelay(0L)
            .build()
        adapter.bluetoothLeScanner.startScan(null, settings, scanCallback)
        handler.removeCallbacks(scanTimeout)
        handler.postDelayed(scanTimeout, 12_000L)
    }

    private val scanTimeout = Runnable {
        stopScan()
        if (listenMode && gatt == null && !connecting) {
            scheduleScanRetry(800L)
        }
    }

    private fun scheduleScanRetry(delayMs: Long) {
        handler.removeCallbacks(retryScan)
        if (listenMode && !isFinishing && !isDestroyed) {
            handler.postDelayed(retryScan, delayMs)
        }
    }

    @SuppressLint("MissingPermission")
    private fun stopScan() {
        handler.removeCallbacks(scanTimeout)
        if (scanActive) {
            adapter.bluetoothLeScanner?.stopScan(scanCallback)
            scanActive = false
        }
    }

    @SuppressLint("MissingPermission")
    private fun stopListening() {
        listenMode = false
        connecting = false
        handler.removeCallbacks(retryScan)
        clearGattTimeout()
        stopScan()
        gatt?.disconnect()
        closeGatt()
        subscriptionsReady = false
        audioButton.isEnabled = false
        audioEngine.setMuted(true)
        connectButton.text = getString(R.string.connect)
        showStatus(getString(R.string.status_idle))
        audioStatus.text = getString(R.string.audio_waiting)
        audioLevel.progress = 0
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            if (!listenMode || !scanActive || isFinishing || isDestroyed) return
            val uuids = result.scanRecord?.serviceUuids.orEmpty()
            val advertised = uuids.any { it.uuid == StreamProtocol.SERVICE }
            val named = result.scanRecord?.deviceName?.startsWith("XIAO Sense Voice") == true
            if ((advertised || named) && !connecting && gatt == null) {
                connectTo(result.device)
            }
        }

        override fun onScanFailed(errorCode: Int) {
            if (!listenMode || !scanActive || isFinishing || isDestroyed) return
            scanActive = false
            showStatus("Bluetooth scan error $errorCode")
            if (listenMode) scheduleScanRetry(1500L)
        }
    }

    @SuppressLint("MissingPermission")
    private fun connectTo(device: BluetoothDevice) {
        if (!listenMode || !scanActive || isFinishing || isDestroyed) return
        connecting = true
        stopScan()
        showStatus("Board found - connecting")
        gatt = device.connectGatt(this, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
        if (gatt == null) {
            connecting = false
            showStatus("Connection could not start")
            scheduleScanRetry(800L)
        } else {
            armGattTimeout("Connection", 10_000L)
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (gatt !== g) {
                g.close()
                return
            }
            if (status == BluetoothGatt.GATT_SUCCESS && newState == BluetoothProfile.STATE_CONNECTED) {
                clearGattTimeout()
                connecting = false
                runOnUiThread {
                    connectButton.text = getString(R.string.disconnect)
                    showStatus("Connected - discovering services")
                }
                armGattTimeout("Service discovery")
                if (!g.discoverServices()) {
                    failGatt(g, "Service discovery could not start")
                }
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED || status != BluetoothGatt.GATT_SUCCESS) {
                clearGattTimeout()
                connecting = false
                subscriptionsReady = false
                audioEngine.setMuted(true)
                g.close()
                if (gatt === g) gatt = null
                runOnUiThread {
                    audioButton.isEnabled = false
                    showStatus(if (listenMode) "Board sleeping - listening for next wake" else "Disconnected")
                    connectButton.text = if (listenMode) getString(R.string.stop) else getString(R.string.connect)
                }
                if (listenMode) scheduleScanRetry(800L)
            }
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            if (gatt !== g) return
            clearGattTimeout()
            val service = g.getService(StreamProtocol.SERVICE)
            if (status != BluetoothGatt.GATT_SUCCESS || service == null ||
                service.getCharacteristic(StreamProtocol.CONTROL) == null ||
                service.getCharacteristic(StreamProtocol.AUDIO) == null ||
                service.getCharacteristic(StreamProtocol.IMU) == null ||
                service.getCharacteristic(StreamProtocol.STATUS) == null
            ) {
                failGatt(g, "Voice + IMU service not found")
                return
            }
            showStatus("Connected - negotiating stream")
            g.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH)
            armGattTimeout("MTU negotiation")
            if (!g.requestMtu(247)) {
                failGatt(g, "MTU negotiation could not start")
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            if (gatt !== g) return
            clearGattTimeout()
            if (status != BluetoothGatt.GATT_SUCCESS ||
                mtu < StreamProtocol.AUDIO_PACKET_BYTES + 3
            ) {
                failGatt(g, "MTU $mtu is too small for audio")
                return
            }
            beginSubscriptions(g)
        }

        override fun onDescriptorWrite(g: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) {
            if (gatt !== g) return
            clearGattTimeout()
            if (status != BluetoothGatt.GATT_SUCCESS) {
                failGatt(g, "Notification setup failed ($status)")
                return
            }
            subscriptionIndex++
            enableNextSubscription(g)
        }

        override fun onCharacteristicWrite(g: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
            if (gatt !== g || characteristic.uuid != StreamProtocol.CONTROL) return
            clearGattTimeout()
            if (status == BluetoothGatt.GATT_SUCCESS) {
                runOnUiThread {
                    audioButton.isEnabled = true
                    showStatus(
                        if (audioEnabled) "Streaming - 16 kHz voice + 10 Hz IMU"
                        else "Streaming - 10 Hz IMU"
                    )
                }
            } else {
                failGatt(g, "Stream request failed ($status)")
            }
        }

        @Deprecated("Used below Android 13")
        @Suppress("DEPRECATION")
        override fun onCharacteristicChanged(g: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            if (gatt !== g) return
            handleNotification(characteristic.uuid, characteristic.value ?: return)
        }

        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
        ) {
            if (gatt !== g) return
            handleNotification(characteristic.uuid, value)
        }
    }

    private fun beginSubscriptions(g: BluetoothGatt) {
        if (gatt !== g) return
        subscriptionIndex = 0
        enableNextSubscription(g)
    }

    @SuppressLint("MissingPermission")
    private fun enableNextSubscription(g: BluetoothGatt) {
        val service = g.getService(StreamProtocol.SERVICE)
        if (service == null) {
            failGatt(g, "Voice + IMU service disappeared")
            return
        }
        val uuid = listOf(StreamProtocol.STATUS, StreamProtocol.AUDIO, StreamProtocol.IMU)
            .getOrNull(subscriptionIndex)
        if (uuid == null) {
            subscriptionsReady = true
            audioEngine.setMuted(!audioEnabled)
            writeStreamControl()
            return
        }
        val characteristic = service.getCharacteristic(uuid)
        val descriptor = characteristic?.getDescriptor(StreamProtocol.CCCD)
        if (characteristic == null || descriptor == null ||
            !g.setCharacteristicNotification(characteristic, true)
        ) {
            failGatt(g, "Cannot enable ${uuid.toString().take(8)} notifications")
            return
        }
        armGattTimeout("Notification setup")
        if (!queueDescriptorWrite(g, descriptor)) {
            failGatt(g, "Notification setup could not start")
        }
    }

    @SuppressLint("MissingPermission")
    private fun writeStreamControl() {
        val currentGatt = gatt ?: return
        if (!subscriptionsReady) return
        val control = currentGatt.getService(StreamProtocol.SERVICE)
            ?.getCharacteristic(StreamProtocol.CONTROL)
        if (control == null) {
            failGatt(currentGatt, "Stream control characteristic disappeared")
            return
        }
        transaction = (transaction + 1) and 0xffff
        val mask = StreamProtocol.STREAM_IMU or
            if (audioEnabled) StreamProtocol.STREAM_AUDIO else 0
        val value = StreamProtocol.control(mask, transaction)
        runOnUiThread { audioButton.isEnabled = false }
        armGattTimeout("Stream request")
        if (!queueCharacteristicWrite(currentGatt, control, value)) {
            failGatt(currentGatt, "Stream request could not start")
        }
    }

    private fun handleNotification(uuid: java.util.UUID, value: ByteArray) {
        when (uuid) {
            StreamProtocol.AUDIO -> StreamProtocol.decodeAudio(value)?.let { frame ->
                receivedAudioFrames++
                if (audioEnabled) audioEngine.offer(frame)
                val now = System.currentTimeMillis()
                if (now - lastUiAudioUpdate >= 100L) {
                    lastUiAudioUpdate = now
                    runOnUiThread {
                        audioLevel.progress = frame.levelPercent
                        audioStatus.text = String.format(
                            Locale.US,
                            "16 kHz IMA-ADPCM - %,d frames - sample %,d",
                            receivedAudioFrames,
                            frame.sampleCounter,
                        )
                    }
                }
            }
            StreamProtocol.IMU -> StreamProtocol.decodeImu(value)?.let { frame ->
                receivedImuFrames++
                imuPlot.add(frame)
                runOnUiThread {
                    imuValues.text = String.format(
                        Locale.US,
                        "A  %+1.3f  %+1.3f  %+1.3f g\nG  %+6.1f  %+6.1f  %+6.1f deg/s",
                        frame.ax, frame.ay, frame.az, frame.gx, frame.gy, frame.gz,
                    )
                }
            }
            StreamProtocol.STATUS -> StreamProtocol.decodeStatus(value)?.let { status ->
                if (status.error != 0) showStatus("Board error ${status.error}")
                runOnUiThread {
                    if (status.audioDrops != 0 || status.imuDrops != 0) {
                        audioStatus.text = getString(
                            R.string.board_drops,
                            status.audioDrops,
                            status.imuDrops,
                        )
                    }
                }
            }
        }
    }

    @SuppressLint("MissingPermission")
    private fun closeGatt() {
        clearGattTimeout()
        gatt?.close()
        gatt = null
    }

    private fun armGattTimeout(operation: String, timeoutMs: Long = 6_000L) {
        pendingGattOperation = operation
        handler.removeCallbacks(gattTimeout)
        handler.postDelayed(gattTimeout, timeoutMs)
    }

    private fun clearGattTimeout() {
        handler.removeCallbacks(gattTimeout)
    }

    @SuppressLint("MissingPermission")
    @Suppress("DEPRECATION")
    private fun queueDescriptorWrite(
        currentGatt: BluetoothGatt,
        descriptor: BluetoothGattDescriptor,
    ): Boolean = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
        currentGatt.writeDescriptor(
            descriptor,
            BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE,
        ) == BluetoothStatusCodes.SUCCESS
    } else {
        descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
        currentGatt.writeDescriptor(descriptor)
    }

    @SuppressLint("MissingPermission")
    @Suppress("DEPRECATION")
    private fun queueCharacteristicWrite(
        currentGatt: BluetoothGatt,
        characteristic: BluetoothGattCharacteristic,
        value: ByteArray,
    ): Boolean = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
        currentGatt.writeCharacteristic(
            characteristic,
            value,
            BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT,
        ) == BluetoothStatusCodes.SUCCESS
    } else {
        characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        characteristic.value = value
        currentGatt.writeCharacteristic(characteristic)
    }

    @SuppressLint("MissingPermission")
    private fun failGatt(failedGatt: BluetoothGatt, reason: String) {
        if (gatt !== failedGatt) return
        clearGattTimeout()
        subscriptionsReady = false
        connecting = false
        audioEngine.setMuted(true)
        gatt = null
        failedGatt.disconnect()
        failedGatt.close()
        runOnUiThread {
            audioButton.isEnabled = false
            audioStatus.text = getString(R.string.audio_waiting)
            showStatus(if (listenMode) "$reason - retrying" else reason)
            connectButton.text = if (listenMode) getString(R.string.stop) else getString(R.string.connect)
        }
        if (listenMode) scheduleScanRetry(800L)
    }

    private fun showStatus(text: String) {
        if (Looper.myLooper() == Looper.getMainLooper()) {
            deviceStatus.text = text
        } else {
            runOnUiThread { deviceStatus.text = text }
        }
    }
}

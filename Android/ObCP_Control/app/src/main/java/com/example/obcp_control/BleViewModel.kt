package com.example.obcp_control

import android.app.Application
import android.bluetooth.le.ScanResult
import androidx.compose.runtime.mutableStateListOf
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.github.mikephil.charting.data.Entry
import kotlinx.coroutines.Job
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeout
import kotlin.math.pow

class BleViewModel(application: Application) : AndroidViewModel(application) {
    val bleManager = BleManager(application)

    // State
    val connectionState = bleManager.connectionState
    val lastResponse = bleManager.lastResponse

    private val _discoveredDevices = MutableStateFlow<List<ScanResult>>(emptyList())
    val discoveredDevices = _discoveredDevices.asStateFlow()

    private var sliderJob: Job? = null

    val accX = mutableStateListOf<Entry>()
    val accY = mutableStateListOf<Entry>()
    val accZ = mutableStateListOf<Entry>()
    val temp = mutableStateListOf<Entry>()

    val redPercent = MutableStateFlow(0f)
    val greenPercent = MutableStateFlow(0f)
    val bluePercent = MutableStateFlow(0f)
    val pwmValue = MutableStateFlow(0f)
    val timerOn = MutableStateFlow(false)

    init {
        bleManager.onDeviceDiscovered = { result ->
            if (_discoveredDevices.value.none { it.device.address == result.device.address }) {
                _discoveredDevices.value = _discoveredDevices.value + result
            }
        }
        bleManager.onScanStarted = {
            _discoveredDevices.value = emptyList()
        }
        bleManager.onDataReceived = { data ->
            parseData(data)
        }
    }

    private fun parseData(data: String) {
        when {
            data.startsWith("X") -> {
                val parts = data.trim().split("Y", "Z")
                val x = parts.getOrNull(0)?.substring(1)?.toFloatOrNull()
                val y = parts.getOrNull(1)?.toFloatOrNull()
                val z = parts.getOrNull(2)?.toFloatOrNull()
                if (x != null && y != null && z != null) {
                    accX.add(Entry(accX.size.toFloat(), x))
                    accY.add(Entry(accY.size.toFloat(), y))
                    accZ.add(Entry(accZ.size.toFloat(), z))
                }
            }
            data.startsWith("T = ") -> {
                val tempValue = data.trim().substring(4).toFloatOrNull()
                if (tempValue != null) {
                    temp.add(Entry(temp.size.toFloat(), tempValue))
                }
            }
        }
    }

    fun sendCommandWithRetry(command: String, expectedResponse: String) {
        sliderJob?.cancel()
        sliderJob = viewModelScope.launch {
            delay(100) // Debounce
            for (attempt in 1..5) {
                bleManager.sendCommand(command)
                try {
                    withTimeout(100) { // 100ms timeout for a response
                        bleManager.lastResponse.first { it.trim() == expectedResponse }
                    }
                    break // Exit retry loop
                } catch (e: TimeoutCancellationException) {
                   // Command failed, will retry
                }
            }
        }
    }

    fun sendCommand(command: String) {
        bleManager.sendCommand(command)
    }

    fun toggleTimer() {
        timerOn.value = !timerOn.value
        sendCommand(if (timerOn.value) "start timer" else "stop timer")
    }

    fun resetControls() {
        sendCommand("#rst")
        if (timerOn.value) {
            sendCommand("stop timer")
        }
        timerOn.value = false
        redPercent.value = 0f
        greenPercent.value = 0f
        bluePercent.value = 0f
        pwmValue.value = 0f
    }

    fun clearGraphs() {
        accX.clear()
        accY.clear()
        accZ.clear()
        temp.clear()
    }
}
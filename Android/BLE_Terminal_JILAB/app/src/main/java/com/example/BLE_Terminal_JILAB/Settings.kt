package com.example.BLE_Terminal_JILAB

import android.content.Context
import android.content.SharedPreferences
import android.os.Environment
import androidx.core.content.edit
import java.io.File

// UUIDs par défaut pour le service Nordic UART
const val DEFAULT_UART_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
const val DEFAULT_TX_CHARACTERISTIC_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
const val DEFAULT_RX_CHARACTERISTIC_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

data class BleSettings(
    val uartServiceUuid: String,
    val txCharacteristicUuid: String,
    val rxCharacteristicUuid: String,
    val logToFileEnabled: Boolean,
    val logFilePath: String,
    val displayAsHex: Boolean,
    val autoReconnectEnabled: Boolean,
    var lastConnectedDeviceAddress: String? // Adresse du dernier appareil
)

class SettingsPersistence(private val context: Context) {
    private val prefs: SharedPreferences = context.getSharedPreferences("settings_prefs", Context.MODE_PRIVATE)

    private fun getDefaultLogPath(): String {
        val dir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOCUMENTS)
        val logDir = File(dir, "BLE_Terminal_AppLogs")
        if (!logDir.exists()) {
            logDir.mkdirs()
        }
        return logDir.absolutePath
    }

    fun save(settings: BleSettings) {
        prefs.edit {
            putString("uart_service_uuid", settings.uartServiceUuid)
            putString("tx_char_uuid", settings.txCharacteristicUuid)
            putString("rx_char_uuid", settings.rxCharacteristicUuid)
            putBoolean("log_to_file", settings.logToFileEnabled)
            putString("log_file_path", settings.logFilePath)
            putBoolean("display_as_hex", settings.displayAsHex)
            putBoolean("auto_reconnect", settings.autoReconnectEnabled)
            putString("last_device_address", settings.lastConnectedDeviceAddress) // On sauvegarde aussi l'adresse
        }
    }

    fun load(): BleSettings {
        val uart = prefs.getString("uart_service_uuid", DEFAULT_UART_SERVICE_UUID) ?: DEFAULT_UART_SERVICE_UUID
        val tx = prefs.getString("tx_char_uuid", DEFAULT_TX_CHARACTERISTIC_UUID) ?: DEFAULT_TX_CHARACTERISTIC_UUID
        val rx = prefs.getString("rx_char_uuid", DEFAULT_RX_CHARACTERISTIC_UUID) ?: DEFAULT_RX_CHARACTERISTIC_UUID
        val log = prefs.getBoolean("log_to_file", false)
        val logPath = prefs.getString("log_file_path", getDefaultLogPath()) ?: getDefaultLogPath()
        val displayHex = prefs.getBoolean("display_as_hex", false)
        val autoReconnect = prefs.getBoolean("auto_reconnect", false)
        val lastDeviceAddress = prefs.getString("last_device_address", null)
        return BleSettings(uart, tx, rx, log, logPath, displayHex, autoReconnect, lastDeviceAddress)
    }

    fun revertToDefault() {
        // On ne réinitialise pas le dernier appareil connecté
        val currentSettings = load()
        save(
            BleSettings(
                DEFAULT_UART_SERVICE_UUID, 
                DEFAULT_TX_CHARACTERISTIC_UUID, 
                DEFAULT_RX_CHARACTERISTIC_UUID, 
                false, 
                getDefaultLogPath(), 
                false, 
                false, 
                currentSettings.lastConnectedDeviceAddress // On préserve l'adresse
            )
        )
    }
}
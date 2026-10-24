package com.example.BLE_Terminal_JILAB

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(onNavigateBack: () -> Unit, macroPersistence: MacroPersistence) {
    val context = LocalContext.current
    val settingsPersistence = remember { SettingsPersistence(context) }
    var currentSettings by remember { mutableStateOf(settingsPersistence.load()) }
    var macroButtons by remember { mutableStateOf(macroPersistence.loadConfigs()) }

    var serviceUuid by remember { mutableStateOf(currentSettings.uartServiceUuid) }
    var txUuid by remember { mutableStateOf(currentSettings.txCharacteristicUuid) }
    var rxUuid by remember { mutableStateOf(currentSettings.rxCharacteristicUuid) }
    var logToFile by remember { mutableStateOf(currentSettings.logToFileEnabled) }
    var logFilePath by remember { mutableStateOf(currentSettings.logFilePath) }
    var displayAsHex by remember { mutableStateOf(currentSettings.displayAsHex) }
    var autoReconnect by remember { mutableStateOf(currentSettings.autoReconnectEnabled) }


    LaunchedEffect(currentSettings) {
        serviceUuid = currentSettings.uartServiceUuid
        txUuid = currentSettings.txCharacteristicUuid
        rxUuid = currentSettings.rxCharacteristicUuid
        logToFile = currentSettings.logToFileEnabled
        logFilePath = currentSettings.logFilePath
        displayAsHex = currentSettings.displayAsHex
        autoReconnect = currentSettings.autoReconnectEnabled
    }


    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Configuration") },
                navigationIcon = {
                    IconButton(onClick = onNavigateBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Retour")
                    }
                }
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .padding(padding)
                .padding(16.dp)
                .verticalScroll(rememberScrollState())
        ) {
            Text("Configuration UUIDs", style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(8.dp))
            TextField(value = serviceUuid, onValueChange = { serviceUuid = it }, label = { Text("Service UUID") }, modifier = Modifier.fillMaxWidth())
            Spacer(Modifier.height(8.dp))
            TextField(value = txUuid, onValueChange = { txUuid = it }, label = { Text("TX Characteristic UUID (Notify)") }, modifier = Modifier.fillMaxWidth())
            Spacer(Modifier.height(8.dp))
            TextField(value = rxUuid, onValueChange = { rxUuid = it }, label = { Text("RX Characteristic UUID (Write)") }, modifier = Modifier.fillMaxWidth())

            Spacer(Modifier.height(16.dp))
            HorizontalDivider()
            Spacer(Modifier.height(16.dp))

            Text("Fonctionnalités", style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(8.dp))
            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                Text("Reconnexion automatique")
                Spacer(Modifier.weight(1f))
                Checkbox(checked = autoReconnect, onCheckedChange = { autoReconnect = it })
            }

            Spacer(Modifier.height(16.dp))
            HorizontalDivider()
            Spacer(Modifier.height(16.dp))

            Text("Affichage & Logs", style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(8.dp))

            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                Text("Afficher en Héxadecimal (HEX)")
                Spacer(Modifier.weight(1f))
                Checkbox(checked = displayAsHex, onCheckedChange = { displayAsHex = it })
            }
            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                Text("Enregistrer les logs dans un fichier")
                Spacer(Modifier.weight(1f))
                Checkbox(checked = logToFile, onCheckedChange = { logToFile = it })
            }
            TextField(value = logFilePath, onValueChange = { logFilePath = it }, label = { Text("Chemin du fichier log") }, modifier = Modifier.fillMaxWidth())

            Spacer(Modifier.height(16.dp))
            HorizontalDivider()
            Spacer(Modifier.height(16.dp))

            Text("Interface", style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(8.dp))
            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                Text("Nombre de boutons macros: ${macroButtons.size}")
                Spacer(Modifier.weight(1f))
                Button(onClick = { 
                    if (macroButtons.size > 1) {
                        macroButtons = macroButtons.dropLast(1)
                    }
                }) { Text("-") }
                Button(onClick = { 
                    if (macroButtons.size < 32) {
                        macroButtons = macroButtons + MacroButtonConfig("Bouton ${macroButtons.size + 1}", "")
                    }
                 }) { Text("+") } // Limite à 32 pour la raison
            }

            Spacer(Modifier.height(32.dp))

            Row(horizontalArrangement = Arrangement.SpaceEvenly, modifier = Modifier.fillMaxWidth()) {
                Button(onClick = {
                    val newSettings = BleSettings(
                        uartServiceUuid = serviceUuid,
                        txCharacteristicUuid = txUuid,
                        rxCharacteristicUuid = rxUuid,
                        logToFileEnabled = logToFile,
                        logFilePath = logFilePath,
                        displayAsHex = displayAsHex,
                        autoReconnectEnabled = autoReconnect,
                        lastConnectedDeviceAddress = currentSettings.lastConnectedDeviceAddress
                    )
                    settingsPersistence.save(newSettings)
                    macroPersistence.saveConfigs(macroButtons)
                    currentSettings = newSettings
                    onNavigateBack()
                }) {
                    Text("Enregistrer")
                }

                Button(onClick = {
                    settingsPersistence.revertToDefault()
                    currentSettings = settingsPersistence.load()
                    macroButtons = List(8) { i -> MacroButtonConfig("Bouton ${i + 1}", "") }
                    macroPersistence.saveConfigs(macroButtons)

                }) {
                    Text("Reset par défaut")
                }
            }
        }
    }
}
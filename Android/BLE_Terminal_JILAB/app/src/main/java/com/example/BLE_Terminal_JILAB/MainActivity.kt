package com.example.BLE_Terminal_JILAB

import android.Manifest
import android.app.Activity
import android.bluetooth.*
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.app.ActivityCompat
import androidx.core.content.edit
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import com.example.BLE_Terminal_JILAB.ui.theme.Jojo_First_AppliTheme
import com.google.gson.Gson
import com.google.gson.reflect.TypeToken
import kotlinx.coroutines.launch
import java.io.File
import java.io.IOException
import java.text.SimpleDateFormat
import java.util.*

private const val TAG = "MainActivity"

data class MacroButtonConfig(val label: String, val command: String)

enum class MessageType { TX, RX, STATUS }
data class Message(val data: ByteArray, val type: MessageType, val timestamp: String) {
    fun toDisplayString(isHex: Boolean): String {
        return if (type == MessageType.STATUS) {
            data.toString(Charsets.UTF_8)
        } else {
            if (isHex) data.toHexString() else data.toString(Charsets.UTF_8)
        }
    }

    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false

        other as Message

        if (!data.contentEquals(other.data)) return false
        if (type != other.type) return false
        if (timestamp != other.timestamp) return false

        return true
    }

    override fun hashCode(): Int {
        var result = data.contentHashCode()
        result = 31 * result + type.hashCode()
        result = 31 * result + timestamp.hashCode()
        return result
    }
}

@OptIn(ExperimentalUnsignedTypes::class)
fun ByteArray.toHexString() = asUByteArray().joinToString(" ") { it.toString(16).uppercase().padStart(2, '0') }

class MainActivity : ComponentActivity() {

    private val settingsPersistence by lazy { SettingsPersistence(this) }
    private lateinit var bleSettings: BleSettings

    private val bluetoothAdapter: BluetoothAdapter by lazy {
        val bluetoothManager = getSystemService(BLUETOOTH_SERVICE) as BluetoothManager
        bluetoothManager.adapter
    }
    private val bleScanner by lazy { bluetoothAdapter.bluetoothLeScanner }
    var bluetoothGatt: BluetoothGatt? = null

    var connectionState by mutableStateOf("Déconnecté")
    val receivedMessages = mutableStateListOf<Message>()
    var isScanning by mutableStateOf(false)
    val foundDevices = mutableStateListOf<BluetoothDevice>()

    private val requestEnableBluetooth =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            if (result.resultCode == RESULT_OK) {
                startBleScan()
            } else {
                logMessage("L'activation du Bluetooth est nécessaire.".toByteArray(), MessageType.STATUS)
            }
        }

    private val requestMultiplePermissions =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { permissions ->
            if (permissions.values.all { it }) {
                startBleScan()
            } else {
                logMessage("Les permissions sont nécessaires.".toByteArray(), MessageType.STATUS)
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        bleSettings = settingsPersistence.load()

        setContent {
            Jojo_First_AppliTheme {
                AppNavigation(mainActivity = this)
            }
        }

        if (bleSettings.autoReconnectEnabled && bleSettings.lastConnectedDeviceAddress != null) {
            bleSettings.lastConnectedDeviceAddress?.let {
                if (bluetoothAdapter.isEnabled) {
                    try {
                        val device = bluetoothAdapter.getRemoteDevice(it)
                        connectToDevice(device)
                    } catch (_: IllegalArgumentException) {
                        Log.e(TAG, "Adresse MAC invalide pour la reconnexion: $it")
                    }
                } else {
                    logMessage("Bluetooth non activé, reconnexion auto annulée.".toByteArray(), MessageType.STATUS)
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        bleSettings = settingsPersistence.load()
        if (!bluetoothAdapter.isEnabled) {
            promptEnableBluetooth()
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        if (ActivityCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED) {
             bluetoothGatt?.close()
        }
        bluetoothGatt = null
    }

    private fun promptEnableBluetooth() {
        if (!bluetoothAdapter.isEnabled) {
            val enableBtIntent = Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE)
            requestEnableBluetooth.launch(enableBtIntent)
        }
    }

    private fun checkAndRequestPermissions() {
        val requiredPermissions = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT, Manifest.permission.ACCESS_FINE_LOCATION)
        } else {
            arrayOf(Manifest.permission.BLUETOOTH, Manifest.permission.BLUETOOTH_ADMIN, Manifest.permission.ACCESS_FINE_LOCATION)
        }
        val missingPermissions = requiredPermissions.filter { ActivityCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED }
        if (missingPermissions.isNotEmpty()) {
            requestMultiplePermissions.launch(missingPermissions.toTypedArray())
        }
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            super.onScanResult(callbackType, result)
            if (!foundDevices.any { it.address == result.device.address }) {
                runOnUiThread { foundDevices.add(result.device) }
            }
        }

        override fun onScanFailed(errorCode: Int) {
            Log.e(TAG, "Scan a échoué: $errorCode")
            runOnUiThread { isScanning = false }
        }
    }

    fun startBleScan() {
        if (!bluetoothAdapter.isEnabled) {
            promptEnableBluetooth()
            return
        }
        val requiredPermission = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) Manifest.permission.BLUETOOTH_SCAN else Manifest.permission.ACCESS_FINE_LOCATION
        if (ActivityCompat.checkSelfPermission(this, requiredPermission) != PackageManager.PERMISSION_GRANTED) {
            checkAndRequestPermissions()
            return
        }
        if (!isScanning) {
            runOnUiThread {
                foundDevices.clear()
                isScanning = true
            }
            bleScanner.startScan(null, ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), scanCallback)
            Log.d(TAG, "Scan BLE démarré")
        }
    }

    fun stopBleScan() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S && ActivityCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_SCAN) != PackageManager.PERMISSION_GRANTED) {
            return
        }
        if (isScanning) {
            runOnUiThread { isScanning = false }
            bleScanner.stopScan(scanCallback)
            Log.d(TAG, "Scan BLE arrêté")
        }
    }

    fun connectToDevice(device: BluetoothDevice) {
        bleSettings = settingsPersistence.load() // Recharger les paramètres avant la connexion
        stopBleScan()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S && ActivityCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
             checkAndRequestPermissions()
            return
        }
        runOnUiThread { connectionState = "Connexion à ${getDeviceName(this, device)}..." }
        bluetoothGatt = device.connectGatt(this, false, gattCallback)
        bleSettings.lastConnectedDeviceAddress = device.address
        settingsPersistence.save(bleSettings)
    }

    fun disconnect() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S && ActivityCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
            return
        }
        bluetoothGatt?.disconnect()
        bleSettings.lastConnectedDeviceAddress = null
        settingsPersistence.save(bleSettings)
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            val deviceName = getDeviceName(this@MainActivity, gatt.device)
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                Log.i(TAG, "Connecté au GATT server.")
                runOnUiThread {
                    connectionState = "Connecté, découverte des services..."
                    logMessage("--- Connecté à $deviceName ---".toByteArray(), MessageType.STATUS)
                }
                if (ActivityCompat.checkSelfPermission(this@MainActivity, Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    return
                }
                gatt.discoverServices()
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                Log.i(TAG, "Déconnecté du GATT server.")
                runOnUiThread {
                    connectionState = "Déconnecté"
                    logMessage("--- Déconnecté de $deviceName ---".toByteArray(), MessageType.STATUS)
                }
                if (ActivityCompat.checkSelfPermission(this@MainActivity, Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    return
                }
                bluetoothGatt?.close()
                bluetoothGatt = null
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (ActivityCompat.checkSelfPermission(this@MainActivity, Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                return
            }
            if (status == BluetoothGatt.GATT_SUCCESS) {
                val uartService = gatt.getService(UUID.fromString(bleSettings.uartServiceUuid))
                val txChar = uartService?.getCharacteristic(UUID.fromString(bleSettings.txCharacteristicUuid))
                if (txChar != null) {
                    enableNotifications(gatt, txChar)
                } else {
                    logMessage("Caractéristique UART TX (Notify) non trouvée !".toByteArray(), MessageType.STATUS)
                    gatt.disconnect()
                }
            } else {
                Log.w(TAG, "onServicesDiscovered a reçu: $status")
            }
        }

        override fun onDescriptorWrite(gatt: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) {
            if (ActivityCompat.checkSelfPermission(this@MainActivity, Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                return
            }
            if (status == BluetoothGatt.GATT_SUCCESS) {
                Log.d(TAG, "Descripteur écrit avec succès. Prêt à communiquer.")
                runOnUiThread { connectionState = "Prêt à communiquer" }
            } else {
                logMessage("Erreur lors de l'activation des notifications".toByteArray(), MessageType.STATUS)
                runOnUiThread { connectionState = "Erreur de communication" }
            }
        }

        @Deprecated("Utilisé pour Android < 13", ReplaceWith("onCharacteristicChanged(gatt, characteristic, characteristic.value)"))
        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            @Suppress("DEPRECATION")
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
                handleReceivedMessage(characteristic.value)
            }
        }

        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, value: ByteArray) {
             if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                handleReceivedMessage(value)
             }
        }
    }

    private fun handleReceivedMessage(value: ByteArray) {
        logMessage(value, MessageType.RX)
    }

    private fun enableNotifications(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
        if (ActivityCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            return
        }
        if ((characteristic.properties and BluetoothGattCharacteristic.PROPERTY_NOTIFY) == 0) {
            logMessage("ERREUR: La caractéristique TX n'a pas la propriété NOTIFY.".toByteArray(), MessageType.STATUS)
            return
        }
        val cccdUuid = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
        val descriptor = characteristic.getDescriptor(cccdUuid)
        if (descriptor == null) {
            logMessage("AVERTISSEMENT: Le descripteur CCCD est introuvable.".toByteArray(), MessageType.STATUS)
            return
        }
        gatt.setCharacteristicNotification(characteristic, true)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            gatt.writeDescriptor(descriptor, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
        } else {
            @Suppress("DEPRECATION")
            descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            @Suppress("DEPRECATION")
            gatt.writeDescriptor(descriptor)
        }
    }

    @OptIn(ExperimentalStdlibApi::class)
    fun sendMessage(message: String) {
        val gatt = bluetoothGatt ?: return
        val rxChar = gatt.getService(UUID.fromString(bleSettings.uartServiceUuid))?.getCharacteristic(UUID.fromString(bleSettings.rxCharacteristicUuid)) ?: run {
            logMessage("Caractéristique RX (Write) non trouvée".toByteArray(), MessageType.STATUS)
            return
        }
        if ((rxChar.properties and BluetoothGattCharacteristic.PROPERTY_WRITE) == 0 && (rxChar.properties and BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE) == 0) {
            logMessage("ERREUR: La caractéristique RX n'a pas la propriété WRITE.".toByteArray(), MessageType.STATUS)
            return
        }
        if (ActivityCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            return
        }

        val messageBytes = if (bleSettings.displayAsHex) {
            try {
                message.replace(Regex("\\s"), "").hexToByteArray()
            } catch (_: Exception) {
                logMessage("Erreur: '$message' n'est pas un format HEX valide.".toByteArray(), MessageType.STATUS)
                return
            }
        } else {
            message.toByteArray(Charsets.UTF_8)
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            gatt.writeCharacteristic(rxChar, messageBytes, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)
        } else {
            @Suppress("DEPRECATION")
            rxChar.value = messageBytes
            @Suppress("DEPRECATION")
            gatt.writeCharacteristic(rxChar)
        }
        logMessage(messageBytes, MessageType.TX)
    }

    fun clearMessages() {
        receivedMessages.clear()
    }

    private fun logMessage(data: ByteArray, type: MessageType) {
        val timestamp = SimpleDateFormat("HH:mm:ss", Locale.getDefault()).format(Date())
        val message = Message(data, type, timestamp)
        runOnUiThread { receivedMessages.add(message) }

        if (bleSettings.logToFileEnabled) {
            try {
                val logFile = File(bleSettings.logFilePath, "ble_log.txt")
                if (!logFile.exists()) {
                    logFile.createNewFile()
                }
                val sdf = SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.getDefault())
                val timestampLog = sdf.format(Date())
                val textForLog = message.toDisplayString(bleSettings.displayAsHex)
                logFile.appendText("$timestampLog - [${type.name}] $textForLog\n")
            } catch (e: IOException) {
                Log.e(TAG, "Erreur d'écriture dans le fichier log", e)
                runOnUiThread { receivedMessages.add(Message("Erreur de log: ${e.localizedMessage}".toByteArray(), MessageType.STATUS, timestamp)) }
            }
        }
    }
}

// --- PERSISTANCE DES MACROS ---
class MacroPersistence(context: Context) {
    private val prefs: SharedPreferences = context.getSharedPreferences("macro_prefs", Context.MODE_PRIVATE)
    private val gson = Gson()

    fun saveConfigs(configs: List<MacroButtonConfig>) {
        val json = gson.toJson(configs)
        prefs.edit {
            putString("macro_button_configs", json)
        }
    }

    fun loadConfigs(): List<MacroButtonConfig> {
        val json = prefs.getString("macro_button_configs", null)
        if (json != null) {
            val type = object : TypeToken<List<MacroButtonConfig>>() {}.type
            return gson.fromJson(json, type)
        }
        return List(8) { i -> MacroButtonConfig("Bouton ${i + 1}", "") } // Default to 8 buttons
    }
}

@Composable
fun AppNavigation(mainActivity: MainActivity) {
    val navController = rememberNavController()
    val settingsPersistence = remember(mainActivity) { SettingsPersistence(mainActivity) }
    var bleSettings by remember { mutableStateOf(settingsPersistence.load()) }

    NavHost(navController = navController, startDestination = "main") {
        composable("main") {
             BleTerminalScreen(
                connectionState = mainActivity.connectionState,
                receivedMessages = mainActivity.receivedMessages,
                onSendMessage = { message -> mainActivity.sendMessage(message) },
                isScanning = mainActivity.isScanning,
                foundDevices = mainActivity.foundDevices,
                onStartScan = { mainActivity.startBleScan() },
                onStopScan = { mainActivity.stopBleScan() },
                onConnectDevice = { device -> mainActivity.connectToDevice(device) },
                onDisconnect = { mainActivity.disconnect() },
                macroPersistence = MacroPersistence(mainActivity),
                onNavigateToSettings = { navController.navigate("settings") },
                bleSettings = bleSettings,
                onClearMessages = { mainActivity.clearMessages() }
            )
        }
        composable("settings") {
            SettingsScreen(onNavigateBack = {
                navController.popBackStack()
                bleSettings = settingsPersistence.load()
            }, macroPersistence = MacroPersistence(mainActivity))
        }
    }
}


// --- COMPOSANTS DE L'INTERFACE ---
private fun getDeviceName(context: Context, device: BluetoothDevice): String {
    return try {
        if (ActivityCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED) {
            device.name ?: "Appareil inconnu"
        } else if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) {
            device.name ?: "Appareil inconnu"
        } else {
            "Appareil inconnu"
        }
    } catch (_: SecurityException) {
        "Appareil inconnu"
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun BleTerminalScreen(
    connectionState: String,
    receivedMessages: List<Message>,
    onSendMessage: (String) -> Unit,
    isScanning: Boolean,
    foundDevices: List<BluetoothDevice>,
    onStartScan: () -> Unit,
    onStopScan: () -> Unit,
    onConnectDevice: (BluetoothDevice) -> Unit,
    onDisconnect: () -> Unit,
    macroPersistence: MacroPersistence,
    onNavigateToSettings: () -> Unit,
    bleSettings: BleSettings,
    onClearMessages: () -> Unit
) {
    var showDisconnect by remember { mutableStateOf(false) }
    val isConnecting = connectionState.startsWith("Connexion")
    val isConnected = connectionState.startsWith("Prêt")

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Terminal BLE UART") },
                actions = {
                    IconButton(onClick = onNavigateToSettings) {
                        Icon(Icons.Default.Settings, contentDescription = "Configuration")
                    }
                }
            )
        }
    ) { paddingValues ->
        Column(
            modifier = Modifier.fillMaxSize().padding(paddingValues).padding(16.dp)
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(modifier = Modifier.size(12.dp).clip(CircleShape).background(when {
                    isConnected -> Color.Green
                    isConnecting -> Color.Yellow
                    else -> Color.Red
                }))
                Spacer(modifier = Modifier.width(8.dp))
                Text(text = "Statut: $connectionState", style = MaterialTheme.typography.titleMedium, modifier = Modifier.clickable(enabled = isConnected) { showDisconnect = !showDisconnect })
            }
            if (showDisconnect && isConnected) {
                Button(onClick = onDisconnect, modifier = Modifier.padding(top = 8.dp)) {
                    Text("Déconnecter")
                }
            }
            Spacer(modifier = Modifier.height(8.dp))

            if (isConnected) {
                TerminalView(receivedMessages, onSendMessage, macroPersistence, bleSettings, onClearMessages)
            } else {
                ScanView(isScanning, foundDevices, onStartScan, onStopScan, onConnectDevice)
            }
        }
    }
}

@Composable
fun ScanView(
    isScanning: Boolean,
    foundDevices: List<BluetoothDevice>,
    onStartScan: () -> Unit,
    onStopScan: () -> Unit,
    onConnectDevice: (BluetoothDevice) -> Unit
) {
    val context = LocalContext.current
    var searchQuery by remember { mutableStateOf("") }
    val filteredDevices = foundDevices.filter {
        val deviceName = getDeviceName(context, it)
        deviceName.contains(searchQuery, ignoreCase = true) || it.address.contains(searchQuery, ignoreCase = true)
    }

    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Row(horizontalArrangement = Arrangement.SpaceEvenly, modifier = Modifier.fillMaxWidth()){
            Button(onClick = onStartScan, enabled = !isScanning) { Text("Scanner") }
            Button(onClick = onStopScan, enabled = isScanning) { Text("Arrêter") }
        }
        Spacer(modifier = Modifier.height(8.dp))
        OutlinedTextField(
            value = searchQuery,
            onValueChange = { searchQuery = it },
            label = { Text("Filtrer les appareils...") },
            modifier = Modifier.fillMaxWidth()
        )
        if (isScanning) {
            Spacer(modifier = Modifier.height(8.dp))
            CircularProgressIndicator(modifier = Modifier.size(24.dp))
        }

        if (filteredDevices.isEmpty()) {
            Box(modifier = Modifier.weight(1f).fillMaxWidth(), contentAlignment = Alignment.Center) {
                Text(if (isScanning) "Recherche en cours..." else "Aucun appareil trouvé. Vérifiez que votre appareil est allumé, à portée et que les services de localisation sont activés.")
            }
        } else {
            LazyColumn(modifier = Modifier.padding(vertical = 8.dp)) {
                items(filteredDevices, key = { it.address }) { device ->
                    val deviceName = getDeviceName(context, device)
                    Button(onClick = { onConnectDevice(device) }, modifier = Modifier.fillMaxWidth()) {
                        Text(text = "$deviceName - ${device.address}")
                    }
                }
            }
        }
    }
}

@Composable
fun TerminalView(
    receivedMessages: List<Message>,
    onSendMessage: (String) -> Unit,
    macroPersistence: MacroPersistence,
    bleSettings: BleSettings,
    onClearMessages: () -> Unit
) {
    val listState = rememberLazyListState()
    val coroutineScope = rememberCoroutineScope()
    var messageToSend by remember { mutableStateOf("") }
    val macroButtons = remember { mutableStateListOf<MacroButtonConfig>()}

    LaunchedEffect(Unit) {
        macroButtons.addAll(macroPersistence.loadConfigs())
    }


    LaunchedEffect(receivedMessages.size) {
        if(receivedMessages.isNotEmpty()) {
            coroutineScope.launch {
                listState.animateScrollToItem(receivedMessages.size - 1)
            }
        }
    }

    Column {
        LazyColumn(modifier = Modifier.weight(1f), state = listState) {
            items(receivedMessages) { msg ->
                val text = msg.toDisplayString(bleSettings.displayAsHex)
                val prefix = "[${msg.timestamp}] ${msg.type.name}:"
                Text(text = "$prefix $text", modifier = Modifier.padding(horizontal = 8.dp), lineHeight = 16.sp)
            }
        }

        Row(modifier = Modifier.fillMaxWidth().padding(top = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(
                value = messageToSend,
                onValueChange = { messageToSend = it },
                label = { Text(if (bleSettings.displayAsHex) "Message (HEX)" else "Message") },
                modifier = Modifier.weight(1f)
            )
            Spacer(modifier = Modifier.width(8.dp))
            IconButton(onClick = onClearMessages) {
                Icon(Icons.Default.Delete, contentDescription = "Effacer")
            }
            Button(onClick = {
                if(messageToSend.isNotBlank()) {
                   onSendMessage(messageToSend)
                   messageToSend = ""
                }
            }) {
                Text("Envoyer")
            }
        }
        Spacer(modifier = Modifier.height(16.dp))
        Box(modifier = Modifier.height(160.dp)) {
            MacroButtonsGrid(onSendMessage, macroPersistence, macroButtons)
        }
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun MacroButtonsGrid(onSendCommand: (String) -> Unit, persistence: MacroPersistence, buttons: MutableList<MacroButtonConfig>) {
    var showDialog by remember { mutableStateOf(false) }
    var editingIndex by remember { mutableIntStateOf(-1) }

    if (showDialog && editingIndex != -1) {
        buttons.getOrNull(editingIndex)?.let { config ->
            EditMacroDialog(
                config = config,
                onDismiss = { showDialog = false },
                onSave = { newConfig ->
                    buttons[editingIndex] = newConfig
                    persistence.saveConfigs(buttons.toList())
                    showDialog = false
                }
            )
        }
    }

    val columns = 4
    val buttonCount = buttons.size
    val rows = (buttonCount + columns - 1) / columns

    Column(
        modifier = Modifier.fillMaxHeight(),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        (0 until rows).forEach { rowIndex ->
            Row(
                modifier = Modifier.weight(1f),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                (0 until columns).forEach { colIndex ->
                    val index = rowIndex * columns + colIndex
                    if (index < buttonCount) {
                        buttons.getOrNull(index)?.let { config ->
                             val baseFontSize = 16.sp
                            val minFontSize = 8.sp
                            val scaleFactor = (rows - 2).coerceAtLeast(0)
                            val fontSizeValue = (baseFontSize.value - (scaleFactor * 2f)).coerceAtLeast(minFontSize.value)
                            val fontSize = fontSizeValue.sp

                            Surface(
                                modifier = Modifier
                                    .weight(1f)
                                    .fillMaxHeight()
                                    .combinedClickable(
                                        onClick = {
                                            if (config.command.isNotBlank()) onSendCommand(config.command)
                                        },
                                        onLongClick = {
                                            editingIndex = index
                                            showDialog = true
                                        }
                                    ),
                                shape = RoundedCornerShape(8.dp),
                                color = MaterialTheme.colorScheme.primary,
                                contentColor = MaterialTheme.colorScheme.onPrimary
                            ) {
                                Box(contentAlignment = Alignment.Center, modifier = Modifier.padding(4.dp)) {
                                    Text(
                                        text = config.label,
                                        fontSize = fontSize,
                                        maxLines = 2,
                                        overflow = TextOverflow.Ellipsis
                                    )
                                }
                            }
                        }
                    } else {
                        Spacer(modifier = Modifier.weight(1f))
                    }
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun EditMacroDialog(config: MacroButtonConfig, onDismiss: () -> Unit, onSave: (MacroButtonConfig) -> Unit) {
    var label by remember { mutableStateOf(TextFieldValue(config.label)) }
    var command by remember { mutableStateOf(TextFieldValue(config.command)) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Configurer le bouton") },
        text = {
            Column {
                OutlinedTextField(
                    value = label,
                    onValueChange = { label = it },
                    label = { Text("Étiquette du bouton") },
                    singleLine = true
                )
                Spacer(modifier = Modifier.height(8.dp))
                OutlinedTextField(
                    value = command,
                    onValueChange = { command = it },
                    label = { Text("Commande à envoyer") },
                    singleLine = true
                )
            }
        },
        confirmButton = {
            Button(onClick = { onSave(MacroButtonConfig(label.text, command.text)) }) {
                Text("Enregistrer")
            }
        },
        dismissButton = {
            Button(onClick = onDismiss) {
                Text("Annuler")
            }
        }
    )
}

@OptIn(ExperimentalStdlibApi::class)
fun String.hexToByteArray(): ByteArray {
    check(length % 2 == 0) { "Must have an even length" }
    return chunked(2)
        .map { it.toInt(16).toByte() }
        .toByteArray()
}

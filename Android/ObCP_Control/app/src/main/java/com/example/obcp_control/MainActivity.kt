package com.example.obcp_control

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.le.ScanResult
import android.content.Intent
import android.content.pm.PackageManager
import android.content.res.Configuration
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.foundation.Image
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.LinkAnnotation
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.app.ActivityCompat
import com.example.obcp_control.ui.theme.ObCP_ControlTheme
import com.example.obcp_control.ui.theme.Orange
import com.github.mikephil.charting.charts.LineChart
import com.github.mikephil.charting.data.Entry
import com.github.mikephil.charting.data.LineData
import com.github.mikephil.charting.data.LineDataSet
import kotlin.math.pow

@OptIn(ExperimentalMaterial3Api::class)
class MainActivity : ComponentActivity() {

    private val viewModel: BleViewModel by viewModels()

    private val requestEnableBluetooth = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        if (result.resultCode == RESULT_OK) {
            startScan()
        }
    }

    private val requestBluetoothPermissions = registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { permissions ->
        if (permissions.values.all { it }) {
            startScan()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        setContent {
            ObCP_ControlTheme {
                val connectionState by viewModel.connectionState.collectAsState()
                val discoveredDevices by viewModel.discoveredDevices.collectAsState()

                MainScreen(
                    viewModel = viewModel,
                    connectionState = connectionState,
                    devices = discoveredDevices,
                    onConnectClick = { startScan() },
                    onStopScanClick = { viewModel.bleManager.stopScan() },
                    onDeviceSelected = { device ->
                        viewModel.bleManager.stopScan()
                        viewModel.bleManager.connect(device)
                    }
                )
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        if (isFinishing) {
            viewModel.bleManager.disconnect()
        }
    }

    private fun startScan() {
        if (!hasRequiredPermissions()) {
            requestBluetoothPermissions.launch(getRequiredPermissions())
            return
        }

        if (!viewModel.bleManager.isBluetoothEnabled()) {
            val enableBtIntent = Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE)
            requestEnableBluetooth.launch(enableBtIntent)
            return
        }
        viewModel.bleManager.startScan()
    }

    private fun hasRequiredPermissions(): Boolean {
        return getRequiredPermissions().all { ActivityCompat.checkSelfPermission(this, it) == PackageManager.PERMISSION_GRANTED }
    }

    private fun getRequiredPermissions(): Array<String> {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(
                Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_CONNECT,
                Manifest.permission.ACCESS_FINE_LOCATION
            )
        } else {
            arrayOf(
                Manifest.permission.BLUETOOTH,
                Manifest.permission.BLUETOOTH_ADMIN,
                Manifest.permission.ACCESS_FINE_LOCATION
            )
        }
    }
}

@Composable
fun MainScreen(
    viewModel: BleViewModel,
    connectionState: ConnectionState,
    devices: List<ScanResult>,
    onConnectClick: () -> Unit,
    onStopScanClick: () -> Unit,
    onDeviceSelected: (BluetoothDevice) -> Unit
) {
    var showInfoDialog by remember { mutableStateOf(false) }

    if (showInfoDialog) {
        InfoDialog { showInfoDialog = false }
    }

    if (connectionState == ConnectionState.CONNECTED) {
        ControlScreen(viewModel)
    } else {
        DeviceListScreen(connectionState, devices, onConnectClick, onStopScanClick, onDeviceSelected) { showInfoDialog = true }
    }
}

@Composable
fun InfoDialog(onDismiss: () -> Unit) {
    val annotatedString = buildAnnotatedString {
        append("This application was created specifically to control the ObCP device made at Supmicrotech (description ")
        pushLink(LinkAnnotation.Url("https://github.com/jimbaud/LAB_ObCP_L476_STM32CubeIDE"))
        withStyle(style = SpanStyle(color = MaterialTheme.colorScheme.primary, textDecoration = TextDecoration.Underline)) {
            append("here")
        }
        pop()
        append(") by Joël IMBAUD (GitHub profile ")
        pushLink(LinkAnnotation.Url("https://github.com/jimbaud/"))
        withStyle(style = SpanStyle(color = MaterialTheme.colorScheme.primary, textDecoration = TextDecoration.Underline)) {
            append("here")
        }
        pop()
        append(").")
    }
    val configuration = LocalConfiguration.current

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("About This Application") },
        text = {
            if (configuration.orientation == Configuration.ORIENTATION_LANDSCAPE) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.padding(8.dp)
                ) {
                    Image(
                        painter = painterResource(id = R.drawable.obcp_dessus_blanc),
                        contentDescription = "ObCP Device",
                        modifier = Modifier.weight(1f)
                    )
                    Spacer(modifier = Modifier.width(16.dp))
                    Text(
                        text = annotatedString,
                        style = MaterialTheme.typography.bodyMedium.copy(textAlign = TextAlign.Start),
                        modifier = Modifier.weight(1.5f)
                    )
                }
            } else { // Portrait
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Image(painter = painterResource(id = R.drawable.obcp_dessus_blanc), contentDescription = "ObCP Device")
                    Spacer(modifier = Modifier.height(16.dp))
                    Text(text = annotatedString, style = MaterialTheme.typography.bodyMedium.copy(textAlign = TextAlign.Center))
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) {
                Text("Close")
            }
        }
    )
}


@OptIn(ExperimentalMaterial3Api::class)
@SuppressLint("MissingPermission")
@Composable
fun DeviceListScreen(
    connectionState: ConnectionState,
    devices: List<ScanResult>,
    onScanClick: () -> Unit,
    onStopScanClick: () -> Unit,
    onDeviceSelected: (BluetoothDevice) -> Unit,
    onInfoClick: () -> Unit
) {
    var searchText by remember { mutableStateOf("") }
    val filteredDevices = devices.filter {
        val name = it.device.name ?: "Unknown"
        val address = it.device.address
        searchText.isEmpty() || name.contains(searchText, ignoreCase = true) || address.contains(searchText, ignoreCase = true)
    }
    val uriHandler = LocalUriHandler.current

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Connect to a Device") },
                actions = {
                    Image(
                        painter = painterResource(id = R.drawable.logo_supmicrotech),
                        contentDescription = "Supmicrotech Logo",
                        modifier = Modifier
                            .height(40.dp)
                            .padding(end = 8.dp)
                            .clickable {
                                uriHandler.openUri("https://www.supmicrotech.fr")
                            }
                    )
                }
            )
        },
        floatingActionButton = {
            FloatingActionButton(onClick = onInfoClick) {
                Icon(Icons.Default.Info, contentDescription = "Information")
            }
        }
    ) { paddingValues ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues)
                .padding(horizontal = 16.dp)
        ) {
            Spacer(modifier = Modifier.height(16.dp))

            OutlinedTextField(
                value = searchText,
                onValueChange = { searchText = it },
                modifier = Modifier.fillMaxWidth(),
                label = { Text("Search by name or address") },
                leadingIcon = { Icon(Icons.Default.Search, contentDescription = "Search") },
                singleLine = true
            )

            Spacer(modifier = Modifier.height(8.dp))

            ScanButton(connectionState = connectionState, onScanClick = onScanClick, onStopClick = onStopScanClick)

            Spacer(modifier = Modifier.height(16.dp))

            if (connectionState == ConnectionState.SCANNING && filteredDevices.isEmpty() && searchText.isEmpty()) {
                Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        CircularProgressIndicator()
                        Text("Searching for devices...")
                    }
                }
            } else if (filteredDevices.isEmpty()) {
                Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    Text(if (searchText.isNotEmpty()) "No devices match your search." else "No devices found. Try scanning again.")
                }
            } else {
                LazyColumn(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    items(filteredDevices) { scanResult ->
                        DeviceListItem(scanResult = scanResult, onDeviceSelected = onDeviceSelected)
                    }
                }
            }
        }
    }
}

@Composable
fun ScanButton(
    connectionState: ConnectionState,
    onScanClick: () -> Unit,
    onStopClick: () -> Unit
) {
    when (connectionState) {
        ConnectionState.SCANNING -> {
            Button(
                onClick = onStopClick,
                modifier = Modifier.fillMaxWidth(),
                colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error)
            ) {
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    CircularProgressIndicator(
                        modifier = Modifier.size(24.dp),
                        color = MaterialTheme.colorScheme.onError,
                        strokeWidth = 2.dp
                    )
                    Text("Stop Scan")
                }
            }
        }
        ConnectionState.CONNECTING -> {
            Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.Center) {
                CircularProgressIndicator()
                Spacer(modifier = Modifier.width(8.dp))
                Text("Connecting...")
            }
        }
        else -> {
            Button(onClick = onScanClick, modifier = Modifier.fillMaxWidth()) {
                Text("Scan for Devices")
            }
        }
    }
}

@SuppressLint("MissingPermission")
@Composable
fun DeviceListItem(scanResult: ScanResult, onDeviceSelected: (BluetoothDevice) -> Unit) {
    Card(modifier = Modifier
        .fillMaxWidth()
        .clickable { onDeviceSelected(scanResult.device) }) {
        Row(
            modifier = Modifier.padding(16.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            Icon(Icons.Default.Bluetooth, contentDescription = "Bluetooth Device")
            Column {
                Text(text = scanResult.device.name ?: "Unknown Device", style = MaterialTheme.typography.titleMedium)
                Text(text = scanResult.device.address, style = MaterialTheme.typography.bodySmall)
            }
        }
    }
}


@OptIn(ExperimentalLayoutApi::class, ExperimentalMaterial3Api::class)
@Composable
fun ControlScreen(viewModel: BleViewModel) {
    val configuration = LocalConfiguration.current

    Scaffold {
        padding ->
        if (configuration.orientation == Configuration.ORIENTATION_LANDSCAPE) {
            Row(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(padding)
            ) {
                Column(Modifier.weight(0.4f).padding(horizontal = 8.dp)) {
                    ControlPanel(viewModel)
                }
                Column(Modifier.weight(0.6f).padding(horizontal = 8.dp)) {
                    GraphPanel(viewModel)
                }
            }
        } else { // Portrait
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(padding)
                    .padding(horizontal = 16.dp),
            ) {
                ControlPanel(viewModel)
                GraphPanel(viewModel)
            }
        }
    }
}

@Composable
fun ControlPanel(viewModel: BleViewModel) {
    val redPercent by viewModel.redPercent.collectAsState()
    val greenPercent by viewModel.greenPercent.collectAsState()
    val bluePercent by viewModel.bluePercent.collectAsState()
    val pwmValue by viewModel.pwmValue.collectAsState()
    val timerOn by viewModel.timerOn.collectAsState()
    val uriHandler = LocalUriHandler.current

    Column {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Image(
                    painter = painterResource(id = R.drawable.logo_supmicrotech_mu),
                    contentDescription = "Supmicrotech Mu Logo",
                    modifier = Modifier
                        .height(32.dp)
                        .clickable {
                            uriHandler.openUri("https://www.supmicrotech.fr")
                        }
                )
                Spacer(modifier = Modifier.width(8.dp))
                Text("ObCP Control", style = MaterialTheme.typography.titleMedium)
            }
            IconButton(onClick = { viewModel.bleManager.disconnect() }) {
                Icon(Icons.Default.Close, contentDescription = "Disconnect")
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        Card(modifier = Modifier.fillMaxWidth()) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Row(horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                    Column(Modifier.weight(1f)) {
                        CustomSlider("Red",
                            value = redPercent,
                            onValueChange = { viewModel.redPercent.value = it },
                            onValueChangeFinished = {
                                val logValue = (65535 * (redPercent / 100f).pow(3)).toInt()
                                viewModel.sendCommandWithRetry("r$logValue", "Red = $logValue")
                            },
                            valueRange = 0f..100f,
                            valueText = { "${it.toInt()}%" },
                            sliderColors = SliderDefaults.colors(thumbColor = MaterialTheme.colorScheme.error, activeTrackColor = MaterialTheme.colorScheme.error.copy(alpha = 0.5f), inactiveTrackColor = MaterialTheme.colorScheme.error.copy(alpha = 0.2f))
                        )
                        CustomSlider("Green",
                            value = greenPercent,
                            onValueChange = { viewModel.greenPercent.value = it },
                            onValueChangeFinished = {
                                val logValue = (65535 * (greenPercent / 100f).pow(3)).toInt()
                                viewModel.sendCommandWithRetry("v$logValue", "Green = $logValue")
                            },
                            valueRange = 0f..100f,
                            valueText = { "${it.toInt()}%" },
                            sliderColors = SliderDefaults.colors(thumbColor = MaterialTheme.colorScheme.secondary, activeTrackColor = MaterialTheme.colorScheme.secondary.copy(alpha = 0.5f), inactiveTrackColor = MaterialTheme.colorScheme.secondary.copy(alpha = 0.2f))
                        )
                    }
                    Column(Modifier.weight(1f)) {
                        CustomSlider("Blue",
                            value = bluePercent,
                            onValueChange = { viewModel.bluePercent.value = it },
                            onValueChangeFinished = {
                                val logValue = (65535 * (bluePercent / 100f).pow(3)).toInt()
                                viewModel.sendCommandWithRetry("b$logValue", "Blue = $logValue")
                            },
                            valueRange = 0f..100f,
                            valueText = { "${it.toInt()}%" },
                            sliderColors = SliderDefaults.colors(thumbColor = MaterialTheme.colorScheme.primary, activeTrackColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.5f), inactiveTrackColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.2f))
                        )
                        CustomSlider("PWM",
                            value = pwmValue,
                            onValueChange = { viewModel.pwmValue.value = it },
                            onValueChangeFinished = {
                                val uint16Value = (65535 * (pwmValue / 100f))
                                viewModel.sendCommandWithRetry("m${uint16Value.toInt()}", "PWM = ${uint16Value.toInt()}")
                            },
                            valueRange = 0f..100f,
                            valueText = { "${it.toInt()}%" },
                            sliderColors = SliderDefaults.colors(thumbColor = Orange, activeTrackColor = Orange.copy(alpha = 0.5f), inactiveTrackColor = Orange.copy(alpha = 0.2f))
                        )
                    }
                }

                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceAround, verticalAlignment = Alignment.CenterVertically) {
                    IconButton(onClick = { viewModel.resetControls() }) {
                        Icon(Icons.Default.Refresh, contentDescription = "Reset Controls")
                    }
                    IconButton(onClick = { viewModel.toggleTimer() }) {
                        if (timerOn) {
                            Icon(Icons.Default.Stop, contentDescription = "Stop Timer", tint = MaterialTheme.colorScheme.error)
                        } else {
                            Icon(Icons.Default.PlayArrow, contentDescription = "Start Timer")
                        }
                    }
                    IconButton(onClick = { viewModel.clearGraphs() }) {
                        Icon(Icons.Default.Delete, contentDescription = "Clear Graphs")
                    }
                }
            }
        }
    }
}

@Composable
fun GraphPanel(viewModel: BleViewModel) {
    Column(Modifier.fillMaxSize()) {
        Box(modifier = Modifier.weight(1f)) {
            Chart(
                modifier = Modifier.fillMaxSize(),
                entries = listOf(viewModel.accX, viewModel.accY, viewModel.accZ),
                labels = listOf("X", "Y", "Z"),
                colors = listOf(MaterialTheme.colorScheme.primary.toArgb(), MaterialTheme.colorScheme.secondary.toArgb(), MaterialTheme.colorScheme.error.toArgb())
            )
            if (viewModel.accX.isNotEmpty() || viewModel.accY.isNotEmpty() || viewModel.accZ.isNotEmpty()) {
                Text(
                    text = "Acceleration (g)",
                    style = MaterialTheme.typography.bodyMedium,
                    modifier = Modifier
                        .align(Alignment.BottomCenter)
                )
            }
        }
        Box(modifier = Modifier.weight(1f)) {
            Chart(
                modifier = Modifier.fillMaxSize(),
                entries = listOf(viewModel.temp),
                labels = listOf("Temp"),
                colors = listOf(Orange.toArgb())
            )
            if (viewModel.temp.isNotEmpty()) {
                Text(
                    text = "Temperature (°C)",
                    style = MaterialTheme.typography.bodyMedium,
                    modifier = Modifier
                        .align(Alignment.BottomCenter)
                )
            }
        }
    }
}

@Composable
fun Chart(modifier: Modifier = Modifier, entries: List<List<Entry>>, labels: List<String>, colors: List<Int>) {
    val legendTextColor = MaterialTheme.colorScheme.onSurface.toArgb()
    AndroidView(
        modifier = modifier,
        factory = { context ->
            LineChart(context).apply {
                description.isEnabled = false
                isDragEnabled = true
                setScaleEnabled(true)
                setPinchZoom(true)
                legend.textColor = legendTextColor
                setNoDataTextColor(legendTextColor)
            }
        },
        update = { chart ->
            val dataSets = entries.mapIndexed { index, entryList ->
                LineDataSet(entryList, labels[index]).apply {
                    color = colors[index]
                    setCircleColor(legendTextColor)
                 }
            }
            chart.data = if (dataSets.any { it.entryCount > 0 }) LineData(dataSets.toList()) else null
            chart.invalidate()
            chart.notifyDataSetChanged()
            if (chart.data != null && chart.data.entryCount > 0) chart.moveViewToX(chart.data.entryCount.toFloat())
        }
    )
}

@Composable
fun CustomSlider(
    name: String,
    value: Float,
    valueRange: ClosedFloatingPointRange<Float>,
    valueText: (Float) -> String,
    onValueChange: (Float) -> Unit,
    onValueChangeFinished: () -> Unit,
    sliderColors: SliderColors,
    isEnabled: Boolean = true
) {
    Column {
        Text(text = "$name: ${valueText(value)}", style = MaterialTheme.typography.bodyMedium)
        Slider(
            value = value,
            onValueChange = onValueChange,
            onValueChangeFinished = onValueChangeFinished,
            valueRange = valueRange,
            enabled = isEnabled,
            colors = sliderColors
        )
    }
}

package com.example.stevejobvn // <-- Giữ nguyên dòng package của bạn nếu khác dòng này

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.util.Log
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.animation.*
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.stevejobvn.ui.theme.StevejobVNTheme
import okhttp3.*
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.JSONObject
import java.io.IOException

// -------------------------------------------------------------
// TRẠNG THÁI ỨNG DỤNG TOÀN CỤC
// -------------------------------------------------------------
object AuthState {
    var isLoggedIn by mutableStateOf(false)
    var accessToken by mutableStateOf("")
    var targetNodeId by mutableStateOf("GXp8RMtKvADiMvtDWnyiT6") // Gợi ý sẵn Node ID từ Mục 9
    var isAuthenticating by mutableStateOf(false)
    var statusText by mutableStateOf("")
}

// -------------------------------------------------------------
// TẦNG DỊCH VỤ RAINMAKER & AWS COGNITO OAUTH2
// -------------------------------------------------------------
object RainMakerService {
    private const val TAG = "RainMaker"
    private val client = OkHttpClient()

    private const val CLIENT_ID = "60i6kac5f9rjuetqnq5mnmaqv6"
    private const val AUTH_URL = "https://auth.rainmaker.espressif.com/oauth2"
    private const val REDIRECT_URI = "rainmaker://com.espressif.rainmaker/success"
    private const val BASE_URL = "https://api.rainmaker.espressif.com/v1"
    private val JSON_MEDIA = "application/json; charset=utf-8".toMediaType()

    val GOOGLE_LOGIN_URL = "$AUTH_URL/authorize?identity_provider=Google&redirect_uri=$REDIRECT_URI&response_type=CODE&client_id=$CLIENT_ID"

    // 1. ĐỔI MÃ CODE LẤY TOKEN THẬT
    fun exchangeOAuthCode(code: String, onResult: (Boolean, String, String) -> Unit) {
        Log.d(TAG, "🔑 Đang gửi mã Code lên AWS Cognito để lấy Token...")

        val formBody = FormBody.Builder()
            .add("grant_type", "authorization_code")
            .add("client_id", CLIENT_ID)
            .add("code", code)
            .add("redirect_uri", REDIRECT_URI)
            .build()

        val request = Request.Builder()
            .url("$AUTH_URL/token")
            .post(formBody)
            .build()

        client.newCall(request).enqueue(object : Callback {
            override fun onFailure(call: Call, e: IOException) {
                Log.e(TAG, "❌ Lỗi mạng: ${e.message}")
                onResult(false, "", "Lỗi mạng: ${e.localizedMessage}")
            }

            override fun onResponse(call: Call, response: Response) {
                val body = response.body?.string() ?: ""
                Log.d(TAG, "📥 Phản hồi AWS Token: HTTP ${response.code}")
                if (response.isSuccessful) {
                    try {
                        val resJson = JSONObject(body)
                        val token = resJson.getString("access_token")
                        Log.i(TAG, "🎉 [THÀNH CÔNG] Đã nhận Access Token!")
                        onResult(true, token, "Đăng nhập Google thành công!")
                    } catch (e: Exception) {
                        onResult(false, "", "Lỗi đọc Token: ${e.message}")
                    }
                } else {
                    Log.e(TAG, "❌ Thất bại: $body")
                    onResult(false, "", "Lỗi xác thực (Mã: ${response.code})")
                }
            }
        })
    }

    // 2. TỰ ĐỘNG DÒ TÌM NODE ID TỪ CLOUD (Chạy ngầm)
    fun fetchNodeIdFromCloud(token: String, onResult: (String?, String) -> Unit) {
        Log.d(TAG, "🔍 Đang quét danh sách Node từ Cloud...")
        val request = Request.Builder()
            .url("$BASE_URL/user/nodes?node_details=true")
            .addHeader("Authorization", token)
            .get()
            .build()

        client.newCall(request).enqueue(object : Callback {
            override fun onFailure(call: Call, e: IOException) {
                Log.e(TAG, "❌ Lỗi kết nối Cloud: ${e.message}")
                onResult(null, e.localizedMessage ?: "Lỗi mạng")
            }

            override fun onResponse(call: Call, response: Response) {
                val body = response.body?.string() ?: ""
                Log.i(TAG, "📦 Danh sách Nodes từ Cloud (HTTP ${response.code}): $body")
                try {
                    val resJson = JSONObject(body)
                    val nodeIds = resJson.optJSONArray("node_ids")
                    val nodeDetails = resJson.optJSONArray("node_details")

                    val foundId = when {
                        nodeIds != null && nodeIds.length() > 0 -> nodeIds.getString(0)
                        nodeDetails != null && nodeDetails.length() > 0 -> nodeDetails.getJSONObject(0).optString("id", nodeDetails.getJSONObject(0).optString("node_id"))
                        else -> null
                    }
                    onResult(foundId, body)
                } catch (e: Exception) {
                    onResult(null, body)
                }
            }
        })
    }

    // 3. GỬI LỆNH ĐIỀU KHIỂN THẬT
    fun updateDeviceParam(token: String, nodeId: String, paramName: String, value: Any, onResult: (Boolean, String) -> Unit) {
        Log.d(TAG, "⚡ Gửi lệnh: $paramName = $value tới Node: $nodeId")
        val payload = JSONObject().apply {
            put("Light", JSONObject().apply { put(paramName, value) })
        }

        val request = Request.Builder()
            .url("$BASE_URL/user/nodes/params?node_id=$nodeId")
            .addHeader("Authorization", token)
            .put(payload.toString().toRequestBody(JSON_MEDIA))
            .build()

        client.newCall(request).enqueue(object : Callback {
            override fun onFailure(call: Call, e: IOException) {
                Log.e(TAG, "❌ Lỗi gửi lệnh: ${e.message}")
                onResult(false, e.localizedMessage ?: "Lỗi kết nối")
            }
            override fun onResponse(call: Call, response: Response) {
                val respBody = response.body?.string() ?: ""
                Log.d(TAG, "Phản hồi điều khiển: HTTP ${response.code} | Chi tiết: $respBody")
                if (!response.isSuccessful) {
                    Log.e(TAG, "❌ Lỗi HTTP ${response.code} từ Cloud: $respBody")
                }
                onResult(response.isSuccessful, respBody)
            }
        })
    }
}

// -------------------------------------------------------------
// MAIN ACTIVITY
// -------------------------------------------------------------
class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        handleIntent(intent)

        setContent {
            StevejobVNTheme {
                Scaffold(modifier = Modifier.fillMaxSize()) { innerPadding ->
                    MainAppFlow(modifier = Modifier.padding(innerPadding))
                }
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        handleIntent(intent)
    }

    private fun handleIntent(intent: Intent?) {
        val uri: Uri? = intent?.data
        if (uri != null && uri.scheme == "rainmaker" && uri.host == "com.espressif.rainmaker") {
            val code = uri.getQueryParameter("code")
            if (!code.isNullOrBlank()) {
                Log.i("RainMaker", "🎯 Nhận mã Code từ Google: $code")

                AuthState.isAuthenticating = true
                AuthState.statusText = "Đang kết nối AWS Cognito..."

                RainMakerService.exchangeOAuthCode(code) { success, token, msg ->
                    runOnUiThread {
                        AuthState.isAuthenticating = false
                        Toast.makeText(this@MainActivity, msg, Toast.LENGTH_SHORT).show()

                        if (success) {
                            AuthState.accessToken = token
                            // 🚀 CHUYỂN MÀN HÌNH NGAY LẬP TỨC!
                            AuthState.isLoggedIn = true

                            // Chạy ngầm quét Node ID tự động
                            RainMakerService.fetchNodeIdFromCloud(token) { detectedId, rawBody ->
                                runOnUiThread {
                                    if (!detectedId.isNullOrBlank()) {
                                        AuthState.targetNodeId = detectedId
                                        Toast.makeText(this@MainActivity, "Đã nhận diện Node: $detectedId", Toast.LENGTH_SHORT).show()
                                    } else {
                                        Log.w("RainMaker", "⚠️ Tài khoản chưa sở hữu Node: $rawBody")
                                        Toast.makeText(this@MainActivity, "Chưa tìm thấy Node trên Cloud!", Toast.LENGTH_SHORT).show()
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
fun MainAppFlow(modifier: Modifier = Modifier) {
    AnimatedContent(
        targetState = AuthState.isLoggedIn,
        label = "ScreenTransition",
        modifier = modifier
    ) { loggedIn ->
        if (!loggedIn) {
            LoginScreen()
        } else {
            DeviceControlScreen(
                token = AuthState.accessToken,
                initialNodeId = AuthState.targetNodeId,
                onLogout = {
                    AuthState.isLoggedIn = false
                    AuthState.accessToken = ""
                }
            )
        }
    }
}

@Composable
fun LoginScreen() {
    val context = LocalContext.current

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(horizontal = 28.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Text(text = "💡", fontSize = 64.sp)
        Spacer(modifier = Modifier.height(8.dp))
        Text(
            text = "ESP RainMaker",
            fontSize = 28.sp,
            fontWeight = FontWeight.Bold,
            color = MaterialTheme.colorScheme.primary
        )
        Text(
            text = "Mục 10.4.3: Đăng Nhập Tài Khoản Google",
            fontSize = 14.sp,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )

        Spacer(modifier = Modifier.height(48.dp))

        if (AuthState.isAuthenticating) {
            CircularProgressIndicator(modifier = Modifier.size(48.dp))
            Spacer(modifier = Modifier.height(16.dp))
            Text(
                text = AuthState.statusText,
                fontSize = 14.sp,
                fontWeight = FontWeight.Medium,
                color = MaterialTheme.colorScheme.primary
            )
        } else {
            Button(
                onClick = {
                    Toast.makeText(context, "Mở trang xác thực Google...", Toast.LENGTH_SHORT).show()
                    val browserIntent = Intent(Intent.ACTION_VIEW, Uri.parse(RainMakerService.GOOGLE_LOGIN_URL))
                    context.startActivity(browserIntent)
                },
                modifier = Modifier
                    .fillMaxWidth()
                    .height(54.dp),
                shape = RoundedCornerShape(14.dp)
            ) {
                Text(
                    text = "🌐  Đăng nhập bằng tài khoản Google",
                    fontSize = 16.sp,
                    fontWeight = FontWeight.Bold
                )
            }

            Spacer(modifier = Modifier.height(16.dp))

            Text(
                text = "Sử dụng tài khoản Gmail bạn đã dùng khi kết nối đèn ở Mục 9.",
                fontSize = 12.sp,
                color = Color.Gray,
                textAlign = TextAlign.Center
            )
        }
    }
}

@Composable
fun DeviceControlScreen(token: String, initialNodeId: String, onLogout: () -> Unit) {
    val context = LocalContext.current
    var isPowerOn by remember { mutableStateOf(false) }
    var brightness by remember { mutableFloatStateOf(80f) }
    var selectedColor by remember { mutableStateOf(Color(0xFFFFC107)) }
    var currentNodeId by remember { mutableStateOf(initialNodeId) }
    val scrollState = rememberScrollState()

    // Tự động kiểm tra danh sách Node từ Cloud khi mở màn hình
    LaunchedEffect(token) {
        RainMakerService.fetchNodeIdFromCloud(token) { detectedId, rawBody ->
            (context as? ComponentActivity)?.runOnUiThread {
                if (!detectedId.isNullOrBlank()) {
                    currentNodeId = detectedId
                    Toast.makeText(context, "Đã nhận diện Node: $detectedId", Toast.LENGTH_SHORT).show()
                } else {
                    Log.w("RainMaker", "Tài khoản hiện tại chưa có Node: $rawBody")
                    Toast.makeText(context, "Tài khoản Google này chưa có Node! Vui lòng kiểm tra Node ID.", Toast.LENGTH_LONG).show()
                }
            }
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(scrollState)
            .padding(20.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        // Thanh tiêu đề & Node ID
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text(text = "Smart Light (ESP32)", fontSize = 20.sp, fontWeight = FontWeight.Bold)
                Text(
                    text = "Cloud Online",
                    fontSize = 13.sp,
                    color = Color(0xFF4CAF50),
                    fontWeight = FontWeight.SemiBold
                )
            }
            TextButton(onClick = onLogout) {
                Text("Đăng xuất", color = MaterialTheme.colorScheme.error)
            }
        }

        Spacer(modifier = Modifier.height(12.dp))

        // Ô cấu hình Node ID
        OutlinedTextField(
            value = currentNodeId,
            onValueChange = { currentNodeId = it },
            label = { Text("Node ID Thiết Bị ESP32") },
            singleLine = true,
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(10.dp)
        )

        Spacer(modifier = Modifier.height(20.dp))

        // Bóng đèn phát sáng
        val lampColor = if (isPowerOn) selectedColor.copy(alpha = (brightness / 100f).coerceIn(0.2f, 1f)) else Color.DarkGray
        Box(
            modifier = Modifier
                .size(120.dp)
                .clip(CircleShape)
                .background(lampColor)
                .shadow(elevation = if (isPowerOn) (brightness / 4).dp else 0.dp, shape = CircleShape),
            contentAlignment = Alignment.Center
        ) {
            Text(text = if (isPowerOn) "💡" else "🌑", fontSize = 50.sp)
        }

        Spacer(modifier = Modifier.height(8.dp))
        Text(
            text = if (isPowerOn) "ĐÈN ĐANG BẬT" else "ĐÈN ĐÃ TẮT",
            fontWeight = FontWeight.Bold,
            color = if (isPowerOn) MaterialTheme.colorScheme.primary else Color.Gray
        )

        Spacer(modifier = Modifier.height(20.dp))

        // 1. Công tắc Bật/Tắt THẬT
        Card(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(16.dp),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(16.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column {
                    Text(text = "Công tắc Nguồn (Power)", fontWeight = FontWeight.SemiBold)
                    Text(text = "Gửi lệnh HTTPS -> MQTT tới ESP32", fontSize = 12.sp, color = Color.Gray)
                }
                Switch(
                    checked = isPowerOn,
                    onCheckedChange = { newState ->
                        isPowerOn = newState
                        RainMakerService.updateDeviceParam(token, currentNodeId.trim(), "Power", newState) { success, errMsg ->
                            (context as? ComponentActivity)?.runOnUiThread {
                                if (success) Toast.makeText(context, "Kit ESP32: Power = $newState", Toast.LENGTH_SHORT).show()
                                else Toast.makeText(context, "Lỗi Cloud: $errMsg", Toast.LENGTH_LONG).show()
                            }
                        }
                    }
                )
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        // 2. Độ sáng THẬT
        Card(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(16.dp),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    Text(text = "Độ sáng (Brightness)", fontWeight = FontWeight.SemiBold)
                    Text(text = "${brightness.toInt()}%", fontWeight = FontWeight.Bold, color = MaterialTheme.colorScheme.primary)
                }
                Slider(
                    value = brightness,
                    onValueChange = { brightness = it },
                    valueRange = 0f..100f,
                    enabled = isPowerOn,
                    onValueChangeFinished = {
                        val valInt = brightness.toInt()
                        RainMakerService.updateDeviceParam(token, currentNodeId.trim(), "Brightness", valInt) { success, errMsg ->
                            (context as? ComponentActivity)?.runOnUiThread {
                                if (success) Toast.makeText(context, "Đã gửi: Brightness = $valInt%", Toast.LENGTH_SHORT).show()
                                else Toast.makeText(context, "Lỗi Cloud: $errMsg", Toast.LENGTH_LONG).show()
                            }
                        }
                    }
                )
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        // 3. Đổi màu RGB THẬT
        Card(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(16.dp),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text(text = "Bảng màu RGB (Hue / Saturation)", fontWeight = FontWeight.SemiBold)
                Spacer(modifier = Modifier.height(12.dp))

                val colors = listOf(
                    Triple(Color(0xFFF44336), 0, "Đỏ"),
                    Triple(Color(0xFFFFC107), 45, "Vàng"),
                    Triple(Color(0xFF4CAF50), 120, "Xanh lá"),
                    Triple(Color(0xFF2196F3), 210, "Xanh dương"),
                    Triple(Color(0xFF9C27B0), 280, "Tím")
                )

                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceAround) {
                    colors.forEach { (c, hueVal, name) ->
                        Box(
                            modifier = Modifier
                                .size(40.dp)
                                .clip(CircleShape)
                                .background(c)
                                .clickable(enabled = isPowerOn) {
                                    selectedColor = c
                                    RainMakerService.updateDeviceParam(token, currentNodeId.trim(), "Hue", hueVal) { success, errMsg ->
                                        (context as? ComponentActivity)?.runOnUiThread {
                                            if (success) Toast.makeText(context, "ESP32 đổi màu: $name", Toast.LENGTH_SHORT).show()
                                            else Toast.makeText(context, "Lỗi Cloud: $errMsg", Toast.LENGTH_LONG).show()
                                        }
                                    }
                                }
                        )
                    }
                }
            }
        }
    }
}

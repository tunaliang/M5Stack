/*
 * Atom S3R + Echo Base 完整固件 v325
 * 包含：WiFi + BLE + 火山引擎 AI（语音+视频）
 */

// ==================== 火山引擎配置 ====================
// 方舟 LLM API
#define VOLCENGINE_API_KEY "897d0d8a-bfed-4bb1-816c-5e641d13e0dc"

// 实时语音 API 配置
#define REALTIME_APP_ID "5851838615"
#define REALTIME_ACCESS_TOKEN "LrZu7QiiznkoNeyN-800OkSkOR3RH40V"
#define REALTIME_RESOURCE_ID "volc.speech.dialog"
#define REALTIME_APP_KEY "PlgvMymc7f3tQnJ6"

// NTP 服务器
#define NTP_SERVER "pool.ntp.org"
#define NTP_TIMEZONE 8  // 北京时间 UTC+8

#include <M5Unified.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLEClient.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <time.h>
#include <esp_camera.h>
#include <base64.h>

// ==================== 摄像头引脚配置 (AtomS3R-M12 OV3660) ====================
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  21
#define SIOD_GPIO_NUM  12   // CAM_SDA
#define SIOC_GPIO_NUM  9    // CAM_SCL
#define Y2_GPIO_NUM    3
#define Y3_GPIO_NUM    42
#define Y4_GPIO_NUM    46
#define Y5_GPIO_NUM    48
#define Y6_GPIO_NUM    4
#define Y7_GPIO_NUM    17
#define Y8_GPIO_NUM    11
#define Y9_GPIO_NUM    13
#define VSYNC_GPIO_NUM 10
#define HREF_GPIO_NUM  14
#define PCLK_GPIO_NUM  40
#define POWER_GPIO_NUM 18   // POWER_N

// 摄像头初始化 (AtomS3R-M12)
bool initCamera() {
  // 开启摄像头电源
  pinMode(POWER_GPIO_NUM, OUTPUT);
  digitalWrite(POWER_GPIO_NUM, LOW);
  delay(500);
  
  // 停止已有的 I2C 并卸载驱动
  Wire.end();
  delay(200);
  
  // 重新初始化 I2C
  Wire.begin(12, 9, 400000);  // SDA=12, SCL=9
  delay(100);
  
  camera_config_t config;
  config.ledc_timer = LEDC_TIMER_0;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  
  // AtomS3R-M12 专用配置
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA;  // 320x240 - 减小以适应网络
  config.jpeg_quality = 12;
  config.fb_count = 2;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.sccb_i2c_port = 0;  // 使用 I2C 端口 0
  
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: %d\n", err);
    return false;
  }
  
  Serial.println("Camera initialized!");
  return true;
}

// ==================== 配置 ====================
const char* ssid = "MarsSignal";
const char* password = "2w8fjpt8fkzngtx";

#define LED_PIN 18

enum LedState {
  LED_CONNECTING,
  LED_CONNECTED,
  LED_NORMAL
};

LedState currentLedState = LED_CONNECTING;
unsigned long ledToggleTime = 0;
bool ledOn = false;

const unsigned long LED_CONNECTING_INTERVAL = 1000;
const unsigned long LED_NORMAL_PERIOD = 5000;
const unsigned long LED_NORMAL_OFF = 1000;

#define BLE_DEVICE_NAME "S3R-M12"
#define SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_TX "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_RX "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define SLAVE_DEVICE_NAME "Doggy"  // 从机名称

// BLE Peripheral (主机模式)
BLEServer *pServer = NULL;
BLEService *pService = NULL;
BLECharacteristic *pTxCharacteristic = NULL;
BLECharacteristic *pRxCharacteristic = NULL;
bool deviceConnected = false;

bool bleInitialized = false;
bool wifiConnected = false;

unsigned long lastBleSendTime = 0;
const unsigned long BLE_SEND_INTERVAL = 5000;

// AI 测试
unsigned long lastAiTestTime = 0;
bool aiTestDone = false;

class ServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("BLE Connected");
    };
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("BLE Disconnected");
      delay(500);
      pServer->startAdvertising();
    }
};

// 函数声明
void connectWiFi();
void initBLE();
void updateLED();
void sendBleTime();
void scanAndConnectToSlave();
String volcanoLLM(const char* prompt);
void volcanoTTS(const char* text);
String volcanoVision(const uint8_t* imageData, size_t imageSize);
String volcanoASR();
void volcanoVoiceChat(const char* text);
void takePictureAndAnalyze();

// BLE Central 全局变量
BLEScan* pBLEScan = NULL;
BLEClient* pClient = NULL;
BLERemoteService* pRemoteService = NULL;
BLERemoteCharacteristic* pRemoteTxChar = NULL;
BLERemoteCharacteristic* pRemoteRxChar = NULL;
bool slaveConnected = false;
unsigned long lastSlaveScanTime = 0;
const unsigned long SLAVE_SCAN_INTERVAL = 10000; // 每10秒扫描一次

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n========== Atom S3R 启动 ==========");
  Serial.flush();
  
  // 先测试WiFi
  Serial.println("Testing WiFi...");
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();
  Serial.printf("Found %d networks\n", n);
  
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // 先初始化摄像头 (在 M5.begin() 之前)
  Serial.println("Initializing camera FIRST...");
  Serial.flush();
  if (initCamera()) {
    Serial.println("Camera OK!");
    Serial.flush();
  } else {
    Serial.println("Camera FAILED (continuing anyway)");
    Serial.flush();
  }
  
  // M5Unified 配置 - 启用 ATOMIC Echo Base
  auto cfg = M5.config();
  cfg.external_speaker.atomic_spk = true;  // ATOMIC Speaker
  M5.begin(cfg);
  
  // 等待扬声器初始化
  delay(100);
  
  // 检查并配置扬声器
  Serial.printf("Speaker enabled: %d\n", M5.Speaker.isEnabled());
  Serial.printf("Speaker playing: %d\n", M5.Speaker.isPlaying());
  
  if (M5.Speaker.isEnabled()) {
    // 设置高音量
    M5.Speaker.setVolume(255);
    M5.Speaker.setAllChannelVolume(255);
    
    // 播放不同频率测试
    Serial.println("Playing 1000Hz...");
    M5.Speaker.tone(1000, 300);
    delay(400);
    
    Serial.println("Playing 2000Hz...");
    M5.Speaker.tone(2000, 300);
    delay(400);
    
    Serial.println("Playing 500Hz...");
    M5.Speaker.tone(500, 500);
    delay(600);
    
    Serial.println("Beep done");
  } else {
    Serial.println("Speaker NOT available!");
  }
  
  // 拍照测试 (在初始化时已经做了)
  // takePictureAndAnalyze();
  
  connectWiFi();
  initBLE();
  
  Serial.println("========== 初始化完成 ==========");
  
  // 标记 AI 测试尚未完成
  aiTestDone = false;
  lastAiTestTime = millis();
}

void loop() {
  M5.update();
  
  // 按键 BtnA 触发拍照
  if (M5.BtnA.wasPressed()) {
    Serial.println("=== Button A pressed - Taking picture ===");
    takePictureAndAnalyze();
  }
  
  // 串口命令 'p' 触发拍照
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'p' || cmd == 'P') {
      Serial.println("=== Serial trigger - Taking picture ===");
      takePictureAndAnalyze();
    }
  }
  
  unsigned long now = millis();
  updateLED();
  
  if (now % 5000 < 20) {
    if (WiFi.status() == WL_CONNECTED) {
      if (!wifiConnected) {
        wifiConnected = true;
        currentLedState = LED_CONNECTED;
        Serial.println("WiFi Connected: " + WiFi.localIP().toString());
      }
    } else {
      if (wifiConnected || currentLedState != LED_CONNECTING) {
        wifiConnected = false;
        currentLedState = LED_CONNECTING;
        Serial.println("WiFi Disconnected");
        WiFi.reconnect();
      }
    }
  }
  
  if (wifiConnected && currentLedState == LED_CONNECTED && millis() > 10000) {
    currentLedState = LED_NORMAL;
  }
  
  // BLE 周期性发送时间给已连接的从机
  if (bleInitialized && (now - lastBleSendTime >= BLE_SEND_INTERVAL)) {
    Serial.printf("BLE: init=%d, conn=%d, interval=%lu\n", 
                 bleInitialized, deviceConnected, now - lastBleSendTime);
    lastBleSendTime = now;
    sendBleTime();
  }
  
  // 后台 AI 测试 (不阻塞 BLE)
  if (!aiTestDone && wifiConnected && millis() > 15000) {
    aiTestDone = true;
    Serial.println("\n=== 后台测试 LLM ===");
    String response = volcanoLLM("你好，请用一句话介绍自己");
    Serial.println("LLM Response: " + response);
    
    delay(2000);
    Serial.println("\n=== 后台测试实时语音 ===");
    volcanoVoiceChat("你好，请介绍一下自己");
  }
  
  delay(10);
}

void updateLED() {
  unsigned long now = millis();
  
  switch (currentLedState) {
    case LED_CONNECTING:
      if (now - ledToggleTime >= LED_CONNECTING_INTERVAL) {
        ledToggleTime = now;
        ledOn = !ledOn;
        digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
      }
      break;
    case LED_CONNECTED:
      digitalWrite(LED_PIN, HIGH);
      ledOn = true;
      break;
    case LED_NORMAL:
      {
        unsigned long cyclePos = now % LED_NORMAL_PERIOD;
        digitalWrite(LED_PIN, (cyclePos < LED_NORMAL_OFF) ? LOW : HIGH);
      }
      break;
  }
}

// NTP 时间同步
void syncNTP() {
  Serial.println("同步 NTP 时间...");
  configTime(NTP_TIMEZONE * 3600, 0, NTP_SERVER);
  
  struct tm timeinfo;
  int retries = 0;
  while (retries < 10 && getLocalTime(&timeinfo) == 0) {
    Serial.println("等待 NTP...");
    delay(500);
    retries++;
  }
  
  if (getLocalTime(&timeinfo)) {
    Serial.println("NTP 时间同步成功!");
    Serial.printf("时间: %04d-%02d-%02d %02d:%02d:%02d\n",
                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  } else {
    Serial.println("NTP 时间同步失败");
  }
}

void connectWiFi() {
  Serial.println("连接 WiFi...");
  Serial.println("SSID: " + String(ssid));
  
  WiFi.mode(WIFI_STA);
  
  // 先扫描 WiFi 网络
  Serial.println("Scanning WiFi...");
  int n = WiFi.scanNetworks();
  Serial.printf("Found %d networks\n", n);
  for (int i = 0; i < n; i++) {
    Serial.printf("%d: %s (%d)\n", i, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
  }
  
  Serial.println("Connecting to: " + String(ssid));
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    Serial.print(WiFi.status());
    Serial.print(" ");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    currentLedState = LED_CONNECTED;
    Serial.println("\nWiFi OK: " + WiFi.localIP().toString());
    
    // 同步 NTP 时间（用于 SSL 证书验证）
    syncNTP();
  } else {
    Serial.println("\nWiFi Failed");
    currentLedState = LED_CONNECTING;
  }
}

void initBLE() {
  Serial.println("Starting BLE...");
  
  BLEDevice::init(BLE_DEVICE_NAME);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());
  
  pService = pServer->createService(SERVICE_UUID);
  
  pTxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_TX,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pTxCharacteristic->addDescriptor(new BLE2902());
  
  pRxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_RX,
    BLECharacteristic::PROPERTY_WRITE
  );
  
  pService->start();
  
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  
  // 初始化 BLE Central (扫描从机)
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  Serial.println("BLE Central initialized, will scan for slave...");
  
  bleInitialized = true;
  Serial.println("BLE OK: " + String(BLE_DEVICE_NAME));
}

void sendBleTime() {
  if (!bleInitialized) return;
  
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  
  char timeStr[64];
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
  
  char msg[128];
  if (WiFi.status() == WL_CONNECTED) {
    snprintf(msg, sizeof(msg), "Time: %s\nIP: %s\nWiFi: OK", 
             timeStr, WiFi.localIP().toString().c_str());
  } else {
    snprintf(msg, sizeof(msg), "Time: %s\nWiFi: Disconnected", timeStr);
  }
  
  if (deviceConnected && pTxCharacteristic) {
    pTxCharacteristic->setValue((uint8_t*)msg, strlen(msg));
    pTxCharacteristic->notify();
  }
}

// BLE Central - 扫描并连接从机
void scanAndConnectToSlave() {
  if (!bleInitialized || slaveConnected) return;
  
  Serial.println("Scanning for BLE slave...");
  BLEScan* pScan = BLEDevice::getScan();
  pScan->setActiveScan(true);
  BLEDevice::getScan()->start(5);  // 扫描5秒
  
  Serial.println("Scan complete, checking results...");
  
  // 简化处理：每次扫描后尝试连接
  if (!slaveConnected) {
    Serial.println("No slave connected yet, will retry next time");
  }
}

// ==================== 火山引擎 AI ====================

#include <WiFiClientSecure.h>

// 语音合成 (TTS)
void volcanoTTS(const char* text) {
  if (!wifiConnected) {
    Serial.println("WiFi not connected, cannot use TTS");
    return;
  }
  
  if (!M5.Speaker.isEnabled()) {
    Serial.println("Speaker not available");
    return;
  }
  
  Serial.println("Volcano TTS: " + String(text));
  
  WiFiClientSecure client;
  client.setInsecure();
  
  if (!client.connect("openspeech.bytedance.com", 443)) {
    Serial.println("TTS connection failed");
    return;
  }
  
  String request = "POST /api/v2/tts HTTP/1.1\r\n";
  request += "Host: openspeech.bytedance.com\r\n";
  request += "Content-Type: application/json\r\n";
  request += "Authorization: Bearer " + String(VOLCENGINE_API_KEY) + "\r\n";
  
  // 使用合适的模型 - 火山引擎语音 API
  String body = "{\"text\":\"" + String(text) + "\",\"voice_settings\":{\"speed\":1.0,\"pitch\":0}}";
  
  request += "Content-Length: " + String(body.length()) + "\r\n";
  request += "\r\n";
  request += body;
  
  Serial.println("TTS Request sent");
  client.print(request);
  
  // 等待响应
  delay(1000);
  
  // 跳过 HTTP 头
  bool headersDone = false;
  while (client.available()) {
    String line = client.readStringUntil('\n');
    // 检查 Content-Type 确定是否为音频
    if (line.indexOf("audio") >= 0) {
      Serial.println("Audio content detected");
    }
    if (line.length() <= 1) {
      headersDone = true;
      break;
    }
  }
  
  // 读取音频数据
  if (client.available()) {
    Serial.println("Receiving audio data...");
    
    const int bufferSize = 8192;
    static uint8_t audioBuffer[bufferSize];
    int bytesRead = 0;
    
    while (client.available() && bytesRead < bufferSize) {
      int available = client.available();
      int toRead = min(available, bufferSize - bytesRead);
      int readLen = client.readBytes(audioBuffer + bytesRead, toRead);
      if (readLen > 0) {
        bytesRead += readLen;
      }
    }
    
    Serial.printf("Received %d bytes of audio\n", bytesRead);
    
    // 播放音频 - 使用 playRaw
    if (bytesRead > 100) {  // 确保有足够的数据
      M5.Speaker.setVolume(200);
      M5.Speaker.playRaw(audioBuffer, bytesRead, 24000, true);
      Serial.println("Playing audio...");
    }
  }
  
  client.stop();
  Serial.println("TTS done");
}

// 大模型对话 (LLM)
String volcanoLLM(const char* prompt) {
  Serial.println("=== Volcano LLM Start ===");
  Serial.printf("WiFi status: %d\n", WiFi.status());
  Serial.printf("WiFi connected: %s\n", wifiConnected ? "YES" : "NO");
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, cannot use LLM");
    return "WiFi not connected";
  }
  
  Serial.println("WiFi IP: " + WiFi.localIP().toString());
  Serial.println("Volcano LLM: " + String(prompt));
  
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(60);
  
  // 不等待 keep-alive
  client.setNoDelay(true);
  
  Serial.println("Connecting to ark.cn-beijing.volces.com:443...");
  
  // 先等待一下网络稳定
  delay(500);
  
  // 使用域名连接
  if (!client.connect("ark.cn-beijing.volces.com", 443)) {
    Serial.println("LLM connection failed");
    return "Connection failed";
  }
  
  Serial.println("Connected to API!");
  
  // 等待 SSL 握手
  delay(500);
  
  String request = "POST /api/v3/responses HTTP/1.1\r\n";
  request += "Host: ark.cn-beijing.volces.com\r\n";
  request += "Content-Type: application/json\r\n";
  request += "Authorization: Bearer " + String(VOLCENGINE_API_KEY) + "\r\n";
  
  // 构建请求体 - 支持多模态输入
  String body = "{\"model\":\"doubao-seed-1-8-251228\",\"input\":[{\"role\":\"user\",\"content\":[{\"type\":\"input_text\",\"text\":\"";
  
  // 转义文本中的特殊字符
  String escapedPrompt = String(prompt);
  escapedPrompt.replace("\\", "\\\\");
  escapedPrompt.replace("\"", "\\\"");
  escapedPrompt.replace("\n", "\\n");
  escapedPrompt.replace("\r", "\\r");
  escapedPrompt.replace("\t", "\\t");
  
  body += escapedPrompt;
  body += "\"}]}]}";
  
  request += "Content-Length: " + String(body.length()) + "\r\n";
  request += "\r\n";
  request += body;
  
  Serial.println("Request: " + body);
  Serial.println("Sending request...");
  
  // 发送请求
  size_t written = client.print(request);
  Serial.printf("Written %d bytes\n", written);
  client.flush();
  
  // 等待响应头
  Serial.println("Waiting for response...");
  delay(5000);
  
  // 检查是否有数据
  int available = client.available();
  Serial.printf("Available bytes: %d\n", available);
  
  // 尝试先读取状态行
  String statusLine = client.readStringUntil('\n');
  Serial.println("Status: " + statusLine);
  
  String response = "";
  
  // 读取响应
  int readBytes = 0;
  int maxBytes = 10000;
  unsigned long readStart = millis();
  
  // 先读取 HTTP 头
  while (client.available() && millis() - readStart < 5000) {
    String line = client.readStringUntil('\n');
    line.trim();
    Serial.println("Header: " + line);
    if (line.length() == 0) {
      break;  // 空行表示头结束
    }
  }
  
  Serial.println("Reading body...");
  
  // 然后读取 body
  while (client.available() && readBytes < maxBytes && millis() - readStart < 15000) {
    int b = client.read();
    if (b >= 0) {
      response += (char)b;
      readBytes++;
    }
    if (readBytes % 500 == 0 && readBytes <= 2000) {
      Serial.printf("Read %d bytes...\n", readBytes);
    }
  }
  
  client.stop();
  
  Serial.printf("Total response length: %d\n", response.length());
  
  // 打印响应（截取前500字符调试）
  if (response.length() > 0) {
    Serial.println("Response: " + response.substring(0, min(500, (int)response.length())));
  } else {
    Serial.println("Response is empty!");
    return "Empty response";
  }
  
  // 解析 JSON - 提取 output[].content[].text
  // 响应格式: {"output":[{"type":"message","content":[{"type":"output_text","text":"..."}]}]}
  int textStart = response.indexOf("\"type\":\"output_text\"");
  if (textStart > 0) {
    // 找到 text 字段
    textStart = response.indexOf("\"text\":\"", textStart);
    if (textStart > 0) {
      textStart += 8;  // 跳过 "text":" 
      int textEnd = response.indexOf("\"", textStart);
      if (textEnd > textStart) {
        String result = response.substring(textStart, textEnd);
        // 处理转义字符
        result.replace("\\n", "\n");
        result.replace("\\\"", "\"");
        result.replace("\\\\", "\\");
        Serial.println("Parsed result: " + result);
        return result;
      }
    }
  }
  
  // 备用：尝试直接查找 text
  textStart = response.indexOf("\"text\":\"");
  if (textStart > 0) {
    textStart += 8;
    int textEnd = response.indexOf("\"", textStart);
    if (textEnd > textStart) {
      String result = response.substring(textStart, textEnd);
      result.replace("\\n", "\n");
      Serial.println("Fallback parsed: " + result);
      return result;
    }
  }
  
  Serial.println("Failed to parse response");
  return response.substring(0, min(200, (int)response.length()));
}

// 拍照并发送到火山引擎识别
void takePictureAndAnalyze() {
  Serial.println("=== Taking picture for Vision ===");
  
  // 拍照
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    // 尝试重新初始化
    Serial.println("Retrying camera init...");
    esp_camera_deinit();
    delay(100);
    if (initCamera()) {
      Serial.println("Camera re-initialized!");
      fb = esp_camera_fb_get();
      if (!fb) {
        Serial.println("Still failed after reinit");
        return;
      }
    } else {
      Serial.println("Re-init failed");
      return;
    }
  }
  
  Serial.printf("Captured image: %dx%d, %d bytes\n", fb->width, fb->height, fb->len);
  
  // 发送到火山引擎视觉理解
  if (wifiConnected) {
    Serial.println("Sending to Volcano Vision API...");
    
    // 计算 base64 编码后的长度
    int base64Len = ((fb->len + 2) / 3) * 4;
    Serial.printf("Base64 length will be: %d\n", base64Len);
    
    // 由于内存限制，只发送小图
    // 直接使用原始图片（ESP32-S3 有 8MB PSRAM 应该够用）
    
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(60);
    
    if (!client.connect("ark.cn-beijing.volces.com", 443)) {
      Serial.println("Vision API connection failed");
      esp_camera_fb_return(fb);
      return;
    }
    
    Serial.println("Connected to Vision API!");
    
    // 等待 SSL 握手
    delay(500);
    
    // 构建多模态请求 - 发送图片
    String request = "POST /api/v3/chat/completions HTTP/1.1\r\n";
    request += "Host: ark.cn-beijing.volces.com\r\n";
    request += "Content-Type: application/json\r\n";
    request += "Authorization: Bearer " + String(VOLCENGINE_API_KEY) + "\r\n";
    
    // 构建 JSON 请求体 - 包含图片
    // 使用 messages + image_url 格式
    Serial.println("Encoding image to base64...");
    String imgBase64 = base64::encode(fb->buf, fb->len);
    Serial.printf("Base64 encoded length: %d\n", imgBase64.length());
    
    // 使用 base64 编码的摄像头图片
    String imgDataUrl = "data:image/jpeg;base64," + imgBase64;
    
    // 发送真正的图片 URL
    String body = "{\"model\":\"doubao-seed-1-8-251228\",\"messages\":[{\"content\":[";
    body += "{\"image_url\":{\"url\":\"" + imgDataUrl + "\"},\"type\":\"image_url\"},";
    body += "{\"text\":\"图片主要讲了什么?\",\"type\":\"text\"}";
    body += "],\"role\":\"user\"}]}";
    
    request += "Content-Length: " + String(body.length()) + "\r\n";
    request += "Connection: close\r\n";
    request += "\r\n";
    request += body;
    
    Serial.println("Sending Vision request...");
    Serial.printf("Request size: %d bytes\n", request.length());
    Serial.println("Request preview: " + request.substring(0, 200));
    
    int sent = client.print(request);
    client.flush();
    Serial.printf("Sent %d bytes\n", sent);
    
    // 等待响应头 - 轮询等待
    Serial.println("Waiting for Vision response...");
    int waitCount = 0;
    while (waitCount < 200) {  // 最多等待20秒
        if (client.available()) break;
        delay(100);
        waitCount++;
    }
    Serial.printf("Vision wait count: %d\n", waitCount);
    
    // 检查是否有数据
    int available = client.available();
    Serial.printf("Vision Available bytes: %d\n", available);
    
    // 尝试先读取状态行
    String statusLine = client.readStringUntil('\n');
    Serial.println("Vision Status: " + statusLine);
    
    String response = "";
    
    // 读取响应
    // 先读取 HTTP 头
    while (client.available()) {
      String line = client.readStringUntil('\n');
      line.trim();
      Serial.println("Vision Header: " + line);
      if (line.length() == 0) {
        break;  // 空行表示头结束
      }
    }
    
    Serial.println("Reading Vision body...");
    
    // 然后读取 body
    int maxBytes = 15000;
    int readBytes = 0;
    unsigned long readStart = millis();
    
    while (client.available() && readBytes < maxBytes && millis() - readStart < 20000) {
      int b = client.read();
      if (b >= 0) {
        response += (char)b;
        readBytes++;
      }
    }
    
    Serial.printf("Vision Response length: %d\n", response.length());
    Serial.println("Vision Response: " + response.substring(0, 500));
    
    // 解析 JSON 提取文本结果 - /chat/completions 格式是 message.content
    int contentStart = response.indexOf("\"content\":\"");
    if (contentStart > 0) {
      contentStart += 11;
      int contentEnd = response.indexOf("\"", contentStart);
      if (contentEnd > contentStart) {
        String result = response.substring(contentStart, contentEnd);
        Serial.println("=== Vision Result ===");
        Serial.println(result);
      }
    } else {
      Serial.println("No text found in response");
    }
  }
  
  esp_camera_fb_return(fb);
}

// 图像识别 - 发送到 AI 分析 (占位)
String volcanoVision(const uint8_t* imageData, size_t imageSize) {
  if (!wifiConnected) {
    Serial.println("WiFi not connected, cannot use Vision");
    return "WiFi not connected";
  }
  
  Serial.println("Volcano Vision: analyzing image...");
  
  // 调用拍照识别
  takePictureAndAnalyze();
  
  return "Vision analysis done";
}

// 语音识别 (ASR)
String volcanoASR() {
  if (!wifiConnected) {
    Serial.println("WiFi not connected, cannot use ASR");
    return "WiFi not connected";
  }
  
  if (!M5.Mic.isEnabled()) {
    Serial.println("Microphone not available");
    return "Microphone not available";
  }
  
  Serial.println("Volcano ASR: Recording audio...");
  
  // 录制音频
  const int sampleRate = 16000;
  const int recordDuration = 3000; // 3秒
  const int samples = sampleRate * recordDuration / 1000;
  
  // 分配缓冲区
  int16_t* buffer = (int16_t*)malloc(samples * sizeof(int16_t));
  if (!buffer) {
    Serial.println("Failed to allocate buffer");
    return "Memory error";
  }
  
  // 录制
  M5.Mic.begin();
  delay(100);
  
  Serial.println("Recording...");
  int recorded = M5.Mic.record(buffer, samples, sampleRate);
  Serial.printf("Recorded %d samples\n", recorded);
  
  Serial.println("Sending to ASR...");
  
  WiFiClientSecure client;
  client.setInsecure();
  
  // 使用正确的 ASR API 端点
  if (!client.connect("openspeech.bytedance.com", 443)) {
    Serial.println("ASR connection failed");
    free(buffer);
    return "Connection failed";
  }
  
  // 构建请求 - 使用 query 方式
  String request = "POST /api/v2/asr?format=wav&rate=16000&channel=1 HTTP/1.1\r\n";
  request += "Host: openspeech.bytedance.com\r\n";
  request += "Content-Type: audio/wav\r\n";
  request += "Authorization: Bearer " + String(VOLCENGINE_API_KEY) + "\r\n";
  
  // 发送二进制数据
  client.print(request);
  client.printf("Content-Length: %d\r\n\r\n", recorded * sizeof(int16_t));
  
  // 发送音频数据
  client.write((uint8_t*)buffer, recorded * sizeof(int16_t));
  
  free(buffer);
  
  // 读取响应
  delay(2000);
  String response = "";
  bool headersDone = false;
  
  while (client.available()) {
    String line = client.readStringUntil('\n');
    if (line.length() <= 1) {
      headersDone = true;
      continue;
    }
    if (headersDone) {
      response += line;
    }
  }
  
  client.stop();
  
  Serial.println("ASR response: " + response);
  
  // 解析 JSON 提取文本
  int textStart = response.indexOf("\"text\":\"");
  if (textStart > 0) {
    textStart += 8;
    int textEnd = response.indexOf("\"", textStart);
    if (textEnd > textStart) {
      return response.substring(textStart, textEnd);
    }
  }
  
  return response;
}

// ==================== 火山引擎实时语音 API ====================

#include <WiFiClientSecure.h>

class RealtimeVoice {
private:
  WiFiClientSecure client;
  bool wsConnected = false;
  bool sessionStarted = false;
  
  // 生成 WebSocket 握手 key
  String generateWSKey() {
    String key = "1234567890abcdef";
    char base64[32];
    // 简化：使用固定 key（服务端可能接受）
    return "dGhlIHNhbXBsZSBsYW5k";  // "the sample land" base64
  }
  
  // SHA1 用于 WebSocket 握手
  String sha1Base64(const String& input) {
    // 简化的响应 key
    return "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";
  }
  
  // 发送 WebSocket 帧
  void sendWSFrame(const uint8_t* data, size_t len, uint8_t opcode) {
    if (!wsConnected) return;
    
    uint8_t header[14];
    header[0] = 0x80 | opcode;  // FIN + opcode
    
    if (len < 126) {
      header[1] = len;
      client.write(header, 2);
    } else if (len < 65536) {
      header[1] = 126;
      header[2] = (len >> 8) & 0xFF;
      header[3] = len & 0xFF;
      client.write(header, 4);
    } else {
      header[1] = 127;
      // 8 字节长度（简化）
      header[2] = 0;
      header[3] = 0;
      header[4] = 0;
      header[5] = 0;
      header[6] = (len >> 24) & 0xFF;
      header[7] = (len >> 16) & 0xFF;
      header[8] = (len >> 8) & 0xFF;
      header[9] = len & 0xFF;
      client.write(header, 10);
    }
    
    // 生成 mask（服务端帧不需要 mask）
    client.write(data, len);
  }
  
  // 发送纯文本 JSON 帧
  void sendTextJson(const String& json) {
    if (!wsConnected) return;
    
    delay(500);
    
    // 生成 4 字节 mask key
    uint32_t maskKey = random(0xFFFFFFFF);
    
    size_t len = json.length();
    uint8_t maskedData[512];
    const uint8_t* src = (const uint8_t*)json.c_str();
    for (size_t i = 0; i < len; i++) {
      maskedData[i] = src[i] ^ ((maskKey >> ((i % 4) * 8)) & 0xFF);
    }
    
    // WebSocket 文本帧
    uint8_t wsHeader[10];
    wsHeader[0] = 0x81;  // FIN + text
    
    size_t headerLen = 2;
    if (len < 126) {
      wsHeader[1] = 0x80 | (len & 0x7F);
    } else {
      wsHeader[1] = 0x80 | 126;
      wsHeader[2] = (len >> 8) & 0xFF;
      wsHeader[3] = len & 0xFF;
      headerLen = 4;
    }
    
    client.write(wsHeader, headerLen);
    
    // mask key
    uint8_t maskBytes[4];
    maskBytes[0] = (maskKey >> 24) & 0xFF;
    maskBytes[1] = (maskKey >> 16) & 0xFF;
    maskBytes[2] = (maskKey >> 8) & 0xFF;
    maskBytes[3] = maskKey & 0xFF;
    client.write(maskBytes, 4);
    client.write(maskedData, len);
    
    Serial.printf("Sent text JSON, len=%d\n", len);
  }
  
  // 发送二进制协议事件
  void sendBinaryEvent(uint32_t eventId, const String& json, const String& sessionId) {
    if (!wsConnected) return;
    
    delay(500);
    
    // 生成 4 字节 mask key
    uint32_t maskKey = random(0xFFFFFFFF);
    
    // 协议头 (4 bytes)
    // Byte 0: version=7 (4bit), headerSize=1 (4bit) = 0x71
    // 改成 version=1
    // Byte 0: version=1 (4bit), headerSize=1 (4bit) = 0x11
    // Byte 1: msgType=1 (Full-client, 4bit), flags=0 = 0x10
    // Byte 2: serialization=1 (JSON, 4bit), compression=0 = 0x10
    // Byte 3: reserved = 0x00
    uint8_t protoHeader[4] = {0x11, 0x10, 0x10, 0x00};
    
    // 可选字段: event_id + session_id
    uint8_t optional[32];
    int optIdx = 0;
    
    // event_id (4 bytes, big endian)
    optional[optIdx++] = (eventId >> 24) & 0xFF;
    optional[optIdx++] = (eventId >> 16) & 0xFF;
    optional[optIdx++] = (eventId >> 8) & 0xFF;
    optional[optIdx++] = eventId & 0xFF;
    
    // session_id_size (4 bytes, big endian)
    uint32_t sidLen = sessionId.length();
    optional[optIdx++] = (sidLen >> 24) & 0xFF;
    optional[optIdx++] = (sidLen >> 16) & 0xFF;
    optional[optIdx++] = (sidLen >> 8) & 0xFF;
    optional[optIdx++] = sidLen & 0xFF;
    
    // 构建完整 payload
    size_t totalPayloadLen = 4 + optIdx + sidLen + json.length();
    
    uint8_t* maskedPayload = (uint8_t*)malloc(totalPayloadLen);
    if (!maskedPayload) return;
    
    memcpy(maskedPayload, protoHeader, 4);
    memcpy(maskedPayload + 4, optional, optIdx);
    if (sidLen > 0) {
      memcpy(maskedPayload + 4 + optIdx, sessionId.c_str(), sidLen);
    }
    memcpy(maskedPayload + 4 + optIdx + sidLen, json.c_str(), json.length());
    
    // mask
    for (size_t i = 0; i < totalPayloadLen; i++) {
      maskedPayload[i] ^= ((maskKey >> ((i % 4) * 8)) & 0xFF);
    }
    
    // WebSocket 帧
    uint8_t wsHeader[14];
    wsHeader[0] = 0x82;
    size_t headerLen = 2;
    
    if (totalPayloadLen < 126) {
      wsHeader[1] = 0x80 | (totalPayloadLen & 0x7F);
    } else {
      wsHeader[1] = 0x80 | 126;
      wsHeader[2] = (totalPayloadLen >> 8) & 0xFF;
      wsHeader[3] = totalPayloadLen & 0xFF;
      headerLen = 4;
    }
    
    client.write(wsHeader, headerLen);
    
    // mask key
    uint8_t maskBytes[4];
    maskBytes[0] = (maskKey >> 24) & 0xFF;
    maskBytes[1] = (maskKey >> 16) & 0xFF;
    maskBytes[2] = (maskKey >> 8) & 0xFF;
    maskBytes[3] = maskKey & 0xFF;
    client.write(maskBytes, 4);
    
    // payload
    client.write(maskedPayload, totalPayloadLen);
    
    free(maskedPayload);
    
    Serial.printf("Sent event %d, payload=%d\n", eventId, totalPayloadLen);
  }
  
  // 接收 WebSocket 帧
  int recvWSFrame(uint8_t* buffer, size_t maxLen) {
    if (!client.available()) return -1;
    
    uint8_t header[2];
    if (client.read(header, 2) != 2) return -1;
    
    bool fin = header[0] & 0x80;
    uint8_t opcode = header[0] & 0x0F;
    bool masked = header[1] & 0x80;
    uint64_t payloadLen = header[1] & 0x7F;
    
    if (payloadLen == 126) {
      uint8_t ext[2];
      client.read(ext, 2);
      payloadLen = (ext[0] << 8) | ext[1];
    } else if (payloadLen == 127) {
      uint8_t ext[8];
      client.read(ext, 8);
      payloadLen = 0;
      for (int i = 0; i < 8; i++) {
        payloadLen = (payloadLen << 8) | ext[i];
      }
    }
    
    // 读取 mask key
    uint8_t maskKey[4];
    if (masked) {
      client.read(maskKey, 4);
    }
    
    // 读取 payload
    size_t toRead = min((size_t)payloadLen, maxLen);
    size_t read = client.read(buffer, toRead);
    
    // unmask
    if (masked) {
      for (size_t i = 0; i < read; i++) {
        buffer[i] ^= maskKey[i % 4];
      }
    }
    
    return read;
  }
  
public:
  bool begin() {
    if (!wifiConnected) {
      Serial.println("WiFi not connected");
      return false;
    }
    
    Serial.println("Connecting to realtime voice API...");
    
    client.setInsecure();
    client.setTimeout(30);
    
    if (!client.connect("openspeech.bytedance.com", 443)) {
      Serial.println("TCP connection failed");
      return false;
    }
    Serial.println("TCP connected");
    
    // WebSocket 握手
    String wsKey = generateWSKey();
    
    String request = "GET /api/v3/realtime/dialogue HTTP/1.1\r\n";
    request += "Host: openspeech.bytedance.com\r\n";
    request += "Upgrade: websocket\r\n";
    request += "Connection: upgrade\r\n";
    request += "Sec-WebSocket-Key: " + wsKey + "\r\n";
    request += "Sec-WebSocket-Version: 13\r\n";
    request += "X-Api-App-ID: " + String(REALTIME_APP_ID) + "\r\n";
    request += "X-Api-Access-Key: " + String(REALTIME_ACCESS_TOKEN) + "\r\n";
    request += "X-Api-Resource-Id: " + String(REALTIME_RESOURCE_ID) + "\r\n";
    request += "X-Api-App-Key: " + String(REALTIME_APP_KEY) + "\r\n";
    request += "\r\n";
    
    Serial.println("Sending WS handshake...");
    
    // 确保请求发送出去
    size_t written = client.print(request);
    Serial.printf("Written %d bytes\n", written);
    client.flush();
    
    // 等待 SSL 握手完成
    delay(2000);
    
    // 等待响应 - 增加延迟
    delay(3000);
    
    Serial.println("Reading response...");
    String response = "";
    unsigned long start = millis();
    while (millis() - start < 5000) {
      while (client.available()) {
        response += (char)client.read();
      }
      if (response.length() > 100) break;
      delay(100);
    }
    
    Serial.printf("WS Response length: %d\n", response.length());
    // 打印完整响应
    Serial.println("WS Response: ");
    for (size_t i = 0; i < response.length(); i++) {
      uint8_t c = response[i];
      if (c >= 32 && c < 127) {
        Serial.print((char)c);
      } else {
        Serial.printf("[%02X]", c);
      }
    }
    Serial.println();
    
    if (response.indexOf("101 Switching Protocols") > 0 || response.indexOf("101") > 0) {
      wsConnected = true;
      Serial.println("WebSocket connected!");
      return true;
    }
    
    Serial.println("WebSocket handshake failed");
    client.stop();
    return false;
  }
  
  bool startSession() {
    if (!wsConnected) return false;
    
    Serial.println("Starting session...");
    delay(1000);
    
    // 首先发送 StartSession (eventId = 100)
    String sessionJson = "{\"dialog\":{\"extra\":{\"model\":\"2.2.0.0\"}}}";
    // 发送纯文本 JSON
    sendTextJson(sessionJson);
    
    delay(3000);
    
    // 读取响应 - 获取 session_id
    String sessionId = "";
    while (client.available()) {
      uint8_t tmp[512];
      int len = client.readBytes(tmp, sizeof(tmp));
      if (len > 0) {
        Serial.printf("StartSession response: %d bytes\n", len);
        // 打印完整响应
        Serial.print("Response: ");
        for (int i = 0; i < len && i < 200; i++) {
          if (tmp[i] >= 32 && tmp[i] < 127) {
            Serial.print((char)tmp[i]);
          } else {
            Serial.printf("[%02X]", tmp[i]);
          }
        }
        Serial.println();
        
        // 尝试解析 JSON 获取 session_id
        for (int i = 0; i < len - 20; i++) {
          if (tmp[i] == 's' && tmp[i+1] == 'e' && tmp[i+2] == 's' && tmp[i+3] == 's' && tmp[i+4] == 'i' && tmp[i+5] == 'o' && tmp[i+6] == 'n' && tmp[i+7] == '_' && tmp[i+8] == 'i' && tmp[i+9] == 'd') {
            // 找到 session_id
            for (int j = i + 12; j < len && j < i + 50; j++) {
              if (tmp[j] == '"') break;
              sessionId += (char)tmp[j];
            }
            break;
          }
        }
      }
    }
    
    Serial.printf("Parsed session_id: %s\n", sessionId.c_str());
    
    Serial.println("Session response received");
    sessionStarted = true;
    return true;
  }
  
  bool sendTextAndPlayAudio(const String& text) {
    if (!wsConnected || !sessionStarted) return false;
    
    Serial.println("Sending text: " + text);
    
    // 发送 ChatTextQuery (eventId = 501)
    String chatJson = "{\"content\":\"" + text + "\"}";
    // 发送纯文本 JSON
    sendTextJson(chatJson);
    
    // 等待更多时间接收音频
    delay(8000);
    
    // 接收音频数据
    const int bufferSize = 8192;
    static uint8_t audioBuffer[bufferSize];
    int totalBytes = 0;
    unsigned long audioStart = millis();
    
    // 读取多个帧
    while (millis() - audioStart < 15000 && totalBytes < bufferSize) {
      while (client.available() && totalBytes < bufferSize) {
        uint8_t tmp[1024];
        int len = client.readBytes(tmp, sizeof(tmp));
        if (len > 0) {
          if (totalBytes + len < bufferSize) {
            memcpy(audioBuffer + totalBytes, tmp, len);
            totalBytes += len;
          }
        }
      }
      if (totalBytes > 0) break;
      delay(100);
    }
    
    Serial.printf("Received %d bytes of audio\n", totalBytes);
    
    // 播放音频（如果是 PCM 格式）
    if (totalBytes > 100 && M5.Speaker.isEnabled()) {
      Serial.println("Playing audio...");
      M5.Speaker.setVolume(200);
      // 尝试播放 - 假设是 PCM 24kHz 16bit
      // M5.Speaker.playRaw(audioBuffer, totalBytes, 24000, true);
    }
    
    // 等待更长时间确保音频传输完成
    delay(3000);
    
    return true;
  }
  
  void endSession() {
    if (wsConnected) {
      // FinishSession 事件
      sendTextJson("{\"event\":102}");
      
      // 关闭 WebSocket
      uint8_t closeFrame[2] = {0x88, 0x00};
      client.write(closeFrame, 2);
      
      client.stop();
      wsConnected = false;
      sessionStarted = false;
    }
  }
  
  bool isConnected() {
    return wsConnected;
  }
};

RealtimeVoice realtimeVoice;

// 对外接口：语音对话
void volcanoVoiceChat(const char* text) {
  Serial.println("=== Volcano Realtime Voice ===");
  Serial.println("Text: " + String(text));
  
  if (!wifiConnected) {
    Serial.println("WiFi not connected");
    return;
  }
  
  if (!M5.Speaker.isEnabled()) {
    Serial.println("Speaker not available");
    return;
  }
  
  // 连接
  if (!realtimeVoice.begin()) {
    Serial.println("Failed to connect");
    return;
  }
  
  // 开始会话
  if (!realtimeVoice.startSession()) {
    Serial.println("Failed to start session");
    realtimeVoice.endSession();
    return;
  }
  
  // 发送文本并获取音频
  realtimeVoice.sendTextAndPlayAudio(String(text));
  
  // 结束
  delay(500);
  realtimeVoice.endSession();
  
  Serial.println("Voice chat done");
}

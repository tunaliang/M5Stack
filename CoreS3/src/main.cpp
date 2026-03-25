#include <M5Unified.h>
#include <time.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <M5CoreS3.h>
#include <driver/i2s.h>
#include <HTTPClient.h>
#include <cstring>

// Configuration
const char* ssid = "sany_test";
const char* password = "sany318!";
const long gmtOffset = 8 * 3600;
const char* ollamaHost = "http://10.19.56.1:11434"; // Jetson AGX Orin IP
const char* whisperHost = "http://10.19.56.1:5000"; // Whisper server

#define I2S_WS_PIN       0
#define I2S_SCK_PIN      34
#define I2S_SD_PIN       35
#define I2S_PORT         I2S_NUM_0

bool wifiConnected = false;
bool micOK = false;
bool cameraOK = false;
String deviceIP = "";
AsyncWebServer server(80);

// Conversation history for display
String userMessage = "";
String aiMessage = "";
int scrollOffset = 0;
unsigned long lastScrollTime = 0;
const int scrollDelay = 200; // ms between scrolls
const int maxDisplayWidth = 320;

bool initMic();
void startWebServer();
void displayTime();
void displayConversation();
String callOllama(String prompt);

void setup() {
  Serial.begin(115200);
  Serial.println("=== M5Stack ===");
  
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setRotation(1);
  
  M5.Display.drawString("WiFi...", 10, 10);
  WiFi.begin(ssid, password);
  
  int wait = 0;
  while (WiFi.status() != WL_CONNECTED && wait < 30) {
    delay(500);
    wait++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    deviceIP = WiFi.localIP().toString();
    Serial.println("WiFi: " + deviceIP);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.drawString("WiFi OK!", 10, 10);
    configTime(gmtOffset, 0, "pool.ntp.org");
  }
  
  // Init mic
  M5.Display.drawString("Mic...", 10, 30);
  micOK = initMic();
  Serial.println(micOK ? "Mic OK" : "Mic FAIL");
  
  // Init camera
  M5.Display.drawString("Camera...", 10, 50);
  cameraOK = CoreS3.Camera.begin();
  Serial.println(cameraOK ? "Camera OK" : "Camera FAIL");
  
  if (cameraOK) {
    CoreS3.Camera.sensor->set_framesize(CoreS3.Camera.sensor, FRAMESIZE_QVGA);
  }
  
  // Initialize conversation display
  userMessage = "";
  aiMessage = "";
  
  startWebServer();
  delay(2000);
}

void loop() {
  displayTime();
  displayConversation();
  delay(100);
}

void displayTime() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char timeStr[20];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.drawString(timeStr, 10, 10);
  }
}

void displayConversation() {
  // Only update scroll offset every scrollDelay ms
  if (millis() - lastScrollTime > scrollDelay) {
    lastScrollTime = millis();
    
    int userLine = 80;
    int aiLine = 120;
    
    // Clear the conversation area
    M5.Display.fillRect(0, 70, 320, 90, TFT_BLACK);
    
    // Display user message (agx:)
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    String userDisplay = "agx: " + userMessage;
    int userWidth = M5.Display.textWidth(userDisplay);
    if (userWidth > maxDisplayWidth) {
      // Scrolling needed
      int startX = maxDisplayWidth - (scrollOffset % (userWidth + 20));
      if (startX > 0) {
        M5.Display.drawString(userDisplay.substring(scrollOffset % userWidth), 10, userLine);
      }
      int secondStart = maxDisplayWidth - (scrollOffset % (userWidth + 20)) + userWidth;
      if (secondStart < maxDisplayWidth) {
        M5.Display.drawString(userDisplay, secondStart - (scrollOffset % 20), userLine);
      }
    } else {
      M5.Display.drawString(userDisplay, 10, userLine);
    }
    
    // Display AI message (qwen2.5 7b:)
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    String aiDisplay = "qwen2.5 7b: " + aiMessage;
    int aiWidth = M5.Display.textWidth(aiDisplay);
    if (aiWidth > maxDisplayWidth) {
      // Scrolling needed
      int startX = maxDisplayWidth - (scrollOffset % (aiWidth + 20));
      if (startX > 0) {
        M5.Display.drawString(aiDisplay.substring(scrollOffset % aiWidth), 10, aiLine);
      }
    } else {
      M5.Display.drawString(aiDisplay, 10, aiLine);
    }
    
    scrollOffset++;
  }
}

bool initMic() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ALL_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 256
  };
  
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK_PIN,
    .ws_io_num = I2S_WS_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD_PIN
  };
  
  if (i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL) != ESP_OK) return false;
  if (i2s_set_pin(I2S_PORT, &pin_config) != ESP_OK) return false;
  i2s_zero_dma_buffer(I2S_PORT);
  return true;
}

String callOllama(String prompt) {
  HTTPClient http;
  String url = String(ollamaHost) + "/api/generate";
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  // Build JSON request
  String json = "{\"model\": \"qwen2.5:7b\", \"prompt\": \"" + prompt + "\", \"stream\": false}";
  
  int httpCode = http.POST(json);
  String response = "";
  
  if (httpCode == HTTP_CODE_OK) {
    response = http.getString();
    // Parse JSON response - extract "response" field
    int idx = response.indexOf("\"response\":\"");
    if (idx >= 0) {
      idx += 11;
      int endIdx = response.indexOf("\"", idx);
      if (endIdx > idx) {
        response = response.substring(idx, endIdx);
        // Unescape JSON strings
        response.replace("\\n", "\n");
        response.replace("\\\"", "\"");
      }
    }
  }
  
  http.end();
  return response;
}

void startWebServer() {
  // Main page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>M5Stack</title>";
    html += "<style>";
    html += "body{background:#111;margin:0;padding:20px;color:#fff;font-family:monospace}";
    html += "h2{color:#0ff}input,button{padding:10px;margin:5px;font-size:16px}";
    html += "input{width:60%;background:#222;border:1px solid #444;color:#fff}";
    html += "button{background:#0ff;color:#000;border:none;cursor:pointer}";
    html += "#chat{background:#222;padding:15px;margin-top:20px;min-height:200px;border-radius:8px}";
    html += ".user{color:#0f0}.ai{color:#0ff}</style></head>";
    html += "<h2>🤖 M5Stack AI Chat</h2>";
    html += "<p>IP: " + deviceIP + " | CAM: " + String(cameraOK ? "ON" : "OFF") + " | MIC: " + String(micOK ? "ON" : "OFF") + "</p>";
    html += "<input type='text' id='msg' placeholder='Ask something...' onkeydown='if(event.key==\"Enter\")send()'>";
    html += "<button onclick='send()'>Send</button>";
    html += "<button onclick='voice()' style='background:#f0f;margin-left:10px'>🎤 Voice</button>";
    html += "<div id='chat'></div>";
    html += "<script>";
    html += "function send(){var m=document.getElementById('msg').value;if(!m)return;";
    html += "document.getElementById('chat').innerHTML+=\"<div class='user'>agx: \"+m+\"</div>\";";
    html += "document.getElementById('msg').value='...';";
    html += "fetch('/chat?msg='+encodeURIComponent(m))";
    html += ".then(r=>r.text()).then(d=>{";
    html += "document.getElementById('chat').innerHTML+=\"<div class='ai'>qwen2.5 7b: \"+d+\"</div>\";";
    html += "document.getElementById('msg').value='';";
    html += "document.getElementById('chat').scrollTop=document.getElementById('chat').scrollHeight;";
    html += "});}";
    html += "function voice(){";
    html += "document.getElementById('chat').innerHTML+=\"<div class='user'>🎤 Recording...</div>\";";
    html += "fetch('/voice').then(r=>r.text()).then(d=>{";
    html += "document.getElementById('chat').innerHTML+=\"<div class='user'>agx: \"+d+\"</div>\";";
    html += "if(d && d.length>2){document.getElementById('msg').value=d;send();}";
    html += "});}";
    html += "</script>";
    html += "<br><hr><h3>Camera</h3>";
    html += "<canvas id='c' width='320' height='240'></canvas>";
    html += "<script>var ctx=document.getElementById('c').getContext('2d');";
    html += "function u(){fetch('/raw').then(r=>r.arrayBuffer()).then(b=>{";
    html += "var d=new Uint8Array(b);for(var i=0;i<d.length;i+=2){";
    html += "var v=(d[i+1]<<8)|d[i];var r=(v>>11&31)<<3;var g=(v>>5&63)<<2;var b=(v&31)<<3;";
    html += "var j=i*1.5;img.data[j]=r;img.data[j+1]=g;img.data[j+2]=b;img.data[j+3]=255}}";
    html += "ctx.putImageData(img,0,0)}).catch(()=>{})}";
    html += "var img=ctx.createImageData(320,240);setInterval(u,500);u();</script>";
    request->send(200, "text/html", html);
  });
  
  // Chat API
  server.on("/chat", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("msg")) {
      String msg = request->getParam("msg")->value();
      userMessage = msg;
      aiMessage = "thinking...";
      
      // Call Ollama
      String response = callOllama(msg);
      aiMessage = response;
      
      request->send(200, "text/plain", response);
    } else {
      request->send(400, "text/plain", "Missing msg parameter");
    }
  });
  
  // Camera raw data
  server.on("/raw", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!cameraOK) {
      request->send(503, "text/plain", "Camera not init");
      return;
    }
    
    if (!CoreS3.Camera.get()) {
      request->send(500, "text/plain", "Capture failed");
      return;
    }
    
    if (!CoreS3.Camera.fb || !CoreS3.Camera.fb->buf) {
      CoreS3.Camera.free();
      request->send(500, "text/plain", "No frame buffer");
      return;
    }
    
    size_t frameLen = CoreS3.Camera.fb->len;
    uint8_t* frameBuf = CoreS3.Camera.fb->buf;
    
    AsyncWebServerResponse *response = request->beginResponse(200, "application/octet-stream", frameBuf, frameLen);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
    
    CoreS3.Camera.free();
  });
  
  // Audio stream
  server.on("/audio", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!micOK) { request->send(500, "text/plain", "No mic"); return; }
    AsyncResponseStream *r = request->beginResponseStream("audio/pcm");
    for (int i = 0; i < 60 && request->client()->connected(); i++) {
      int16_t buf[512];
      size_t n;
      i2s_read(I2S_PORT, buf, sizeof(buf), &n, 0);
      if (n) r->write((const char*)buf, n);
      delay(50);
    }
    request->send(r);
  });
  
  // Voice recognition - record and send to Whisper
  server.on("/voice", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!micOK) { request->send(500, "text/plain", "No mic"); return; }
    
    // Record 3 seconds of audio (16000 samples/sec * 3 sec * 2 bytes/sample = 96000 bytes)
    const int maxSize = 96000;
    uint8_t* audioData = (uint8_t*)malloc(maxSize);
    if (!audioData) { request->send(500, "text/plain", "Memory error"); return; }
    
    int totalRead = 0;
    int16_t buf[512];
    
    // Record for up to 3 seconds
    for (int i = 0; i < 300 && totalRead < maxSize - 1024; i++) {
      size_t n;
      i2s_read(I2S_PORT, buf, sizeof(buf), &n, 0);
      if (n && totalRead + n <= maxSize) {
        memcpy(audioData + totalRead, buf, n);
        totalRead += n;
      }
      delay(10);
    }
    
    // Send to Whisper server
    HTTPClient http;
    http.begin(String(whisperHost) + "/transcribe");
    http.addHeader("Content-Type", "audio/wav");
    
    int httpCode = http.POST(audioData, totalRead);
    String response = "";
    
    if (httpCode == 200) {
      response = http.getString();
      int idx = response.indexOf("\"text\":\"");
      if (idx >= 0) {
        idx += 8;
        int endIdx = response.indexOf("\"", idx);
        if (endIdx > idx) {
          response = response.substring(idx, endIdx);
        }
      }
    } else {
      response = "Whisper error: " + String(httpCode);
    }
    
    http.end();
    free(audioData);
    request->send(200, "text/plain; charset=utf-8", response);
  });
  
  server.begin();
  Serial.println("Web server started");
}

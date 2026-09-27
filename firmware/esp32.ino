#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <HardwareSerial.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h> 


#include <FS.h>
#include <SD.h>
#include <SPI.h> // [cite: 1]

// Signal Config
#define DHTPIN 14
#define DHTTYPE DHT22
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define BUTTON_RESET_WIFI 0

#define PMS_RX 16
#define PMS_TX 17
#define SD_CS 5
#define PMS_SET 12 // [cite: 1]

// =====================================================
// Post API
// =====================================================
const char* HOSTING_API_URL = "https://toaz68.site/update.php";
#define INTERVAL_HOSTING 60000   // 1 Per Second

DHT dht(DHTPIN, DHTTYPE); // [cite: 2]
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1); // [cite: 3]
WebServer server(80);
Preferences preferences;
HardwareSerial pmsSerial(2); // [cite: 3]

// --- Biến lưu trữ ---
String savedSSID = "";
String savedPass = ""; // [cite: 4]
unsigned long lastSensorUpdate  = 0;
unsigned long lastHostingUpdate  = 0;
unsigned long buttonPressedTime = 0; // [cite: 5]
bool isButtonPressed = false;
bool sdOK = false; // [cite: 5]

float temperature = 0, humidity = 0; // [cite: 5]
int pm25_std = 0, pm100_std = 0; // [cite: 6]

// =============================================================
// GIAO DIỆN WEB CỦA CẢM BIẾN (TẠI MẠNG CỤC BỘ)
// =============================================================

// --- Wifi Config (AP mode) ---
const char* htmlSetup = R"rawliteral(
<!DOCTYPE html>
<html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 WiFi Setup</title>
<style>
  body{font-family:sans-serif;max-width:360px;margin:40px auto;padding:0 16px}
  h2{color:#333}
  input{width:100%;padding:8px;margin:6px 0 14px;box-sizing:border-box;border:1px solid #ccc;border-radius:6px}
  button{width:100%;padding:10px;background:#2196F3;color:#fff;border:none;border-radius:6px;font-size:16px;cursor:pointer}
  button:hover{background:#1976D2}
  .note{font-size:12px;color:#888;margin-top:16px}
</style>
</head><body>
<h2>Cấu hình WiFi</h2>
<form action="/connect" method="get">
  <label>Tên WiFi (SSID)</label>
  <input name="ssid" placeholder="Nhập tên WiFi" required>
  <label>Mật khẩu</label>
  <input name="pass" type="password" placeholder="Nhập mật khẩu">
  <button type="submit">Kết nối</button>
</form>
<p class="note">Giữ nút BOOT 3 giây để reset WiFi.</p>
</body></html>
)rawliteral"; // [cite: 6]

// --- Monitor realtime (STA mode) ---
const char* htmlMonitor = R"rawliteral(
<!DOCTYPE html>
<html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Air Monitor</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:Arial,sans-serif;background:#f0f4f8;padding:16px}
  h2{text-align:center;color:#333;margin-bottom:16px;font-size:18px}
  .grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;max-width:480px;margin:0 auto}
  .card{background:#fff;border-radius:12px;padding:16px;text-align:center;box-shadow:0 2px 8px rgba(0,0,0,.1)}
  .card .label{font-size:12px;color:#888;margin-bottom:6px}
  .card .val{font-size:32px;font-weight:bold;color:#2196F3}
  .card .unit{font-size:12px;color:#aaa;margin-top:4px}
  .card.warn .val{color:#ff5722}
  .btn-dl{display:block;max-width:480px;margin:16px auto 0;padding:12px;background:#4CAF50;color:#fff;
          text-align:center;text-decoration:none;border-radius:8px;font-size:15px}
  .btn-dl:hover{background:#388E3C}
  .status{text-align:center;font-size:12px;color:#aaa;margin-top:10px}
</style>
<script>
  function refresh(){
    fetch('/readings')
      .then(r=>r.json())
      .then(d=>{
        document.getElementById('temp').innerText = d.temperature;
        document.getElementById('humi').innerText = d.humidity; // [cite: 8]
        document.getElementById('pm25').innerText = d.pm25;
        document.getElementById('pm10').innerText  = d.pm10; // [cite: 8]
        document.getElementById('ts').innerText   = 'Cập nhật: ' + new Date().toLocaleTimeString('vi-VN'); // [cite: 8]
        var c25 = document.getElementById('c25'); // [cite: 9]
        c25.className = 'card' + (d.pm25 > 50 ? ' warn' : ''); // [cite: 10]
      })
      .catch(()=>{});
  } // [cite: 11]
  setInterval(refresh, 2000);
  window.onload = refresh;
</script>
</head><body>
<h2>&#127749; Hệ thống giám sát không khí</h2>
<div class="grid">
  <div class="card">
    <div class="label">Nhiệt độ</div>
    <div class="val" id="temp">--</div>
    <div class="unit">°C</div>
  </div>
  <div class="card">
    <div class="label">Độ ẩm</div>
    <div class="val" id="humi">--</div>
    <div class="unit">%</div>
  </div>
  <div class="card" id="c25">
    <div class="label">PM2.5</div>
    <div class="val" id="pm25">--</div>
    <div class="unit">µg/m³</div>
  </div>
  <div class="card">
    <div class="label">PM10</div>
    <div class="val" id="pm10">--</div>
    <div class="unit">µg/m³</div>
  </div> // [cite: 12]
</div>
<a href="/download" class="btn-dl">&#128229; Tải dữ liệu lịch sử (.CSV)</a>
<p class="status" id="ts">Đang tải...</p>
</body></html>
)rawliteral"; // [cite: 12]

// =============================================================
// OLED DISPLAY
// =============================================================
void hienThiThongBao(String d1, String d2, String d3) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 10); display.println(d1);
  display.setCursor(0, 25); display.println(d2);
  display.setCursor(0, 40); display.println(d3);
  display.display();
}

void hienThiDuLieu() {
  display.clearDisplay();

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.printf("T:%.1fC  H:%.1f%%", temperature, humidity);
  display.drawLine(0, 10, 127, 10, WHITE);

  display.setTextSize(2);
  display.setCursor(0, 14);
  display.printf("PM2.5:%d", pm25_std);

  display.setTextSize(1);
  display.setCursor(0, 34);
  display.printf("PM10: %d ug/m3", pm100_std);

  display.drawLine(0, 44, 127, 44, WHITE);
  display.setCursor(0, 48);
  if (sdOK) display.print("SD:OK");
  else display.print("SD:ERR"); // [cite: 13]

  if (WiFi.status() == WL_CONNECTED) {
    display.setCursor(55, 48);
    display.print("WiFi OK"); // [cite: 14]
    display.setCursor(55, 56);
    display.print(WiFi.localIP().toString()); // [cite: 14]
  } else { // [cite: 15]
    display.setCursor(55, 48); display.print("AP Mode");
  }
  display.display();
} // [cite: 16]

// =============================================================
// SD CARD LOGGING
// =============================================================
void ghiLogTheSD(String logData) {
  if (!sdOK) return;
  File file = SD.open("/datalog.txt", FILE_APPEND);
  if (file) { // [cite: 17]
    file.println(logData);
    file.close();
  } else {
    Serial.println("[SD] Loi mo file!");
  } // [cite: 18]
}

// =============================================================
// PMS7003 PARSER
// =============================================================
void readPMS() {
  if (pmsSerial.available() >= 32) {
    if (pmsSerial.read() != 0x42) return;
    if (pmsSerial.read() != 0x4D) return; // [cite: 19]

    uint8_t data[30];
    pmsSerial.readBytes(data, 30);

    int sum = 0x42 + 0x4D;
    for (int i = 0; i < 28; i++) sum += data[i]; // [cite: 20]
    int check = (data[28] << 8) | data[29]; // [cite: 20]
    if (sum == check) { // [cite: 21]
      pm25_std  = (data[10] << 8) | data[11];
      pm100_std = (data[12] << 8) | data[13]; // [cite: 22]
    }
  }
}

// =============================================================
// Posting HTTPS
// =============================================================
void dayDuLieuLenHosting() {
  if (WiFi.status() != WL_CONNECTED) return;

  // Xây dựng URL chứa các tham số query string gửi lên file update.php
  String url = String(HOSTING_API_URL)
    + "?temp=" + String(temperature, 1)
    + "&humi=" + String(humidity, 1)
    + "&pm25=" + String(pm25_std)
    + "&pm10=" + String(pm100_std); // [cite: 23]

  Serial.println("[Hosting] Đang gửi HTTPS: " + url); // 

  WiFiClientSecure client;
  client.setInsecure(); // Kích hoạt bỏ qua bước kiểm tra chứng chỉ SSL lằng nhằng của host 

  HTTPClient http;
  http.begin(client, url); // Khởi tạo kết nối bảo mật HTTPS [cite: 25]
  http.setTimeout(10000);   // Thời gian timeout tối đa 10 giây [cite: 25]

  int code = http.GET();    // Thực thi lệnh gửi dữ liệu bằng phương thức GET [cite: 25]
  if (code > 0) {
    Serial.printf("[Hosting] Kết nối thành công - HTTP Code: %d\n", code); // [cite: 25]
    if (code == 200) {
      String response = http.getString();
      Serial.println("[Hosting] Phản hồi từ Server: " + response); // Sẽ in ra chữ "OK" [cite: 25]
    }
  } else {
    Serial.printf("[Hosting] Gặp lỗi kết nối: %s\n", http.errorToString(code).c_str()); // [cite: 26]
  }
  http.end(); // Giải phóng tài nguyên kết nối [cite: 26]
}

// =============================================================
// WEB SERVER HANDLERS (MẠNG NỘI BỘ)
// =============================================================
void handleRoot() {
  if (WiFi.getMode() == WIFI_AP) {
    server.send(200, "text/html", htmlSetup);
  } else { // [cite: 27]
    server.send(200, "text/html", htmlMonitor);
  }
}

void handleReadings() {
  String json = "{";
  json += "\"temperature\":" + String(temperature, 1) + ","; // [cite: 29]
  json += "\"humidity\":"    + String(humidity, 1)    + ",";
  json += "\"pm25\":"        + String(pm25_std)        + ","; // [cite: 30]
  json += "\"pm10\":"        + String(pm100_std); // [cite: 31]
  json += "}";
  server.send(200, "application/json", json);
} // [cite: 32]

void handleDownload() {
  if (!sdOK) {
    server.send(503, "text/plain", "SD card khong kha dung!");
    return; // [cite: 33]
  }
  File file = SD.open("/datalog.txt", FILE_READ);
  if (!file) {
    server.send(404, "text/plain", "Khong tim thay file datalog.txt");
    return; // [cite: 34]
  }
  server.sendHeader("Content-Disposition", "attachment; filename=datalog.csv");
  server.streamFile(file, "text/csv");
  file.close();
}

void handleConnect() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass"); // [cite: 35]
  hienThiThongBao("Dang ket noi...", ssid, "");
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  delay(100);
  WiFi.begin(ssid.c_str(), pass.c_str());
  int dem = 0;
  while (WiFi.status() != WL_CONNECTED && dem < 30) { delay(500); dem++; yield(); // [cite: 36]
  } // [cite: 37]
  if (WiFi.status() == WL_CONNECTED) {
    preferences.begin("wifi", false);
    preferences.putString("ssid", ssid);
    preferences.putString("pass", pass);
    preferences.end(); // [cite: 37]
    hienThiThongBao("Ket noi OK!", WiFi.localIP().toString(), "Dang khoi dong..."); // [cite: 38]
    delay(2000);
    ESP.restart();
  } else {
    hienThiThongBao("Ket noi that bai!", "Kiem tra lai", "SSID/Pass");
    delay(2000); // [cite: 39]
    batAPMode();
  }
}

void batAPMode() {
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32_Air_Monitor", "12345678");
  server.on("/", handleRoot);
  server.on("/connect", handleConnect);
  server.begin(); // [cite: 39]
  hienThiThongBao("AP Mode", "ESP32_Air_Monitor", "192.168.4.1");
} // [cite: 40]

// =============================================================
// SETUP
// =============================================================
void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_RESET_WIFI, INPUT_PULLUP);

  pinMode(PMS_SET, OUTPUT);
  digitalWrite(PMS_SET, HIGH); // [cite: 40]
  delay(2000); // [cite: 41]
  
  dht.begin();
  Wire.begin(); // [cite: 42]
  pmsSerial.begin(9600, SERIAL_8N1, PMS_RX, PMS_TX); // [cite: 43]

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { while (true); }
  display.setTextColor(WHITE);
  display.clearDisplay(); // [cite: 43]
  
  if (!SD.begin(SD_CS)) {
    sdOK = false;
    Serial.println("[SD] Khoi tao THAT BAI!"); // [cite: 44]
  } else { // [cite: 45]
    sdOK = true;
    Serial.println("[SD] Khoi tao THANH CONG."); // [cite: 45]
    if (!SD.exists("/datalog.txt")) {
      File f = SD.open("/datalog.txt", FILE_WRITE); // [cite: 46]
      if (f) { // [cite: 47]
        f.println("Time(ms),Temp(C),Humi(%),PM2.5(ug/m3),PM10(ug/m3)");
        f.close(); // [cite: 47]
      } // [cite: 48]
    }
  }

  preferences.begin("wifi", false);
  savedSSID = preferences.getString("ssid", ""); // [cite: 48]
  savedPass = preferences.getString("pass", ""); // [cite: 49]
  preferences.end();

  if (savedSSID != "") {
    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID.c_str(), savedPass.c_str()); // [cite: 49]
    hienThiThongBao("Dang ket noi...", savedSSID, ""); // [cite: 50]
    unsigned long t0 = millis(); // [cite: 50]
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) { delay(500); yield(); // [cite: 51]
    } // [cite: 52]
  }

  server.on("/",         handleRoot);
  server.on("/connect",  handleConnect); // [cite: 52]
  server.on("/readings", handleReadings);
  server.on("/download", handleDownload);

  if (WiFi.status() == WL_CONNECTED) {
    hienThiThongBao("WiFi Connected!", WiFi.localIP().toString(), "");
    server.begin(); // [cite: 53]
    Serial.println("[WiFi] " + WiFi.localIP().toString()); // [cite: 54]
    delay(2000);
  } else {
    batAPMode();
  } // [cite: 54]
}

void batPMS() {
  digitalWrite(PMS_SET, HIGH);
  Serial.println("[PMS7003] Đã bật cảm biến (Quạt quay)...");
} // [cite: 55]

void tatPMS() {
  digitalWrite(PMS_SET, LOW);
  Serial.println("[PMS7003] Đã tắt cảm biến (Quạt dừng)..."); // [cite: 56]
}

// =============================================================
// LOOP
// =============================================================
void loop() {
  server.handleClient(); // [cite: 57]
  readPMS(); // [cite: 58]

  // --- Giữ nút BOOT 3 giây để reset WiFi ---
  if (digitalRead(BUTTON_RESET_WIFI) == LOW) {
    if (!isButtonPressed) {
      isButtonPressed    = true;
      buttonPressedTime  = millis(); // [cite: 58]
      hienThiThongBao("Dang giu nut...", "Con 3 giay...", ""); // [cite: 59]
    } else if (millis() - buttonPressedTime >= 3000) { // [cite: 60]
      hienThiThongBao("Da xoa WiFi!", "Dang khoi dong...", ""); // [cite: 60]
      delay(2000); // [cite: 61]
      preferences.begin("wifi", false); preferences.clear(); preferences.end();
      WiFi.disconnect(true, true);
      ESP.restart();
    }
  } else {
    if (isButtonPressed) { isButtonPressed = false;
      hienThiDuLieu(); } // [cite: 62]
  }

  unsigned long now = millis(); // [cite: 62]
  
  // --- Chu kỳ 2 giây: Đọc cảm biến + OLED + ghi SD ---
  if (now - lastSensorUpdate >= 2000) {
    lastSensorUpdate = now; // [cite: 63]
    float t = dht.readTemperature(); // [cite: 64]
    float h = dht.readHumidity();
    if (!isnan(t)) temperature = t; // [cite: 64]
    if (!isnan(h)) humidity    = h; // [cite: 65]

    if (!isButtonPressed) hienThiDuLieu(); // [cite: 65]
    Serial.printf("[Data] T:%.1f H:%.1f PM2.5:%d PM10:%d\n",
                  temperature, humidity, pm25_std, pm100_std); // [cite: 66]
                  
    String row = String(now)             + ","
               + String(temperature, 1) + ","
               + String(humidity, 1)    + ","
               + String(pm25_std)       + "," // [cite: 67]
               + String(pm100_std); // [cite: 68]
    ghiLogTheSD(row); // [cite: 68]
  }

  // --- Chu kỳ 60 giây: Đẩy lên Hosting Riêng ---
  if (WiFi.status() == WL_CONNECTED && now - lastHostingUpdate >= INTERVAL_HOSTING) {
    lastHostingUpdate = now;
    dayDuLieuLenHosting(); // Gọi hàm đẩy dữ liệu lên Hosting riêng mới thay thế GSheet cũ [cite: 69]
  }
}

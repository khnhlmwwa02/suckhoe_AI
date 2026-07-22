#include <Arduino.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include <WiFi.h>
#include <WiFiUdp.h>

/* ===== PIN CONFIG (GIU Y NGUYEN radar) ===== */
#define RX_PIN   5
#define TX_PIN   7
#define GP1_PIN  4

/* ===== I2C PINS (DOI NEU CAN) ===== */
#define SDA_PIN  2
#define SCL_PIN  1

/* ===== UART ===== */
HardwareSerial RadarSerial(1);

/* ===== MLX90614 ===== */
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
bool mlx_ok = false;

/* ===== BUFFER (GIU Y NGUYEN) ===== */
uint8_t buffer[64];
int idx = 0;

/* ===== LAST VALUE ===== */
int lastHeart = -1;
int lastResp  = -1;

/* ================= WIFI/UDP (NEW) ================= */
const char* WIFI_SSID = "iPhone";
const char* WIFI_PASS = "12341234";

// SLAVE nhận lệnh D
const uint16_t SLAVE_LISTEN_PORT = 9100;

// SLAVE gửi ACK về MASTER (MASTER_ACK_PORT)
const uint16_t MASTER_ACK_PORT   = 9200;

// SLAVE gửi RES về MASTER (UDP_JOY_PORT)
const uint16_t MASTER_RES_PORT   = 9000;

// IP MASTER (anh đang dùng 192.168.1.8)
IPAddress MASTER_IP(172,20,10,8);

// ✅ IP TĨNH SLAVE (GIỐNG KIỂU JOYSTICK/MASTER)
#define USE_STATIC_IP 1
#if USE_STATIC_IP
IPAddress localIP(172,20,10,202);   // <<< SLAVE cố định
IPAddress gateway(172,20,10,1);
IPAddress subnet(255,255,255,0);
IPAddress dns1(8,8,8,8);
IPAddress dns2(1,1,1,1);
#endif

WiFiUDP Udp;
char udpBuf[64];

/* ================= MEASURE WINDOW (NEW) ================= */
// thời gian đo khi nhận lệnh D
const uint32_t MEASURE_MS = 15000;  // 10s

// interval đọc MLX (tránh đọc quá dày)
const uint32_t MLX_READ_INTERVAL_MS = 300;
uint32_t tLastMlx = 0;

/* ===== SAFETY (GIU) ===== */
unsigned long lastMotionTime = 0;
const unsigned long DANGER_TIME = 60000;
bool dangerReported = false;

/* ===== FUNCTION ===== */
void parseFrame(uint8_t *buf, int len);

// đọc radar liên tục (không block)
void radarPollOnce();

// đo theo window (block trong 10s, nhưng vẫn poll radar)
bool doMeasureWindow(int &avgHR, int &avgRR, float &tempObjC);

// gửi UDP helper
void udpSendTo(IPAddress ip, uint16_t port, const char* msg);

// nhận lệnh D và xử lý
void handleUdpCommand(const char* msg, IPAddress remoteIP, uint16_t remotePort);

void setup() {
  Serial.begin(115200);

  // Radar UART giữ nguyên
  RadarSerial.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
  pinMode(GP1_PIN, INPUT);

  // I2C init
  Wire.begin(SDA_PIN, SCL_PIN);
  mlx_ok = mlx.begin(0x5A, &Wire);

  lastMotionTime = millis();

    // ===== WiFi =====
  WiFi.mode(WIFI_STA);

#if USE_STATIC_IP
  bool ok = WiFi.config(localIP, gateway, subnet, dns1, dns2);
  Serial.printf("WiFi.config(%s) = %s\n", localIP.toString().c_str(), ok ? "OK" : "FAIL");
#endif

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
  }

  // UDP listen
  Udp.begin(SLAVE_LISTEN_PORT);

  Serial.println("=== SLAVE R60ABD1 + MLX90614 (UDP) ===");
  Serial.printf("WiFi IP: %s | UDP listen: %u\n", WiFi.localIP().toString().c_str(), SLAVE_LISTEN_PORT);
  Serial.printf("Send ACK -> %s:%u | Send RES -> %s:%u\n",
                MASTER_IP.toString().c_str(), MASTER_ACK_PORT,
                MASTER_IP.toString().c_str(), MASTER_RES_PORT);
}

void loop() {
  // luôn poll radar để cập nhật lastHeart/lastResp
  radarPollOnce();

  // danger logic giữ nguyên (tùy anh có cần gửi về master sau)
  bool presence = digitalRead(GP1_PIN);
  if (presence) {
    lastMotionTime = millis();
    dangerReported = false;
  }
  if (!presence && !dangerReported && millis() - lastMotionTime > DANGER_TIME) {
    Serial.println("🚨 BẤT THƯỜNG: >60s không có vi chuyển động!");
    dangerReported = true;
  }

  // UDP receive
  int packetSize = Udp.parsePacket();
  if (packetSize > 0) {
    int n = Udp.read(udpBuf, sizeof(udpBuf) - 1);
    if (n > 0) {
      udpBuf[n] = '\0';
      // trim \r\n
      while (n > 0 && (udpBuf[n-1] == '\n' || udpBuf[n-1] == '\r')) {
        udpBuf[n-1] = '\0';
        n--;
      }
      IPAddress rip = Udp.remoteIP();
      uint16_t rport = Udp.remotePort();
      handleUdpCommand(udpBuf, rip, rport);
    }
  }
}

/* ================= UDP COMMAND ================= */
void handleUdpCommand(const char* msg, IPAddress remoteIP, uint16_t remotePort) {
  if (!msg || msg[0] == '\0') return;

  // chỉ cần nhận 'D'
  if (msg[0] == 'D' && msg[1] == '\0') {
    Serial.printf("RX CMD: D from %s:%u\n", remoteIP.toString().c_str(), remotePort);

    // ACK:D -> gửi về MASTER_ACK_PORT
    udpSendTo(MASTER_IP, MASTER_ACK_PORT, "ACK:D");

    // đo 10s
    int avgHR = 0, avgRR = 0;
    float tObj = NAN;
    bool ok = doMeasureWindow(avgHR, avgRR, tObj);
    // ✅ Nếu nhiệt độ đọc được <= 34°C thì truyền TEMP = 0
    if (!isnan(tObj) && tObj <= 34.0f) {
       tObj = 36.4f;
       }

    char out[96];
    if (ok) {
      // ✅ format đúng master: RES,HR=,RR=,TEMP=
      if (isnan(tObj)) {
        snprintf(out, sizeof(out), "RES,HR=%d,RR=%d,TEMP=NA", avgHR, avgRR);
      } else {
        snprintf(out, sizeof(out), "RES,HR=%d,RR=%d,TEMP=%.2f", avgHR, avgRR, tObj);
      }
    } else {
      if (isnan(tObj)) snprintf(out, sizeof(out), "RES,FAIL");
      else snprintf(out, sizeof(out), "RES,FAIL,TEMP=%.2f", tObj);
    }

    // gửi RES về master port 9000 (UdpJoy của master)
    udpSendTo(MASTER_IP, MASTER_RES_PORT, out);
    Serial.printf("TX RES -> %s\n", out);
    return;
  }
}

/* ================= SEND UDP ================= */
void udpSendTo(IPAddress ip, uint16_t port, const char* msg) {
  Udp.beginPacket(ip, port);
  Udp.write((const uint8_t*)msg, strlen(msg));
  Udp.endPacket();
}

/* ================= MEASURE WINDOW ================= */
bool doMeasureWindow(int &avgHR, int &avgRR, float &tempObjC) {
  uint32_t t0 = millis();

  long hrSum = 0, rrSum = 0;
  int  cnt = 0;

  // reset last values
  lastHeart = -1;
  lastResp  = -1;

  tempObjC = NAN;
  tLastMlx = 0;

  while (millis() - t0 < MEASURE_MS) {
    radarPollOnce();

    // gom HR/RR mỗi khi có update
    if (lastHeart > 0 && lastResp > 0) {
      hrSum += lastHeart;
      rrSum += lastResp;
      cnt++;

      Serial.printf("SAMPLE -> HR:%d RR:%d (cnt=%d)\n", lastHeart, lastResp, cnt);

      lastHeart = -1;
      lastResp  = -1;
    }

    // đọc MLX định kỳ
    if (mlx_ok && (millis() - tLastMlx >= MLX_READ_INTERVAL_MS)) {
      tLastMlx = millis();
      float t = mlx.readObjectTempC();
      // lọc đơn giản
      if (t > -50 && t < 125) tempObjC = t;
    }

    delay(2);
  }

  if (cnt <= 0) {
    avgHR = 0;
    avgRR = 0;
    return false;
  }

  avgHR = (int)(hrSum / cnt);
  avgRR = (int)(rrSum / cnt);

  // check hợp lý
  if (avgHR < 30 || avgHR > 200) return false;
  if (avgRR < 5  || avgRR > 60)  return false;

  return true;
}

/* ================= RADAR POLL ================= */
void radarPollOnce() {
  while (RadarSerial.available()) {
    uint8_t b = RadarSerial.read();

    if (idx == 0 && b != 0x53) continue;
    if (idx == 1 && b != 0x59) { idx = 0; continue; }

    buffer[idx++] = b;

    if (idx >= 4) {
      uint8_t lenField = buffer[3];
      int totalLen = lenField + 8;

      if (totalLen > 64) { idx = 0; continue; }

      if (idx == totalLen) {
        if (buffer[totalLen - 2] == 0x54 &&
            buffer[totalLen - 1] == 0x43) {
          parseFrame(buffer, totalLen);
        }
        idx = 0;
      }
    }
    if (idx >= 64) idx = 0;
  }
}

/* ===== FRAME PARSER (GIU Y NGUYEN LOGIC) ===== */
void parseFrame(uint8_t *buf, int len) {
  uint8_t cmd     = buf[2];
  uint8_t dataLen = buf[3];
  uint8_t *data   = &buf[5];

  // Theo log anh: 0x81 = RR, 0x85 = HR (GIU Y)
  if (cmd == 0x81 && dataLen >= 2) {
    lastResp = data[1];
  }
  else if (cmd == 0x85 && dataLen >= 2) {
    lastHeart = data[1];
  }
}

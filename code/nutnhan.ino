#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

// ================= WIFI =================
const char* WIFI_SSID = "iPhone";
const char* WIFI_PASS = "12341234";

// ================= MASTER (receiver) =================
IPAddress MASTER_IP(172,20,10,8);
const uint16_t MASTER_PORT_9000 = 9000;

WiFiUDP Udp;

// ================= TIMING =================
const uint32_t DEBOUNCE_MS = 30;
const uint32_t HOLD_MS     = 300;   // giữ 300ms

// ================= BUTTON STATE =================
struct Btn {
  uint8_t pin;
  int stableState;        // trạng thái ổn định hiện tại
  int lastReading;        // lần đọc gần nhất
  uint32_t tChange;       // thời điểm reading đổi
  uint32_t tPressStart;   // thời điểm bắt đầu nhấn
  bool longSent;          // đã gửi lệnh giữ chưa
};  // <-- phải có dấu ; ở đây

// ================= HELPERS =================
void sendCode(int v) {
  char msg[16];
  snprintf(msg, sizeof(msg), "%d", v);
  Udp.beginPacket(MASTER_IP, MASTER_PORT_9000);
  Udp.write((uint8_t*)msg, strlen(msg));
  Udp.endPacket();

  Serial.print("SEND -> ");
  Serial.println(msg);
}

// Nút thường: chỉ gửi 1 lần khi vừa nhấn xuống
void handleSinglePress(Btn &b, int code, uint32_t now) {
  int reading = digitalRead(b.pin);

  // phát hiện thay đổi thô
  if (reading != b.lastReading) {
    b.lastReading = reading;
    b.tChange = now;
  }

  // nếu trạng thái đã ổn định đủ lâu -> chấp nhận
  if ((now - b.tChange) >= DEBOUNCE_MS && b.stableState != b.lastReading) {
    b.stableState = b.lastReading;

    // chỉ gửi khi nhấn xuống HIGH -> LOW
    if (b.stableState == LOW) {
      sendCode(code);
    }
  }
}

// Nút có 2 kiểu:
// - bấm ngắn  -> shortCode
// - giữ >=300ms -> holdCode
// Mỗi lần nhấn chỉ gửi 1 lần
void handleHoldPress(Btn &b, int shortCode, int holdCode, uint32_t now) {
  int reading = digitalRead(b.pin);

  // phát hiện thay đổi thô
  if (reading != b.lastReading) {
    b.lastReading = reading;
    b.tChange = now;
  }

  // nếu trạng thái ổn định đủ lâu -> chấp nhận trạng thái mới
  if ((now - b.tChange) >= DEBOUNCE_MS && b.stableState != b.lastReading) {
    int oldState = b.stableState;
    b.stableState = b.lastReading;

    // vừa nhấn xuống
    if (oldState == HIGH && b.stableState == LOW) {
      b.tPressStart = now;
      b.longSent = false;
    }

    // vừa nhả ra
    if (oldState == LOW && b.stableState == HIGH) {
      // nếu chưa gửi lệnh giữ thì coi là bấm ngắn
      if (!b.longSent) {
        sendCode(shortCode);
      }
    }
  }

  // đang giữ nút, đủ 300ms và chưa gửi long press
  if (b.stableState == LOW && !b.longSent && (now - b.tPressStart >= HOLD_MS)) {
    sendCode(holdCode);
    b.longSent = true;
  }
}

// ================= BUTTONS =================
Btn b4 {4, HIGH, HIGH, 0, 0, false};
Btn b2 {2, HIGH, HIGH, 0, 0, false};
Btn b6 {6, HIGH, HIGH, 0, 0, false};
Btn b5 {5, HIGH, HIGH, 0, 0, false};

void setup() {
  Serial.begin(115200);
  delay(200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("SLAVE IP: ");
  Serial.println(WiFi.localIP());

  pinMode(b4.pin, INPUT_PULLUP);
  pinMode(b2.pin, INPUT_PULLUP);
  pinMode(b6.pin, INPUT_PULLUP);
  pinMode(b5.pin, INPUT_PULLUP);

  uint32_t now = millis();

  b4.stableState = digitalRead(b4.pin);
  b4.lastReading = b4.stableState;
  b4.tChange = now;

  b2.stableState = digitalRead(b2.pin);
  b2.lastReading = b2.stableState;
  b2.tChange = now;

  b6.stableState = digitalRead(b6.pin);
  b6.lastReading = b6.stableState;
  b6.tChange = now;

  b5.stableState = digitalRead(b5.pin);
  b5.lastReading = b5.stableState;
  b5.tChange = now;
}

void loop() {
  uint32_t now = millis();

  // Pin 4:
  // bấm ngắn -> +2
  // giữ 300ms -> +1
  handleHoldPress(b4, +2, +1, now);

  // Pin 2:
  // bấm ngắn -> -2
  // giữ 300ms -> -1
  handleHoldPress(b2, -2, -1, now);

  // Pin 6: gửi 4
  handleSinglePress(b6, 4, now);

  // Pin 5: gửi 3
  handleSinglePress(b5, 3, now);

  delay(1);
}
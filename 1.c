#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "HX711.h"
#include <WiFi.h>
#include <PubSubClient.h>

/* ================= OLED ================= */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SDA_PIN 21
#define SCL_PIN 22

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

/* ================= PINS ================= */
// Box 1
#define TRIG1 13
#define ECHO1 12
#define DT1   4

// Box 2
#define TRIG2 14
#define ECHO2 27
#define DT2   5

// Shared
#define HX_SCK 18
#define BUZZER 23

/* ================= OBJECTS ================= */
HX711 scale1;
HX711 scale2;

/* ================= SETTINGS ================= */
#define BOX_DEPTH 4.0
#define CAL1 420.0
#define CAL2 420.0
#define MONITOR_TIME 60000
#define UPDATE_TIME  500

/* ================= ALARMS ================= */
int alarmHour[2]   = {6, 9};
int alarmMinute[2] = {30, 0};
bool alarmDone[2] = {false, false};

/* ================= CLOCK ================= */
int hour = 8, minute = 59, second = 40;
unsigned long lastClock = 0;

/* ================= WIFI & MQTT ================= */
const char* ssid = "ACT-ai_101777424511";
const char* password = "81871049";

const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char* mqtt_topic = "smartbox/medicine";

WiFiClient espClient;
PubSubClient client(espClient);

/* ================= SETUP ================= */
void setup()
{
  Serial.begin(115200);

  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    while (1);

  display.setTextColor(WHITE);

  pinMode(TRIG1, OUTPUT);
  pinMode(ECHO1, INPUT);

  pinMode(TRIG2, OUTPUT);
  pinMode(ECHO2, INPUT);

  pinMode(BUZZER, OUTPUT);

  // HX711 setup
  scale1.begin(DT1, HX_SCK);
  scale2.begin(DT2, HX_SCK);

  scale1.set_scale(CAL1);
  scale2.set_scale(CAL2);

  scale1.tare();
  scale2.tare();

  showMessage("Smart Box", "Ready");
  delay(2000);

  // Connect WiFi
  WiFi.begin(ssid, password);

  display.clearDisplay();
  display.setCursor(0, 20);
  display.println("Connecting WiFi...");
  display.display();

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    display.print(".");
    display.display();
  }

  Serial.println("\nWiFi Connected!");

  display.println("Connected!");
  display.display();

  // Connect MQTT
  client.setServer(mqtt_server, mqtt_port);

  while (!client.connected()) {
    Serial.println("Connecting to MQTT...");
    if (client.connect("ESP32SmartBox")) {
      Serial.println("MQTT Connected!");
    } else {
      Serial.print("Failed rc=");
      Serial.println(client.state());
      delay(5000);
    }
  }
}

/* ================= LOOP ================= */
void loop()
{
  client.loop();

  updateClock();

  for (int i = 0; i < 2; i++) {
    if (hour == alarmHour[i] &&
        minute == alarmMinute[i] &&
        !alarmDone[i]) {

      startCheck(i);
      alarmDone[i] = true;
    }
  }

  if (hour == 0 && minute == 0) {
    alarmDone[0] = false;
    alarmDone[1] = false;
  }

  showClock();
  delay(200);
}

/* ================= FUNCTIONS ================= */

void updateClock() {

  if (millis() - lastClock >= 1000) {

    lastClock = millis();
    second++;

    if (second >= 60) { second = 0; minute++; }
    if (minute >= 60) { minute = 0; hour++; }
    if (hour >= 24) hour = 0;
  }
}

void beep(int ms) {

  digitalWrite(BUZZER, HIGH);
  delay(ms);
  digitalWrite(BUZZER, LOW);
}

float readDistance(int t, int e) {

  digitalWrite(t, LOW);
  delayMicroseconds(2);

  digitalWrite(t, HIGH);
  delayMicroseconds(10);
  digitalWrite(t, LOW);

  long d = pulseIn(e, HIGH, 30000);

  if (d == 0) return -1;

  return d * 0.034 / 2;
}

float getQty(int box) {

  float d = (box == 0) ?
            readDistance(TRIG1, ECHO1) :
            readDistance(TRIG2, ECHO2);

  if (d < 0) return 0;

  float q = (BOX_DEPTH - d) * 100 / BOX_DEPTH;

  return constrain(q, 0, 100);
}

float getWeight(int box) {

  float w = (box == 0) ?
            scale1.get_units(5) :
            scale2.get_units(5);

  if (w < 0) w = 0;

  return round(w * 10) / 10;
}

void showClock() {

  display.clearDisplay();

  display.setTextSize(2);
  display.setCursor(10, 20);

  display.printf("%02d:%02d:%02d", hour, minute, second);

  display.display();
}

void showMessage(String a, String b) {

  display.clearDisplay();

  display.setTextSize(1);

  display.setCursor(0, 20);
  display.println(a);

  display.setCursor(0, 35);
  display.println(b);

  display.display();
}

void startCheck(int box) {

  float w0 = getWeight(box);
  float q0 = getQty(box);

  beep(3000);

  unsigned long start = millis();
  bool taken = false;

  while (millis() - start < MONITOR_TIME) {

    float w = getWeight(box);
    float q = getQty(box);

    display.clearDisplay();
    display.setTextSize(1);

    display.setCursor(0, 0);
    display.print("BOX ");
    display.println(box + 1);

    display.setCursor(0, 15);
    display.print("Wt: ");
    display.print(w, 1);
    display.println(" g");

    display.setCursor(0, 30);
    display.print("Qty: ");
    display.print(q, 0);
    display.println(" %");

    if (abs(w - w0) > 1 || abs(q - q0) > 5) {

      taken = true;

      display.setCursor(0, 50);
      display.println("TAKEN");

    } else {

      display.setCursor(0, 50);
      display.println("WAITING...");
    }

    display.display();

    client.loop();
    delay(UPDATE_TIME);
  }

  /* ===== MQTT PAYLOAD WITH STATUS ===== */

  String payload = "{";

  payload += "\"box\":" + String(box + 1) + ",";
  payload += "\"weight_before\":" + String(w0, 1) + ",";
  payload += "\"weight_after\":" + String(getWeight(box), 1) + ",";
  payload += "\"qty_before\":" + String(q0, 0) + ",";
  payload += "\"qty_after\":" + String(getQty(box), 0) + ",";

  if (taken)
    payload += "\"status\":\"TAKEN\"";
  else
    payload += "\"status\":\"NOT_TAKEN\"";

  payload += "}";

  if (client.connected()) {

    client.publish(mqtt_topic, payload.c_str());

    Serial.println("Published:");
    Serial.println(payload);
  }

  if (taken)
    showMessage("Good Job", "Next Alarm");
  else
    showMessage("Not Taken", "Next Alarm");

  delay(4000);
}
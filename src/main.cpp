#include <Arduino.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"
#include "logic.h"

OneWire oneWire(PIN_ONEWIRE);
DallasTemperature temperatureSensor(&oneWire);
Adafruit_SSD1306 display(128, 64, &Wire, -1);
bool oledPresent = false;
bool sensorPresent = false;
TempState lastState = TempState::SensorFault;
bool hasState = false;
unsigned long previousRead = 0;
unsigned long previousStatus = 0;
unsigned long previousAlarm = 0;
float lastTemperature = NAN;

void showStatus(float temp, TempState state) {
  if (!oledPresent) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("VACCINE CARRIER DEMO");
  display.setTextSize(2);
  display.setCursor(0, 18);
  if (state == TempState::SensorFault) display.println("--.- C");
  else { display.print(temp, 1); display.println(" C"); }
  display.setTextSize(1);
  display.setCursor(0, 48);
  display.println(stateText(state));
  display.display();
}

// Alerts are events, not messages: this avoids repeated emails/SMS at every sensor read.
// Genuine delivery logic (Wi-Fi / SIM800), SD logging and timestamps are NOT implemented.
void recordEvent(TempState state, float temp) {
  Serial.print("EVENT: ");
  Serial.print(stateText(state));
  Serial.print(" temperatureC=");
  Serial.println(temp);
  Serial.println("PENDING: SD log and network notification are NOT implemented.");
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW); // Assumes active-high buzzer; verify actual hardware.
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  oledPresent = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!oledPresent) Serial.println("OLED not detected: serial monitor remains usable.");
  temperatureSensor.begin();
  sensorPresent = temperatureSensor.getDeviceCount() > 0;
  if (!sensorPresent) Serial.println("No DS18B20 found; SENSOR ERROR state expected.");
  Serial.println("Vaccine Carrier ESP32 DEMO: core logic only");
  Serial.println("NOT for use with real vaccines. 2-8 C range is configurable.");
}

void loop() {
  const unsigned long now = millis();
  if (!hasState || now - previousRead >= READ_INTERVAL_MS) {
    previousRead = now;
    // DS18B20 conversion can block for up to ~750ms at 12 bits.
    if (sensorPresent) {
      temperatureSensor.requestTemperatures();
      lastTemperature = temperatureSensor.getTempCByIndex(0);
    } else {
      lastTemperature = NAN;
      temperatureSensor.begin();
      sensorPresent = temperatureSensor.getDeviceCount() > 0;
    }
    TempState state = classifyTemperature(lastTemperature,TEMP_MIN_C,TEMP_MAX_C);
    const bool changed = !hasState || state != lastState;
    if (changed) {
      Serial.print("STATE CHANGE: ");
      Serial.println(stateText(state));
      recordEvent(state, lastTemperature);
      lastState = state;
      hasState = true;
    }
    const bool alarm = state != TempState::Normal;
    digitalWrite(PIN_BUZZER, alarm ? HIGH : LOW);
    if (alarm && (!previousAlarm || now - previousAlarm >= ALERT_REPEAT_MS)) {
      // Demonstrate a reminder event; not an actual remote delivery.
      Serial.print("ALARM REMINDER: ");
      Serial.println(stateText(state));
      previousAlarm = now;
    }
    if (!alarm) previousAlarm = 0;
    showStatus(lastTemperature, state);
  }
  if (now - previousStatus >= STATUS_INTERVAL_MS) {
    previousStatus = now;
    Serial.print("STATUS: ");
    Serial.print(stateText(lastState));
    Serial.print(" T=");
    Serial.println(lastTemperature);
  }
}

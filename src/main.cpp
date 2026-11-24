#include <Arduino.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"
#include "logic.h"
#include "sms_policy.h"

OneWire oneWire(PIN_ONEWIRE);
DallasTemperature temperatureSensor(&oneWire);
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
bool oledPresent = false;
bool sensorPresent = false;
AlertTracker alertTracker;
SmsPolicyTracker smsTracker;
uint32_t previousRead = 0;
uint32_t previousStatus = 0;
float lastTemperature = NAN;

void showStatus(float temp, TempState state) {
  if (!oledPresent) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("VACCINE");
  display.setCursor(82, 0);
  display.print("GSM --");  // Reserved: modem transport is not implemented.
  display.setTextSize(2);
  display.setCursor(0, 8);
  if (state == TempState::SensorFault) display.println("--.- C");
  else { display.print(temp, 1); display.println(" C"); }
  display.setTextSize(1);
  display.setCursor(0, 24);
  display.println(stateText(state));
  display.display();
}

// Local state-change diagnostics. SMS policy events below are not transmissions.
void recordEvent(TempState state, float temp) {
  Serial.print("EVENT: ");
  Serial.print(stateText(state));
  Serial.print(" temperatureC=");
  Serial.println(temp);
}

const char* smsEventText(SmsEventKind kind) {
  switch (kind) {
    case SmsEventKind::AbnormalTransition: return "ABNORMAL TRANSITION";
    case SmsEventKind::AbnormalReminder: return "ABNORMAL REMINDER";
    case SmsEventKind::Recovery: return "RECOVERY";
    case SmsEventKind::None: return "NONE";
  }
  return "UNKNOWN";
}

void setup() {
  Serial.begin(115200);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  oledPresent = display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS);
  if (!oledPresent) Serial.println("OLED not detected: serial monitor remains usable.");
  temperatureSensor.begin();
  sensorPresent = temperatureSensor.getDeviceCount() > 0;
  if (!sensorPresent) Serial.println("No DS18B20 found; SENSOR ERROR state expected.");
  Serial.println("Vaccine Carrier ESP32 DEMO: local monitoring and SMS policy");
  Serial.println("NOT for use with real vaccines. 2-8 C range is configurable.");
}

void loop() {
  const uint32_t now = static_cast<uint32_t>(millis());
  if (!alertTracker.hasPrevious || now - previousRead >= READ_INTERVAL_MS) {
    previousRead = now;
    lastTemperature = NAN;
    if (!sensorPresent) {
      temperatureSensor.begin();
      sensorPresent = temperatureSensor.getDeviceCount() > 0;
    }
    // DS18B20 conversion can block for up to ~750ms at 12 bits.
    // VC-005 modem operations must remain cooperative despite this limit.
    if (sensorPresent) {
      temperatureSensor.requestTemperatures();
      lastTemperature = temperatureSensor.getTempCByIndex(0);
      if (classifyTemperature(lastTemperature, TEMP_MIN_C, TEMP_MAX_C) ==
          TempState::SensorFault) {
        sensorPresent = false; // Rescan on the next polling cycle.
      }
    }
    const TempState state = classifyTemperature(lastTemperature, TEMP_MIN_C, TEMP_MAX_C);
    const uint32_t readingAt = static_cast<uint32_t>(millis());
    const AlertDecision alert = updateAlert(alertTracker, state, readingAt, ALERT_REPEAT_MS);
    const SmsEvent smsEvent = updateSmsPolicy(smsTracker, state, readingAt, SMS_REPEAT_MS);
    if (alert.stateChanged) {
      Serial.print("STATE CHANGE: ");
      Serial.println(stateText(state));
      recordEvent(state, lastTemperature);
    }
    if (alert.reminderDue) {
      // Demonstrate a reminder event; not an actual remote delivery.
      Serial.print("ALARM REMINDER: ");
      Serial.println(stateText(state));
    }
    if (smsEvent.kind != SmsEventKind::None) {
      Serial.print("SMS POLICY EVENT (NOT SENT): ");
      Serial.print(smsEventText(smsEvent.kind));
      Serial.print(" state=");
      Serial.println(stateText(smsEvent.state));
    }
    showStatus(lastTemperature, state);
  }
  if (now - previousStatus >= STATUS_INTERVAL_MS) {
    previousStatus = now;
    Serial.print("STATUS: ");
    Serial.print(stateText(alertTracker.previousState));
    Serial.print(" T=");
    Serial.println(lastTemperature);
  }
}

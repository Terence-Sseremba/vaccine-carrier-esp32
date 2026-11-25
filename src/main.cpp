#include <Arduino.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"
#include "logic.h"
#include "sms_policy.h"
#include "sim800_uart.h"
#if __has_include("local_sms_config.h")
#include "local_sms_config.h"
#else
#define SMS_RECIPIENT ""
#endif

OneWire oneWire(PIN_ONEWIRE);
DallasTemperature temperatureSensor(&oneWire);
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
bool oledPresent = false;
bool sensorPresent = false;
AlertTracker alertTracker;
SmsPolicyTracker smsTracker;
HardwareSerial gsmSerial(2);
Sim800Uart gsmUart(gsmSerial);
SmsTransport smsTransport(SMS_RECIPIENT);
uint32_t previousRead = 0;
uint32_t previousStatus = 0;
float lastTemperature = NAN;
TempState lastState = TempState::SensorFault;
bool firstSample = true;
bool conversionPending = false;
uint32_t conversionStarted = 0;
constexpr uint32_t DS18B20_CONVERSION_MS = 750U;

void showStatus(float temp, TempState state) {
  if (!oledPresent) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("VACCINE");
  display.setCursor(82, 0);
  const char* gsmLabel = smsTransport.statusLabel();
  if (smsTransport.phase() == SmsTransport::Phase::Ready) {
    if (smsTransport.lastOutcome() == SmsOutcome::SUBMITTED) gsmLabel = "ACCEPT";
    else if (smsTransport.lastOutcome() == SmsOutcome::AMBIGUOUS) gsmLabel = "UNCERT";
    else if (smsTransport.lastOutcome() == SmsOutcome::REJECTED) gsmLabel = "FAIL";
  }
  display.print(gsmLabel);
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

const char* smsOutcomeText(SmsOutcome outcome) {
  switch (outcome) {
    case SmsOutcome::NOT_ATTEMPTED: return "NOT_ATTEMPTED";
    case SmsOutcome::IN_PROGRESS: return "IN_PROGRESS";
    case SmsOutcome::SUBMITTED: return "SUBMITTED (MODEM ACK ONLY)";
    case SmsOutcome::REJECTED: return "REJECTED";
    case SmsOutcome::AMBIGUOUS: return "AMBIGUOUS (NO AUTO RETRY)";
    case SmsOutcome::DEFERRED: return "DEFERRED";
    case SmsOutcome::EXPIRED: return "EXPIRED";
  }
  return "UNKNOWN";
}

void setup() {
  Serial.begin(115200);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  oledPresent = display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS);
  if (!oledPresent) Serial.println("OLED not detected: serial monitor remains usable.");
  temperatureSensor.begin();
  temperatureSensor.setWaitForConversion(false);
  sensorPresent = temperatureSensor.getDeviceCount() > 0;
  if (sensorPresent) temperatureSensor.setResolution(12);
  if (smsTransport.enabled()) gsmUart.begin(GSM_UART_BAUD, PIN_GSM_RX, PIN_GSM_TX);
  else Serial.println("SMS disabled: local recipient is missing or invalid.");
  if (!sensorPresent) Serial.println("No DS18B20 found; SENSOR ERROR state expected.");
  Serial.println("Vaccine Carrier ESP32 DEMO: local monitoring and SIM800L transport");
  Serial.println("NOT for use with real vaccines. 2-8 C range is configurable.");
}

void processSample(float temperature, uint32_t now) {
  lastTemperature = temperature;
  const TempState state = classifyTemperature(temperature, TEMP_MIN_C, TEMP_MAX_C);
  lastState = state;
  const AlertDecision alert = updateAlert(alertTracker, state, now, ALERT_REPEAT_MS);
  const SmsEvent smsEvent = updateSmsPolicy(smsTracker, state, now, SMS_REPEAT_MS);
  if (alert.stateChanged) {
    Serial.print("STATE CHANGE: "); Serial.println(stateText(state));
    recordEvent(state, temperature);
  }
  if (alert.reminderDue) {
    Serial.print("ALARM REMINDER: "); Serial.println(stateText(state));
  }
  if (smsEvent.kind != SmsEventKind::None) {
    Serial.print("SMS EVENT GENERATED: "); Serial.print(smsEventText(smsEvent.kind));
    Serial.print(" state="); Serial.println(stateText(smsEvent.state));
  }
  const uint32_t beforeFull = smsTransport.droppedFull();
  const uint32_t beforeQueued = smsTransport.queued();
  smsTransport.onSample(state, smsEvent, now, gsmUart);
  if (smsTransport.queued() > beforeQueued) {
    Serial.print("SMS EVENT QUEUED: count="); Serial.println(smsTransport.queued());
  }
  if (smsTransport.droppedFull() != beforeFull)
    Serial.println("SMS QUEUE OVERFLOW: oldest event evicted.");
  showStatus(temperature, state);
}

void loop() {
  const uint32_t now = static_cast<uint32_t>(millis());
  const uint32_t priorOutcomeId = smsTransport.lastOutcomeId();
  const SmsOutcome priorOutcome = smsTransport.lastOutcome();
  const uint32_t priorStale = smsTransport.droppedStale();
  const uint32_t priorObsolete = smsTransport.droppedObsolete();
  const uint32_t priorReconnects = smsTransport.reconnects();
  const uint32_t priorAttempts = smsTransport.totalAttempts();
  const uint32_t priorTextFailures = smsTransport.textModeFailures();
  const uint32_t priorParserOverflows = smsTransport.parserOverflows();
  if (smsTransport.enabled()) gsmUart.service(smsTransport, now);

  if (!conversionPending && (firstSample || uint32_t(now - previousRead) >= READ_INTERVAL_MS)) {
    firstSample = false;
    previousRead = now;
    if (!sensorPresent) {
      temperatureSensor.begin();
      sensorPresent = temperatureSensor.getDeviceCount() > 0;
      if (sensorPresent) temperatureSensor.setResolution(12);
    }
    if (sensorPresent) {
      temperatureSensor.requestTemperatures(); // Nonblocking after setup configuration.
      conversionStarted = now;
      conversionPending = true;
    } else {
      processSample(NAN, now);
    }
  }
  if (conversionPending && uint32_t(now - conversionStarted) >= DS18B20_CONVERSION_MS) {
    conversionPending = false;
    const float temperature = temperatureSensor.getTempCByIndex(0);
    if (classifyTemperature(temperature, TEMP_MIN_C, TEMP_MAX_C) == TempState::SensorFault)
      sensorPresent = false; // Rescan before the next fresh conversion.
    processSample(temperature, now);
  }

  if (smsTransport.enabled()) {
    smsTransport.tick(now, gsmUart);
    gsmUart.service(smsTransport, now);
  }
  if (smsTransport.reconnects() != priorReconnects)
    Serial.println("GSM READY: awaiting fresh temperature sample.");
  if (smsTransport.textModeFailures() != priorTextFailures)
    Serial.println("GSM CONFIG REJECTED: text mode unavailable.");
  if (smsTransport.parserOverflows() != priorParserOverflows)
    Serial.println("GSM RESPONSE OVERFLOW: oversized line ignored.");
  if (smsTransport.droppedStale() != priorStale)
    Serial.println("SMS EVENT EXPIRED: old queued event discarded.");
  if (smsTransport.droppedObsolete() != priorObsolete)
    Serial.println("SMS EVENT DEFERRED: obsolete queued state discarded.");
  if (smsTransport.totalAttempts() != priorAttempts) {
    Serial.print("SMS ATTEMPT: event="); Serial.print(smsTransport.lastAttemptEventId());
    Serial.print(" attempt="); Serial.println(smsTransport.lastAttemptNumber());
  }
  if (smsTransport.lastOutcomeId() != priorOutcomeId || smsTransport.lastOutcome() != priorOutcome) {
    Serial.print("SMS OUTCOME: event="); Serial.print(smsTransport.lastOutcomeId());
    Serial.print(" result="); Serial.print(smsOutcomeText(smsTransport.lastOutcome()));
    if (smsTransport.lastCmsError() >= 0) {
      Serial.print(" CMS="); Serial.print(smsTransport.lastCmsError());
    }
    Serial.println();
  }
  if (uint32_t(now - previousStatus) >= STATUS_INTERVAL_MS) {
    previousStatus = now;
    Serial.print("STATUS: "); Serial.print(stateText(lastState));
    Serial.print(" T="); Serial.print(lastTemperature);
    Serial.print(" GSM="); Serial.print(smsTransport.statusLabel());
    Serial.print(" queued="); Serial.println(smsTransport.queued());
  }
}

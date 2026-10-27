#include <Arduino.h>
#include <math.h>
#include <WiFi.h>
#include <WebServer.h>

#define VIB_DO       27
#define CURRENT_PIN  35
#define RELAY_PIN    33
#define BUZZER_PIN   25
#define MOTION_PIN   32
#define TEMP_PIN     26

#define RELAY_ON  LOW
#define RELAY_OFF HIGH

volatile unsigned long pulses = 0;

void IRAM_ATTR vibrationPulse() {
  pulses++;
}

const int ABNORMAL_VIBRATION = 20;

const float CURRENT_MINIMUM = 0.35;
const float CURRENT_MAXIMUM = 0.70;

const unsigned long CURRENT_TIME = 1000;

const float ACS_SENSITIVITY = 0.185;

float zeroVoltage = 0.0;

const unsigned long STARTUP_TIME = 3000;

unsigned long motorStartTime = 0;

bool faultLatched = false;

WebServer server(80);

unsigned long webVibrationCount = 0;
float webCurrent = 0.0;
int webTemperature = 0;
String webStatus = "MOTOR STARTING";
String webReason = "No fault";
bool webMachineStarted = false;

bool motionWarning = false;
unsigned long motionHighStart = 0;
const unsigned long MOTION_CONFIRM_TIME = 500;
const unsigned long PIR_STARTUP_IGNORE = 10000;
unsigned long pirStartTime = 0;

bool temperatureWarning = false;
bool temperatureCritical = false;
bool currentWarning = false;
bool lowCurrent = false;
bool highCurrent = false;
unsigned long currentFaultStart = 0;
bool currentFaultTiming = false;

void handleRoot();
void handleData();
float readCurrent();
int readTemperature();
void triggerFault(const char* reason);

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(VIB_DO, INPUT);
  attachInterrupt(digitalPinToInterrupt(VIB_DO), vibrationPulse, RISING);

  pinMode(MOTION_PIN, INPUT);
  pinMode(TEMP_PIN, INPUT);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_OFF);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  analogReadResolution(12);
  analogSetPinAttenuation(CURRENT_PIN, ADC_11db);

  WiFi.mode(WIFI_AP);
  WiFi.softAP("machine", "YOUR_AP_PASSWORD");

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();

  Serial.println();
  
  Serial.println("   INDUSTRIAL MACHINE MONITOR");
 
  Serial.println();
  Serial.println("Web dashboard is ready.");
  Serial.println("WiFi name: machine");
  Serial.println("WiFi password: YOUR_AP_PASSWORD");
  Serial.println("Open http://192.168.4.1");
  Serial.println();
  Serial.println("MOTOR OFF");
  Serial.println("Calibrating ACS712...");
  Serial.println("Do not start motor.");

  delay(3000);

  long total = 0;

  for (int i = 0; i < 2000; i++) {
    total += analogRead(CURRENT_PIN);
    delay(2);
  }

  float average = total / 2000.0;

  zeroVoltage = average * 3.3 / 4095.0;

  Serial.print("Zero voltage: ");
  Serial.print(zeroVoltage, 3);
  Serial.println(" V");

  Serial.println();
  Serial.println("Calibration complete.");
  Serial.println("Starting machine...");

  digitalWrite(RELAY_PIN, RELAY_ON);

  webMachineStarted = true;
  webStatus = "MOTOR STARTING";
  motorStartTime = millis();
  pirStartTime = millis();

  Serial.println("RELAY = ON");
  Serial.println("Startup protection = 3 seconds");
  Serial.println("PIR stabilization = 10 seconds");
  Serial.println();
}

void loop() {
  server.handleClient();

  if (faultLatched) {
    digitalWrite(RELAY_PIN, RELAY_OFF);
    digitalWrite(BUZZER_PIN, HIGH);

    Serial.println("!!! MACHINE FAULT LOCKED !!!");
    Serial.println("Relay = OFF");
    Serial.println("Buzzer = ON");
    Serial.println("Restart ESP32 to reset.");

    delay(2000);
    return;
  }

  bool startupFinished = millis() - motorStartTime >= STARTUP_TIME;
  bool pirReady = millis() - pirStartTime >= PIR_STARTUP_IGNORE;
  int motionState = digitalRead(MOTION_PIN);

  if (pirReady) {
    if (motionState == HIGH) {
      if (motionHighStart == 0) {
        motionHighStart = millis();
        Serial.println("PIR: motion signal detected...");
      }

      if (millis() - motionHighStart >= MOTION_CONFIRM_TIME) {
        motionWarning = true;
        triggerFault("PERSON DETECTED IN MACHINE ZONE");
        return;
      }
    } else {
      motionHighStart = 0;
      motionWarning = false;
    }
  }

  noInterrupts();
  pulses = 0;
  interrupts();

  delay(500);

  noInterrupts();
  unsigned long vibrationCount = pulses;
  interrupts();

  if (pirReady) {
    motionState = digitalRead(MOTION_PIN);

    if (motionState == HIGH) {
      if (motionHighStart == 0) {
        motionHighStart = millis();
      }

      if (millis() - motionHighStart >= MOTION_CONFIRM_TIME) {
        motionWarning = true;
        triggerFault("PERSON DETECTED IN MACHINE ZONE");
        return;
      }
    } else {
      motionHighStart = 0;
      motionWarning = false;
    }
  }

  float current = readCurrent();
  float currentMagnitude = fabs(current);
  int temperature = readTemperature();

  webVibrationCount = vibrationCount;
  webCurrent = currentMagnitude;
  webTemperature = temperature;

  Serial.println();
  Serial.println("--------------------------------------");
  Serial.print("Vibration pulses : ");
  Serial.println(vibrationCount);
  Serial.print("Current          : ");
  Serial.print(current, 2);
  Serial.println(" A");
  Serial.print("Current magnitude: ");
  Serial.print(currentMagnitude, 2);
  Serial.println(" A");
  Serial.print("NTC sensor O     : ");
  Serial.println(temperature);
  Serial.print("PIR              : ");
  Serial.println(motionState == HIGH ? "MOTION" : "NO MOTION");

  if (!startupFinished) {
    webStatus = "MOTOR STARTING";
    Serial.println("STATUS: MOTOR STARTING");
    return;
  }

  lowCurrent = currentMagnitude < CURRENT_MINIMUM;
  highCurrent = currentMagnitude > CURRENT_MAXIMUM;
  currentWarning = lowCurrent || highCurrent;

  if (temperature == 1) {
    temperatureWarning = true;
  } else {
    temperatureWarning = false;
  }

  temperatureCritical = temperatureWarning;

  if (currentWarning) {
    webStatus = "CURRENT WARNING";
  } else if (temperatureWarning) {
    webStatus = "HIGH TEMPERATURE";
  } else {
    webStatus = "NORMAL";
  }

  webReason = "No fault";

  if (temperatureCritical) {
    triggerFault("HIGH TEMPERATURE");
    return;
  }

  if (vibrationCount > ABNORMAL_VIBRATION) {
    triggerFault("ABNORMAL VIBRATION");
    return;
  }

  if (currentWarning && temperatureWarning) {
    triggerFault("ABNORMAL CURRENT + HIGH TEMPERATURE");
    return;
  }

  if (currentWarning) {
    Serial.println();
    Serial.println("WARNING: ABNORMAL CURRENT");

    if (lowCurrent) {
      Serial.print("Current BELOW minimum: ");
      Serial.print(CURRENT_MINIMUM);
      Serial.println(" A");
    }

    if (highCurrent) {
      Serial.print("Current ABOVE maximum: ");
      Serial.print(CURRENT_MAXIMUM);
      Serial.println(" A");
    }

    Serial.println("Machine continues running.");
  }

  if (temperatureWarning) {
    Serial.println();
    Serial.println("WARNING: HIGH TEMPERATURE");
    Serial.println("Machine stopped.");
  }

  if (!currentWarning && !temperatureWarning && vibrationCount <= ABNORMAL_VIBRATION) {
    Serial.println("MACHINE NORMAL");
  }

  delay(100);
}

float readCurrent() {
  long total = 0;

  const int samples = 300;

  for (int i = 0; i < samples; i++) {
    total += analogRead(CURRENT_PIN);
    delayMicroseconds(500);
  }

  float average = total / (float)samples;

  float voltage = average * 3.3 / 4095.0;

  float current = (voltage - zeroVoltage) / ACS_SENSITIVITY;

  if (fabs(current) < 0.03) {
    current = 0.0;
  }

  return current;
}

int readTemperature() {
  int state = digitalRead(TEMP_PIN);

  if (state == HIGH) {
    Serial.println("NTC: HIGH TEMPERATURE");
    return 1;
  } else {
    Serial.println("NTC: TEMPERATURE NORMAL");
    return 0;
  }
}

void handleRoot() {
  server.send(200, "text/html", R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Machine Monitor</title>
<style>
body {
  font-family: Arial;
  max-width: 700px;
  margin: 24px auto;
  padding: 0 16px;
  background: #f4f6f8;
  color: #222;
}
h1 { font-size: 26px; }
.status {
  padding: 14px;
  border-radius: 8px;
  font-weight: bold;
  margin: 16px 0;
}
.normal { background: #d9f7df; color: #14532d; }
.warning { background: #fff0c2; color: #854d0e; }
.danger { background: #ffdede; color: #991b1b; }
.grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 12px;
}
.card {
  background: white;
  padding: 16px;
  border-radius: 8px;
  box-shadow: 0 1px 4px #0001;
}
.label { color: #666; font-size: 14px; }
.value { font-size: 23px; font-weight: bold; margin-top: 8px; }
.details {
  background: white;
  padding: 16px;
  border-radius: 8px;
  margin-top: 12px;
  line-height: 1.8;
}
@media (max-width: 480px) {
  .grid { grid-template-columns: 1fr; }
}
</style>
</head>
<body>
<h1>Industrial Machine Monitor</h1>
<p>Live sensor readings</p>

<div id="status" class="status warning">Connecting to ESP32...</div>

<div class="grid">
  <div class="card">
    <div class="label">Vibration</div>
    <div class="value"><span id="vibration">--</span> pulses</div>
  </div>
  <div class="card">
    <div class="label">Current</div>
    <div class="value"><span id="current">--</span> A</div>
  </div>
  <div class="card">
    <div class="label">Temperature Sensor</div>
    <div class="value" id="temperature">--</div>
  </div>
  <div class="card">
    <div class="label">PIR Motion</div>
    <div class="value" id="motion">--</div>
  </div>
  <div class="card">
    <div class="label">Motor Relay</div>
    <div class="value" id="relay">--</div>
  </div>
  <div class="card">
    <div class="label">Buzzer</div>
    <div class="value" id="buzzer">--</div>
  </div>
</div>

<div class="details">
  <div>Message: <strong id="reason">Loading...</strong></div>
</div>

<script>
async function updateDashboard() {
  try {
    const response = await fetch("/data");
    const data = await response.json();

    const status = document.getElementById("status");
    status.textContent = data.status;

    if (data.status === "NORMAL") {
      status.className = "status normal";
    } else if (data.status === "MACHINE STOPPED") {
      status.className = "status danger";
    } else {
      status.className = "status warning";
    }

    document.getElementById("vibration").textContent = data.vibration;
    document.getElementById("current").textContent = Number(data.current).toFixed(2);
    document.getElementById("temperature").textContent = data.temperature === 1 ? "HIGH" : "NORMAL";
    document.getElementById("motion").textContent = data.motion;
    document.getElementById("relay").textContent = data.relay;
    document.getElementById("buzzer").textContent = data.buzzer;
    document.getElementById("reason").textContent = data.reason;
  } catch (error) {
    document.getElementById("status").textContent = "Connection lost. Check WiFi.";
    document.getElementById("status").className = "status danger";
  }
}

updateDashboard();
setInterval(updateDashboard, 1000);
</script>
</body>
</html>
)rawliteral");
}

void handleData() {
  String json = "{";

  json += "\"status\":\"" + webStatus + "\",";
  json += "\"reason\":\"" + webReason + "\",";
  json += "\"vibration\":" + String(webVibrationCount) + ",";
  json += "\"current\":" + String(webCurrent, 2) + ",";
  json += "\"temperature\":" + String(webTemperature) + ",";
  json += "\"motion\":\"" + String(motionWarning ? "DETECTED" : "NO MOTION") + "\",";
  json += "\"relay\":\"" + String(webMachineStarted && !faultLatched ? "ON" : "OFF") + "\",";
  json += "\"buzzer\":\"" + String(faultLatched ? "ON" : "OFF") + "\"";

  json += "}";

  server.send(200, "application/json", json);
}

void triggerFault(const char* reason) {
  webStatus = "MACHINE STOPPED";
  webReason = String(reason);
  webMachineStarted = false;

  Serial.println();
  
  Serial.println("       MACHINE STOPPED");
  Serial.print("Reason: ");
  Serial.println(reason);

  

  digitalWrite(RELAY_PIN, RELAY_OFF);
  digitalWrite(BUZZER_PIN, HIGH);

  faultLatched = true;

  Serial.println("Relay  : OFF");
  Serial.println("Buzzer : ON");
  Serial.println("Fault  : CRITICAL / LOCKED");
  Serial.println("Restart ESP32 to reset.");
}

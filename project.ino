#define BLYNK_TEMPLATE_ID "TMPL6Ccw0Z9-J"
#define BLYNK_TEMPLATE_NAME "Home Automation"
#define BLYNK_AUTH_TOKEN "zY69Jh6i03UnS85cUEVLls3Dp5EpZXIO"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <ESP32Servo.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>

// WiFi credentials
char ssid[] = "Your_WiFi_Name";
char pass[] = "Password";

// Pin Definitions
#define TRIG_PIN 17
#define ECHO_PIN 16
#define DOOR_SERVO_PIN 13
#define DOOR_BUTTON_PIN 14
#define RAIN_SENSOR_PIN 27
#define CLOTHESLINE_SERVO_PIN 26
#define DHT_PIN 4
#define DHT_TYPE DHT22
#define LDR_PIN 32
#define LIGHT_PIN 33
#define LIGHT_BUTTON_PIN 35

// Objects
Servo doorServo;
Servo clotheslineServo;
DHT dht(DHT_PIN, DHT_TYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Variables
bool autoMode = true;
bool doorOpen = false;
bool autoLightMode = true;
bool lightOn = false;
bool clotheslineOut = true;  // Start extended (clothesline out)

unsigned long lastTempRead = 0;
unsigned long lastUltrasonicRead = 0;
unsigned long lastLightRead = 0;
const long tempInterval = 2000;
const long ultrasonicInterval = 200;
const long lightInterval = 500;

int doorButtonState = HIGH;
int doorLastButtonState = HIGH;
int lightButtonState = HIGH;
int lightLastButtonState = HIGH;
unsigned long doorLastDebounceTime = 0;
unsigned long lightLastDebounceTime = 0;
const unsigned long debounceDelay = 50;

float temperature = 0;
float humidity = 0;

unsigned long lastRainCheck = 0;
int lastRainState = HIGH;
bool rainCheckInitialized = false;  // NEW: Track if we've done initial rain check

unsigned long doorOpenTime = 0;
const long doorOpenDuration = 5000;

unsigned long doorStatusDisplayTime = 0;
bool showingDoorStatus = false;
const long doorStatusDisplayDuration = 2000;

bool lastShowingDoorStatus = false;
bool lastDoorOpenState = false;
float lastDisplayedTemp = -999;
float lastDisplayedHum = -999;

void setup() {
  Serial.begin(115200);
  
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(DOOR_BUTTON_PIN, INPUT_PULLUP);
  pinMode(RAIN_SENSOR_PIN, INPUT);
  pinMode(LDR_PIN, INPUT);
  pinMode(LIGHT_PIN, OUTPUT);
  pinMode(LIGHT_BUTTON_PIN, INPUT_PULLUP);
  
  digitalWrite(LIGHT_PIN, LOW);
  
  doorServo.attach(DOOR_SERVO_PIN);
  clotheslineServo.attach(CLOTHESLINE_SERVO_PIN);
  doorServo.write(90);
  clotheslineServo.write(180);  // Start extended at 180 degrees (no rain position)
  
  dht.begin();
  
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Home Automation");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");
  delay(2000);
  lcd.clear();

  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  
  // Initialize rain sensor state
  lastRainState = digitalRead(RAIN_SENSOR_PIN);
  Serial.print("Initial rain state: ");
  Serial.println(lastRainState == LOW ? "WET" : "DRY");
  
  Serial.println("System Initialized");
}

void loop() {
  Blynk.run();
  unsigned long currentMillis = millis();
  
  handleDoorSystem(currentMillis);
  handleClotheslineSystem();
  handleTemperatureSystem(currentMillis);
  handleLightSystem(currentMillis);
  updateLCD(currentMillis);
}

void handleDoorSystem(unsigned long currentMillis) {
  int doorReading = digitalRead(DOOR_BUTTON_PIN);
  
  if (doorReading != doorLastButtonState) {
    doorLastDebounceTime = currentMillis;
  }
  
  if ((currentMillis - doorLastDebounceTime) > debounceDelay) {
    if (doorReading != doorButtonState) {
      doorButtonState = doorReading;
      if (doorButtonState == LOW) {
        toggleDoor();
      }
    }
  }
  doorLastButtonState = doorReading;
  
  if (autoMode && (currentMillis - lastUltrasonicRead >= ultrasonicInterval)) {
    lastUltrasonicRead = currentMillis;
    long distance = getUltrasonicDistance();
    
    if (distance > 0 && distance < 20 && !doorOpen) {
      openDoor();
      doorOpenTime = currentMillis;
    }
  }
  
  if (autoMode && doorOpen && (currentMillis - doorOpenTime >= doorOpenDuration)) {
    long distance = getUltrasonicDistance();
    if (distance >= 20 || distance == -1) {
      closeDoor();
    } else {
      doorOpenTime = currentMillis;
    }
  }
}

long getUltrasonicDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return -1;
  
  long distance = duration * 0.034 / 2;
  return distance;
}

void openDoor() {
  doorServo.write(0);
  doorOpen = true;
  Blynk.virtualWrite(V2, "Open");
  Serial.println("Door Opened");
  showingDoorStatus = true;
  doorStatusDisplayTime = millis();
}

void closeDoor() {
  doorServo.write(90);
  doorOpen = false;
  Blynk.virtualWrite(V2, "Closed");
  Serial.println("Door Closed");
  showingDoorStatus = true;
  doorStatusDisplayTime = millis();
}

void toggleDoor() {
  if (doorOpen) {
    closeDoor();
  } else {
    openDoor();
  }
}

void handleClotheslineSystem() {
  unsigned long currentMillis = millis();
  
  if (currentMillis - lastRainCheck >= 1000) {
    lastRainCheck = currentMillis;
    int rainDetected = digitalRead(RAIN_SENSOR_PIN);
    
    // FIXED: Only move servo when state actually changes
    if (rainDetected != lastRainState || !rainCheckInitialized) {
      lastRainState = rainDetected;
      rainCheckInitialized = true;
      
      if (rainDetected == LOW && clotheslineOut) {
        Serial.println("RAIN DETECTED - Retracting clothesline");
        clotheslineServo.write(90);  // Retracted position (0 degrees)
        clotheslineOut = false;
        Blynk.virtualWrite(V3, "Retracted");
      } 
      else if (rainDetected == HIGH && !clotheslineOut) {
        Serial.println("NO RAIN - Extending clothesline");
        clotheslineServo.write(180);  // Extended position (180 degrees - opposite direction)
        clotheslineOut = true;
        Blynk.virtualWrite(V3, "Extended");
      }
    }
  }
}

void handleTemperatureSystem(unsigned long currentMillis) {
  if (currentMillis - lastTempRead >= tempInterval) {
    lastTempRead = currentMillis;
    temperature = dht.readTemperature();
    humidity = dht.readHumidity();
    
    if (isnan(temperature) || isnan(humidity)) {
      Serial.println("DHT Sensor Error");
    } else {
      Blynk.virtualWrite(V4, temperature);
      Blynk.virtualWrite(V5, humidity);
      Serial.print("Temperature: ");
      Serial.print(temperature);
      Serial.print("°C, Humidity: ");
      Serial.print(humidity);
      Serial.println("%");
    }
  }
}

void updateLCD(unsigned long currentMillis) {
  if (showingDoorStatus && (currentMillis - doorStatusDisplayTime >= doorStatusDisplayDuration)) {
    showingDoorStatus = false;
  }
  
  if (showingDoorStatus) {
    if (lastDoorOpenState != doorOpen || !lastShowingDoorStatus) {
      lcd.clear();
      lcd.setCursor(0, 0);
      if (doorOpen) {
        lcd.print("Door Opened");
      } else {
        lcd.print("Door Closed");
      }
      lcd.setCursor(0, 1);
      lcd.print("                ");
      lastDoorOpenState = doorOpen;
    }
  } 
  else if (currentMillis - lastTempRead < tempInterval || lastTempRead > 0) {
    if (!isnan(temperature) && !isnan(humidity)) {
      if (fabs(temperature - lastDisplayedTemp) > 0.1 || 
          fabs(humidity - lastDisplayedHum) > 0.1 || 
          lastShowingDoorStatus) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Temp: ");
        lcd.print(temperature, 1);
        lcd.print((char)223);
        lcd.print("C");
        
        lcd.setCursor(0, 1);
        lcd.print("Hum: ");
        lcd.print(humidity, 1);
        lcd.print("%");
        
        lastDisplayedTemp = temperature;
        lastDisplayedHum = humidity;
      }
    } else {
      if (lastDisplayedTemp != -999 || lastShowingDoorStatus) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Sensor Error");
        lastDisplayedTemp = -999;
        lastDisplayedHum = -999;
      }
    }
  }
  
  lastShowingDoorStatus = showingDoorStatus;
}

void handleLightSystem(unsigned long currentMillis) {
  int lightReading = digitalRead(LIGHT_BUTTON_PIN);
  
  if (lightReading != lightLastButtonState) {
    lightLastDebounceTime = currentMillis;
  }
  
  if ((currentMillis - lightLastDebounceTime) > debounceDelay) {
    if (lightReading != lightButtonState) {
      lightButtonState = lightReading;
      if (lightButtonState == LOW) {
        toggleLight();
      }
    }
  }
  lightLastButtonState = lightReading;
  
  if (autoLightMode && (currentMillis - lastLightRead >= lightInterval)) {
    lastLightRead = currentMillis;
    int ldrValue = digitalRead(LDR_PIN);
    
    if (ldrValue == HIGH && !lightOn) {
      turnOnLight();
    } else if (ldrValue == LOW && lightOn) {
      turnOffLight();
    }
  }
}

void turnOnLight() {
  digitalWrite(LIGHT_PIN, HIGH);
  lightOn = true;
  Blynk.virtualWrite(V7, "ON");
  Serial.println("Light turned ON");
}

void turnOffLight() {
  digitalWrite(LIGHT_PIN, LOW);
  lightOn = false;
  Blynk.virtualWrite(V7, "OFF");
  Serial.println("Light turned OFF");
}

void toggleLight() {
  if (lightOn) {
    turnOffLight();
  } else {
    turnOnLight();
  }
}

BLYNK_WRITE(V0) {
  autoMode = param.asInt();
  Serial.print("Door Mode: ");
  Serial.println(autoMode ? "Auto" : "Manual");
}

BLYNK_WRITE(V1) {
  int doorControl = param.asInt();
  if (doorControl == 1 && !doorOpen) {
    openDoor();
  } else if (doorControl == 0 && doorOpen) {
    closeDoor();
  }
}

BLYNK_WRITE(V6) {
  int lightControl = param.asInt();
  if (lightControl == 1) {
    turnOnLight();
  } else {
    turnOffLight();
  }
}

BLYNK_WRITE(V8) {
  autoLightMode = param.asInt();
  Serial.print("Light Mode: ");
  Serial.println(autoLightMode ? "Auto" : "Manual");
}

BLYNK_CONNECTED() {
  Blynk.syncVirtual(V0, V1, V6, V8);
}
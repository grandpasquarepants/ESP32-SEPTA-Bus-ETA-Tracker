// ====== SEPTA Bus ETA Display ======
// ====== 11/23/2024 ======

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <vector>
#include <algorithm>

// WiFi credentials
const char* ssid = ""; // Your WiFi SSID
const char* password = ""; // Your WiFi password

// 7-segment display pins
#define DATA_PIN 13
#define SRCK_PIN 12
#define RCK_PIN 14

// ====== SEPTA configuration ======
// The easiest way to find this data is to use SEPTA's website. 
// https://www.septa.org/realtime-map/stop/10266?routes=48&focused=48&direction=0

const String ROUTE_ID = "48"; // Route ID (48)
const String DIRECTION_ID = "0"; // Direction ID (Southbound)
const String STOP_ID = "10266"; // Stop ID (Market St & 15th St)

// What order to show the displays
enum DisplayState {
    SHOW_TIME,
    SHOW_ROUTE_NUM,
    SHOW_BUS,
    SHOW_ETA_TEXT,
    SHOW_ETA
};

// Time configuration
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -18000;  // Timezone offset in seconds (EST: UTC-5 * 3600)
const int daylightOffset_sec = 3600; // Daylight savings offset in seconds (1 hour)

// Display timing intervals
const unsigned long TIME_DISPLAY_DURATION = 3000; // Time display duration (3 seconds)
const unsigned long ROUTE_NUM_DURATION = 1000;    // Route number display duration (1 second)
const unsigned long BUS_TEXT_DURATION = 1000;     // "BUS" display duration (1 second)
const unsigned long ETA_TEXT_DURATION = 1000;     // "ETA" display duration (1 second)
const unsigned long ETA_DISPLAY_DURATION = 6000;  // ETA display duration (6 seconds)
const unsigned long FLASH_INTERVAL = 500;         // ETA flash duration (half second)

// Data fetching intervals
const unsigned long TIME_UPDATE_INTERVAL = 1000;  // Update time every second
const unsigned long SEPTA_UPDATE_INTERVAL = 10000; // 10 seconds between SEPTA checks

// Task handles
TaskHandle_t septaTaskHandle = NULL;
TaskHandle_t displayTaskHandle = NULL;

// Shared variables protected by mutex
SemaphoreHandle_t septaDataMutex = NULL;
time_t soonestETA = -1;
bool validBusFound = false;

const byte CHARS[128] = {
    ~0x3F, ~0x06, ~0x5B, ~0x4F, ~0x66,  // 0-4
    ~0x6D, ~0x7D, ~0x07, ~0x7F, ~0x6F,  // 5-9
    ~0x77, ~0x7C, ~0x39, ~0x5E, ~0x79,  // A-E
    ~0x71, ~0x3D, ~0x76, ~0x06, ~0x1E,  // F-J
    ~0x76, ~0x38, ~0x37, ~0x54, ~0x3F,  // K-O
    ~0x73, ~0x67, ~0x50, ~0x6D, ~0x78,  // P-T
    ~0x3E, ~0x3E, ~0x7E, ~0x76, ~0x6E,  // U-Y
    ~0x5B,                               // Z
    ~0x40, ~0x80, 0xFF                   // -,.space
};
const byte BLANK = 0xFF;

void sendByte(byte data) {
  for (int i = 7; i >= 0; i--) {
    digitalWrite(DATA_PIN, (data >> i) & 1);
    digitalWrite(SRCK_PIN, HIGH);
    digitalWrite(SRCK_PIN, LOW);
  }
}

void displayDigits(byte d1, byte d2, byte d3, byte d4, bool colon) {
  digitalWrite(RCK_PIN, LOW);
  sendByte(d1);
  sendByte(colon ? (d2 & ~0x80) : d2);
  sendByte(d3);
  sendByte(d4);
  digitalWrite(RCK_PIN, HIGH);
}

byte getCharPattern(char c) {
  if (c >= '0' && c <= '9') return CHARS[c - '0'];
  if (c >= 'A' && c <= 'Z') return CHARS[c - 'A' + 10];
  if (c >= 'a' && c <= 'z') return CHARS[c - 'a' + 10];
  if (c == '-') return ~0x40;
  if (c == '.') return ~0x80;
  return BLANK;
}

void displayTask(void * parameter) {
    unsigned long lastUpdate = 0;
    unsigned long lastStateChange = 0;
    unsigned long lastFlash = 0;
    unsigned long lastTimeSync = 0;
    const unsigned long TIME_RESYNC_INTERVAL = 3600000;
    DisplayState currentState = SHOW_TIME;
    bool shouldShowBusInfo = false;
    bool flashState = true;
    
    while(true) {
        unsigned long currentMillis = millis();
        
        if (currentMillis - lastTimeSync >= TIME_RESYNC_INTERVAL) {
            configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
            lastTimeSync = currentMillis;
        }
        
        xSemaphoreTake(septaDataMutex, portMAX_DELAY);
        shouldShowBusInfo = validBusFound;
        xSemaphoreGive(septaDataMutex);
        
        if (shouldShowBusInfo) {
            switch(currentState) {
                case SHOW_TIME:
                    if (currentMillis - lastUpdate >= 1000) {
                        displayCurrentTime();
                        lastUpdate = currentMillis;
                    }
                    if (currentMillis - lastStateChange >= TIME_DISPLAY_DURATION) {
                        currentState = SHOW_ROUTE_NUM;
                        lastStateChange = currentMillis;
                    }
                    break;
                    
                case SHOW_ROUTE_NUM:
                    displayRouteNumber();
                    if (currentMillis - lastStateChange >= ROUTE_NUM_DURATION) {
                        currentState = SHOW_BUS;
                        lastStateChange = currentMillis;
                    }
                    break;
                    
                case SHOW_BUS:
                    displayBusText();
                    if (currentMillis - lastStateChange >= BUS_TEXT_DURATION) {
                        currentState = SHOW_ETA_TEXT;
                        lastStateChange = currentMillis;
                    }
                    break;
                    
                case SHOW_ETA_TEXT:
                    displayEtaText();
                    if (currentMillis - lastStateChange >= ETA_TEXT_DURATION) {
                        currentState = SHOW_ETA;
                        lastStateChange = currentMillis;
                    }
                    break;
                    
                case SHOW_ETA:
                    if (currentMillis - lastFlash >= FLASH_INTERVAL) {
                        flashState = !flashState;
                        displayETA();
                        lastFlash = currentMillis;
                    }
                    if (currentMillis - lastStateChange >= ETA_DISPLAY_DURATION) {
                        currentState = SHOW_TIME;
                        lastStateChange = currentMillis;
                    }
                    break;
            }
        } else {
            if (currentMillis - lastUpdate >= 1000) {
                displayCurrentTime();
                lastUpdate = currentMillis;
                currentState = SHOW_TIME;
            }
        }
        
        vTaskDelay(1);
    }
}

void displayCurrentTime() {
    static bool timeInitialized = false;
    static int failedAttempts = 0;
    const int MAX_FAILED_ATTEMPTS = 5;  // Try 5 times before showing error
    struct tm timeinfo;

    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        failedAttempts++;
        
        if (failedAttempts >= MAX_FAILED_ATTEMPTS) {
            // Show error pattern after multiple failed attempts
            byte dash = getCharPattern('-');
            displayDigits(dash, dash, dash, dash, false);
        }
        
        // Try to reinitialize time sync
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        return;
    }

    // Reset counters if we successfully got the time
    failedAttempts = 0;
    timeInitialized = true;
    
    // Convert to 12-hour format
    int hour12 = timeinfo.tm_hour % 12;
    if (hour12 == 0) hour12 = 12;  // Handle midnight/noon
    
    char timeStr[5];
    sprintf(timeStr, "%2d%02d", hour12, timeinfo.tm_min);
    
    byte d1 = (hour12 < 10) ? BLANK : getCharPattern(timeStr[0]);
    byte d2 = getCharPattern(timeStr[1]);
    byte d3 = getCharPattern(timeStr[2]);
    byte d4 = getCharPattern(timeStr[3]);
    
    static bool colonState = false;
    colonState = !colonState;  // Toggle colon every second
    
    displayDigits(d1, d2, d3, d4, colonState);
}

void displayRouteNumber() {
    String route = ROUTE_ID;
    byte d1 = getCharPattern(route[0]);
    byte d2 = getCharPattern(route[1]);
    byte d3 = BLANK;
    byte d4 = BLANK;
    displayDigits(d1, d2, d3, d4, false);
}

void displayBusText() {
    byte d1 = getCharPattern('b');
    byte d2 = getCharPattern('u');
    byte d3 = getCharPattern('s');
    byte d4 = BLANK;
    displayDigits(d1, d2, d3, d4, false);
}

void displayEtaText() {
    byte d1 = getCharPattern('e');
    byte d2 = getCharPattern('t');
    byte d3 = getCharPattern('a');
    byte d4 = BLANK;
    displayDigits(d1, d2, d3, d4, false);
}

void displayETA() {
    static bool showDisplay = true;
    time_t now;
    time(&now);
    
    xSemaphoreTake(septaDataMutex, portMAX_DELAY);
    time_t currentETA = soonestETA;
    bool hasValidBus = validBusFound;
    xSemaphoreGive(septaDataMutex);
    
    if (currentETA > 0 && hasValidBus) {
        // Calculate time difference in seconds
        int secondsUntilArrival = currentETA - now;
        
        // Round up to nearest minute
        int minutesUntilArrival = (secondsUntilArrival + 59) / 60;
        
        if (minutesUntilArrival >= 0 && minutesUntilArrival < 120) {
            if (showDisplay) {
                // Display as "  :XX"
                byte d1 = BLANK;
                byte d2 = BLANK;
                byte d3 = getCharPattern('0' + (minutesUntilArrival / 10));
                byte d4 = getCharPattern('0' + (minutesUntilArrival % 10));
                displayDigits(d1, d2, d3, d4, true);  // true to show colon
            } else {
                // All blank when flashing off
                displayDigits(BLANK, BLANK, BLANK, BLANK, false);
            }
        } else {
            // Show ---- for any invalid state
            byte dash = getCharPattern('-');
            displayDigits(dash, dash, dash, dash, false);
        }
    } else {
        // Show ---- for any invalid state
        byte dash = getCharPattern('-');
        displayDigits(dash, dash, dash, dash, false);
    }
    
    showDisplay = !showDisplay;
}

void septaTask(void * parameter) {
  while(true) {
    fetchSeptaData();
    vTaskDelay(pdMS_TO_TICKS(SEPTA_UPDATE_INTERVAL));
  }
}

void fetchSeptaData() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Can't fetch SEPTA data - WiFi disconnected");
    xSemaphoreTake(septaDataMutex, portMAX_DELAY);
    validBusFound = false;
    xSemaphoreGive(septaDataMutex);
    return;
  }
  
  HTTPClient http;
  String url = "https://www3.septa.org/api/v2/trips/?route_id=" + ROUTE_ID;
  
  Serial.println("\nFetching SEPTA data...");
  
  http.setTimeout(5000);
  http.begin(url);
  http.addHeader("User-Agent", "ESP32");
  http.addHeader("Accept", "application/json");
  
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    
    if (payload.length() > 0) {
      DynamicJsonDocument doc(32768);
      DeserializationError error = deserializeJson(doc, payload);
      
      if (!error) {
        soonestETA = processSeptaData(doc);
      } else {
        Serial.println("JSON parsing failed: " + String(error.c_str()));
        xSemaphoreTake(septaDataMutex, portMAX_DELAY);
        validBusFound = false;
        xSemaphoreGive(septaDataMutex);
      }
    } else {
      Serial.println("Empty response received");
      xSemaphoreTake(septaDataMutex, portMAX_DELAY);
      validBusFound = false;
      xSemaphoreGive(septaDataMutex);
    }
  } else {
    Serial.println("HTTP request failed with code: " + String(httpCode));
    if (httpCode < 0) {
      Serial.println("Error: " + http.errorToString(httpCode));
    }
    xSemaphoreTake(septaDataMutex, portMAX_DELAY);
    validBusFound = false;
    xSemaphoreGive(septaDataMutex);
  }
  
  http.end();
}

time_t processSeptaData(JsonDocument& doc) {
  time_t soonestETA = -1;
  time_t now;
  time(&now);
  bool foundBus = false;
  std::vector<time_t> allETAs;
  
  Serial.println("\n=== Processing SEPTA data ===");
  Serial.print("Current time: ");
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  Serial.printf("%02d:%02d:%02d\n", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  
  for (JsonVariant trip : doc.as<JsonArray>()) {
    String tripId = trip["trip_id"].as<String>();
    String direction = trip["direction_id"].as<String>();
    String status = trip["status"].as<String>();
    
    // Accept the trip if it's in the right direction and not CANCELED or NO GPS
    if (direction == DIRECTION_ID && 
        status != "CANCELED" && 
        status != "NO GPS") {
        
      Serial.printf("\nProcessing trip %s\n", tripId.c_str());
      
      HTTPClient http;
      String updateUrl = "https://www3.septa.org/api/v2/trip-update/?trip_id=" + tripId;
      
      http.setTimeout(5000);
      http.begin(updateUrl);
      http.addHeader("User-Agent", "ESP32");
      http.addHeader("Accept", "application/json");
      
      int httpCode = http.GET();
      
      if (httpCode == HTTP_CODE_OK) {
        String updatePayload = http.getString();
        DynamicJsonDocument updateDoc(updatePayload.length() + 512);
        DeserializationError error = deserializeJson(updateDoc, updatePayload);
        
        if (!error) {
          for (JsonVariant stop : updateDoc["stop_times"].as<JsonArray>()) {
            String stopId = stop["stop_id"].as<String>();
            bool departed = stop["departed"].as<bool>();
            
            if (stopId == STOP_ID && !departed) {
              time_t eta = stop["eta"].as<unsigned long>();
              int minutesUntil = (eta - now) / 60;
              
              localtime_r(&eta, &timeinfo);
              Serial.printf("Found arrival at %02d:%02d:%02d (%d minutes from now)\n",
                          timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, minutesUntil);
              
              if (eta > now) {
                allETAs.push_back(eta);
                foundBus = true;
              }
            }
          }
        } else {
          Serial.println("Error parsing trip update: " + String(error.c_str()));
        }
      }
      http.end();
    }
  }
  
  if (!allETAs.empty()) {
    std::sort(allETAs.begin(), allETAs.end());
    
    // Print all upcoming buses first
    Serial.println("\nUpcoming buses:");
    for (time_t eta : allETAs) {
      localtime_r(&eta, &timeinfo);
      int mins = (eta - now) / 60;
      Serial.printf("%02d:%02d:%02d (%d minutes from now)\n",
                   timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, mins);
    }
    
    // Then print the summary
    soonestETA = allETAs[0];
    int minutesUntil = (soonestETA - now) / 60;
    
    Serial.println("\n=== Results ===");
    Serial.println("Found " + String(allETAs.size()) + " upcoming buses");
    Serial.print("Soonest arrival in " + String(minutesUntil) + " minutes at ");
    localtime_r(&soonestETA, &timeinfo);
    Serial.printf("%02d:%02d:%02d\n", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  } else {
    Serial.println("\nNo upcoming buses found");
  }
  
  xSemaphoreTake(septaDataMutex, portMAX_DELAY);
  validBusFound = foundBus;
  xSemaphoreGive(septaDataMutex);
  
  return soonestETA;
}

void setupWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connection failed!");
  }
}

void setup() {
  // Initialize pins
  pinMode(DATA_PIN, OUTPUT);
  pinMode(SRCK_PIN, OUTPUT);
  pinMode(RCK_PIN, OUTPUT);
  
  digitalWrite(SRCK_PIN, LOW);
  digitalWrite(RCK_PIN, HIGH);
  
  // Start serial
  Serial.begin(115200);
  Serial.println("\n\nStarting SEPTA Bus Monitor...");
  
  // Create mutex
  septaDataMutex = xSemaphoreCreateMutex();
  
  // Setup network and time
  setupWiFi();
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  // Wait a bit for initial time sync
  delay(1000);
  
  // Create tasks
  xTaskCreatePinnedToCore(
    septaTask,           // Task function
    "SEPTA Task",        // Name of task
    10000,              // Stack size of task
    NULL,               // Parameter of the task
    1,                  // Priority of the task
    &septaTaskHandle,   // Task handle
    0                   // Core where the task should run (0)
  );
  
  xTaskCreatePinnedToCore(
    displayTask,         // Task function
    "Display Task",      // Name of task
    10000,              // Stack size of task
    NULL,               // Parameter of the task
    1,                  // Priority of the task
    &displayTaskHandle, // Task handle
    1                   // Core where the task should run (1)
  );
  
  Serial.println("Setup complete!");
}

void loop() {
  // Everything is handled in the tasks
  vTaskDelay(pdMS_TO_TICKS(1000)); // Just sleep to prevent watchdog errors
}

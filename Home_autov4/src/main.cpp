#if defined(ESP32)
  #include <WiFi.h>
  #include <Wire.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <Wire.h>
#endif

#include "..\.pio\libdeps\esp32-s3-YD-WROOM-1\Firebase Arduino Client Library for ESP8266 and ESP32\src\Firebase_ESP_Client.h"        // unified client
#include "..\.pio\libdeps\esp32-s3-YD-WROOM-1\Firebase Arduino Client Library for ESP8266 and ESP32\src\addons\TokenHelper.h"         // token generation helper
#include "..\.pio\libdeps\esp32-s3-YD-WROOM-1\Firebase Arduino Client Library for ESP8266 and ESP32\src\addons\RTDBHelper.h"          // RTDB helpers
#include <ESP32Time.h>

/* 1. Define Wi-Fi credentials */
#define WIFI_SSID      "HUAWEI-87JN"
#define WIFI_PASSWORD  "uRWbrg7X"

#define API_KEY        "AIzaSyAXkXcdits7ciCUp46TqCxxJDsg0GXfz8c"// Replace with your actual API key
#define DATABASE_URL   "https://homeautomation-b6d6d-default-rtdb.firebaseio.com/"// Replace with your actual database URL
#define USER_EMAIL     "homeautomation1336@gmail.com"// Replace with your actual user email
#define USER_PASSWORD  "home123"// Replace with your actual user email and password
const char* Account_ID = "a5FQqKF5SuYmPlrmWKBrDg3Kq483";// Replace with your actual Account ID

// Firebase objects
FirebaseData   fbdo; // For general use, primarily Firestore
FirebaseConfig config;
FirebaseAuth   auth;

// Time synchronization variables
ESP32Time rtc(28800); // Initialize with GMT offset in seconds (28800 for UTC+8 Asia/Manila)

// Relay assignments
const int numRelays = 9; //10 before
const int relayPins[numRelays] = {37, 2, 1, 17, 16, 14, 15, 47, 12}; // Pin assignments for relays 1-9 (PIN 3 removed)

// IR sensor assignments
const int numIRSensors = 5; //6 before
const int irPins[numIRSensors] = {5, 6, 7, 4, 18}; //pins of IR sensor 1-5 (PIN 48 removed)
int lastIRState[numIRSensors] = {0};

// Relay state tracking
int relayStates[numRelays] = {0}; // Current state of each relay
float relayWattage[numRelays] = {0.0}; // wattage rating for each relay
float relayUsagetime[numRelays] = {0.0}; // usagetime for each relay
float presentHourlyUsage[numRelays] = {0.0}; // Current hourly usage for each relay

// Timing variables
unsigned long lastPollMillis = 0;
const unsigned long pollingInterval = 1000; // Poll Firebase every 1 second
unsigned long lastUsageUpdateMillis = 0;
const unsigned long usageUpdateInterval = 300000; // Update usage calculations every 5 minutes

// Time tracking variables (for ON/OFF timestamps)
bool wasRelayActive[numRelays] = {false};         // Track previous state
int lastDay = -1; // To track day changes for daily reset

// Add to global variables
const char* monthNames[12] = {"jan", "feb", "mar", "apr", "may", "jun",
                             "jul", "aug", "sep", "oct", "nov", "dec"};

// Function to extract the current year and update Firestore
void updatePresentYear() {
  int currentYear = rtc.getYear();

  if (currentYear > 0) {
    char docPath[100];
    sprintf(docPath, "users/%s", Account_ID);

    FirebaseJson updateJson;
    // Correctly structure the Firestore field with type
    updateJson.set("fields/presentYear/stringValue", String(currentYear));

    // Update using patchDocument with the correct field path
    if (Firebase.Firestore.patchDocument(&fbdo, "homeautomation-b6d6d", "(default)", docPath, updateJson.raw(), "presentYear")) {
      Serial.printf("Updated presentYear to %d\n", currentYear);
    } else {
      Serial.printf("Error: %s\n", fbdo.errorReason().c_str());
    }
  }
}

// Function prototypes
String getDayOfWeekStr(int dayOfWeek) {
  switch (dayOfWeek) {
    case 0: return "Sun";
    case 1: return "Mon";
    case 2: return "Tue";
    case 3: return "Wed";
    case 4: return "Thu";
    case 5: return "Fri";
    case 6: return "Sat";
    default: return "";
  }
}

void processSerialCommands() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim(); // Remove whitespace

    if (input.startsWith("relay") && input.indexOf("==") != -1) {
      int relayNum = input.substring(5, input.indexOf("==")).toInt(); // Extract relay number
      String stateStr = input.substring(input.indexOf("==") + 2);
      stateStr.trim();
      stateStr.toUpperCase();

      if (relayNum >= 1 && relayNum <= 9) {
        int relayIndex = relayNum - 1;
        // Active-LOW: "ON" means LOW signal, "OFF" means HIGH signal
        int newSignal = (stateStr == "ON") ? LOW : HIGH;
        digitalWrite(relayPins[relayIndex], newSignal);
        relayStates[relayIndex] = newSignal; // Store the signal (LOW for ON, HIGH for OFF)

        // Update to Firestore
        char documentPath[100];
        sprintf(documentPath, "users/%s/relay_states/relay%d", Account_ID, relayNum);

        FirebaseJson updateJson;
        // Firestore state: 1 for ON (LOW signal), 0 for OFF (HIGH signal)
        updateJson.set("fields/state/integerValue", String(newSignal == LOW ? 1 : 0));

        if (Firebase.Firestore.patchDocument(&fbdo, "homeautomation-b6d6d", "(default)", documentPath, updateJson.raw(), "state")) {
          // Serial output: "ON" if signal is LOW, "OFF" if signal is HIGH
          Serial.printf("Relay %d set to %s via Serial\n", relayNum, newSignal == LOW ? "ON" : "OFF");
        } else {
          Serial.printf("Error updating relay %d: %s\n", relayNum, fbdo.errorReason().c_str());
        }
      } else {
        Serial.println("Relay number must be between 1 and 9.");
      }
    } else {
      Serial.println("Invalid command format. Use: relay{1-9} == ON or relay{1-9} == OFF");
    }
  }
}


void setup() {
  Serial.begin(115200); // Start serial communication
  delay(2000); // Wait for serial monitor to open
  Serial.println("Serial communication started.");

  // Connect to Wi-Fi
  Serial.printf("Connecting to %s ", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD); // Connect to the Wi-Fi network
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println(" CONNECTED");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP()); // Print the assigned IP address
  Serial.println("Wi-Fi connected.");
  Serial.println();

  // Initialize time synchronization with NTP using time zone string for Asia/Manila
  configTzTime(":Asia/Manila", "pool.ntp.org", "time.nist.gov");
  Serial.println("Waiting for NTP time synchronization...");
  delay(20000); // Increased delay to allow more time for NTP sync
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    rtc.setTimeStruct(timeinfo); // Set ESP32Time with synchronized time
    Serial.println("Time synchronized with NTP");
  } else {
    Serial.println("Failed to obtain time from NTP");
  }
  Serial.println("Time synchronization setup complete.");


  Serial.printf("Firebase Client v%s\n\n", FIREBASE_CLIENT_VERSION); // Print Firebase client version

  // Initialize Firebase
  config.api_key       = API_KEY; // Set Firebase API key
  config.database_url  = DATABASE_URL; // Set Firebase database URL
  auth.user.email      = USER_EMAIL; // Set user email for authentication
  auth.user.password   = USER_PASSWORD; // Set user password for authentication
  config.token_status_callback = tokenStatusCallback; // Set token status callback function
  Firebase.begin(&config, &auth); // Initialize Firebase
  Firebase.reconnectWiFi(true); // Enable auto-reconnect to Wi-Fi
  Firebase.setDoubleDigits(5); // Set precision for double values
  Serial.println("Firebase initialized.");

  // Confirm wattage_usage document path
  char wattageUsageDocPath[100];
  sprintf(wattageUsageDocPath, "users/%s/wattage_usage", Account_ID);
  if (Firebase.Firestore.getDocument(&fbdo, "homeautomation-b6d6d", "(default)", wattageUsageDocPath)) {
    Serial.printf("Confirmed access to wattage_usage document path: %s\n", wattageUsageDocPath);
  } else {
    Serial.printf("Failed to access wattage_usage document path %s: %s\n", wattageUsageDocPath, fbdo.errorReason().c_str());
  }

  // Setup relay pins
  for (int i = 0; i < numRelays; i++) {
    pinMode(relayPins[i], OUTPUT);           // Set as output
    digitalWrite(relayPins[i], HIGH);        // OFF state (HIGH for active-LOW relays)
    relayStates[i] = HIGH;                   // Track initial state (HIGH means OFF for active-LOW)
  }
  Serial.printf("%d relay pins set as OUTPUT and initialized to HIGH (OFF for active-LOW).\n", numRelays);

  // Setup IR sensor pins
  for (int i = 0; i < numIRSensors; i++) {
    pinMode(irPins[i], INPUT); // Set IR sensor pins as input
    lastIRState[i] = digitalRead(irPins[i]); // Read initial state of IR sensors
  }
  Serial.printf("%d IR sensor pins set as INPUT and initial states read.\n", numIRSensors);
}

void loop() {
  processSerialCommands();  // Check for serial relay commands
  unsigned long now = millis(); // Get current time in milliseconds

  // Get current day of the month using ESP32Time
  int currentDay = rtc.getDay();

  // Update the present year in Firestore periodically (e.g., daily)
  updatePresentYear();

  // ----- Poll Firebase for relay states and wattage values -----
  if (Firebase.ready() && (now - lastPollMillis) > pollingInterval) {
    lastPollMillis = now; // Update last poll time

    // Poll for relays 1 to 9 from Firestore
    for (int i = 0; i < 9; i++) {
      char documentPath[100];
      sprintf(documentPath, "users/%s/relay_states/relay%d", Account_ID, i + 1);

      if (Firebase.Firestore.getDocument(&fbdo, "homeautomation-b6d6d", "(default)", documentPath)) {
        if (fbdo.payload()) {
          FirebaseJson json;
          json.setJsonData(fbdo.payload());
          FirebaseJsonData applianceStatusField, wattageField, assignedField, stateField, usagetimeField;
          String assigned = "";
          float wattage = 0.0;
          float usagetime = 0.0;
          int state = LOW;

          // Get state field
          if (json.get(stateField, "fields/state/integerValue")) {
            state = stateField.intValue == 1 ? LOW : HIGH; // Active-LOW: 1 (ON) means LOW signal
          } else if (json.get(stateField, "fields/state/doubleValue")) {
            state = ((int)stateField.doubleValue) == 1 ? LOW : HIGH; // Active-LOW: 1 (ON) means LOW signal
          }

          // Get wattage
          if (json.get(wattageField, "fields/wattage/doubleValue")) {
            wattage = wattageField.doubleValue;
          } else if (json.get(wattageField, "fields/wattage/integerValue")) {
            wattage = wattageField.intValue;
          }

          // Get usagetime
          if (json.get(usagetimeField, "fields/usagetime/doubleValue")) {
            usagetime = usagetimeField.doubleValue;
          } else if (json.get(usagetimeField, "fields/usagetime/integerValue")) {
            usagetime = usagetimeField.intValue;
          }

          // Get assigned field
          if (json.get(assignedField, "fields/assigned/stringValue")) {
            assigned = assignedField.stringValue;
          }

          // Get irControlled field
          FirebaseJsonData irControlledField;
          bool irControlled = false;
          if (json.get(irControlledField, "fields/irControlled/booleanValue")) {
            irControlled = irControlledField.boolValue;
          }

          // Update physical relay only if not IR controlled
          if (!irControlled) {
            digitalWrite(relayPins[i], state); // Apply LOW for ON, HIGH for OFF (active-LOW)
          } else {
            state = digitalRead(relayPins[i]); // Read actual relay signal
          }

          // Scheduling logic
          if (!assigned.isEmpty()) {
            char appliancePath[150];
            sprintf(appliancePath, "users/%s/appliances/%s", Account_ID, assigned.c_str());

            if (Firebase.Firestore.getDocument(&fbdo, "homeautomation-b6d6d", "(default)", appliancePath)) {
              if (fbdo.payload()) {
                FirebaseJson applianceJson;
                applianceJson.setJsonData(fbdo.payload());
                FirebaseJsonData startTimeField, endTimeField, daysField;

                String startTimeStr = "";
                String endTimeStr = "";
                String daysStr = "";

                if (applianceJson.get(startTimeField, "fields/startTime/stringValue")) {
                  startTimeStr = startTimeField.stringValue;
                }
                if (applianceJson.get(endTimeField, "fields/endTime/stringValue")) {
                  endTimeStr = endTimeField.stringValue;
                }
                if (applianceJson.get(daysField, "fields/days/stringValue")) {
                  daysStr = daysField.stringValue;
                }

                if (!startTimeStr.isEmpty() && !endTimeStr.isEmpty() && !daysStr.isEmpty()) {
                  int currentHour = rtc.getHour(true);
                  int currentMinute = rtc.getMinute();
                  String currentDayStr = getDayOfWeekStr(rtc.getDayofWeek());

                  if (daysStr.indexOf(currentDayStr) != -1) {
                    int startHour, startMinute, endHour, endMinute;
                    sscanf(startTimeStr.c_str(), "%d:%d", &startHour, &startMinute);
                    sscanf(endTimeStr.c_str(), "%d:%d", &endHour, &endMinute);

                    if (!irControlled) {
                      if (currentHour == startHour && currentMinute == startMinute) {
                        digitalWrite(relayPins[i], LOW); // Turn ON
                        FirebaseJson updateJson;
                        updateJson.set("fields/state/integerValue", "1");
                        Firebase.Firestore.patchDocument(&fbdo, "homeautomation-b6d6d", "(default)", documentPath, updateJson.raw(), "state");
                      } else if (currentHour == endHour && currentMinute == endMinute) {
                        digitalWrite(relayPins[i], HIGH); // Turn OFF
                        FirebaseJson updateJson;
                        updateJson.set("fields/state/integerValue", "0");
                        Firebase.Firestore.patchDocument(&fbdo, "homeautomation-b6d6d", "(default)", documentPath, updateJson.raw(), "state");
                      }
                    }
                  }
                }
              }
            }
          }

          relayStates[i] = state; // state is the signal (LOW for ON, HIGH for OFF)
          relayWattage[i] = wattage;
          relayUsagetime[i] = usagetime;

          Serial.printf("Relay %d : State = %s , Wattage = %.2f, Usagetime = %.2f, Assigned = %s, IR Controlled = %s\n",
                        i + 1, state == LOW ? "ON" : "OFF", wattage, usagetime, assigned.c_str(), irControlled ? "true" : "false"); // Active-LOW: LOW signal means ON

        }
      }
      delay(50); // Delay between reads
    }
  }

  // ----- IR sensor handling -----
  for (int i = 0; i < numIRSensors; i++) {
    int curr = digitalRead(irPins[i]);
    if (lastIRState[i] == LOW && curr == HIGH) { // Assuming IR sensor triggers on LOW to HIGH transition
      Serial.printf("IR %d triggered\n", i + 1);
      int r = i % numRelays; // Determine which relay is associated with this IR sensor
      int currentRelaySignal = digitalRead(relayPins[r]);
      
      // Toggle relay state for active-LOW:
      // If current signal is HIGH (OFF), new signal is LOW (ON).
      // If current signal is LOW (ON), new signal is HIGH (OFF).
      int newRelaySignal = (currentRelaySignal == HIGH) ? LOW : HIGH;
      digitalWrite(relayPins[r], newRelaySignal);

      // Update Firestore if relay index is 0–8 (corresponds to relays 1–9)
      if (r >= 0 && r <= 8) {
        char documentPath[100];
        sprintf(documentPath, "users/%s/relay_states/relay%d", Account_ID, r + 1);

        FirebaseJson updateJson;
        // Firestore state: 1 if new signal is LOW (ON), 0 if new signal is HIGH (OFF)
        updateJson.set("fields/state/integerValue", String(newRelaySignal == LOW ? 1 : 0));
        // irControlled is true if IR set the relay to ON (LOW signal for active-LOW)
        updateJson.set("fields/irControlled/booleanValue", newRelaySignal == LOW);

        Firebase.Firestore.patchDocument(&fbdo, "homeautomation-b6d6d", "(default)", documentPath, updateJson.raw(), "state,irControlled");
      }
    }
    lastIRState[i] = curr;
  }

  lastDay = currentDay; // Update for day tracking
}
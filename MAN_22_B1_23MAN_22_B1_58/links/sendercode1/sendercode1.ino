#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

// Main ESP32 MAC address (Controller) - UPDATE THIS WITH ACTUAL MAC ADDRESS!
// Run the main ESP32 code first to get the correct MAC address from Serial Monitor
// IMPORTANT: Update this with the MAC address shown in your main ESP32 serial monitor
uint8_t mainESP32Address[] = {0xEC, 0xE3, 0x34, 0xB2, 0xED, 0x34};

// Ultrasonic sensor pins
#define TRIG_PIN  5
#define ECHO_PIN  18

// Tank specifications
#define TANK_HEIGHT_CM   200.0  // 2 meters = 200 cm
#define MIN_DISTANCE_CM  2.0    // Minimum measurable distance
#define MAX_DISTANCE_CM  300.0  // Maximum sensor range

// Structure to send sensor data (must match main ESP32)
typedef struct struct_message {
  int waterLevel;
  float distance;
  bool sensorStatus;
} struct_message;

// Global variables
unsigned long lastReading = 0;
const unsigned long READING_INTERVAL = 2000; // Read every 2 seconds
int consecutiveFailures = 0;
const int MAX_FAILURES = 5;
bool espNowInitialized = false;
bool peerAdded = false;

// Function to read distance from ultrasonic sensor
float readDistanceCM() {
  // Clear the trigger pin
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  
  // Send 10 microsecond pulse to trigger pin
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  // Read the echo pin with timeout (25ms = ~4m max range)
  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 25000);
  
  if (duration == 0) {
    Serial.println("⚠  Ultrasonic sensor timeout - no echo received");
    return -1; // Timeout - no echo received
  }
  
  // Calculate distance: (duration * speed of sound) / 2
  // Speed of sound = 343 m/s = 0.0343 cm/microsecond
  float distance = (duration * 0.0343) / 2.0;
  
  // Validate reading
  if (distance < MIN_DISTANCE_CM || distance > MAX_DISTANCE_CM) {
    Serial.println("⚠  Distance reading out of valid range: " + String(distance, 2) + " cm");
    return -1;
  }
  
  Serial.println("📏 Raw distance measurement: " + String(distance, 2) + " cm");
  return distance;
}

// Function to calculate water level percentage
int calculateWaterLevelPercent(float distance) {
  if (distance < 0) return -1; // Invalid reading
  
  // Water level = Tank height - distance from sensor to water surface
  float waterLevel = TANK_HEIGHT_CM - distance;
  
  // Ensure water level is not negative
  if (waterLevel < 0) waterLevel = 0;
  
  // Calculate percentage
  int percent = (int)((waterLevel / TANK_HEIGHT_CM) * 100);
  
  // Constrain to 0-100%
  percent = constrain(percent, 0, 100);
  
  return percent;
}

// Callback function when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("📤 Data send status to ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", mac_addr[i]);
    if (i < 5) Serial.print(":");
  }
  
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println(" -> ✅ SUCCESS");
    consecutiveFailures = 0;
  } else {
    Serial.println(" -> ❌ FAILED");
    consecutiveFailures++;
    
    // Debug information for failed sends
    Serial.println("❌ Send failure details:");
    Serial.println("   - Check if main ESP32 is powered on");
    Serial.println("   - Verify MAC address is correct");
    Serial.println("   - Ensure both devices are on same WiFi channel");
    Serial.println("   - Check distance between devices (max ~200m)");
  }
}

// Function to initialize ESP-NOW
bool initializeESPNOW() {
  // Set WiFi mode
  WiFi.mode(WIFI_STA);
  
  // Set WiFi channel to 1 (same as main ESP32)
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  
  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW initialization failed");
    return false;
  }
  
  // Register send callback
  esp_now_register_send_cb(OnDataSent);
  
  Serial.println("✅ ESP-NOW initialized successfully");
  espNowInitialized = true;
  return true;
}

// Function to add peer
bool addMainESP32Peer() {
  if (!espNowInitialized) {
    Serial.println("❌ ESP-NOW not initialized - cannot add peer");
    return false;
  }
  
  // Check if peer already exists
  if (esp_now_is_peer_exist(mainESP32Address)) {
    Serial.println("ℹ  Main ESP32 peer already exists - removing and re-adding");
    esp_now_del_peer(mainESP32Address);
  }
  
  // Prepare peer info
  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, mainESP32Address, 6);
  peerInfo.channel = 1;  // Same channel as set above
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;
  
  // Add peer
  esp_err_t addPeerResult = esp_now_add_peer(&peerInfo);
  if (addPeerResult != ESP_OK) {
    Serial.println("❌ Failed to add main ESP32 as peer");
    Serial.print("   Error code: ");
    Serial.println(addPeerResult);
    return false;
  }
  
  Serial.println("✅ Main ESP32 added as peer successfully");
  peerAdded = true;
  return true;
}

// Function to send data via ESP-NOW
bool sendDataESPNOW(int waterLevel, float distance, bool sensorOK) {
  if (!espNowInitialized || !peerAdded) {
    Serial.println("❌ ESP-NOW not properly initialized or peer not added");
    return false;
  }
  
  struct_message sensorData;
  sensorData.waterLevel = waterLevel;
  sensorData.distance = distance;
  sensorData.sensorStatus = sensorOK;
  
  Serial.println("📡 Sending data via ESP-NOW:");
  Serial.println("   Water Level: " + String(waterLevel) + "%");
  Serial.println("   Distance: " + String(distance, 2) + " cm");
  Serial.println("   Sensor Status: " + String(sensorOK ? "OK" : "ERROR"));
  
  esp_err_t result = esp_now_send(mainESP32Address, (uint8_t *) &sensorData, sizeof(sensorData));
  
  if (result == ESP_OK) {
    Serial.println("📤 Data queued for transmission successfully");
    return true;
  } else {
    Serial.println("❌ Error queuing data for transmission");
    Serial.print("   Error code: ");
    Serial.println(result);
    consecutiveFailures++;
    return false;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000); // Give serial time to initialize
  
  Serial.println("\n🚀 Starting Ultrasonic Sensor Node...");
  Serial.println("=======================================");
  
  // Initialize pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  
  Serial.println("📍 Pin Configuration:");
  Serial.println("   Trigger Pin: " + String(TRIG_PIN));
  Serial.println("   Echo Pin: " + String(ECHO_PIN));
  Serial.println("   Tank Height: " + String(TANK_HEIGHT_CM) + " cm");
  
  // Print MAC addresses for debugging
  Serial.println("\n📍 MAC Address Information:");
  Serial.println("   This device MAC: " + WiFi.macAddress());
  Serial.print("   Target main ESP32 MAC: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", mainESP32Address[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
  
  // Initialize ESP-NOW
  Serial.println("\n🔧 Initializing ESP-NOW...");
  if (!initializeESPNOW()) {
    Serial.println("❌ ESP-NOW initialization failed - restarting in 5 seconds...");
    delay(5000);
    ESP.restart();
  }
  
  // Add main ESP32 as peer
  Serial.println("🔧 Adding main ESP32 as peer...");
  if (!addMainESP32Peer()) {
    Serial.println("❌ Failed to add peer - restarting in 5 seconds...");
    delay(5000);
    ESP.restart();
  }
  
  Serial.println("=======================================");
  Serial.println("🎯 System ready! Starting sensor readings...");
  Serial.println("📊 Reading interval: " + String(READING_INTERVAL / 1000) + " seconds");
  Serial.println("=======================================\n");
  
  // Test sensor immediately
  Serial.println("🧪 Testing ultrasonic sensor...");
  float testDistance = readDistanceCM();
  if (testDistance > 0) {
    Serial.println("✅ Sensor test successful: " + String(testDistance, 2) + " cm");
  } else {
    Serial.println("⚠  Sensor test failed - check connections");
  }
  Serial.println();
}

void loop() {
  if (millis() - lastReading >= READING_INTERVAL) {
    lastReading = millis();
    
    Serial.println("🔍 Taking sensor reading...");
    
    // Take multiple readings for better accuracy
    float totalDistance = 0;
    int validReadings = 0;
    
    for (int i = 0; i < 3; i++) {
      float distance = readDistanceCM();
      if (distance > 0) {
        totalDistance += distance;
        validReadings++;
      }
      delay(100); // Small delay between readings
    }
    
    bool validReading = (validReadings > 0);
    float averageDistance = validReadings > 0 ? totalDistance / validReadings : -1;
    
    if (validReading) {
      // Calculate water level percentage
      int waterLevelPercent = calculateWaterLevelPercent(averageDistance);
      
      // Calculate actual water height
      float waterHeight = TANK_HEIGHT_CM - averageDistance;
      if (waterHeight < 0) waterHeight = 0;
      
      Serial.println("════════════════════════════════════");
      Serial.println("📊 SENSOR READING RESULTS:");
      Serial.println("   Valid readings: " + String(validReadings) + "/3");
      Serial.println("   Average distance to water: " + String(averageDistance, 2) + " cm");
      Serial.println("   Water height in tank: " + String(waterHeight, 2) + " cm");
      Serial.println("   Water level percentage: " + String(waterLevelPercent) + "%");
      Serial.println("   Tank capacity: " + String(TANK_HEIGHT_CM) + " cm");
      
      // Visual water level indicator
      Serial.print("   Water Level: [");
      int bars = waterLevelPercent / 5; // 20 bars for 100%
      for (int i = 0; i < 20; i++) {
        if (i < bars) Serial.print("█");
        else Serial.print("░");
      }
      Serial.println("] " + String(waterLevelPercent) + "%");
      
      // Status indicators
      if (waterLevelPercent >= 100) {
        Serial.println("   🚨 STATUS: TANK FULL - OVERFLOW RISK!");
      } else if (waterLevelPercent >= 95) {
        Serial.println("   🟡 STATUS: TANK NEARLY FULL!");
      } else if (waterLevelPercent >= 80) {
        Serial.println("   ✅ STATUS: High water level");
      } else if (waterLevelPercent >= 50) {
        Serial.println("   🟡 STATUS: Medium water level");
      } else if (waterLevelPercent >= 20) {
        Serial.println("   🟠 STATUS: Low water level");
      } else if (waterLevelPercent >= 5) {
        Serial.println("   🔴 STATUS: Very low water level");
      } else {
        Serial.println("   ⚠  STATUS: CRITICALLY LOW - CHECK SENSOR!");
      }
      
      // Show pump recommendation
      if (waterLevelPercent < 20) {
        Serial.println("   💡 RECOMMENDATION: Consider turning ON pump");
      } else if (waterLevelPercent >= 95) {
        Serial.println("   💡 RECOMMENDATION: Turn OFF pump immediately");
      }
      
      Serial.println("════════════════════════════════════");
      
      // Send data to main ESP32 via ESP-NOW
      bool dataSent = sendDataESPNOW(waterLevelPercent, averageDistance, true);
      
      if (!dataSent) {
        Serial.println("❌ Failed to send data to main ESP32!");
        if (consecutiveFailures >= MAX_FAILURES) {
          Serial.println("🔄 Too many consecutive failures - reinitializing ESP-NOW...");
          espNowInitialized = false;
          peerAdded = false;
          if (initializeESPNOW() && addMainESP32Peer()) {
            Serial.println("✅ ESP-NOW reinitialized successfully");
            consecutiveFailures = 0;
          }
        }
      }
      
    } else {
      Serial.println("❌ All sensor readings invalid - sending error status");
      Serial.println("   - Check ultrasonic sensor connections");
      Serial.println("   - Ensure sensor has clear line of sight to water");
      Serial.println("   - Check if water level is within sensor range");
      
      // Send error status to main ESP32
      sendDataESPNOW(0, -1, false);
    }
    
    // Show status information
    Serial.println("⏱  Next reading in " + String(READING_INTERVAL / 1000) + " seconds...");
    Serial.println("📊 Consecutive failures: " + String(consecutiveFailures) + "/" + String(MAX_FAILURES));
    Serial.println("🔗 ESP-NOW Status: " + String(espNowInitialized ? "OK" : "FAILED"));
    Serial.println("👥 Peer Status: " + String(peerAdded ? "ADDED" : "NOT ADDED"));
    Serial.println();
  }
  
  delay(100); // Small delay to prevent watchdog issues
}
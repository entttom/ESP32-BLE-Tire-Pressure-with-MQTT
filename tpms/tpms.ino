// TPMS BLE ESP32
// 2020 RA6070
// Refactored version with stability improvements

/* TPMS BLE "manufacturer data" format
 * "000180eaca108a78e36d0000e60a00005b00"
 *  0001                                    Manufacturer (0001: TomTom)
 *      80                                  Sensor Number (80:1, 81:2, 82:3, 83:4, ..)
 *      80eaca108a78                        Sensor Address
 *                  e36d0000                Pressure
 *                          e60a0000        Temperature
 *                                  5b      Battery percentage
 *                                    00    Alarm Flag (00: OK, 01: No Pressure Alarm)
 *
 * How to calculate Sensor Address: (Sensor number):EA:CA:(Code binding reported in the leaflet) - i.e. 80:EA:CA:10:8A:78
 */

// ==========================================
// LIBRARIES
// ==========================================
#include "NimBLEDevice.h"  
#include "EspMQTTClient.h"

// ==========================================
// CONSTANTS AND CONFIGURATION
// ==========================================
#define WIFI_SSID "WIFI Name"
#define WIFI_PASSWORD "WIFI Password"
#define MQTT_SERVER "192.168.1.2"
#define MQTT_USERNAME "Username"
#define MQTT_PASSWORD "Password"
#define MQTT_CLIENTNAME "Expansion_Tank"  // Previously "Ausdehnungsgefaess" (German for expansion tank)
#define MQTT_TOPIC_BASE "Pressure_Monitoring/Expansion_Tank"  // Previously "Pressuremonitoring/Ausdehnungsgefaess"

// Sensor identifiers - the last 6 digits from the MAC address
#define SENSOR_FRONT "130711"
#define SENSOR_REAR "21a50a"

// BLE configuration
#define BLE_SCAN_INTERVAL 97 // How often the scan occurs / switches channels; in milliseconds
#define BLE_SCAN_WINDOW 37   // How long to scan during the interval; in milliseconds
#define BLE_SCAN_CACHE_SIZE 1000
#define RESTART_COUNTER_LIMIT 20 // For avoiding freeze after certain counts

// Stability improvement settings
#define WATCHDOG_TIMEOUT 30000 // Watchdog timeout in milliseconds (30 seconds)
#define HEAP_CHECK_INTERVAL 60000 // Check heap every minute
#define MIN_HEAP_SIZE 20000 // Minimum acceptable heap size in bytes
#define BLE_SCAN_TIMEOUT 5000 // Maximum time for a BLE scan in milliseconds
#define MAX_MQTT_FAILURES 5 // Maximum MQTT failures before reconnect

// ==========================================
// GLOBAL VARIABLES
// ==========================================
bool sendFlag = false;
String pressure_to_send_sn_front = "";
int counter = 0; 
unsigned long lastHeapCheck = 0;
unsigned long lastScanStart = 0;
int mqttFailures = 0;
hw_timer_t *watchdogTimer = NULL;

// Sensor data
float Pressure_1 = 200;  
int Temperature_1 = 30;
byte Battery_1 = 25;
byte Alarm_1 = 1;

float Pressure_2 = 200;  
int Temperature_2 = 30;
byte Battery_2 = 25;
byte Alarm_2 = 1;

// BLE objects
NimBLEScan* pBLEScan;

// MQTT client
EspMQTTClient client(
  WIFI_SSID,
  WIFI_PASSWORD,
  MQTT_SERVER,  
  MQTT_USERNAME,
  MQTT_PASSWORD,
  MQTT_CLIENTNAME
);

// ==========================================
// UTILITY FUNCTIONS
// ==========================================

// Reset watchdog timer
void IRAM_ATTR resetWatchdog() {
  timerWrite(watchdogTimer, 0);
}

// Check system health and restart if necessary
void checkSystemHealth() {
  unsigned long currentMillis = millis();
  
  // Check heap size regularly
  if (currentMillis - lastHeapCheck > HEAP_CHECK_INTERVAL) {
    lastHeapCheck = currentMillis;
    size_t freeHeap = ESP.getFreeHeap();
    Serial.print("Free heap: ");
    Serial.println(freeHeap);
    
    if (freeHeap < MIN_HEAP_SIZE) {
      Serial.println("Low memory, restarting...");
      ESP.restart();
    }
  }
  
  // Check if BLE scan is stuck
  if (pBLEScan->isScanning() && (currentMillis - lastScanStart > BLE_SCAN_TIMEOUT)) {
    Serial.println("BLE scan stuck, stopping scan...");
    pBLEScan->stop();
    delay(100);
  }
  
  // Reset watchdog
  resetWatchdog();
}

// Safe string extraction from BLE advertisement
String safeSubstring(const String &str, int start, int length) {
  if (str.length() < start + length) {
    return ""; // Return empty string if out of bounds
  }
  return str.substring(start, start + length);
}

// Extract manufacturer data string from the BLE advertisement
String retmanData(String txt, int shift) {
  int index = txt.indexOf("data: ");
  if (index == -1) {
    return ""; // Not found
  }
  
  int start = index + 6 + shift;
  if (start >= txt.length()) {
    return ""; // Out of bounds
  }
  
  int end = start + (36 - shift);
  if (end > txt.length()) {
    end = txt.length(); // Adjust end if too long
  }
  
  return txt.substring(start, end);
}

// Extract a single byte from a hex string at the specified position
byte retByte(String Data, int start) {
  if (Data.length() < (start + 1) * 2) {
    return 0; // Return 0 if out of bounds
  }
  
  int sp = (start) * 2;
  char *ptr;
  return strtoul(Data.substring(sp, sp + 2).c_str(), &ptr, 16);
}

// Extract a long value with little endian conversion from the data string
long returnData(String Data, int start) {
  if (Data.length() < (start + 4) * 2) {
    return 0; // Return 0 if out of bounds
  }
  
  return retByte(Data, start) | 
         retByte(Data, start + 1) << 8 | 
         retByte(Data, start + 2) << 16 | 
         retByte(Data, start + 3) << 24;
}

// Extract battery percentage from the data string
int returnBatt(String Data) {
  return retByte(Data, 16);
}

// Extract alarm status from the data string
int returnAlarm(String Data) {
  return retByte(Data, 17);
}

// Extract initial identifier from the data string
int returninitial(String Data) {
  return retByte(Data, 2);
}

// Safely publish MQTT message with error checking
bool safePublish(const String &topic, const String &message) {
  bool published = client.publish(topic, message);
  if (!published) {
    mqttFailures++;
    Serial.print("MQTT publish failed to ");
    Serial.println(topic);
    
    if (mqttFailures >= MAX_MQTT_FAILURES) {
      Serial.println("Too many MQTT failures, reconnecting...");
      client.disconnect();
      mqttFailures = 0;
      return false;
    }
  } else {
    mqttFailures = 0; // Reset counter on success
  }
  return published;
}

// Process sensor data, update variables, and print to serial monitor
void processSensorData(String instring, int sensorNumber) {
  // Validate input string
  if (instring.length() < 18) {
    Serial.println("Invalid data string length");
    return;
  }
  
  float pressure = returnData(instring, 8) / 1000.0;
  float temperature = returnData(instring, 12) / 100.0;
  byte battery = returnBatt(instring);
  byte alarm = returnAlarm(instring);
  
  // Store sensor data in the appropriate variables
  if (sensorNumber == 1) {
    Pressure_1 = pressure;
    Temperature_1 = temperature;
    Battery_1 = battery;
    Alarm_1 = alarm;
    
    // Send MQTT data for front sensor
    pressure_to_send_sn_front = String(pressure);
    bool published = safePublish(MQTT_TOPIC_BASE "/Pressure", pressure_to_send_sn_front);
    if (published) {
      pBLEScan->setActiveScan(false);
      delay(100);
      sendFlag = true;
    }
  } else if (sensorNumber == 2) {
    Pressure_2 = pressure;
    Temperature_2 = temperature;
    Battery_2 = battery;
    Alarm_2 = alarm;
  }
  
  // Print sensor data to serial monitor
  Serial.print("Temperature" + String(sensorNumber) + ": ");
  Serial.print(temperature);
  Serial.println("C°");
  
  Serial.print("Pressure:    ");
  Serial.print(pressure);
  Serial.println("kPa");
  
  Serial.print("Battery:     ");
  Serial.print(battery);
  Serial.println("%");
  
  Serial.print("ALARM " + String(sensorNumber) + ": ");
  Serial.println(sensorNumber == 1 ? Alarm_1 : Alarm_2);
  Serial.println("######");
  
  if (sensorNumber == 1) {
    // Print connection status for first sensor only
    Serial.print("isConnected (" + String(client.isConnected()) + ")");
    Serial.print(" - isWifiConnected (" + String(client.isWifiConnected()) + ")");
    Serial.println(" - isMqttConnected (" + String(client.isMqttConnected()) + ")");
  }
}

// ==========================================
// BLE CALLBACKS
// ==========================================
class MyAdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
    // Reset watchdog as we're receiving data
    resetWatchdog();
    
    // Get manufacturer data
    String ManufData = advertisedDevice->toString().c_str();
    String instring = retmanData(ManufData, 0);
    
    // Skip if invalid data
    if (instring.length() < 16) {
      return;
    }
    
    // Increment counter and restart if needed to prevent freezing
    counter++;
    Serial.println(counter);
    if (counter == RESTART_COUNTER_LIMIT) {
      Serial.println("Planned restart after " + String(RESTART_COUNTER_LIMIT) + " cycles");
      ESP.restart(); 
    }

    // Check which sensor has been detected based on its ID
    String sensorId = safeSubstring(instring, 10, 6);
    if (sensorId == SENSOR_FRONT) {
      processSensorData(instring, 1);
    }
    else if (sensorId == SENSOR_REAR) { 
      processSensorData(instring, 2);
    }
  }
};

// ==========================================
// MQTT CALLBACK
// ==========================================
void onConnectionEstablished() {
  // Send alive message when MQTT connection is established
  Serial.println("MQTT connected, sending alive message");
  safePublish(MQTT_TOPIC_BASE "/Alive", "true");
  mqttFailures = 0;
  sendFlag = false;
}

// ==========================================
// SETUP AND LOOP
// ==========================================
void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  delay(100);
  Serial.println("Starting BLE TPMS Monitor");
  
  // Setup watchdog timer
  watchdogTimer = timerBegin(0, 80, true); // Timer 0, divider 80, count up
  timerAttachInterrupt(watchdogTimer, &resetWatchdog, true); // Edge interrupt
  timerAlarmWrite(watchdogTimer, WATCHDOG_TIMEOUT * 1000, false); // Convert to microseconds
  timerAlarmEnable(watchdogTimer);
  
  // Print initial memory stats
  Serial.print("Initial free heap: ");
  Serial.println(ESP.getFreeHeap());
  
  // Configure BLE scanner
  NimBLEDevice::setScanFilterMode(CONFIG_BTDM_SCAN_DUPL_TYPE_DEVICE);
  NimBLEDevice::setScanDuplicateCacheSize(BLE_SCAN_CACHE_SIZE);
  NimBLEDevice::init("");

  // Set up BLE scanning parameters
  pBLEScan = NimBLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(), false);
  pBLEScan->setActiveScan(false);
  pBLEScan->setInterval(BLE_SCAN_INTERVAL);
  pBLEScan->setWindow(BLE_SCAN_WINDOW);
  pBLEScan->setMaxResults(0); // Do not store the scan results, use callback only
  
  // Initial values
  sendFlag = true;
  lastHeapCheck = millis();
  
  // Indicate setup complete
  Serial.println("Setup complete, scanning for TPMS sensors...");
  resetWatchdog();
}

void loop() {
  // Check system health
  checkSystemHealth();
  
  // If not sending data, ensure BLE scanning is active
  if (sendFlag == false) {
    if (pBLEScan->isScanning() == false) {
      // Start scan with: duration = 0 seconds (forever), no scan end callback, not a continuation of a previous scan
      lastScanStart = millis();
      pBLEScan->start(0, false);
      Serial.println("Starting BLE scan..."); 
    }
  }

  // If sending data, handle MQTT client operations
  if (sendFlag == true) {
    client.loop();
  }
  
  // Short delay to prevent CPU hogging
  delay(10);
}



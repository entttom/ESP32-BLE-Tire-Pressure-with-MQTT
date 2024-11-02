// TPMS BLE ESP32
// 2020 RA6070
// v0.2 06/08/20
//
// TPMS BLE "manufacturer data" format
// "000180eaca108a78e36d0000e60a00005b00"
//  0001                                    Manufacturer (0001: TomTom)
//      80                                  Sensor Number (80:1, 81:2, 82:3, 83:4, ..)
//      80eaca108a78                        Sensor Address
//                  e36d0000                Pressure
//                          e60a0000        Temperature
//                                  5b      Battery percentage
//                                    00    Alarm Flag (00: OK, 01: No Pressure Alarm)
//
// How calculate Sensor Address:            (Sensor number):EA:CA:(Code binding reported in the leaflet) - i.e. 80:EA:CA:10:8A:78


// BLE Service

#include "NimBLEDevice.h"  
#include "EspMQTTClient.h"


bool send;
String pressure_to_send_sn_front = "";




EspMQTTClient client(
  "WIFI Name",
  "WIFI Password",
  "192.168.1.2",  // MQTT Broker server ip
  "Username",   // Can be omitted if not needed
  "Password",   // Can be omitted if not needed
  "Ausdehnungsgefaess"      // Client name that uniquely identify your device
);


NimBLEScan* pBLEScan;

//================================
// sensor
//================================

String S_N_front = "130711" ; // the last 6 digits from the MAC adress
String S_N_rear  = "21a50a" ; // the last 6 digits from the MAC adress

// Variables

float Pressure_1 = 200;  
int  Temperature_1 = 30;
byte Battery_1 = 25;
byte Alarm_1 = 1;
float Pressure_2 = 200;  
int Temperature_2 = 30;
byte Battery_2 = 25;
byte Alarm_2 = 1;
int counter = 0; 

//================================
// Functions
//================================
// FUNCTIONS 

String retmanData(String txt, int shift) {
  // Return only manufacturer data string
  int start=txt.indexOf("data: ")+6+shift;
  return txt.substring(start,start+(36-shift));  
}

byte retByte(String Data,int start) {
  // Return a single byte from string
  int sp=(start)*2;
  char *ptr;
  return strtoul(Data.substring(sp,sp+2).c_str(),&ptr, 16);
}

long returnData(String Data,int start) {
  // Return a long value with little endian conversion
  return retByte(Data,start)|retByte(Data,start+1)<<8|retByte(Data,start+2)<<16|retByte(Data,start+3)<<24;
}

int returnBatt(String Data) {
  // Return battery percentage
  return retByte(Data,16);
}

int returnAlarm(String Data) {
  // Return battery percentage
  return retByte(Data,17);
}

int returninitial(String Data) {
  // Return 
  return retByte(Data,2);
}


      class MyAdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks {
      void onResult(NimBLEAdvertisedDevice* advertisedDevice) {

      String ManufData = advertisedDevice->toString().c_str();      
      String instring = retmanData(ManufData, 0); 
       //Serial.print("Instring : ");
       //Serial.println(instring);
       //Serial.print("S/N : ");
       //String S_N = (instring.substring(10,16));
       //Serial.println(S_N);
       //digitalWrite(ledstate, HIGH); 
      
       instring = retmanData(ManufData, 0); 

        counter++;
        Serial.println(counter);
        if (counter == 20) { //for avoiding freeze after certain counts restart the device
          ESP.restart(); 
        }

        if (instring.substring(10,16) == S_N_front) {
             
          pressure_to_send_sn_front = String((returnData(instring,8)/1000.0));
          client.publish("Pressuremonitoring/Ausdehnungsgefaess/Pressure", pressure_to_send_sn_front);  
          pBLEScan->setActiveScan(false);
          delay(100);
          send = true;   
        
        Pressure_1 = (returnData(instring,8)/1000.0);  
        Temperature_1 = (returnData(instring,12)/100.0);
        Battery_1 = (returnBatt(instring));
        Alarm_1 = (returnAlarm(instring));  



        // Tire Temperature in C°
        Serial.print("Temperature1: ");
        Serial.print(returnData(instring,12)/100.0);
        Serial.println("C°");
        // Tire pressure in Kpa           
        Serial.print("Pressure:    ");
        Serial.print(returnData(instring,8)/1000.0);
        Serial.println("Kpa");
        // Battery percentage             
        Serial.print("Battery:     ");
        Serial.print(returnBatt(instring));
        Serial.println("%");
        // Alarm state          
        Serial.print("ALARM 1: ");
        Serial.println(Alarm_1);
        Serial.println("");  
        Serial.println("######");
        Serial.print("isConnected (" + String(client.isConnected()) + ")");
        Serial.print(" - isWifiConnected (" + String(client.isWifiConnected()) + ")");
        Serial.println(" - isMqttConnected (" + String(client.isMqttConnected()) + ")");
        }

        else if (instring.substring(10,16) == S_N_rear) { 
          
        Pressure_2 = (returnData(instring,8)/1000.0);  
        Temperature_2 = (returnData(instring,12)/100.0);
        Battery_2 = (returnBatt(instring));
        Alarm_2 = (returnAlarm(instring));  
   



        // Tire Temperature in C°
        Serial.print("Temperature: ");
        Serial.print(returnData(instring,12)/100.0);
        Serial.println("C°");
        // Tire pressure in Kpa           
        Serial.print("Pressure:    ");
        Serial.print(returnData(instring,8)/1000.0);
        Serial.println("Kpa");
        // Battery percentage             
        Serial.print("Battery:     ");
        Serial.print(returnBatt(instring));
        Serial.println("%");    
        // Alarm state          
        Serial.print("ALARM 2: ");
        Serial.println(Alarm_2);
        Serial.println("######");  
 
        }       
       }
     };




void readSensor() 
{
   // IMPORTANT, protect from spawning multiple delayed instructions when the connection is lost
   if(!client.isConnected())
   	return;

   // What you want to execute each (5) seconds
   if(pressure_to_send_sn_front != "")
   client.publish("Pressuremonitoring/S_N_front/Pressure", pressure_to_send_sn_front); 
   
   
   // Re-schedule the instructions
   client.executeDelayed(5 * 1000, readSensor);
}

void setup() {
  // Opening serial port
  send = true;
  Serial.begin(115200);
  delay(100);
  Serial.println("Scanning...");
  NimBLEDevice::setScanFilterMode(CONFIG_BTDM_SCAN_DUPL_TYPE_DEVICE);

  /** *Optional* Sets the scan filter cache size in the BLE controller.
 *  When the number of duplicate advertisements seen by the contrpBLEScanoller
 *  reaches this value it will clear the cache and start reporting previously
 *  seen devices. The larger this number, the longer time between repeated
 *  device reports. Range 10 - 1000. (default 20)
 *
 *  Can only be used BEFORE calling NimBLEDevice::init.
 */
  NimBLEDevice::setScanDuplicateCacheSize(1000);

  NimBLEDevice::init("");

  pBLEScan = NimBLEDevice::getScan(); //create new scan
  // Set the callback for when devices are discovered, no duplicates.
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(), false);
  pBLEScan->setActiveScan(false); // Set active scanning, this will get more data from the advertiser.
  pBLEScan->setInterval(97); // How often the scan occurs / switches channels; in milliseconds,
  pBLEScan->setWindow(37);  // How long to scan during the interval; in milliseconds.
  pBLEScan->setMaxResults(0); // do not store the scan results, use callback only.
}

void onConnectionEstablished() {
          client.publish("Pressuremonitoring/Ausdehnungsgefaess/Alive", "true");  
          //client.executeDelayed(5 * 1000, readSensor);
          
          send = false;
}  

void loop() {
if(send == false)
  if(pBLEScan->isScanning() == false) {
      // Start scan with: duration = 0 seconds(forever), no scan end callback, not a continuation of a previous scan.
      pBLEScan->start(0, false);
      Serial.println("Start BLE"); 
  }

if(send == true)
client.loop();



};



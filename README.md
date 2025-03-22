# ESP32 BLE Tire Pressure with MQTT

This project uses an ESP32 microcontroller to read tire pressure, temperature, battery levels, and alarm states from Bluetooth Low Energy (BLE) TPMS (Tire Pressure Monitoring System) sensors. The collected data is then published to an MQTT broker, allowing for integration with home automation systems like Home Assistant, OpenHAB, or Node-RED.

## Overview

The system continuously scans for BLE advertisements from compatible TPMS sensors and decodes the manufacturer data to extract meaningful information. This information is then:

1. Displayed on the serial monitor for debugging
2. Published to configured MQTT topics for integration with other systems
3. Monitored for alarm conditions (such as low pressure)

Originally forked from [ra6070/BLE-TPMS](https://github.com/ra6070/BLE-TPMS), this version adds MQTT functionality and uses the NimBLE library for improved stability and reduced memory footprint on the ESP32.

## Features

- Monitors multiple TPMS sensors simultaneously (configurable front and rear)
- Extracts tire pressure (kPa), temperature (°C), battery percentage, and alarm states
- Publishes data to MQTT topics for integration with home automation systems
- Auto-restarts periodically to prevent BLE scan slowdowns (every 20 scan cycles)
- Low power consumption through efficient BLE operations
- Highly configurable through simple constant definitions

## Compatible Sensors

This project has been tested and confirmed working with the following TPMS sensors:
- ZEEPIN TPMS Sensors
- TP630 TPMS Sensors
- Various TomTom-compatible TPMS sensors

Other sensors using a similar BLE advertisement format may also work. The system specifically looks for devices with the manufacturer ID `0001` (TomTom).

## The TPMS BLE "manufacturer data" format

The devices cannot be connected or paired to and the devices do not receive any incoming BLE data. All data is broadcast as part of the "Manufacturer data" portion of the BLE advertisement.
Manufacturer data looks like this:

```
000180EACA108A78E36D0000E60A00005B00
```

And now let's analyze in depth the received data:

_bytes 0 and 1_  
`0001`		Manufacturer (see https://www.bluetooth.com/specifications/assigned-numbers/company-identifiers/)

_byte 2_  
`80`		Sensor Number (`80`:1, `81`:2, `82`:3, `83`:4, ...)

_bytes 3 and 4_  
`EACA`		Address Prefix

_bytes 5, 6 and 7_  
`108A78`	Sensor Address

_bytes 8, 9, 10 and 11_  
`E36D0000`	Tire pressure (in kPA)

_bytes 12, 13, 14 and 15_  
`E60A0000`	Tire Temperature (in Celsius)

_byte 16_  
`5B`		Battery Percentage

_byte 17_  
`00`		Alarm Flag (`00`: Ok, `01`: No pressure)

## How to Calculate Values

### Extracting Tire Pressure and Temperature

Bytes 8,9,10 and 11 are a representation of the air pressure in kPA; Bytes 12,13,14 and 15 represent the temperature in Celsius. To get the values we need to do a little-endian conversion.

```
long result = byte0|byte1<<8|byte2<<16|byte3<<24
```

The pressure (Bytes 8,9,10,11) in kPA is obtained by dividing by 1000 the value obtained from the conversion:

```
kPA = result/1000.0
```

The pressure (Bytes 8,9,10,11) in bar is obtained by dividing by 100000 the value obtained from the conversion:

```
bar = result/100000.0
```

The temperature (Bytes 12,13,14,15) in Celsius is obtained by dividing by 100 the value obtained from the conversion:

```
temp = result/100.0
```

### Battery Percentage and Alarm Flag

Byte 16 returns the battery percentage directly.

Byte 17 returns the alarm flag:
- `00`: Normal condition
- `01`: No pressure/Low pressure alarm

## Hardware Requirements

- ESP32 development board (ESP32-WROOM, ESP32-DevKitC, etc.)
- Power supply for the ESP32 (USB or external 5V source)
- WiFi network for MQTT connectivity
- Compatible TPMS sensors installed on tires

## Software Requirements

- Arduino IDE (1.8.x or later) or PlatformIO
- ESP32 Arduino Core
- Required libraries:
  - [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) (Lightweight BLE library for ESP32)
  - [EspMQTTClient](https://github.com/plapointe6/EspMQTTClient) (Simplified MQTT client for ESP32)

## Installation

1. Install the Arduino IDE from [arduino.cc](https://www.arduino.cc/en/software)
2. Add ESP32 support to Arduino IDE:
   - Open Arduino IDE
   - Go to File -> Preferences
   - Add `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json` to Additional Boards Manager URLs
   - Go to Tools -> Board -> Boards Manager
   - Search for ESP32 and install the latest version

3. Install required libraries:
   - Go to Tools -> Manage Libraries
   - Search for and install:
     - NimBLE-Arduino
     - EspMQTTClient

4. Clone or download this repository
5. Open the `tpms.ino` file in Arduino IDE
6. Configure the settings (see Configuration section)
7. Connect your ESP32 board to your computer
8. Select the correct board and port under Tools menu
9. Click Upload to flash the code to your ESP32

## Configuration

Before uploading the code, you need to configure it for your specific environment. Edit the following constants in the code:

```cpp
// WiFi and MQTT configuration
#define WIFI_SSID "Your_WiFi_SSID"
#define WIFI_PASSWORD "Your_WiFi_Password"
#define MQTT_SERVER "192.168.1.2"  // Your MQTT broker IP
#define MQTT_USERNAME "Username"    // Your MQTT username (if required)
#define MQTT_PASSWORD "Password"    // Your MQTT password (if required)
#define MQTT_CLIENTNAME "Expansion_Tank"  // A unique name for this device

// MQTT topic where data will be published
#define MQTT_TOPIC_BASE "Pressure_Monitoring/Expansion_Tank"

// Sensor identifiers - the last 6 digits from each sensor's MAC address
#define SENSOR_FRONT "130711"  // Replace with your front sensor ID
#define SENSOR_REAR "21a50a"   // Replace with your rear sensor ID
```

### Finding Your Sensor IDs

To find your sensor IDs:
1. Set the `SENSOR_FRONT` and `SENSOR_REAR` values to some placeholder
2. Upload and run the code with Serial Monitor open
3. Look for BLE devices in the logs
4. Identify your TPMS sensors by their manufacturer data format
5. Extract the last 6 digits from each sensor's address
6. Update the code with these values and re-upload

## Usage

After uploading the configured code to your ESP32:

1. The ESP32 will automatically connect to the configured WiFi network
2. It will establish a connection to the MQTT broker
3. It will start scanning for BLE advertisements from the configured TPMS sensors
4. When a compatible sensor is found, the data will be processed and published to MQTT

### MQTT Topics

The system publishes data to the following MQTT topics:

- `Pressure_Monitoring/Expansion_Tank/Pressure` - Current tire pressure in kPa
- `Pressure_Monitoring/Expansion_Tank/Alive` - A heartbeat message sent when the MQTT connection is established

You can subscribe to these topics in your home automation system to monitor your tire pressure.

### Serial Monitor Output

For debugging purposes, the system outputs detailed information to the serial monitor:

- Temperature readings
- Pressure readings
- Battery percentage
- Alarm status
- Connection status (WiFi, MQTT)

## Home Assistant Integration

To integrate with Home Assistant, add the following to your `configuration.yaml`:

```yaml
sensor:
  - platform: mqtt
    name: "Tire Pressure"
    state_topic: "Pressure_Monitoring/Expansion_Tank/Pressure"
    unit_of_measurement: "kPa"
    device_class: pressure
    
  - platform: mqtt
    name: "TPMS Connection"
    state_topic: "Pressure_Monitoring/Expansion_Tank/Alive"
```

## Troubleshooting

### ESP32 Not Finding Sensors

- Ensure your TPMS sensors are activated (usually by driving the vehicle)
- Check that you've configured the correct sensor IDs
- Try positioning the ESP32 closer to the tires

### MQTT Connection Issues

- Verify your MQTT broker is running and accessible
- Check your WiFi credentials
- Confirm the MQTT broker address, username, and password

### System Instability

- The code includes an automatic restart every 20 scan cycles to prevent BLE scanning slowdowns
- If you experience frequent disconnections, try adjusting the `RESTART_COUNTER_LIMIT` value

## Advanced Usage

### Modifying BLE Scan Parameters

You can adjust the BLE scanning behavior by modifying these parameters:

```cpp
#define BLE_SCAN_INTERVAL 97  // How often the scan occurs (milliseconds)
#define BLE_SCAN_WINDOW 37    // How long to scan during each interval (milliseconds)
#define BLE_SCAN_CACHE_SIZE 1000  // BLE scan cache size
```

### Adding More Sensors

To monitor additional sensors, add new sensor ID constants and modify the callback function to process data from these sensors.

## License

This project is licensed under the MIT License - see the original repository for details.

## Acknowledgments

- [ra6070/BLE-TPMS](https://github.com/ra6070/BLE-TPMS) for the original TPMS decoding logic
- [h2zero/NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) for the efficient BLE library
- [plapointe6/EspMQTTClient](https://github.com/plapointe6/EspMQTTClient) for the MQTT client library

## Contributing

Contributions to improve the project are welcome. Please feel free to fork the repository and submit pull requests.

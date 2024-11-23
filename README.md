# ESP32 SEPTA Bus Monitor

A digital display system that shows real-time arrival predictions for SEPTA buses using an ESP32 microcontroller and a 4-digit 7-segment display. The system alternates between showing the current time and the estimated time of arrival (ETA) for the next bus at your specified stop.

https://imgur.com/a/eY8CiZn

## Features

- Real-time bus arrival predictions using SEPTA's API
- Current time display synchronized with NTP
- Configurable for any SEPTA bus route, direction, and stop
- Automatic cycling between:
  - Current time
  - Route number
  - "BUS" text
  - "ETA" text
  - Minutes until next arrival (flashing)
- Fault-tolerant design with automatic recovery
- Multi-core operation for reliable data fetching and display updates

## Hardware Requirements

- ESP32 development board
- 4-digit 7-segment display with shift register control
- Power supply (USB or external)
- Basic wiring/jumper cables

## Pin Configuration

| ESP32 Pin | Display Connection |
|-----------|-------------------|
| GPIO 13   | DATA_PIN          |
| GPIO 12   | SRCK_PIN          |
| GPIO 14   | RCK_PIN           |

## Software Dependencies

- WiFi.h
- HTTPClient.h
- ArduinoJson.h
- time.h
- vector (C++ STL)
- algorithm (C++ STL)

## Configuration

Update the following constants in the code to match your setup:

```cpp
// WiFi credentials
const char* ssid = "Your-WiFi-SSID";
const char* password = "Your-WiFi-Password";

// SEPTA configuration
const String ROUTE_ID = "45";      // Your bus route number
const String DIRECTION_ID = "1";    // 0 or 1 depending on direction
const String STOP_ID = "16603";     // Your stop ID
```

To find your stop ID:
1. Visit SEPTA's website
2. Look up your route
3. Find your stop in the schedule
4. Note the stop ID from the URL or schedule information

## Installation

1. Clone this repository
```bash
git clone https://github.com/yourusername/esp32-septa-monitor.git
```

2. Install required libraries in Arduino IDE:
   - ArduinoJson (v6 or later)
   - ESP32 board support package

3. Configure your WiFi credentials and SEPTA settings in the code

4. Upload to your ESP32

## Operation

The display cycles through the following states:
1. Current time (3 seconds)
2. Route number (1 second)
3. "BUS" text (1 second)
4. "ETA" text (1 second)
5. Minutes until next bus arrival (6 seconds, flashing)

If no valid bus data is available, the display will show only the current time.

## Troubleshooting

- Display shows "----":
  - No valid bus data available
  - Network connection issue
  - Time synchronization failure
  
- No information on serial monitor:
  - Check baud rate (115200)
  - Verify USB connection
  - Reset ESP32

- Incorrect times:
  - Verify timezone settings in code
  - Check NTP server connection
  - Reset device to force time resync

## Contributing

Pull requests are welcome. For major changes, please open an issue first to discuss what you would like to change.

## License

[MIT](https://choosealicense.com/licenses/mit/)

## Credits

- SEPTA for providing the real-time API
- ESP32 community for various helpful examples and libraries

## Disclaimer

This project is not officially affiliated with SEPTA. The accuracy of arrival predictions depends on SEPTA's real-time data feed.

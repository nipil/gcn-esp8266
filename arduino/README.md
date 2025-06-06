# GCN on NodeMCU 8266 using Arduino IDE

Thanks to https://www.emqx.com/en/blog/esp8266-connects-to-the-public-mqtt-broker for the tutorial.

## Board setup

- File / Preferences
  - Additionnal boards manager URLs, add using comma separation, then validate :
    - http://arduino.esp8266.com/stable/package_esp8266com_index.json

- Tools / Board ... / Board manager
  - search 8266, and install (version 3.1.2 as of this writing)
    - Documentation : https://github.com/esp8266/Arduino (uses https://www.bearssl.org/ for SSL)
    - Debugging : https://arduino-esp8266.readthedocs.io/en/latest/Troubleshooting/debugging.html
        - Tools / Debug Port / Serial
        - Tools / Debug Level / SSL to debug BearSSL

- Tools / Board ... / esp8266 / NodeMCU 1.0 ESP-12E module

- Use the board and port dropdown
  - select "other board and port"
  - select the NodeMCU Board above
  - plug your board and select your USB com-port

- Test with the blink example
  - File / Examples / 0.1 Basics / blink
    - Sketch / Verify and compile
	- Sketch / Upload
  - The internal board led should blink

## Additional code

- Tools / Manage libraries
  - `PubSubClient` by Nick O'Leary (version 2.8.0 as of this writing)
    - Documentation : http://pubsubclient.knolleary.net/

## How it works

- tries to connect to different Wifi access point

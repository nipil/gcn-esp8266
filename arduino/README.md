# GCN on NodeMCU 8266 using Arduino IDE

Thanks to https://www.emqx.com/en/blog/esp8266-connects-to-the-public-mqtt-broker for the tutorial.

# Arduino monitor

Features :

- no write to flash, ever, for durability
  - multiple hardcoded wifi credentials
  - periodic hardcoded reboots to reinitialize

- network time synchronization via SNTP at boot
  - optional periodic resynchronization

- multiple digital pin monitor
  - input de-bouncing
  - recording upon change interrupt (one per pin)
  - sample recorded in circular buffer of configurable (hardocded) size
  - for seldom changing inputs, works to to cope with temporary "offline periods"

- Optional TLS to the MQTT broker
  - always-on verification of the TLS communication
  - no "disable verification" option
  - restricted to TLS 1.2, and optionnal hardening of TLS ciphers
  - authentication via username/password, no need for client certificate
  - only LetsEncrypt CA certificates are included, for use with publicly reachable servers
  - fast TLS session resumtion for faster reconnection upon deconnection (but not through sleep/reboots)

- Optional remote control capabilities
  - resynchronize time from network
  - disconnect from mqtt
  - disconnect from wifi
  - reboot

- Auxiliary functions
  - blinking led for status and mode
  - fully informative serial output
  - compile-time configurable debugging output
  - collecting metrics (uptime, network, memory, mqtt)

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

- Tools / Stack protection / enable

- Tools / CPU Frequency / 160 MHz (better when dealing with TLS)

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

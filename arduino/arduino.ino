// generated with : sketch / include library / ESP8266Wifi + PubSubClient
#include <ArduinoWiFiServer.h>
#include <BearSSLHelpers.h>
#include <CertStoreBearSSL.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiAP.h>
#include <ESP8266WiFiGeneric.h>
#include <ESP8266WiFiGratuitous.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WiFiSTA.h>
#include <ESP8266WiFiScan.h>
#include <ESP8266WiFiType.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WiFiClientSecureBearSSL.h>
#include <WiFiServer.h>
#include <WiFiServerSecure.h>
#include <WiFiServerSecureBearSSL.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>

#define SERIAL_BAUD 74880 // ESP8266 default baud rate on boot
#define LOOP_DELAY_MS 1000
#define WIFI_MAX_WAIT_MS (10 * 1000)
// #define REBOOT_EVERY_N_MINUTES 10  # set to 0 to disable

const char * const WIFI_SSID[] = {
  "your_ssid",
};

const char * const WIFI_PASSWORD[] = {
  "your_password",
};

// See https://test.mosquitto.org for the various available setups
#define MQTT_BROKER_DNS_NAME "test.mosquitto.org"
#define MQTT_BROKER_TCP_PORT 1883

WiFiClient espClient;

void(* resetFunc) (void) = 0; 

bool tryConnectToWiFi(const char * const ssid, const char * const password, int max_wait_ms) {
    WiFi.begin(ssid, password);
    unsigned long start = millis();
    do {
        delay(100);
        Serial.print(".");
        if (millis() - start > max_wait_ms) {
          Serial.println();
          return false;
        }
    } while (WiFi.status() != WL_CONNECTED);
    Serial.println();
    return true;
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  Serial.println("\nGCN starting...");
  Serial.print("Wifi MAC address is ");
  Serial.println(WiFi.macAddress());

  int len_wifi = min(sizeof(WIFI_SSID), sizeof(WIFI_PASSWORD)) / sizeof(const char * const);

  for (int ssid_tried = 0;ssid_tried < len_wifi; ssid_tried++) {
    const char * const ssid = WIFI_SSID[ssid_tried];
    const char * const password = WIFI_PASSWORD[ssid_tried];
    Serial.print("Trying to connecting to WiFi configuration n°");
    Serial.print(ssid_tried);
    Serial.print(" using SSID ");
    Serial.print(ssid);
    Serial.print(" and password ");
    Serial.println(password);
    if (tryConnectToWiFi(ssid, password, WIFI_MAX_WAIT_MS)) {
      Serial.print("Connected to Wifi ");
      Serial.println(ssid);
      break;
    }
    Serial.print("Failed to connect to ");
    Serial.println(ssid);
    if (ssid_tried + 1 == len_wifi) {
      Serial.println("Could not associate to any Wifi, rebooting.");
      delay(1000);
      resetFunc();
    }
  }
  Serial.println("GCN startup done.");
}

void loop() {
  delay(LOOP_DELAY_MS);
}

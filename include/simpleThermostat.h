#include <OneWire.h>
#include <DallasTemperature.h>
#include <Homie.h>

#include <ArduinoOTA.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
//#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#elif defined(ESP32)
#include <WiFi.h>
#include <WebServer.h>
#else
#error Invalid platform
#endif

#include <Timer.h>
#include <FS.h>
//#include <WebSocketsServer.h>

#define THERMOSTAT_FILE  "/thermostat.conf"

#define DEFAULT_TEMPERATURE_INTERVAL 30000  // Temperature Interval in Milliseconds
#define DEFAULT_SWITCH_INTERVAL      180000 // Switch Flip/Flop internval in milliseconds
//#define DEFAULT_SWITCH_INTERVAL      1800 // Switch Flip/Flop internval in milliseconds

#define ENABLE_DEEP_SLEEP false
//#define DEEP_SLEEP_INTERVAL 30000000  // Interval of deepsleep in microSecond

#define ESP_FIRMWARE_VERSION "0.8.0"
#define ESP_NAME "THERMOSTAT_DATA_LOGGER"
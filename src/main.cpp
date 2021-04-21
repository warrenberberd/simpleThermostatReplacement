//
// FILE: UserDataDemo.ino
// AUTHOR: Rob Tillaart
// VERSION: 0.1.0
// PURPOSE: use of alarm field as user identification demo
// DATE: 2019-12-23
// URL:
//
// Released to the public domain
//
#include <Arduino.h>

#include "simpleThermostat.h"

// Need to be Created for injection of Wifi Credential
#include "WiFiCred.h"

//ESP8266WiFiMulti wifiMulti;       // Create an instance of the ESP8266WiFiMulti class, called 'wifiMulti'
AsyncWebServer server(80);       // Create a webserver object that listens for HTTP request on port 80
//WebSocketsServer webSocket(81);    // create a websocket server on port 81

File fsUploadFile;                 // a File variable to temporarily store the received file

#define ENABLE_DEEP_SLEEP true
//#define DEEP_SLEEP_INTERVAL 30000000  // Interval of deepsleep in microSecond

#define ESP_NAME "RADIATEUR_CUISINE"

const char* mdnsName = ESP_NAME; // Domain name for the mDNS responder

//const char *ssid = ESP_NAME; // The name of the Wi-Fi network that will be created
//const char *password = OTA_PASS;   // The password required to connect to it, leave blank for an open network


const char *OTAName = ESP_NAME;           // A name and a password for the OTA service
const char *OTAPassword = OTA_PASS;

#define LED_PIN   LED_BUILTIN

#define ONE_WIRE_BUS      D3
#define BAT A0

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

#define DEFAULT_TEMPERATURE_INTERVAL 30000

HomieSetting<long> deepSleeIntervalSetting("deepSleepInterval", "Deep Sleep interval in Seconds");  // id, description

HomieNode temperatureNode("temperature", "Temperature", "temperature");
HomieSetting<long> temperatureIntervalSetting("temperatureInterval", "The temperature interval in seconds");

HomieNode voltageNode("voltage", "Voltage", "voltage");

unsigned long lastTemperatureSent = 0;  // When we last set the temperature

uint8_t deviceCount = 0;

float currentTemp=99.99;
float currentVoltage=99.99;

Timer t;    // For DeepSleep

void prepareSleep() {
  Homie.prepareToSleep();
}

// Add 4 prepared sensors to the bus
// use the UserDataWriteBatch demo to prepare 4 different labeled sensors
struct{
  int id;
  DeviceAddress addr;
} T[4];

float getTempByID(int id){
  for (uint8_t index = 0; index < deviceCount; index++)
  {
    if (T[index].id == id)
    {
      return sensors.getTempC(T[index].addr);
    }
  }
  return -999;
}

void printAddress(DeviceAddress deviceAddress){
  for (uint8_t i = 0; i < 8; i++)
  {
    // zero pad the address if necessary
    if (deviceAddress[i] < 16) Serial.print("0");
    Serial.print(deviceAddress[i], HEX);
  }
}

/*__________________________________________________________HELPER_FUNCTIONS__________________________________________________________*/

String formatBytes(size_t bytes) { // convert sizes in bytes to KB and MB
  if (bytes < 1024) {
    return String(bytes) + "B";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0) + "KB";
  } else if (bytes < (1024 * 1024 * 1024)) {
    return String(bytes / 1024.0 / 1024.0) + "MB";
  }

  return String();
}

String getContentType(String filename) { // determine the filetype of a given filename, based on the extension
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

/*__________________________________________________________SERVER_HANDLERS__________________________________________________________*/
bool handleFileRead(AsyncWebServerRequest *request,String path) { // send the right file to the client (if it exists)
  #ifdef DEBUG
    Serial.println("handleFileRead: " + path);
  #endif
  if (path.endsWith("/")) path += "index.html";          // If a folder is requested, send the index file
  String contentType = getContentType(path);             // Get the MIME type
  String pathWithGz = path + ".gz";
  bool download=false;
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) { // If the file exists, either as a compressed archive, or normal

    if (SPIFFS.exists(pathWithGz))                         // If there's a compressed version available
      path += ".gz";                                         // Use the compressed verion
    //File file = SPIFFS.open(path, "r");                    // Open the file
    //request->send(file, contentType,file.size());    // Send it to the client

    AsyncWebServerResponse *response = request->beginResponse(SPIFFS, path,contentType,download);
    response->addHeader("Cache-Control","max-age=3600");
    request->send(response);
    //request->send(SPIFFS, path,contentType,download);
    //file.close();                                          // Close the file again
    #ifdef DEBUG
      Serial.println(String("\tSent file: ") + path);
    #endif
    return true;
  }
  #ifdef DEBUG
    Serial.println(String("\tFile Not Found: ") + path);   // If the file doesn't exist, return false
  #endif
  return false;
}

void handleDataJSON(AsyncWebServerRequest *request){
  Serial.printf("Current temp : %f\n",currentTemp);

  String content="{\n";
  content+="\"temperature\": " + String(currentTemp) + "\n";
  content+="}\n";

  request->send(200,"application/json",content);
}

void handleIndexHTML(AsyncWebServerRequest *request){
  String path="/index.html";

  File file = SPIFFS.open(path, "r");
  String content=file.readString();
  file.close();

  #ifdef DEBUG
    //Serial.printf("Current temp : %f\n",currentTemp);
  #endif
  content.replace("__TEMP_DATA__",String(currentTemp).c_str());
  content.replace("__VOLT_DATA__",String(currentVoltage).c_str());

  request->send(200,"text/html",content);
}

void handleNotFound(AsyncWebServerRequest *request){ // if the requested file or page doesn't exist, return a 404 not found error
  if(!handleFileRead(request,request->url())){          // check if the file exists in the flash memory (SPIFFS), if so, send it
    request->send(404, "text/plain", "404: File Not Found");
  }
}

void handleHomieEvent(const HomieEvent& event) {
  switch(event.type) {
    case HomieEventType::MQTT_READY:
      #ifdef DEBUG
        Homie.getLogger() << "MQTT connected, preparing for deep sleep after 100ms..." << endl;
      #endif
      t.after(100, prepareSleep);
      break;
    case HomieEventType::READY_TO_SLEEP:
      #ifdef DEBUG
        Homie.getLogger() << "Ready to sleep" << endl;
      #endif
  
      if(ENABLE_DEEP_SLEEP) Homie.doDeepSleep(deepSleeIntervalSetting.get() * 1000000);
      break;
  }
}

/* void handleFileUpload(AsyncWebServerRequest *request){ // upload a new file to the SPIFFS
  HTTPUpload& upload = request->upload();
  String path;
  if(upload.status == UPLOAD_FILE_START){
    path = upload.filename;
    if(!path.startsWith("/")) path = "/"+path;
    if(!path.endsWith(".gz")) {                          // The file server always prefers a compressed version of a file 
      String pathWithGz = path+".gz";                    // So if an uploaded file is not compressed, the existing compressed
      if(SPIFFS.exists(pathWithGz))                      // version of that file must be deleted (if it exists)
         SPIFFS.remove(pathWithGz);
    }
    #ifdef DEBUG
    Serial.print("handleFileUpload Name: "); Serial.println(path);
    #endif
    fsUploadFile = SPIFFS.open(path, "w");            // Open the file for writing in SPIFFS (create if it doesn't exist)
    path = String();
  } else if(upload.status == UPLOAD_FILE_WRITE){
    if(fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize); // Write the received bytes to the file
  } else if(upload.status == UPLOAD_FILE_END){
    if(fsUploadFile) {                                    // If the file was successfully created
      fsUploadFile.close();                               // Close the file again
      #ifdef DEBUG
      Serial.print("handleFileUpload Size: "); Serial.println(upload.totalSize);
      #endif
      request->sendHeader("Location","/success.html");      // Redirect the client to the success page
      request->send(303);
    } else {
      request->send(500, "text/plain", "500: couldn't create file");
    }
  }

  request->send(200, "text/plain", "");
} */

float readVoltValue(){
  //int value = LOW;
  //float Tvoltage=0.0;
  float Vvalue=0.0,Rvalue=0.0;

  uint maxCycleAverage=10;

  // Read 10 value of voltage
  for(unsigned int i=0;i<maxCycleAverage;i++){
    Vvalue=Vvalue+analogRead(BAT);         //Read analog Voltage
    delay(5);                              //ADC stable
  }

  Vvalue=(float)Vvalue*1.0/maxCycleAverage;   //Find average of 10 values

  #ifdef DEBUG
    //Serial.printf("Raw ADC Value: %f\n",Vvalue);
  #endif

  Rvalue=(float)(Vvalue/1024.0)*3.3;            //Convert Voltage in 3.3v factor

  currentVoltage=Rvalue;

  return Rvalue;
}

void handleHomieLoop() {
  if (millis() - lastTemperatureSent >= DEFAULT_TEMPERATURE_INTERVAL || lastTemperatureSent < 1000) {
    #ifdef DEBUG
      Homie.getLogger() << "Voltage: " << currentVoltage << " V" << endl;
    #endif
    voltageNode.setProperty("volts").send(String(currentVoltage));

    float temperature = currentTemp;

    #ifdef DEBUG
      Homie.getLogger() << "Temperature: " << temperature << " °C" << endl;
    #endif

          // Do not transmit the temperature if it's too low
    if(currentTemp<-5.0){
      Homie.getLogger() << "[ERROR] Temperature is too low !" << endl;
    }else{
      temperatureNode.setProperty("degrees").send(String(temperature));
    }
    
    lastTemperatureSent = millis();
  }
}

/* void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght) { // When a WebSocket message is received
  switch (type) {
    case WStype_DISCONNECTED:             // if the websocket is disconnected
      #ifdef DEBUG
        Serial.printf("[%u] Disconnected!\n", num);
      #endif
      break;
    case WStype_CONNECTED: {              // if a new websocket connection is established
        IPAddress ip = webSocket.remoteIP(num);
        #ifdef DEBUG
          Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        #endif

      }
      break;
    case WStype_TEXT:                     // if new text data is received
      #ifdef DEBUG
        Serial.printf("[%u] get Text: %s\n", num, payload);
      #endif

      break;
    case WStype_BIN:
      #ifdef DEBUG
        hexdump(payload, lenght);
      #endif
      // echo data back to browser
      webSocket.sendBIN(num, payload, lenght);
      break;
  }
} */

void doOneLoop(){
  sensors.requestTemperatures();
  currentTemp=sensors.getTempC(T[0].addr);  // Get temp of first sensor

  readVoltValue();

  Homie.loop();
  t.update();   // Update the timer

  if(ENABLE_DEEP_SLEEP) return; // If DEEP SLEEP MODE, we stop here

  //webSocket.loop();                           // constantly check for websocket events
  //server.handleClient();                      // run the server
  ArduinoOTA.handle();                        // listen for OTA events
}

/*__________________________________________________________SETUP_FUNCTIONS__________________________________________________________*/
/* void startWiFi() { // Start a Wi-Fi access point, and try to connect to some given access points. Then wait for either an AP or STA connection
  WiFi.softAP(ssid, password);             // Start the access point
  #ifdef DEBUG
  Serial.print("Access Point \"");
  Serial.print(ssid);
  Serial.println("\" started\r\n");
  #endif

  wifiMulti.addAP(WIFI_SSID, WIFI_PASS);   // add Wi-Fi networks you want to connect to
  //wifiMulti.addAP("ssid_from_AP_2", "your_password_for_AP_2");
  //wifiMulti.addAP("ssid_from_AP_3", "your_password_for_AP_3");
  #ifdef DEBUG
    Serial.println("Connecting");
  #endif
  while (wifiMulti.run() != WL_CONNECTED && WiFi.softAPgetStationNum() < 1) {  // Wait for the Wi-Fi to connect
    delay(250);
    #ifdef DEBUG
    Serial.print('.');
    #endif
  }
  #ifdef DEBUG
    Serial.println("\r\n");
  #endif
  if(WiFi.softAPgetStationNum() == 0) {      // If the ESP is connected to an AP
    #ifdef DEBUG
      Serial.print("Connected to ");
      Serial.println(WiFi.SSID());             // Tell us what network we're connected to
      Serial.print("IP address:\t");
      Serial.print(WiFi.localIP());            // Send the IP address of the ESP8266 to the computer
    #endif

    digitalWrite(LED_PIN,LOW); // LED ON
  } else {                                   // If a station is connected to the ESP SoftAP
    #ifdef DEBUG
      Serial.print("Station connected to ESP8266 AP");
    #endif
  }
  Serial.println("\r\n");
} */

void startOTA() { // Start the OTA service
  ArduinoOTA.setHostname(OTAName);
  ArduinoOTA.setPassword(OTAPassword);

  ArduinoOTA.onStart([]() {
    #ifdef DEBUG
      Serial.println("Start");
    #endif
    //digitalWrite(LED_RED, 0);    // turn off the LEDs
    //digitalWrite(LED_GREEN, 0);
    //digitalWrite(LED_BLUE, 0);
  });

  ArduinoOTA.onEnd([]() {
    #ifdef DEBUG
      Serial.println("\r\nEnd");
    #endif
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    #ifdef DEBUG
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    #endif
  });

  ArduinoOTA.onError([](ota_error_t error) {
    #ifdef DEBUG
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    #endif
  });

  ArduinoOTA.begin();
  #ifdef DEBUG
    Serial.println("OTA ready\r\n");
  #endif
}

void startSPIFFS() { // Start the SPIFFS and list all contents
  SPIFFS.begin();                             // Start the SPI Flash File System (SPIFFS)
  #ifdef DEBUG
    Serial.println("SPIFFS started. Contents:");
  #endif
  {
    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) {                      // List the file system contents
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      #ifdef DEBUG
        Serial.printf("\tFS File: %s, size: %s\r\n", fileName.c_str(), formatBytes(fileSize).c_str());
      #endif
    }
    #ifdef DEBUG
      Serial.printf("\n");
    #endif
  }
}

/* void startWebSocket() { // Start a WebSocket server
  webSocket.begin();                          // start the websocket server
  webSocket.onEvent(webSocketEvent);          // if there's an incomming websocket message, go to function 'webSocketEvent'
  #ifdef DEBUG
    Serial.println("WebSocket server started.");
  #endif
} */

void startMDNS() { // Start the mDNS responder
  MDNS.begin(mdnsName);                        // start the multicast domain name server
  #ifdef DEBUG
    Serial.print("mDNS responder started: http://");
    Serial.print(mdnsName);
    Serial.println(".local");
  #endif
}

void startServer() { // Start a HTTP server with a file read handler and an upload handler
  //server.on("/edit.html",  HTTP_POST, handleFileUpload);                       // go to 'handleFileUpload'

  server.on("/",  handleIndexHTML);                       // go to 'handleIndexHTML'
  server.on("/data.json",  handleDataJSON);               // go to 'handleDataJSON'

  server.onNotFound(handleNotFound);          // if someone requests any other file or page, go to function 'handleNotFound'
                                              // and check if the file exists

  server.begin();                             // start the HTTP server
  #ifdef DEBUG
    Serial.println("HTTP server started.");
  #endif
}

void startHomie(){
  Homie_setFirmware(ESP_NAME, "1.0.0");
  Homie.setLoopFunction(handleHomieLoop);

  Homie.onEvent(handleHomieEvent);    // Configure Homie Event Handler

  deepSleeIntervalSetting.setDefaultValue(60).setValidator([] (long candidate) {
    return (candidate > 0) && (candidate < 71*60);
  });

  temperatureNode.advertise("degrees").setName("Degrees")
                                    .setDatatype("float")
                                    .setUnit("ºC");

  voltageNode.advertise("volts").setName("Volts")
                                    .setDatatype("float")
                                    .setUnit("V");

  temperatureIntervalSetting.setDefaultValue(DEFAULT_TEMPERATURE_INTERVAL).setValidator([] (long candidate) {
    return candidate > 0;
  });

  //Homie.setLedPin(16, HIGH); // before Homie.setup() -- 2nd param is the state of the pin when the LED is o
  //Homie.disableLedFeedback(); // before Homie.setup()
  Homie.setup();
}

/* void startDeepSleep(){
  if(!ENABLE_DEEP_SLEEP) return;

  doOneLoop();

  ESP.deepSleep(DEEP_SLEEP_INTERVAL,RF_DEFAULT);  // Go sleeping
} */



void setup(void){
  Serial.begin(115200);

  #ifdef DEBUG
    Serial.println(__FILE__);
    Serial.println("Dallas Temperature Demo");
  #endif

  sensors.begin();
  
  // count devices
  deviceCount = sensors.getDeviceCount();
  #ifdef DEBUG
    Serial.print("#devices: ");
    Serial.println(deviceCount);
  #endif

  // Read ID's per sensor
  // and put them in T array
  for (uint8_t index = 0; index < deviceCount; index++){
    // go through sensors
    sensors.getAddress(T[index].addr, index);
    T[index].id = sensors.getUserData(T[index].addr);
  }

  // Check all 4 sensors are set
  #ifdef DEBUG
    for (uint8_t index = 0; index < deviceCount; index++){
      Serial.println();
      Serial.println(T[index].id);
      printAddress(T[index].addr);
      Serial.println();
    }
    Serial.println();
  #endif

  //startWiFi();                 // Start a Wi-Fi access point, and try to connect to some given access points. Then wait for either an AP or STA connection
  startHomie();                // Starting the MQTT Provider

  if(ENABLE_DEEP_SLEEP) return ;
  
  /* if(ENABLE_DEEP_SLEEP)         
    return startDeepSleep();   // If DEEP SLEEP MODE, We stop here */

  startOTA();                  // Start the OTA service
  startSPIFFS();               // Start the SPIFFS and list all contents
  //startWebSocket();            // Start a WebSocket server
  startMDNS();                 // Start the mDNS responder
  startServer();               // Start a HTTP server with a file read handler and an upload handler
}


void loop(void){
  doOneLoop();

  optimistic_yield(1000);
}

// END OF FILE
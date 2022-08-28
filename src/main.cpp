#include <Arduino.h>

#define ENABLE_WEBSOCKET  true

#include "simpleThermostat.h"

//#define AR53002_LED_PIN     NOT_A_PIN
//#define AR53002_SW_PIN     NOT_A_PIN
#define AR53002_LED_PIN     D5   // Correspond to GPIO14
#define AR53002_SW_PIN      D6   // Correspond to GPIO12

#define ONE_WIRE_BUS        D3    // Correspond to GPIO3
#define BAT                 A0
#define LED                 LED_BUILTIN
#define LED2                NOT_A_PIN

#define RESET_SWITCH        D0


OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Need to be Created for injection of Wifi Credential
#include "WiFiCred.h"

//ESP8266WiFiMulti wifiMulti;       // Create an instance of the ESP8266WiFiMulti class, called 'wifiMulti'
AsyncWebServer server(80);          // Create a webserver object that listens for HTTP request on port 80
AsyncWebSocket webSocket("/ws");           // create a websocket server on port 81

File fsUploadFile;                 // a File variable to temporarily store the received file

const char* mdnsName = ESP_NAME; // Domain name for the mDNS responder

//const char *ssid = ESP_NAME; // The name of the Wi-Fi network that will be created
//const char *password = WIFI_PASS;   // The password required to connect to it, leave blank for an open network


const char *OTAName = ESP_NAME;           // A name and a password for the OTA service
const char *OTAPassword = WIFI_PASS;

uint8_t deviceCount = 0;

unsigned long lastTemperatureSent = 0;  // When we last set the temperature
unsigned long lastSwitchChange = 0;  // When we last change switch State

float currentTemp=-127.00;
float currentThermostat=25.00;    // The default wanted temperature
float currentVoltage=99.99;
String relayStatus="UNKNOWN";
String oldRelayStatus="UNKNOWN";
String currentWifi="UNKNOWN";

//bool switchState=false;

HomieSetting<long> deepSleeIntervalSetting("deepSleepInterval", "Deep Sleep interval in Seconds");  // id, description

HomieNode temperatureNode("temperature", "Temperature", "temperature");
HomieSetting<long> temperatureIntervalSetting("temperatureInterval", "The temperature interval in seconds");

HomieNode voltageNode("voltage", "Voltage", "voltage");
HomieNode switchNode("thermostat", "Thermostat", "thermostat");

Timer tTimer;    // For DeepSleep

// Flag qui indique si on doit envoyer un pulse sur le Switch
// Ce mode de fonctionnement est nécessaire pour permettre au callback WebSocket d'etre court
bool HAVE_TO_PULSE=false; 

// Add 4 prepared sensors to the bus
// use the UserDataWriteBatch demo to prepare 4 different labeled sensors
struct{
  int id;
  DeviceAddress addr;
} T[4];

ICACHE_RAM_ATTR void callbackResetSwitch(){
  ESP.reset();
}

float getTempByID(int id){
  for (uint8_t index = 0; index < deviceCount; index++)
  {
    if (T[index].id == id){
      return sensors.getTempC(T[index].addr);
    }
  }

  #ifdef DEBUG
    Serial.println("Unable to find SPI Devices");
  #endif
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

bool setupTemp(){
  sensors.setResolution(12);  // Set precision to maximum
  sensors.setWaitForConversion(false);  // Dont block requestTemperatures function waiting to convertion

  sensors.begin();

  delay(200);

  // count devices
  deviceCount = sensors.getDeviceCount();
#ifdef DEBUG
  Serial.print("SPI #devices: ");
  Serial.println(deviceCount);
#endif

  // Read ID's per sensor
  // and put them in T array
  for (uint8_t index = 0; index < deviceCount; index++){
    // go through sensors
    sensors.getAddress(T[index].addr, index);
    T[index].id = sensors.getUserData(T[index].addr);

    #ifdef DEBUG
      Serial.write("SPI devices id: ");
      Serial.print(T[index].id);
      //Serial.write("  At adress: ");
      //Serial.write((char)T[index].addr);
      Serial.println("");
    #endif
  }

  return true;
}

bool setupAR53002(){
  if(AR53002_LED_PIN!=NOT_A_PIN) pinMode(AR53002_LED_PIN,INPUT_PULLDOWN_16);
  //if(AR53002_SW_PIN!=NOT_A_PIN) pinMode(AR53002_SW_PIN,OUTPUT);
  if(AR53002_SW_PIN!=NOT_A_PIN) pinMode(AR53002_SW_PIN,INPUT);
  if(LED!=NOT_A_PIN) pinMode(LED,OUTPUT);
  if(LED2!=NOT_A_PIN) pinMode(LED2,OUTPUT);

  if(RESET_SWITCH!=NOT_A_PIN) attachInterrupt(digitalPinToInterrupt(RESET_SWITCH), callbackResetSwitch, CHANGE);

  return true;
}

// Reading VOLT Value from ADC (A0)
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

// Reading temperature from DS2013
float readTempValue(){
  // If not initialized, run setupTemp
  if(T[0].addr==0 || deviceCount==0) setupTemp();

  //Serial.println("    [DEBUG] requestTemperatures()...");
  if(sensors.isConversionComplete()){
    sensors.requestTemperatures();  // Take 480ms

    //Serial.println("    [DEBUG] getTempC()...");
    currentTemp=sensors.getTempC(T[0].addr);  // Get temp of first sensor
    //Serial.println("    [DEBUG] readTempValue() OK.");
  }

  return currentTemp;
}

void sendWSAllValues(){
  // Send Switch Change to WebSocket client
  String outStr="{";
  outStr+="\"temp\": "          + String(currentTemp) + ",";
  outStr+=" \"voltage\": "      + String(currentVoltage) + ",";
  outStr+=" \"switch\": \""     + relayStatus + "\",";
  outStr+=" \"thermostat\": \"" + String(currentThermostat) + "\",";
  outStr+=" \"uptime\": "       + String(millis()) + "";
  outStr+="}";

  // Send Infos to All WebSocket clients
  webSocket.textAll(outStr.c_str());
  //webSocket.textAll("{\"temp\": " + String(currentTemp) + ", \"uptime\": "       + String(millis()) + "}");
}

// Lecture de la LED d'indication d'état du relai
String readRelayStatus(){
  int state = digitalRead(AR53002_LED_PIN);
  int led_state=state;

  oldRelayStatus=relayStatus;

  // If relay go from ON to OFF, recheck due to Led short Blinking
  if(oldRelayStatus=="ON" && state==HIGH){
    delay(100);
    state = digitalRead(AR53002_LED_PIN);
    led_state=state;
  }

  if(state==HIGH){
    relayStatus=String("OFF");
    //switchState=true;
    led_state=HIGH;
  }else{
    relayStatus=String("ON");
    //switchState=false;
    led_state=LOW;
  }

  if(LED!=NOT_A_PIN) digitalWrite(LED,led_state);    // On recopie l'état sur la LED BUILT_IN
  if(LED2!=NOT_A_PIN) digitalWrite(LED2,led_state);    // On recopie l'état sur la LED2 

  return relayStatus;
}

// Read Thermostat from config file
float readThermostatValue(){
  String path=THERMOSTAT_FILE;
  if(!SPIFFS.exists(path)){
    #ifdef DEBUG
      Homie.getLogger() << "Thermostat File does not exists. Returning default" << endl;
    #endif
    currentThermostat=25.0;   // Default value
    return currentThermostat; 
  }

  File file = SPIFFS.open(path, "r");
  String content=file.readString();
  file.close();

  currentThermostat=content.toFloat();
  return currentThermostat;
}

String readWifiSSID(){
  //currentWifi=Homie.getConfiguration().currentWifi.ssid;
  currentWifi=WIFI_SSID;

  return currentWifi;
}

// Save Thermostat in config file
bool writeThermostatValue(float ther){
  String path=THERMOSTAT_FILE;

  File file = SPIFFS.open(path, "w");
  file.write(String(ther).c_str());
  file.close();

  currentThermostat=ther;

  return true;
}


void prepareSleep() {
  Homie.prepareToSleep();
}

bool sendPulseToSwitch(){
  if(!HAVE_TO_PULSE) return false;
  HAVE_TO_PULSE=false;

  #ifdef DEBUG
    Serial.println("   [DEBUG] sendPulseToSwitch()");
  #endif

  if(AR53002_SW_PIN!=NOT_A_PIN){
    //Serial.println(" [DEBUG] Change pinMode to OUTPUT...");
    pinMode(AR53002_SW_PIN,OUTPUT);

    //Serial.println(" [DEBUG] DigitalWrite LOW...");
    digitalWrite(AR53002_SW_PIN,LOW); // Low pulse
    //Serial.println(" [DEBUG] DigitalWrite LOW OK.");
    delay(300); // 500ms pulse
    optimistic_yield(1000);
    //Serial.println(" [DEBUG] DigitalWrite HIGH...");
    digitalWrite(AR53002_SW_PIN,HIGH);

    delay(50);
    //Serial.println(" [DEBUG] DigitalWrite INPUT...");
    pinMode(AR53002_SW_PIN,INPUT);
    optimistic_yield(1000);

    return true;
  }

  webSocket.textAll("{\"switch\": \"" + relayStatus + "\"}");

  return false;
}

bool toggleSwitch(String newState){
  #ifdef DEBUG
    Homie.getLogger() << "Toggle Relay: " << newState << " (newState))" << endl;
  #endif
  uint state;
  if(newState=="ON" && relayStatus=="OFF"){
    //sendPulseToSwitch();  // Send pulse to change Switch state
    HAVE_TO_PULSE=true;

    oldRelayStatus=relayStatus;
    relayStatus="ON";
    state=LOW;
    
    //return true;
  }else if(newState=="OFF" && relayStatus=="ON"){
    //sendPulseToSwitch();  // Send pulse to change Switch state
    HAVE_TO_PULSE=true;

    oldRelayStatus=relayStatus;
    relayStatus="OFF";
    
    state=HIGH;

    //return true;
  }

  lastSwitchChange=millis();

  if(LED!=NOT_A_PIN) digitalWrite(LED,state);    // On recopie l'état sur la LED BUILT_IN
  if(LED2!=NOT_A_PIN) digitalWrite(LED2,state);    // On recopie l'état sur la LED2

  return true;
}

bool changeThermostat(String value){
  #ifdef DEBUG
    Homie.getLogger() << "Modify Thermostat: " << value << " °C" << endl;
  #endif
  float newTherm=value.toFloat();
  if(newTherm<16.0 || newTherm>35.0){
    #ifdef DEBUG
      Homie.getLogger() << "ERROR : Thermostat value out of range" << endl;
    #endif
    return false;
  }

  return writeThermostatValue(newTherm);
}

// Determine if we need to enable or disable thermostat
bool processThermostat(){
  if (millis() - lastSwitchChange < DEFAULT_SWITCH_INTERVAL && lastSwitchChange > 0){
    #ifdef DEBUG
      //Homie.getLogger() << "millis: " << millis()  << "   lastSwitchChange:" << lastSwitchChange << endl;
      //Homie.getLogger() << "Disabling too frequent Switch Change" << endl;
    #endif
    return false;
  }

  bool state=false;
  if(currentTemp>=currentThermostat && relayStatus=="ON") state=toggleSwitch("OFF");
  if(currentTemp<currentThermostat && relayStatus!="ON")  state=toggleSwitch("ON");

  lastSwitchChange=millis();

  return state;
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
  content.replace("__DEVICE_NAME__",ESP_NAME);
  content.replace("__TEMP_DATA__",String(currentTemp).c_str());
  content.replace("__VOLT_DATA__",String(currentVoltage).c_str());
  content.replace("__SWITCH_DATA__",relayStatus.c_str());
  content.replace("__THERMOSTAT_DATA__",String(currentThermostat).c_str());
  content.replace("__UPTIME_DATA__",String(millis()).c_str());

  request->send(200,"text/html",content);
}

void handleListFS(AsyncWebServerRequest *request){
  String content ;
  content+="<html><head><title>Content of SPIFFS</title></head><body>\n";
  content+= "<h2>List of SPIFFS : </h2>\n";

  Dir dir = SPIFFS.openDir("/");
  while (dir.next()) {                      // List the file system contents
    String fileName = dir.fileName();
    size_t fileSize = dir.fileSize();
    #ifdef DEBUG
      Serial.printf("\tFS File: %s, size: %s\r\n", fileName.c_str(), formatBytes(fileSize).c_str());
    #endif

    String href="" + fileName;
    content+=" File: <a href='" + href + "'>" + fileName + "</a> (" + formatBytes(fileSize).c_str()  + ")<br>\n";
  }

  content+="\n</body></html>";
  request->send(200,"text/html",content);
}

void handleToggle(AsyncWebServerRequest *request){
  String newRelayStatus="UNKNOWN";
  if(relayStatus=="ON"){
    newRelayStatus="OFF";
  }else if(relayStatus=="OFF"){
    newRelayStatus="ON";
  }

  Homie.getLogger() << "handleToggle Relay: " << newRelayStatus << " (newState))" << endl;

  String content ;
  content+="<html><head><title>Toggle Relay Status </title></head><body>\n";
  toggleSwitch(newRelayStatus);

  content+="New Relay State : " + newRelayStatus + "";
  content+="\n</body></html>";
  request->send(200,"text/html",content);
}

void handleSetTrigger(AsyncWebServerRequest *request){
  String content;
  String value;

  content+="<html><head><title>Change Trigger</title></head><body>\n";

  if (!request->hasParam("value")) {
    Homie.getLogger() << "✖ handleSetTrigger need param 'value'" << endl;
    content+="<h1>NEED PARAM 'value'</h1>";
    content+="\n</body></html>";
    request->send(500,"text/html",content);
    return ;
  }
  value=request->getParam("value")->value();

  Homie.getLogger() << "handleSetTrigger new Value : " << value << endl;

  
  changeThermostat(value);
  content+="Value : " + value + "<br />\n";

  content+="\n</body></html>";
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
        Homie.getLogger() << "MQTT connected";
        Homie.getLogger() << ", preparing for deep sleep after 100ms...";
        Homie.getLogger() << endl;
      #endif
      if(ENABLE_DEEP_SLEEP) tTimer.after(100, prepareSleep);
      break;
    case HomieEventType::READY_TO_SLEEP:
      #ifdef DEBUG
        Homie.getLogger() << "Ready to sleep" << endl;
      #endif
  
      if(ENABLE_DEEP_SLEEP) Homie.doDeepSleep(deepSleeIntervalSetting.get() * 1000000);
      break;
  }
}

/*
void handleFileUpload(AsyncWebServerRequest *request){ // upload a new file to the SPIFFS
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
}*/

void handleHomieLoop() {
  //Homie.getLogger() << "handleHomieLoop() " << endl;

  if (millis() - lastTemperatureSent >= DEFAULT_TEMPERATURE_INTERVAL || lastTemperatureSent < 1000) {
    #ifdef DEBUG
      Homie.getLogger() << "Sending MQTT Voltage: " << currentVoltage << " V" << endl;
    #endif
    voltageNode.setProperty("volts").send(String(currentVoltage));  // For MQTT/Homie

    float temperature = currentTemp;

    #ifdef DEBUG
      Homie.getLogger() << "Sending MQTT Temperature: " << temperature << " °C" << endl;
    #endif

    // Do not transmit the temperature if it's too low
    if(currentTemp<-5.0){
      Homie.getLogger() << "[ERROR] Temperature is too low !" << endl;
    }else{
      temperatureNode.setProperty("degrees").send(String(temperature)); // For MQTT/Homie
    }

    #ifdef DEBUG
      Homie.getLogger() << "Sending MQTT Switch: " << relayStatus << " " << endl;
      Homie.getLogger() << "Sending MQTT Thermostat: " << currentThermostat << " " << endl;
    #endif

    // For MQTT/Homie
    switchNode.setProperty("relay").send(relayStatus);
    switchNode.setProperty("thermostat").send(String(currentThermostat));
    switchNode.setProperty("wifi").send(currentWifi);
    switchNode.setProperty("uptime").send(String(millis()));
    
    /*String outStr="{";
    outStr+="\"temp\": "          + String(currentTemp) + ",";
    outStr+=" \"voltage\": "      + String(currentVoltage) + ",";
    outStr+=" \"switch\": \""     + relayStatus + "\",";
    outStr+=" \"thermostat\": \"" + String(currentThermostat) + "\",";
    outStr+=" \"uptime\": "       + String(millis()) + ",";
    outStr+="}";

    // Send Infos to All WebSocket clients
    webSocket.textAll(outStr.c_str());*/

    lastTemperatureSent = millis();
  }

  // If Relay has change value
  if(oldRelayStatus!=relayStatus){
    #ifdef DEBUG
      Homie.getLogger() << "Sending Switch: " << relayStatus << " " << endl;
      Homie.getLogger() << "Sending Thermostat: " << currentThermostat << " " << endl;
    #endif
    switchNode.setProperty("relay").send(relayStatus); // For MQTT/Homie
    switchNode.setProperty("thermostat").send(String(currentThermostat)); // For MQTT/Homie

    // Send Switch Change to WebSocket client
    webSocket.textAll("{\"switch\": \"" + relayStatus + "\", \"thermostat\": \"" + String(currentThermostat) + "\"}");
  }
  
}

//void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght) { // When a WebSocket message is received
void webSocketEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
  //Serial.printf("[%u] webSocketEvent(%s)!\n", num, type);
  uint8_t num=0;
  String tempSTR="";
  switch (type) {
    case WS_EVT_DISCONNECT :             // if the websocket is disconnected
      #ifdef DEBUG
        Serial.printf("[%u] WS Disconnected!\n", num);
      #endif
      break;
    case WS_EVT_CONNECT: {              // if a new websocket connection is established
        IPAddress ip = client->remoteIP();
        #ifdef DEBUG
          //Serial.printf("[%u] WS Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], "");
          Serial.printf("[%u] WS Connected from %s url: %s\n", num, ip.toString().c_str(), "");
        #endif

      }
      break;
    case WS_EVT_DATA:                     // if new text data is received
      for(uint i=0; i < len; i++) {
        tempSTR+=(char)data[i];
        //Serial.print((char)data[i]);
        //Serial.print("|");
      }
      #ifdef DEBUG
        Serial.printf("[%u] WS get Text: %s\n", num, tempSTR.c_str());
        //Serial.println();
      #endif

      // if ask to change state
      if(tempSTR.startsWith("toggle:")){
        String state=tempSTR;
        state.replace("toggle:","");

        // Inverting state
        if(state.equals("ON")){
          state="OFF";
          toggleSwitch(state);
        }else if(state.equals("OFF")){
          state="ON";
          toggleSwitch(state);
        }

      }else if(tempSTR.startsWith("changeThermostat:")){  // if ask to change thermostat
        String therm=tempSTR;
        therm.replace("changeThermostat:","");
        changeThermostat(therm);
      }

      

      break;
    /*case WStype_BIN:
      #ifdef DEBUG
        hexdump(payload, lenght);
      #endif
      // echo data back to browser
      webSocket.sendBIN(num, data, lenght);
      break;*/
    /*case WStype_PING:                     // if new text data is received
      #ifdef DEBUG
        Serial.printf("[%u] WS get PING: %s\n", num, "");
      #endif
      //webSocket.sendTXT(num,"PONG");
      break;*/
    case WS_EVT_PONG:                     // if new text data is received
      #ifdef DEBUG
        Serial.printf("[%u] WS get PONG: %s\n", num, "");
      #endif
      client->ping();
      break;
    default:
      #ifdef DEBUG
        Serial.printf("[%u] WS get unknown %u: %s\n", num, type,  "");
      #endif
      break;
  }
}

void doOneLoop(){
  //Serial.println("   [DEBUG] readTempValue()...");
  readTempValue();
  //Serial.println("   [DEBUG] readVoltValue()...");
  readVoltValue();

  //Serial.println("   [DEBUG] processThermostat()...");
  processThermostat();

  //Serial.println("   [DEBUG] readRelayStatus()...");
  readRelayStatus();

  //Serial.println("   [DEBUG] readWifiSSID()...");
  readWifiSSID();

  //Serial.println("   [DEBUG] sendWSAllValues()...");
  sendWSAllValues();

  #ifdef DEBUG
  Serial.write("Temperature: ");
  Serial.print(currentTemp);
  Serial.print(" °C   ");
  Serial.write("Voltage: ");
  Serial.print(currentVoltage);
  Serial.print(" V   ");
  Serial.write("State: ");
  Serial.print(relayStatus);
  Serial.print("   ");
  Serial.write("Thermostat: ");
  Serial.print(currentThermostat);
  Serial.print(" °C   ");
  Serial.print("   ");
  Serial.write("Uptime: ");
  Serial.print(millis());
  Serial.print(" ms   ");
  Serial.print("   ");
  Serial.write("Wifi: ");
  Serial.print(currentWifi);
  Serial.print("   ");
  Serial.write("deviceCount: ");
  Serial.print(deviceCount);
  Serial.println("");
  #endif

  Homie.loop();
  tTimer.update();   // Update the timer (for deep sleep mode)

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
  Serial.println("[INIT] Starting OTA...");

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

void startWebSocket() { // Start a WebSocket server
  Serial.println("[INIT] Starting WebSocket...");
  //webSocket.begin();                          // start the websocket server
  webSocket.onEvent(webSocketEvent);          // if there's an incomming websocket message, go to function 'webSocketEvent'
  #ifdef DEBUG
    Serial.println("WebSocket server started.");
  #endif
}

void startMDNS() { // Start the mDNS responder
  Serial.println("[INIT] Starting MDNS Responder...");
  MDNS.begin(mdnsName);                        // start the multicast domain name server
  #ifdef DEBUG
    Serial.print("mDNS responder started: http://");
    Serial.print(mdnsName);
    Serial.println(".local");
  #endif
}

void startServer() { // Start a HTTP server with a file read handler and an upload handler
  Serial.println("[INIT] Starting WebServer...");
  //server.on("/edit.html",  HTTP_POST, handleFileUpload);                       // go to 'handleFileUpload'

  server.addHandler(&webSocket);                          // Mapping of WebSocke
  server.on("/",            handleIndexHTML);             // go to 'handleIndexHTML'
  server.on("/data.json",   handleDataJSON);              // go to 'handleDataJSON'
  server.on("/ls",          handleListFS);                // go to 'handleListFS'
  server.on("/toggle",      handleToggle);                // go to 'handleToggle'
  server.on("/setTrigger",  handleSetTrigger);            // go to 'handleSetTrigger'

  server.onNotFound(handleNotFound);          // if someone requests any other file or page, go to function 'handleNotFound'
                                              // and check if the file exists

  server.begin();                             // start the HTTP server
  #ifdef DEBUG
    Serial.println("HTTP server started.");
  #endif
}

void startHomie(){
  Homie_setFirmware(ESP_NAME, ESP_FIRMWARE_VERSION);
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

  switchNode.advertise("thermostat").setName("Thermostat")
                                      .setDatatype("float")
                                      .setUnit("°C");

  switchNode.advertise("relay").setName("Relai")
                                      .setDatatype("string");

  switchNode.advertise("wifi").setName("wifi")
                                    .setDatatype("string");

  switchNode.advertise("uptime").setName("uptime")
                                    .setDatatype("integer")
                                    .setUnit("ms");

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
  optimistic_yield(300);

  Serial.println("\n\n[INIT] Setting...");

  setupTemp();

  setupAR53002();

  //startWiFi();                 // Start a Wi-Fi access point, and try to connect to some given access points. Then wait for either an AP or STA connection
  startHomie();                // Starting the MQTT Provider
  Serial.println("\n[INIT] Homie Started.");

  if(ENABLE_DEEP_SLEEP) return ;

  /* if(ENABLE_DEEP_SLEEP)         
  return startDeepSleep();   // If DEEP SLEEP MODE, We stop here */

  startOTA();                  // Start the OTA service
  startSPIFFS();               // Start the SPIFFS and list all contents
  startWebSocket();            // Start a WebSocket server
  startMDNS();                 // Start the mDNS responder
  startServer();               // Start a HTTP server with a file read handler and an upload handler

  Serial.println("[INIT] First Read of Thermostat state...");
  readThermostatValue(); // Should be done AFTER startSPIFFS

  Serial.println("Starting loop...");
}

unsigned long lastTS=0;

void loop(void){
    if(HAVE_TO_PULSE) sendPulseToSwitch();
    //Serial.println(".");
    //optimistic_yield(1000);

    // Only 1 refresh by second
    if(millis()-lastTS>1000 or millis()<lastTS){
      //Serial.println(""); // For newline after "."
      lastTS=millis();
      doOneLoop();
    }

    delay(100);
}
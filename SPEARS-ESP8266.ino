#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WakeOnLan.h>
#include <WiFiUDP.h>
#include <FS.h>
#include <WiFiClient.h>

//specify the output file here
const String sensorOutputFileName = "sensorlog.txt";

//time before sensor recording automatically stops (in microseconds)
const float logTime = 10000000;

const char WiFiName[] = "SPEARS";
const char WiFiPass[] = "SPEARSPEARS";
const char GoProName[] = "GoProSPEARS";
const char GoProPass[] = "swim4693";
byte goProMac[] = {0xF6, 0xDD, 0x9E, 0x90, 0xF7, 0xD5};

//static wireless configuration
//I hope this helps GoPro wifi inconsistency
IPAddress ip(10,5,5,100);
IPAddress gateway(10,5,5,9);
IPAddress subnet(255,255,255,0);

String webLog = "";

//don't change these
bool recorded = false;
bool loggingSensors = false;
bool usingGoPro = false;
long lastGoProPowerOn;

ESP8266WebServer server(80); 

//default method that is called on start
void setup() {
  //open serial connection at 115200 baud
  Serial.begin(115200); 

  //enter AP + Station mode
  WiFi.mode(WIFI_AP_STA);

  //connect GoPro
  WiFi.begin(GoProName, GoProPass);
  WiFi.config(ip, gateway, subnet);

  //attempt to connect to GoPro for 5 seconds
  int waitTime = 0;
  while ((WiFi.status() != WL_CONNECTED) && (waitTime < 10))
  {
    delay(500);
    waitTime++;
    Serial.print(".");
  }

  WiFi.setAutoConnect(false);
  WiFi.setAutoReconnect(false);

  //output connection information
  if(WiFi.status() == WL_CONNECTED) {
    webLog = webLog + "Connected to GoPro! <br />";
    Serial.println("Connected to GoPro");
    usingGoPro = true;
    lastGoProPowerOn = millis();
  } else {
    webLog = webLog + "GoPro connection failed... Turn on GoPro then restart<br />";
    Serial.println("GoPro connection failed... Turn on GoPro then restart<br />");
  }
  Serial.println();
  Serial.print("gopro IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println(WiFi.softAP(WiFiName, WiFiPass)? "\nNetwork Setup Sucessful" : "\nNetwork Setup Failed");
  Serial.print("IP address: ");
  Serial.println(WiFi.softAPIP());

  //initialize the file system and delete previous files
  SPIFFS.begin();
  printFiles();
  
  //server configuration
  server.on(("/"+ sensorOutputFileName).c_str (), handleFileRead);
  server.on("/", handleRoot);
  server.on("/fullsensorlog", HTTP_POST, startFullSensorLog);
  server.on("/partialsensorlog", HTTP_POST, startPartialSensorLog);
  server.on("/restart", HTTP_POST, restart);
  server.on("/powerongopro", HTTP_POST, powerOnGoPro);
  server.on("/disconnectgopro", HTTP_POST, disconnectGoPro);
  server.on("/wipestorage", HTTP_POST, wipeStorage);
  server.onNotFound([](){
    server.send(404, "text/plain", "404: Not found");
  });
  server.begin(); 
  Serial.println("Server started");
}

//this method is called repeatedly during operation
void loop() {
  //write sensor data to file if enabled
  if(loggingSensors) {
    File sensorFile = SPIFFS.open(("/"+sensorOutputFileName), "a");
    long initTime = micros();
    
    //we get much more sensor data by suspending the server
    while ((micros()-initTime) < logTime) {
      sensorFile.print("[");
      sensorFile.print(micros());
      sensorFile.print("] ");
      sensorFile.println(analogRead(A0));
    }
    stopLogging();
    sensorFile.close();
  }
  
  server.handleClient();

  if(usingGoPro && (millis() - lastGoProPowerOn > 10000)) {
    powerOnGoPro();
  }
}

//print all SPIFFS files (debugging)
void printFiles() {
  Serial.println("Files: ");
  String str = "";
  Dir dir = SPIFFS.openDir("/");
  while (dir.next()) {
      str += dir.fileName();
      str += " / ";
      str += dir.fileSize();
      str += "\r\n";
  }
  Serial.print(str);
}


//send sensor log file
void handleFileRead() { 
  String path = server.uri();
  Serial.println("handleFileRead: " + path);
          
  if (SPIFFS.exists(path)) {                            
    File file = SPIFFS.open(path, "r");                 
    size_t sent = server.streamFile(file, "text/plain"); 
    file.close();                                       
    return;
  }

  //NOTE: file will not be found if sensors have not logged data yet
  Serial.println("\tFile Not Found");
  return;                                         
}

//homepage
void handleRoot() {
  //long and annoying homepage code
  server.send(200, "text/html", "<h1>SPEARS Control Center</h1><h3><br /><form action=\"/restart\" method=\"POST\"><input type=\"submit\" value=\"Restart controller\" style=\"font-size:20px\"></form><form action=\"/powerongopro\" method=\"POST\"><input type=\"submit\" value=\"Power on GoPro\" style=\"font-size:20px\"></form><form action=\"/fullsensorlog\" method=\"POST\"><input type=\"submit\" value=\"Begin full sensor logging\" style=\"font-size:20px\"></form><form action=\"/partialsensorlog\" method=\"POST\"><input type=\"submit\" value=\"Begin sensor logging without GoPro\" style=\"font-size:20px\"></form><form action=\"/wipestorage\" method=\"POST\"><input type=\"submit\" value=\"Clear Logs\" style=\"font-size:20px\"></form><br /><a href="+sensorOutputFileName+">Sensor Data Raw Text</a><br /><br />GoPro links (must change WiFi to use): <a href=http://10.5.5.9:8080/videos/DCIM/>Media</a> <a href=http://10.5.5.9:8080/gp/gpControl/command/shutter?p=1>Start</a> <a href=http://10.5.5.9:8080/gp/gpControl/command/shutter?p=0>Stop&nbsp;</a><form action=\"/disconnectgopro\" method = \"POST\"><input type =\"submit\" value=\"Disconnect GoPro\"></form></h3><h2><br /><br /><br /><br /><br /><br />Log</h2><p>"+webLog+"</p>");
}

//called when full log button is pressed
void startFullSensorLog() { 
  if (startRecordingGoPro()) {
   sensorLog(); 
   sendHome();
   return;
  }
  
  webLog = webLog + "Cancelling operation <br />";
  sendHome();
}

//called when partial log button is pressed
void startPartialSensorLog() { 
   sensorLog(); 
   sendHome();
}

void sensorLog() { 
  webLog = webLog + "Starting sensor logging for "+logTime+" milliseconds<br />";
  loggingSensors = true;
}

void stopLogging() {
  webLog = webLog + "Automatically stopping sensor logging <br />";
  loggingSensors = false;
  stopRecordingGoPro();
}

//send a wakeonlan signal to GoPro
void powerOnGoPro() {
  lastGoProPowerOn = millis();
  
  WiFiUDP UDP;
  UDP.begin(9);
  IPAddress goProIP(10, 5, 5, 9);
  WakeOnLan::sendWOL(goProIP, UDP, goProMac, sizeof goProMac);
  sendHome();
}

//disconnect from GoPro for download from PC
void disconnectGoPro() {
  webLog = webLog + "Attempting to disconnect GoPro<br />";
  powerOnGoPro();
  WiFi.disconnect();
  usingGoPro = false;
}

//clear the logs
void wipeStorage() {
  webLog = webLog + "Clearing logs<br />";
  SPIFFS.format();
  sendHome();
}

bool startRecordingGoPro() {
  webLog = webLog + "Starting GoPro capture... ";
  visitURL("/gp/gpControl/command/mode?p=0");
  bool success = visitURL("/gp/gpControl/command/shutter?p=1");
  webLog = webLog + (success? "now recording!<br />" : "failed; is GoPro WiFi on?<br />");
  return success;
}

bool stopRecordingGoPro() {
  webLog = webLog + "Stopping GoPro capture... ";
  bool success = visitURL("/gp/gpControl/command/shutter?p=0");
  webLog = webLog + (success? "GoPro successfully stopped!<br />" : "failed; is GoPro WiFi on?<br />");
  return success;
}

//perform an HTTP GET request to gopro specific link
bool visitURL(String url) {
  //ip of GoPro
  char* host = "10.5.5.9";
  WiFiClient client;
  
  if (!client.connect(host, 8080)) {
    Serial.println("connection failed");
    return false;
  }

  Serial.print("Requesting URL: ");
  Serial.println(url);

  // This will send the request to the server
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" + 
               "Connection: close\r\n\r\n");
  delay(15);

  // Read all the lines of the reply from server and print them to Serial
  Serial.println("Respond:");
  while(client.available()){
    String line = client.readStringUntil('\r');
    Serial.print(line);
  }

  Serial.println();
  Serial.println("closing connection");
  return true;
}

void restart() {
  sendHome();
  ESP.restart();
}

//send browser back to homepage
void sendHome() {
  server.sendHeader("Location","/");        
  server.send(303); 
}


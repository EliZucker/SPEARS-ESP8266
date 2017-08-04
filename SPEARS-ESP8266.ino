#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WakeOnLan.h>
#include <WiFiUDP.h>
#include <FS.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <SPI.h>
#include <SparkFunLSM9DS1.h>

//LSM9DS1 sparkfun IMU definitions
LSM9DS1 imu;
#define LSM9DS1_M  0x1E // Would be 0x1C if SDO_M is LOW
#define LSM9DS1_AG  0x6B // Would be 0x6A if SDO_AG is LOW

#define DECLINATION -13.43 //Declination in ames roverscape

//Mosfet port to control LED
const int LED_PIN = 16;

//specify the output file name here
const String sensorOutputFileName = "sensorlog.txt";

//time before sensor recording automatically stops (in microseconds)
const float logTime = 10000000;

//wifi information
const char WiFiName[] = "SPEARS";
const char WiFiPass[] = "SPEARSPEARS";

//change this if you get a different GoPro
const char GoProName[] = "GoProSPEARS";
const char GoProPass[] = "swim4693";
byte goProMac[] = {0xF6, 0xDD, 0x9E, 0x90, 0xF7, 0xD5};

//static wireless configuration
//I hope this helps GoPro wifi inconsistency
IPAddress ip(10,5,5,100);
IPAddress gateway(10,5,5,9);
IPAddress subnet(255,255,255,0);



//don't change these
String webLog = "";

int currentAnalogAccelValue;

int accelMax1 = 0;
long accelMax1Time = 0;
int accelMax2 = 0;
long accelMax2Time = 0;

bool loggingSensors = false;
bool usingGoPro = false;
long lastGoProPowerOn;

ESP8266WebServer server(80); 

//default method that is called on start
void setup() {
  //open serial connection at 115200 baud
  Serial.begin(115200); 

  //initialize port used for mosfet
  pinMode(LED_PIN, OUTPUT);

  //start IMU
  imu.settings.device.commInterface = IMU_MODE_I2C;
  imu.settings.device.mAddress = LSM9DS1_M;
  imu.settings.device.agAddress = LSM9DS1_AG;
  imu.begin();

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
  server.on("/toggleled", HTTP_POST, toggleLED);
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
    while(micros()-initTime < logTime) {
      //read sensors
      imu.readGyro();
      imu.readAccel();
      imu.readMag();
      currentAnalogAccelValue = analogRead(A0);

      //now right sensors to file
      printSensor(sensorFile);

      //check if accel is max value for first half of launch
      if((micros()-initTime) < logTime/2) {
        if(currentAnalogAccelValue > accelMax1) {
          accelMax1 = currentAnalogAccelValue;
          accelMax1Time = micros();
        }

      //check if accel is max value for second half of launch
      } else {
        if(currentAnalogAccelValue > accelMax2) {
          accelMax2 = currentAnalogAccelValue;
          accelMax2Time = micros();
        }
          
      }
    }

    //print the timestamps of the 2 accel max
    sensorFile.println("FIRST ACCEL MAX TIME: ");
    sensorFile.println(accelMax1Time);
    sensorFile.println("SECOND ACCEL MAX TIME: ");
    sensorFile.println(accelMax2Time);
    stopLogging();
    sensorFile.close();
  }

  //required method
  server.handleClient();

  //power on the gopro once every 10 seconds (gopro actually turns off every 5 minutes)
  if(usingGoPro && (millis() - lastGoProPowerOn > 10000)) {
    powerOnGoPro();
  }
}

//print all of the sensor values to sensorFile
//formatting of log should be adjusted here
void printSensor(File sensorFile) {
    sensorFile.print("Time: ");
    sensorFile.println(micros());
    printGyro(sensorFile); 
    printAccel(sensorFile);
    printMag(sensorFile);  
    printAttitude(imu.ax, imu.ay, imu.az, -imu.my, -imu.mx, imu.mz, sensorFile);
    sensorFile.print("Impact detect: ");
    sensorFile.println(currentAnalogAccelValue);
    sensorFile.println();
}

//print all SPIFFS files (debugging)
//this is called during setup to list all files (useful is storage is mysteriously used up)
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


//send sensor log file when user requests it
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

//homepage code
void handleRoot() {
  //long and annoying homepage code
  server.send(200, "text/html", "<h1>SPEARS Control Center</h1><h3><br /><form action=\"/restart\" method=\"POST\"><input type=\"submit\" value=\"Restart controller\" style=\"font-size:20px\"></form><form action=\"/powerongopro\" method=\"POST\"><input type=\"submit\" value=\"Power on GoPro\" style=\"font-size:20px\"></form><form action =\"/toggleled\" method =\"POST\"><input type=\"submit\" value=\"Toggle LED\" style=\"font-size:20px\"></form><form action=\"/fullsensorlog\" method=\"POST\"><input type=\"submit\" value=\"Begin full sensor logging\" style=\"font-size:20px\"></form><form action=\"/partialsensorlog\" method=\"POST\"><input type=\"submit\" value=\"Begin sensor logging without GoPro\" style=\"font-size:20px\"></form><form action=\"/wipestorage\" method=\"POST\"><input type=\"submit\" value=\"Clear Logs\" style=\"font-size:20px\"></form><br /><a href="+sensorOutputFileName+">Sensor Data Raw Text</a><br /><br />GoPro links (must change WiFi to use): <a href=http://10.5.5.9:8080/videos/DCIM/>Media</a> <a href=http://10.5.5.9:8080/gp/gpControl/command/shutter?p=1>Start</a> <a href=http://10.5.5.9:8080/gp/gpControl/command/shutter?p=0>Stop&nbsp;</a><form action=\"/disconnectgopro\" method = \"POST\"><input type =\"submit\" value=\"Disconnect GoPro\"></form></h3><h2><br /><br /><br /><br /><br /><br />Log</h2><p>"+webLog+"</p>");
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

//called to actual begin sensor logging (no checks)
void sensorLog() { 
  webLog = webLog + "Starting sensor logging for "+logTime+" milliseconds<br />";
  loggingSensors = true;

  //turns the led on
  if(!digitalRead(LED_PIN)) {
    toggleLED();
  }
}

//resents logging variables and stops logging
void stopLogging() {
  webLog = webLog + "Automatically stopping sensor logging <br />";
  loggingSensors = false;
  accelMax1 = 0;
  accelMax2 = 0;
  stopRecordingGoPro();
  toggleLED();
}

//send a wakeonlan signal to GoPro
//goProMac mac address specified above must be correct!
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
  SPIFFS.remove(("/"+sensorOutputFileName));
  sendHome();
}

//attampt to wirelessly start the gopro recording
bool startRecordingGoPro() {
  webLog = webLog + "Starting GoPro capture... ";
  visitURL("/gp/gpControl/command/mode?p=0");
  bool success = visitURL("/gp/gpControl/command/shutter?p=1");
  webLog = webLog + (success? "now recording!<br />" : "failed; is GoPro WiFi on?<br />");
  return success;
}

//attampt to wirelessly stop the gopro recording
bool stopRecordingGoPro() {
  webLog = webLog + "Stopping GoPro capture... ";
  bool success = visitURL("/gp/gpControl/command/shutter?p=0");
  webLog = webLog + (success? "GoPro successfully stopped!<br />" : "failed; is GoPro WiFi on?<br />");
  return success;
}

//set the LED to the opposite state (used for demonstrations)
void toggleLED() {
  digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  sendHome();
}

//perform an HTTP GET request to gopro specific link
//this method is useful for gopro commands (start/stop links)
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

//restart the controller
//NOTE: you will have to press the physical reset switch after restart() if code was just deployed
void restart() {
  sendHome();
  ESP.restart();
}

//send browser back to homepage
void sendHome() {
  server.sendHeader("Location","/");        
  server.send(303); 
}

//preliminary Gyro print function
void printGyro(File sensorFile)
{
  sensorFile.print("G: ");
  sensorFile.print(imu.calcGyro(imu.gx), 2);
  sensorFile.print(", ");
  sensorFile.print(imu.calcGyro(imu.gy), 2);
  sensorFile.print(", ");
  sensorFile.print(imu.calcGyro(imu.gz), 2);
  sensorFile.println(" deg/s");
}

//preliminary Accel print function
void printAccel(File sensorFile)
{  
  sensorFile.print("A: ");
  sensorFile.print(imu.calcAccel(imu.ax), 2);
  sensorFile.print(", ");
  sensorFile.print(imu.calcAccel(imu.ay), 2);
  sensorFile.print(", ");
  sensorFile.print(imu.calcAccel(imu.az), 2);
  sensorFile.println(" g");

}

//preliminary Magnetometer print function
void printMag(File sensorFile)
{  
  sensorFile.print("M: ");
  sensorFile.print(imu.calcMag(imu.mx), 2);
  sensorFile.print(", ");
  sensorFile.print(imu.calcMag(imu.my), 2);
  sensorFile.print(", ");
  sensorFile.print(imu.calcMag(imu.mz), 2);
  sensorFile.println(" gauss");
}

// Calculate pitch, roll, and heading.
// Pitch/roll calculations take from this app note:
// http://cache.freescale.com/files/sensors/doc/app_note/AN3461.pdf?fpsp=1
// Heading calculations taken from this app note:
// http://www51.honeywell.com/aero/common/documents/myaerospacecatalog-documents/Defense_Brochures-documents/Magnetic__Literature_Application_notes-documents/AN203_Compass_Heading_Using_Magnetometers.pdf
void printAttitude(float ax, float ay, float az, float mx, float my, float mz, File sensorFile)
{
  float roll = atan2(ay, az);
  float pitch = atan2(-ax, sqrt(ay * ay + az * az));
  
  float heading;
  if (my == 0)
    heading = (mx < 0) ? PI : 0;
  else
    heading = atan2(mx, my);
    
  heading -= DECLINATION * PI / 180;
  
  if (heading > PI) heading -= (2 * PI);
  else if (heading < -PI) heading += (2 * PI);
  else if (heading < 0) heading += 2 * PI;
  
  // Convert everything from radians to degrees:
  heading *= 180.0 / PI;
  pitch *= 180.0 / PI;
  roll  *= 180.0 / PI;
  
  sensorFile.print("Pitch, Roll: ");
  sensorFile.print(pitch, 2);
  sensorFile.print(", ");
  sensorFile.println(roll, 2);
  sensorFile.print("Heading: "); 
  sensorFile.println(heading, 2);
}



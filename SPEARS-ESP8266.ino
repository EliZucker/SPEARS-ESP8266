#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <FS.h>

const char WiFiName[] = "SPEARS";
const char WiFiPass[] = "SPEARSPEARS";
const String sensorOutputFileName = "test.txt";

//TO BE REPLACED
const int led = 14;

ESP8266WebServer server(80); 

//default method that is called on start
void setup() {
  //open serial connection at 115200 baud
  Serial.begin(115200); 

  //TO BE REPLACED
  //debugging led initialization
  pinMode(led, OUTPUT);

  //create AP and print info
  Serial.println(WiFi.softAP(WiFiName, WiFiPass)? "\nNetwork Setup Sucessful" : "\nNetwork Setup Failed");
  Serial.print("IP address: ");
  Serial.println(WiFi.softAPIP());

  //configure mDNA (not required, but convenient)
  //allows us to connect at spears.local
  if (MDNS.begin("spears")) {              
    Serial.println("mDNS responder started");
  } else {
    Serial.println("Error setting up MDNS responder!");
  }

  //initialize the file system
  SPIFFS.begin();   
  printFiles();

  //server configuration
  server.on(("/"+ sensorOutputFileName).c_str (), handleFileRead);
  server.on("/", handleRoot);
  server.on("/start", HTTP_POST, startLogging);
  server.onNotFound([](){
    server.send(404, "text/plain", "404: Not found");
  });
  server.begin(); 
  Serial.println("Server started");
}

//this method is called repeatedly during operation
void loop() {
  server.handleClient();
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
  server.send(200, "text/html", "<h1>SPEARS Control Center</h1><h2></br><form action=\"/start\" method=\"POST\"><input type=\"submit\" value=\"Begin sensor logging\" style=\"font-size:20px\"></form></br><a href="+sensorOutputFileName+">Sensor Data Raw Text</a></h2>");
}

//called when buttton is pressed
void startLogging() { 
  //TO BE REPLACED                         
  digitalWrite(led,!digitalRead(led)); 

  sendHome();                        
}

//send browser back to homepage
void sendHome() {
  server.sendHeader("Location","/");        
  server.send(303); 
}


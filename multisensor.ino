/*
MQTT MultiSensor
By: John Andrew David
Creation Date: 7/20/2017

Sensors:
PIR Motion
Photocell Photoresistor
Reed Switch
Active Buzzer
Sound Detector 
Button
DHT22 Temperature
*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <elapsedMillis.h>
#include "DHT.h"

WiFiClient espClient;
PubSubClient client(espClient);

//Network
const char* ssid = "";
const char* password = "";
const char* mqtt_server = "";
const char* mqtt_username = "";
const char* mqtt_password = "";
const int mqtt_port = 1883;

// MQTT Topics
const char* MQTT_SENSOR_TOPIC = "home/bedroom/multisensor";
const char* MQTT_TEMPERATURE_SENSOR_TOPIC = "home/bedroom/multisensor/temperature";
const char* MQTT_SET_TOPIC1 = "home/bedroom/buzzer/set";

//OTA
#define SENSORNAME "" //change this to whatever you want to call your device
#define OTApassword "" //the password you will need to enter to upload remotely via the ArduinoIDE
int OTAport = 8266;

//PINS
#define PHOTOPIN A0
#define MAGPIN D1
#define PIRPIN D2
#define BUZZPIN D3
#define SOUNDPIN D4
#define BUTTONPIN D5
#define DHTPIN D6
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

//Global Variables
float humidity;
float farenheit;
float photoResistance;
String motionActivated;
int photoValue;
int photoValueTemp;
int soundClaps = 0;
long soundDetectStart = 0;
long soundDetectSpan = 0;
String motionStatus;
String magnetStatus;
String soundStatus;
String buttonStatus;
boolean pirState = true;
boolean reedState = true;
boolean micState = true;
boolean buttonState = true;

elapsedMillis timeElapsed = 30000;

//JSON
const int BUFFER_SIZE = 300;
#define MQTT_MAX_PACKET_SIZE 512;


//Sensor Functions
void read_dht22(){

  float h = dht.readHumidity();
  float f = dht.readTemperature(true);

  if (isnan(h) || isnan(f)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }
  else {
    humidity = h;
    farenheit = f;
    publishTemperatureData();  
  }
}


void read_motion() {
  int pirValue = digitalRead(PIRPIN);

  if (pirValue == LOW && pirState) {
    motionStatus = "OFF";
    publishData();
    pirState = !pirState; 
  }
  else if (pirValue == HIGH && !pirState) {
    motionStatus = "ON";
    publishData();
    pirState = !pirState;
    Serial.println("Motion Detected");
  }  
}


void read_magnet() {
  int magnetValue = digitalRead(MAGPIN);

  if (magnetValue == LOW && reedState) {
    magnetStatus = "OFF";
    publishData();
    reedState = !reedState;
    Serial.println("Door is Closed");
  }
  else if (magnetValue == HIGH && !reedState) {
    magnetStatus = "ON";
    publishData();
    reedState = !reedState;
    Serial.println("Door is Open");
  }
}


void read_sound() {
  int soundValue = digitalRead(SOUNDPIN);

  //if there is a sound
  if (soundValue == LOW) {
    if (soundClaps == 0) {
      
      soundDetectStart = soundDetectSpan = millis();
      soundClaps++; 

      Serial.println("Sound Claps: " + (String)soundClaps);  
      Serial.println("sound detect span start: " + (String)soundDetectStart);
      Serial.println("");
    }
    else if (soundClaps == 1 && millis()-soundDetectSpan >= 250) {
      
      Serial.println("Millis: " + (String)millis());
      Serial.println("sound detect span: " + (String)soundDetectSpan);
      
      soundDetectSpan = millis();
      soundClaps++;
      Serial.println("Sound Claps: " + (String)soundClaps); 
      Serial.println("");
    }
  }

  if (millis()-soundDetectStart >= 350) {
    if (soundClaps == 2) {
      if (micState) {
        soundStatus = "ON";
        publishData();
        micState = !micState;
        Serial.println("Sound Switch is Open: " + soundStatus);
      }
      else if (!micState) {
        soundStatus = "OFF";
        publishData();
        micState = !micState;
        Serial.println("Sound Switch is Closed: " + soundStatus);
      }
    }
    soundClaps = 0;
  } 
}


void read_button() {
  int buttonValue = digitalRead(BUTTONPIN);

  if (buttonValue == LOW) {
    Serial.println("Button is depressed");

    if (buttonState) {
      buttonStatus = "ON";
      publishData();
      buttonState = !buttonState;    
    } 
    else if (!buttonState) {
      buttonStatus = "OFF";
      publishData();
      buttonState = !buttonState;
    }
    delay(500);
  }
}


void read_photoresistor() {
  photoValue = analogRead(PHOTOPIN);
  
  if (photoValue != photoValueTemp) {
    photoValueTemp = photoValue;
    publishData();  
  }
}


void publishData() {
  StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();

  root["photocell"] = (String)photoValue;
  root["pir"] = (String)motionStatus;
  root["magnet"] = (String)magnetStatus;
  root["sound"] = (String)soundStatus;
  root["button"] = (String)buttonStatus;
  
  root.prettyPrintTo(Serial);
  Serial.println("");
  
  char buffer[root.measureLength() + 1];
  root.printTo(buffer, sizeof(buffer));
  client.publish(MQTT_SENSOR_TOPIC, buffer, true);
}


void publishTemperatureData() {
  StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  
  root["farenheit"] = (String)farenheit;
  root["humidity"] = (String)humidity;
  
  root.prettyPrintTo(Serial);
  Serial.println("");
  
  char buffer[root.measureLength() + 1];
  root.printTo(buffer, sizeof(buffer));
  client.publish(MQTT_TEMPERATURE_SENSOR_TOPIC, buffer, true);
}


void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  
  char message[length + 1];
  for (int i = 0; i < length; i++) {
    message[i] = (char)payload[i];
  }
  message[length] = '\0';
  Serial.print("Payload: ");
  Serial.println(message);

  if (String(topic) == String(MQTT_SET_TOPIC1)) {
    if (String(message) == "ON") {
        digitalWrite(BUZZPIN, HIGH);
    } else {
        digitalWrite(BUZZPIN, LOW);
    }
  }
}


void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(SENSORNAME, mqtt_username, mqtt_password)) {
      Serial.println("connected");
      client.subscribe(MQTT_SET_TOPIC1);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


void setup_ota() {
  //OTA SETUP
  ArduinoOTA.setPort(OTAport);
  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(SENSORNAME);

  // No authentication by default
  ArduinoOTA.setPassword((const char *)OTApassword);

  ArduinoOTA.onStart([]() {
    Serial.println("Starting");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
}
 
void setup() {
  Serial.begin(115200);
  
  setup_wifi(); 
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  setup_ota();

  dht.begin();

  pinMode(PHOTOPIN, OUTPUT);
  pinMode(PIRPIN, INPUT);
  pinMode(MAGPIN, INPUT_PULLUP);
  pinMode(BUZZPIN, OUTPUT);
  pinMode(SOUNDPIN, INPUT);
  pinMode(BUTTONPIN, INPUT_PULLUP);
}

 
void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  ArduinoOTA.handle();

  if (timeElapsed >= 30000) {
    read_dht22();
    read_photoresistor();
    timeElapsed = 0;
  }
  
  read_motion();
  read_magnet();
  read_sound();
  read_button();
}

#include "secrets.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <MQTTClient.h>
#include <ArduinoJson.h>
#include <MFRC522.h>
#include <SPI.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define USE_TLS
#define THINGNAME "ArduinoESP32"
#define AWS_IOT_PUBLISH_TOPIC "iticbcn/assistencia/registre"
#define AWS_IOT_SUBSCRIBE_TOPIC "iticbcn/assistencia/confirm"
#define SS_PIN 5
#define RST_PIN 0
#define LED_VERDE 16
#define LED_ROJO 15

LiquidCrystal_I2C lcd(0x27,16,2);

MFRC522 rfid(SS_PIN, RST_PIN);

MFRC522::MIFARE_Key key;

WiFiClientSecure net = WiFiClientSecure();
MQTTClient mqttClient = MQTTClient(256);

bool WiFiConnected = false;
bool AWSConnected = false;

void SetupWifi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  delay(10);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi conectado");
    Serial.println("IP: ");
    Serial.println(WiFi.localIP());
    WiFiConnected = true;
  }
}

void CheckWifi() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFiConnected = false;
    Serial.println("WiFi desconectado. Intentando reconectar...");
    WiFi.disconnect();
    SetupWifi();
  } else {
    WiFiConnected = true;
  }
}

void SetupRFID() {
  SPI.begin();
  rfid.PCD_Init();
}

bool CheckRFID(String &tag) {
  if (!rfid.PICC_IsNewCardPresent()) {
    return false;
  }
  if (!rfid.PICC_ReadCardSerial()) {
    return false;
  }
  String uidStr = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uidStr += "0";
    uidStr += String(rfid.uid.uidByte[i], HEX);
  }
  uidStr.toUpperCase();
  tag = uidStr;
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  return true;
}

void messageReceived(String &topic, String &payload) {
  Serial.print("MQTT mensaje [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(payload);
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.print("Error en convertir a JSON: ");
    Serial.println(error.c_str());
    return;
  }
  String resultado = doc["resultado"];
  if (resultado == "OK") {
    Serial.print("Se ha guardado correctamente a la BD\n");
    digitalWrite(LED_VERDE, HIGH);
    lcd.setCursor(0, 0);
    lcd.print("OK");
    delay(2000);
    digitalWrite(LED_VERDE, LOW);
    lcd.clear();
  }
  else {
    Serial.print("Error en guardar a la BD\n");
    digitalWrite(LED_ROJO, HIGH);
    lcd.setCursor(0, 0);
    lcd.print("ERROR");
    delay(2000);
    digitalWrite(LED_ROJO, LOW);
    lcd.clear();
  }
}

void SetupAWS() {
  const char *endpoint = AWS_IOT_ENDPOINT;
  int port = AWS_IOT_PORT;
#ifdef USE_TLS
  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);
#endif
  mqttClient.begin(endpoint, port, net);
  mqttClient.onMessage(messageReceived);
  if (WiFiConnected) {
    Serial.print("Conectando MQTT a ");
    Serial.print(endpoint);
    Serial.print(":");
    Serial.println(port);
    if (mqttClient.connect(THINGNAME)) {
      Serial.println("MQTT conectado");
      AWSConnected = true;
      mqttClient.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);
    } else {
      Serial.println("MQTT no conectado");
      AWSConnected = false;
    }
  } else {
    Serial.println("No hay WiFi, no se intenta conectar MQTT");
    AWSConnected = false;
  }
}

void CheckAWS() {
  if (!WiFiConnected) {
    AWSConnected = false;
    return;
  }
  if (!mqttClient.connected()) {
    AWSConnected = false;
    Serial.println("MQTT desconectado. Intentando reconectar...");
    if (mqttClient.connect(THINGNAME)) {
      Serial.println("MQTT reconectado");
      mqttClient.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);
      AWSConnected = true;
    } else {
      Serial.println("Error al reconectar MQTT");
    }
    delay(1000);
  } else {
    mqttClient.loop();
    AWSConnected = true; 
  }
}

void PublicaTag(String tagID) {
  if (!AWSConnected) {
    Serial.println("No conectado a AWS, no se publica");
    return;
  }
  StaticJsonDocument<200> doc;
  doc["uid"] = tagID;
  doc["zona"] = "A03";

  String payload;
  serializeJson(doc, payload);

  String topic = AWS_IOT_PUBLISH_TOPIC;
  Serial.print("Publicando a ");
  Serial.print(topic);
  Serial.print(" -> ");
  Serial.println(payload);
  if (mqttClient.publish(topic, payload)) {
    Serial.println("Publicado OK");
  } else {
    Serial.println("Error al publicar");
  }
}

void setup() {
  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.clear();
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_ROJO, OUTPUT);
  Serial.begin(115200);
  delay(50);
  SetupWifi();
  SetupRFID();
  SetupAWS();
}

void loop() {
  CheckWifi();
  CheckAWS();
  if (AWSConnected) {
    String tag;
    if (CheckRFID(tag)) {
      Serial.print("Tag detectado: ");
      Serial.println(tag);
      PublicaTag(tag);
      delay(500);
    }
  }
  delay(50);
}

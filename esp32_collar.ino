#include <Wire.h>
#include <TinyGPS++.h>
#include <DHT.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <time.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>



#define WIFI_SSID "REDAP_6956"
#define WIFI_PASSWORD "internetaca"

#define DATABASE_URL "https://upetbackendapi.onrender.com/api/v1/add_smart_collar"
// Configuración del GPS
#define RXD2 16
#define TXD2 17
#define GPS_BAUD 9600
TinyGPSPlus gps;
HTTPClient client;
String sensorID = "IOPET_01";


HardwareSerial gpsSerial(2);

// Configuración del DHT11 (ambiente)
#define DHTPIN_AMB 27
#define DHTTYPE DHT11
DHT dhtAmb(DHTPIN_AMB, DHTTYPE);

// Configuración del DHT11 (corporal)
#define DHTPIN_CORP 14
DHT dhtCorp(DHTPIN_CORP, DHTTYPE);

// Configuración del MQ2
#define MQ2_DO_PIN 12

// Configuración del MAX30102
MAX30105 particleSensor;
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
float beatsPerMinute;
int beatAvg;

// Contador para controlar la impresión de datos
int count = 0;
int savedId ;
String url = "";

void setup() {
  // Inicialización del Serial
  Serial.begin(115200);

  // Inicialización de sensores
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, RXD2, TXD2);
  dhtAmb.begin();
  dhtCorp.begin();
  pinMode(MQ2_DO_PIN, INPUT);
  Serial.println("MQ2 iniciado. Calentando...");
  delay(2000);

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 no encontrado.");
  }
  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeGreen(0);
  Serial.println("MAX30102 iniciado.");



  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("WiFi connected");

  client.begin(DATABASE_URL);
  int httpResponseCode = client.GET();
  if (httpResponseCode > 0) {
    Serial.println("Endpoint reachable");
  } else {
    Serial.print("Endpoint error: ");
    Serial.println(httpResponseCode);
  }

  // Timer Client Configuration
  configTime(0, 0, "pool.ntp.org");
}

void loop() {
  // Recopilar datos de sensores
  float tempAmb = dhtAmb.readTemperature();
  float humAmb = dhtAmb.readHumidity();
  float tempCorp = dhtCorp.readTemperature();
  float humCorp = dhtCorp.readHumidity();
  int gasState = digitalRead(MQ2_DO_PIN);
  long irValue = particleSensor.getIR();
  String gpsData = getGPSData();
  String bpmData = getBPMData(irValue);
  int pet_id = 7;
  int battery = 100;
  // Redondear tempCorp a 1 decimal
  float roundedTempCorp = round(tempCorp * 10.0) / 10.0;
  int roundedAvgBpm = round(beatAvg);
  // Ahora puedes usar roundedTempCorp en cálculos u otras funciones
  float lat = gps.location.lat();
  float lng = gps.location.lng();
  

  // Incrementar contador
  count++;
  
  // Imprimir datos solo cada 10 ciclos
  if (count > 99) {
    printData(tempAmb, humAmb, tempCorp, humCorp, gasState, gpsData, bpmData);

    StaticJsonDocument<200> jsonDoc;
    jsonDoc["id"] = "1";
    jsonDoc["serial_number"] = sensorID;
    jsonDoc["temperature"] = tempCorp;
    jsonDoc["lpm"] = roundedAvgBpm;
    jsonDoc["battery"] = battery;
    
    JsonObject location = jsonDoc.createNestedObject("location");
    location["latitude"] = lat;
    location["longitude"] = lng;
    jsonDoc["pet_id"] = pet_id;

    String jsonData;
    serializeJson(jsonDoc, jsonData);

    // Send information to Database (POST request to create ID)
    client.begin(DATABASE_URL);
    client.addHeader("Content-Type", "application/json");
    int httpResponseCode = client.POST(jsonData);

    if (httpResponseCode > 0) {
      String response = client.getString();
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);
      Serial.print("Response from server: ");
      Serial.println(response);
      StaticJsonDocument<200> responseDoc;
      DeserializationError error = deserializeJson(responseDoc, response);
      
      // Verificar si hubo un error al deserializar el JSON
      if (error) {
        Serial.print("Error al deserializar JSON: ");
        Serial.println(error.f_str());
        Serial.print("ID: ");
        Serial.println(savedId);
        StaticJsonDocument<200> jsonDoc1;
        jsonDoc1["id"] = String(savedId);
        jsonDoc1["serial_number"] = sensorID;
        jsonDoc1["temperature"] = tempCorp;
        jsonDoc1["lpm"] = roundedAvgBpm;
        jsonDoc1["battery"] = battery;
        
        JsonObject location = jsonDoc1.createNestedObject("location");
        location["latitude"] = lat;
        location["longitude"] = lng;
        jsonDoc1["pet_id"] = pet_id;

        String jsonData1;
        serializeJson(jsonDoc1, jsonData1);

        Serial.println(url);
        url = "https://upetbackendapi.onrender.com/api/v1/smart-collars/" + String(savedId);
        Serial.print("URL construida: ");
        Serial.println(url);
        // Send information to Database (POST request to create ID)
        client.begin(url);
        client.addHeader("Content-Type", "application/json");
        int httpResponseCode = client.PUT(jsonData);
        if (httpResponseCode > 0) {
          String response = client.getString();
          Serial.print("HTTP Response code: ");
          Serial.println(httpResponseCode);
          Serial.print("Response from server: ");
          Serial.println(response);
        } else {
          Serial.print("Error on sending POST: ");
          Serial.println(httpResponseCode);
          Serial.print("Error details: ");
          Serial.println(client.errorToString(httpResponseCode).c_str());
          }
      } else{
        int id = responseDoc["id"];  // id como entero
        Serial.println(id);
        if (id > 0) {
            savedId = id;
            Serial.println(savedId);
            url = "https://upetbackendapi.onrender.com/api/v1/smart-collars/" + String(savedId);
            Serial.print("URL construida: ");
            Serial.println(url);
        } else {
            Serial.println("Error: El ID no se obtuvo correctamente.");
            }
        String serial_number = responseDoc["serial_number"].as<String>();  // serial_number como String
        Serial.print("ID: ");
        Serial.println(id);
        Serial.print("Serial Number: ");
        Serial.println(serial_number);
        }
      // Imprimir los datos extraídos
      // Extraer datos específicos del JSON

    } else {
      Serial.print("Error on sending POST: ");
      Serial.println(httpResponseCode);
      Serial.print("Error details: ");
      Serial.println(client.errorToString(httpResponseCode).c_str());
      }
    client.end(); // Free resources

  delay(500); // Send data every 5 minutes

  count = 0; // Reiniciar contador
  }
}

String getGPSData() {
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  if (gps.location.isUpdated()) {
    return String("Lat: ") + gps.location.lat() + 
           ", Lng: " + gps.location.lng() + 
           ", Speed (km/h): " + gps.speed.kmph() +
           ", Alt (m): " + gps.altitude.meters() +
           ", Satellites: " + gps.satellites.value();
  }
  return "GPS no actualizado.";
}

String getBPMData(long irValue) {
  if (checkForBeat(irValue) == true) {
    long delta = millis() - lastBeat;
    lastBeat = millis();
    beatsPerMinute = 60 / (delta / 1000.0);

    Serial.print("Delta: ");
    Serial.println(delta);
    Serial.print("BPM calculado: ");
    Serial.println(beatsPerMinute);

    if (beatsPerMinute < 255 && beatsPerMinute > 20) {
      rates[rateSpot++] = (byte)beatsPerMinute;
      rateSpot %= RATE_SIZE;
      beatAvg = 0;
      for (byte x = 0; x < RATE_SIZE; x++) beatAvg += rates[x];
      beatAvg /= RATE_SIZE;
    }
  }
  return String("BPM: ") + beatsPerMinute + 
         ", Avg BPM: " + beatAvg + 
         ", IR: " + irValue;
}

void printData(float tempAmb, float humAmb, float tempCorp, float humCorp, int gas, String gps, String bpm) {
  Serial.println("=== DATOS RECOPILADOS ===");
  Serial.println("Ambiente:");
  Serial.print("  Temperatura: "); Serial.print(tempAmb); Serial.println(" °C");
  Serial.print("  Humedad: "); Serial.print(humAmb); Serial.println(" %");
  Serial.println("Corporal:");
  Serial.print("  Temperatura: "); Serial.print(tempCorp); Serial.println(" °C");
  Serial.print("  Sudor (Humedad): "); Serial.print(humCorp); Serial.println(" %");
  Serial.print("Gas: ");
  Serial.println(gas == HIGH ? "Presente" : "No presente");
  Serial.println("GPS:");
  Serial.println("  " + gps);
  Serial.println("Frecuencia Cardiaca:");
  Serial.println("  " + bpm);
  Serial.println("=========================");
}

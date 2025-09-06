#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "db_read.h"

// Remplace par les tiens
static const char* API_URL  = "https://www.meteospit.fr/api/stationdirect/2";
static const char* API_KEY  = "2878ece33344e4f6d9e1105c0362f0671d9432fb4d997023acb734f4c6e6793a";
static const int   STATION_ID = 2;
static const char* STATION_METEOS_PATH = "api/station_meteos/2";

// Transforme "YYYY-MM-DD HH:MM:SS" -> "YYYY-MM-DDTHH:MM:SS+00:00"
static String toISO8601(const String& tsSqlite) {
  if (tsSqlite.length() < 19) return String(""); // fallback si vide
  String s = tsSqlite;
  s.replace(" ", "T");
  return s + "+00:00"; // adapte si tu veux le fuseau local
}

// Convertit 12.34 -> "12,34" (ton API veut une virgule pour pointRose)
static String frenchDecimal(float v, uint8_t digits = 1) {
  char buf[24];
  dtostrf(v, 0, digits, buf);
  String s(buf);
  s.replace('.', ',');
  return s;
}

// Lis la dernière ligne de station_direct et l’envoie à l’API
bool sendLatestRowToApi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[API] WiFi non connecté");
    return false;
  }

  StationDirect sd;
  if (!readLatestStationDirect(sd)) {
    Serial.println("[API] Impossible de lire station_direct");
    return false;
  }

  // Construction du JSON attendu par ton API
  StaticJsonDocument<768> doc;

doc["dateheure"]   = toISO8601(sd.timestamp);  
doc["tempdh22"]    = roundf(sd.tempdht22 * 10) / 10.0f;
doc["tempbmp280"]  = roundf(sd.tempbmp280 * 10) / 10.0f;
doc["humidite"]    = (int)sd.humiditer;
doc["pression"]    = roundf(sd.pression * 10) / 10.0f;
doc["lumiere"]     = roundf(sd.lumiere * 10) / 10.0f;
doc["anemometre"]  = roundf(sd.anemometre * 10) / 10.0f;
doc["girouette"]   = (int)sd.girouette;
doc["pluviometre"] = roundf(sd.pluviometre * 10) / 10.0f;
doc["pointRose"]   = String(sd.pointderosee);
doc["Eclaire1km"]  = 0;
doc["Eclaire10km"] = 0;
doc["Eclaire50km"] = 0;
doc["alertemeteofrance"] = nullptr;
doc["couleurmeteofrance"] = "1";
doc["datedebutmeteofrance"] = nullptr;
doc["datefinmeteofrance"]   = nullptr;
doc["tpsvie"]       = String((int)sd.tpsvie);
doc["ghost"]        = (int)sd.ghost;
doc["stationId"]    = STATION_ID;
doc["stationMeteos"]= STATION_METEOS_PATH;

  String payload;
  serializeJson(doc, payload);

  Serial.println("Payload JSON envoyé :");
  Serial.println(payload);

  // Envoi HTTPS (certificat ignoré pour simplifier)
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, API_URL)) {
    Serial.println("[API] http.begin() a échoué");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  http.addHeader("X-API-KEY", API_KEY);

  int code = http.PUT(payload);
  Serial.printf("[API] PUT %s -> code %d\n", API_URL, code);
  if (code > 0) {
    String resp = http.getString();
    Serial.println(resp);
  } else {
    Serial.printf("[API] Erreur HTTP: %d\n", code);
  }
  http.end();
  return code >= 200 && code < 300;
}

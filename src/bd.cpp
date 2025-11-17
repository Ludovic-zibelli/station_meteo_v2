#include <Arduino.h>
#include <sqlite3.h>
#include "bd.h"

extern sqlite3 *db;

bool updateStationDirect(
    int id,
    float tempdht22,
    float humiditer,
    float tempbmp280,
    float pression,
    float lumiere,
    float anemometre,
    float girouette,
    float pluviometre,
    float pointderosee,
    float ghost,
    float tpsvie,
    String ts,
    float rafale
) {
    sqlite3_stmt *stmt;
    const char *sql =
        "UPDATE station_direct SET "
        "tempdht22=?, humiditer=?, tempbmp280=?, pression=?, lumiere=?, "
        "anemometre=?, girouette=?, pluviometre=?, pointderosee=?, ghost=?, tpsvie=?, timestamp=?, rafale=? "
        "WHERE id=?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_double(stmt, 1, tempdht22);
        sqlite3_bind_double(stmt, 2, humiditer);
        sqlite3_bind_double(stmt, 3, tempbmp280);
        sqlite3_bind_double(stmt, 4, pression);
        sqlite3_bind_double(stmt, 5, lumiere);
        sqlite3_bind_double(stmt, 6, anemometre);
        sqlite3_bind_double(stmt, 7, girouette);
        sqlite3_bind_double(stmt, 8, pluviometre);
        sqlite3_bind_double(stmt, 9, pointderosee);
        sqlite3_bind_double(stmt, 10, ghost);
        sqlite3_bind_double(stmt, 11, tpsvie);
        sqlite3_bind_text(stmt, 12, ts.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 13, rafale);
        sqlite3_bind_int(stmt, 14, id);
        Serial.printf("SQLite error: %s\n", sqlite3_errmsg(db));

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            sqlite3_finalize(stmt);
            return true;
        }
        sqlite3_finalize(stmt);
    }
    return false;
}

//mise a jour de l'anémomètre uniquement
bool updateAnemometre(int id, float anemometre) {
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "UPDATE station_direct SET anemometre=? WHERE id=?;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        // Optionnel : log d'erreur
        // Serial.printf("SQLite prepare error: %s\n", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_double(stmt, 1, anemometre);
    sqlite3_bind_int(stmt, 2, id);
    Serial.printf("SQLite error: %s\n", sqlite3_errmsg(db));

    int rc = sqlite3_step(stmt);
    bool ok = (rc == SQLITE_DONE);

    // Facultatif : vérifier qu'au moins une ligne a été affectée
    // ok = ok && (sqlite3_changes(db) > 0);

    sqlite3_finalize(stmt);
    return ok;
}

bool updateTensions(int id, float tension_batterie, float tension_solaire) {
    sqlite3_stmt *stmt;
    const char *sql = "UPDATE tensions SET tension_batterie=?, tension_solaire=? WHERE id=?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_double(stmt, 1, tension_batterie);
        sqlite3_bind_double(stmt, 2, tension_solaire);
        sqlite3_bind_int(stmt, 3, id);
        Serial.printf("SQLite error: %s\n", sqlite3_errmsg(db));

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            sqlite3_finalize(stmt);
            return true;
        }
        sqlite3_finalize(stmt);
    }
    return false;
}

//Mise a jour des etats des capteurs
bool updateEtatCapteurs(int id,
                        int capteur_dht22, int capteur_bmp280, int capteur_pluvio,
                        int capteur_girou, int capteur_anemo, float tension_batterie, float tension_solaire) {
    sqlite3_stmt *stmt;
    const char *sql = "UPDATE etatcapteurs SET "
                      "capteur_dht22=?, capteur_bmp280=?, capteur_pluvio=?, "
                      "capteur_girou=?, capteur_anemo=?,  tension_solaire=?, tension_batterie=? "
                      "WHERE id=?;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, capteur_dht22);
        sqlite3_bind_int(stmt, 2, capteur_bmp280);
        sqlite3_bind_int(stmt, 3, capteur_pluvio);
        sqlite3_bind_int(stmt, 4, capteur_girou);
        sqlite3_bind_int(stmt, 5, capteur_anemo);
        sqlite3_bind_double(stmt, 6, tension_solaire);
        sqlite3_bind_double(stmt, 7, tension_batterie);
        sqlite3_bind_int(stmt, 8, id);
        Serial.printf("SQLite error: %s\n", sqlite3_errmsg(db));

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            sqlite3_finalize(stmt);
            return true;
        }
        sqlite3_finalize(stmt);
    }
    return false;
}


bool updateModulesInDB(int id, int bmp280, int dht22, int sht40, int anemo, int girou, int pluvio, int tension, int bitvie)
 {
  sqlite3_stmt *stmt;
 
const char *sql =
  "UPDATE etatcapteurs SET module_bmp280=?, module_dht22=?, module_sht40=?, "
  "module_anemo=?, module_girou=?, module_pluvio=?, module_tension=?, module_bitvie=? WHERE id=?;";


  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    Serial.printf("Erreur prepare (updateModulesInDB): %s\n", sqlite3_errmsg(db));
    return false;
  }

  sqlite3_bind_int(stmt, 1, bmp280);
  sqlite3_bind_int(stmt, 2, dht22);
  sqlite3_bind_int(stmt, 3, sht40);
  sqlite3_bind_int(stmt, 4, anemo);
  sqlite3_bind_int(stmt, 5, girou);
  sqlite3_bind_int(stmt, 6, pluvio);
  sqlite3_bind_int(stmt, 7, tension);
  sqlite3_bind_int(stmt, 8, bitvie);
  sqlite3_bind_int(stmt, 9, id);


  bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
  if (!ok) Serial.printf("Erreur step (updateModulesInDB): %s\n", sqlite3_errmsg(db));
  sqlite3_finalize(stmt);
  return ok;
}


bool updateActivationApiInDB(int id, int activation_envoi_api) {
    sqlite3_stmt *stmt;
    const char *sql = "UPDATE config SET activation_envoi_api = ? WHERE id = ?;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        Serial.printf("❌ Erreur prepare (updateActivationApiInDB): %s\n", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_int(stmt, 1, activation_envoi_api);
    sqlite3_bind_int(stmt, 2, id);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        Serial.printf("❌ Erreur step (updateActivationApiInDB): %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_finalize(stmt);
    return true;
}

// Mise à jour de la configuration de l'application
bool updateAppConfig(const AppConfig& c) {
  const char* sql =
    "UPDATE config SET "
    "ssid_wifi=?, pass_wifi=?, IP_WIFI=?, ID_STATION=?, adresse_api=?, token=?, activation_envoi_api=? "
    "WHERE id=1;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    // Serial.printf("Prepare failed: %s\n", sqlite3_errmsg(db));
    return false;
  }

  // bind 1..7
  sqlite3_bind_text(stmt, 1, c.ssid_wifi.c_str(),   -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, c.pass_wifi.c_str(),   -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, c.ip_wifi.c_str(),     -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, c.id_station.c_str(),  -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, c.adresse_api.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, c.token.c_str(),       -1, SQLITE_TRANSIENT);
  sqlite3_bind_int (stmt, 7, c.activation_envoi_api);

  bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  return ok;
}


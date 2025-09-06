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
    String ts
) {
    sqlite3_stmt *stmt;
    const char *sql =
        "UPDATE station_direct SET "
        "tempdht22=?, humiditer=?, tempbmp280=?, pression=?, lumiere=?, "
        "anemometre=?, girouette=?, pluviometre=?, pointderosee=?, ghost=?, tpsvie=?, timestamp=? "
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
        sqlite3_bind_int(stmt, 13, id);
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
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            sqlite3_finalize(stmt);
            return true;
        }
        sqlite3_finalize(stmt);
    }
    return false;
}
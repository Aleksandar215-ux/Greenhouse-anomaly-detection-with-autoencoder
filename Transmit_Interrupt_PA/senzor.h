// =====================================================================
// senzor.h  -  citanje senzora na predajnoj strani (ESP32 u plasteniku)
//
// Koriscenje u .ino skici:
//   #include "senzor.h"
//   setup():  senzor_init();
//   loop():   if (senzor_procitaj(&T, &RH)) { ... }
//
// Izaberi senzor ispod. Za DHT je potrebna biblioteka "DHT sensor library"
// (Adafruit), za BME280 "Adafruit BME280 Library".
// =====================================================================
#pragma once

#include <Arduino.h>

// --- IZABERI JEDAN ---------------------------------------------------
#define SENZOR_DHT11      // proba - vlaznost samo do 90%, rezolucija 1 stepen
//#define SENZOR_DHT22      // dobro - 0.1 rezolucija, RH 0-100%
//#define SENZOR_BME280       // najbolje - I2C, RH 0-100%, daje i pritisak

#define DHT_PIN     46      // samo za DHT
#define BME_ADRESA  0x76    // ako ne radi, probaj 0x77 (vidi ispis pri startu)

// --- interval slanja -------------------------------------------------
// MORA biti 5 minuta - model je treniran na tom koraku.
// Prozori u modelu: 36 uzoraka = 3h, 73 uzoraka = ~6h.
#define INTERVAL_MS  300000UL

// ---------------------------------------------------------------------
#if defined(SENZOR_DHT11) || defined(SENZOR_DHT22)
  #include <DHT.h>
  #ifdef SENZOR_DHT11
    DHT dht(DHT_PIN, DHT11);
    #define IME_SENZORA "DHT11"
  #else
    DHT dht(DHT_PIN, DHT22);
    #define IME_SENZORA "DHT22"
  #endif
#elif defined(SENZOR_BME280)
  #include <Adafruit_BME280.h>
  Adafruit_BME280 bme;
  #define IME_SENZORA "BME280"
#endif

// ---------------------------------------------------------------------
static bool senzor_init() {
#if defined(SENZOR_DHT11) || defined(SENZOR_DHT22)
  dht.begin();
  Serial.println("Senzor: " IME_SENZORA);
  #ifdef SENZOR_DHT11
  Serial.println("UPOZORENJE: DHT11 meri vlaznost samo do 90%.");
  Serial.println("U plasteniku je RH cesto preko toga -> ocekuj lazne alarme.");
  Serial.println("U detekcija.h odkomentarisi IGNORISI_VLAZNOST za probu.");
  #endif
  return true;

#elif defined(SENZOR_BME280)
  // Wire je vec pokrenut u setupBoards()
  if (!bme.begin(BME_ADRESA)) {
    Serial.println("BME280 nije nadjen - probaj drugu adresu (0x76 / 0x77)");
    return false;
  }
  // rezim za sporo pracenje ambijenta, manja potrosnja i manje samozagrevanje
  bme.setSampling(Adafruit_BME280::MODE_FORCED,
                  Adafruit_BME280::SAMPLING_X1,   // temperatura
                  Adafruit_BME280::SAMPLING_X1,   // pritisak
                  Adafruit_BME280::SAMPLING_X1,   // vlaznost
                  Adafruit_BME280::FILTER_OFF);
  Serial.println("Senzor: " IME_SENZORA);
  return true;
#endif
}

// ---------------------------------------------------------------------
// Vraca false ako merenje nije uspelo - tada NE slati staru vrednost,
// nego "ERR". Ponavljanje poslednje vrednosti bi model procitao kao
// zaglavljen senzor.
// ---------------------------------------------------------------------
static bool senzor_procitaj(float* T, float* RH) {
#if defined(SENZOR_DHT11) || defined(SENZOR_DHT22)
  *T  = dht.readTemperature();
  *RH = dht.readHumidity();

#elif defined(SENZOR_BME280)
  bme.takeForcedMeasurement();
  *T  = bme.readTemperature();
  *RH = bme.readHumidity();
#endif

  if (isnan(*T) || isnan(*RH)) return false;
  if (*T < -20 || *T > 60)     return false;    // isto ciscenje kao u Pythonu
  if (*RH < 0  || *RH > 100)   return false;
  return true;
}

// ---------------------------------------------------------------------
// Paket: "T,RH"  npr "22.3,78.5"   ili  "ERR" ako merenje nije uspelo
// ---------------------------------------------------------------------
static String senzor_paket() {
  float T, RH;
  if (!senzor_procitaj(&T, &RH)) return "ERR";
  return String(T, 1) + "," + String(RH, 1);
}

// =====================================================================
// detekcija.h  -  detekcija anomalija na prijemnoj strani (ESP32 u kuci)
//
// Koriscenje u .ino skici:
//   #include "detekcija.h"
//   setup():  detekcija_init();
//   loop():   obradi(T, RH, dan_u_godini, mesec, sat, &tip);
//
// Zahteva pored sebe:  model_data_0.h, model_data_1.h, model_data_2.h,
//                      klimatologija.h, konstante.h
// =====================================================================
#pragma once

#include <Arduino.h>
#include <tflm_esp32.h>
#include <eloquent_tinyml.h>

#include "model_data_0.h"
#include "model_data_1.h"
#include "model_data_2.h"
#include "klimatologija.h"
#include "konstante.h"

// Model ima ~1200 parametara. 10000 je sa rezervom.
// Ako begin() javi gresku, povecavaj po 4000.
#define ARENA_SIZE 10000

static Eloquent::TF::Sequential<10, ARENA_SIZE> tf0;
static Eloquent::TF::Sequential<10, ARENA_SIZE> tf1;
static Eloquent::TF::Sequential<10, ARENA_SIZE> tf2;
static Eloquent::TF::Sequential<10, ARENA_SIZE>* MODELI[3] = {&tf0, &tf1, &tf2};

// ---------------------------------------------------------------------
// Kruzni baferi
// ---------------------------------------------------------------------
static float bufT[BAFER_STD], bufRH[BAFER_STD];
static int   buf_i = 0, buf_n = 0;

static float bufErr[BAFER_MEDIJAN];
static int   err_i = 0, err_n = 0;

// ---------------------------------------------------------------------
static bool detekcija_init() {
  const unsigned char* podaci[3] = {model0, model1, model2};
  for (int m = 0; m < BROJ_MODELA; m++) {
    MODELI[m]->setNumInputs(BROJ_FEATURES);
    MODELI[m]->setNumOutputs(BROJ_FEATURES);
    MODELI[m]->resolver.AddFullyConnected();
    MODELI[m]->resolver.AddRelu();

    int pokusaj = 0;
    while (!MODELI[m]->begin(podaci[m]).isOk()) {
      Serial.print("Model "); Serial.print(m); Serial.print(": ");
      Serial.println(MODELI[m]->exception.toString());
      if (++pokusaj > 3) return false;
      delay(1000);
    }
    Serial.print("Model "); Serial.print(m); Serial.println(" ucitan");
  }
  return true;
}

// ---------------------------------------------------------------------
// Greska rekonstrukcije iz gotovog vektora feature-a (bez bafera).
// Sluzi i za proveru prenosa: pusti isti vektor kroz Python i ovde.
// ---------------------------------------------------------------------
static float greska_iz_features(const float x[BROJ_FEATURES], int* dominantni) {

  float xs[BROJ_FEATURES];
  for (int i = 0; i < BROJ_FEATURES; i++)
    xs[i] = (x[i] - SCALER_CENTER[i]) / SCALER_SCALE[i];

  float E[BROJ_FEATURES] = {0}; 
  float suma = 0;
  int   brojac = 0;

  for (int m = 0; m < BROJ_MODELA; m++) {
    if (!MODELI[m]->predict((float*)xs).isOk()) {
      Serial.println(MODELI[m]->exception.toString());
      return -1.0f;
    }
    for (int i = 0; i < BROJ_FEATURES; i++) {
      float d = xs[i] - MODELI[m]->output(i);
      float e = d * d * W[m][i];
      E[i] += e;
      suma  += e;
      brojac++;
    }
  }

  if (dominantni) {
    int maxi = 0;
    for (int i = 1; i < BROJ_FEATURES; i++) if (E[i] > E[maxi]) maxi = i;
    *dominantni = maxi;
  }
  return suma / brojac;
}

// bafer za zalepljen seznor---------------------------------------------------------------------
static float std_bafera(const float* b, int n) {
  if (n < 2) return 0.0f;
  float s = 0;
  for (int i = 0; i < n; i++) s += b[i];
  float m = s / n, v = 0;
  for (int i = 0; i < n; i++) { float d = b[i] - m; v += d * d; }
  return sqrtf(v / (n - 1));
} 
// prozor od 73
static float medijan(float nova) {
  bufErr[err_i] = nova;
  err_i = (err_i + 1) % BAFER_MEDIJAN;
  if (err_n < BAFER_MEDIJAN) err_n++;

  static float t[BAFER_MEDIJAN];
  memcpy(t, bufErr, err_n * sizeof(float));
  for (int i = 1; i < err_n; i++) {
    float k = t[i]; int j = i - 1;
    while (j >= 0 && t[j] > k) { t[j + 1] = t[j]; j--; }
    t[j + 1] = k;
  }
  return (err_n % 2) ? t[err_n / 2] : 0.5f * (t[err_n / 2 - 1] + t[err_n / 2]);
}

// ---------------------------------------------------------------------
const char* opis_kvara(int f) {
  switch (f) {
    case 0: case 5: return "temperatura odstupa (grejac/ventilacija/drift)";
    case 1: case 6: return "vlaznost odstupa (ventilacija)";
    case 7:         return "temperatura se ne menja (zaglavljen senzor?)";
    case 8:         return "vlaznost se ne menja (zaglavljen senzor?)";
    default:        return "nepoznato odstupanje";
  }
}

// ---------------------------------------------------------------------
// Glavna funkcija. Pozvati kad stigne merenje.
//   dan 1..366, mesec 1..12, sat 0..23
//   vraca true ako je anomalija; *tip = indeks dominantnog feature-a
// ---------------------------------------------------------------------
static bool obradi(float T, float RH, int dan, int mesec, int sat,
                   int* tip, float* skor_out = nullptr) {

  // bafer za T_std / RH_std
  bufT[buf_i] = T;  bufRH[buf_i] = RH;
  buf_i = (buf_i + 1) % BAFER_STD;
  if (buf_n < BAFER_STD) buf_n++; //ceka se da se napuni bafer

  // klimatologija
  float T_ocek  = T_SEZ[dan - 1]  + T_CIK[mesec - 1][sat];
  float RH_ocek = RH_SEZ[dan - 1] + RH_CIK[mesec - 1][sat];

  // redosled MORA odgovarati listi features iz Pythona
  float x[BROJ_FEATURES];
  x[0] = T;
  x[1] = RH;
  x[2] = sinf(2.0f * PI * sat / 24.0f);
  x[3] = cosf(2.0f * PI * sat / 24.0f);
  x[4] = (float)dan;
  x[5] = T  - T_ocek;
  x[6] = RH - RH_ocek;
  x[7] = std_bafera(bufT,  buf_n);
  x[8] = std_bafera(bufRH, buf_n);

  float sirova = greska_iz_features(x, tip);
  if (sirova < 0) return false;

  float skor = medijan(sirova);
  if (skor_out) *skor_out = skor;

  Serial.print("T="); Serial.print(T, 1);
  Serial.print(" ocek="); Serial.print(T_ocek, 1);
  Serial.print(" T_dev="); Serial.print(x[5], 2);
  Serial.print(" | RH="); Serial.print(RH, 1);
  Serial.print(" RH_dev="); Serial.print(x[6], 2);
  Serial.print(" | skor="); Serial.print(skor, 3);
  Serial.print("/"); Serial.print(THRESHOLD, 2);

  if (buf_n < BAFER_STD) {
    Serial.println("  [zagrevanje]");
    return false;
  }

  if (skor > THRESHOLD) {
    Serial.print("  ANOMALIJA -> ");
    Serial.println(opis_kvara(*tip));
    return true;
  }
  Serial.println("  normalno");
  return false;
}

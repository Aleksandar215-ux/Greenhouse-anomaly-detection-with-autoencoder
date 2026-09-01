/*
   Prijemnik (kuca) - LoRa + detekcija anomalija
   Zasnovano na RadioLib Receive with Interrupts primeru
*/

#include <RadioLib.h>
#include "LoRaBoards.h"
#include <WiFi.h>
#include <time.h>
#include "detekcija.h"

// ---- WiFi samo da se jednom uzme tacno vreme, pa se gasi ----
#define WIFI_SSID     " "    //write Wi-Fi SSID
#define WIFI_LOZINKA  " "    //write Wi-Fi password

#if     defined(USING_SX1276)
#ifndef CONFIG_RADIO_FREQ
#define CONFIG_RADIO_FREQ           868.0
#endif
#ifndef CONFIG_RADIO_OUTPUT_POWER
#define CONFIG_RADIO_OUTPUT_POWER   17
#endif
#ifndef CONFIG_RADIO_BW
#define CONFIG_RADIO_BW             125.0
#endif
SX1276 radio = new Module(RADIO_CS_PIN, RADIO_DIO0_PIN, RADIO_RST_PIN, RADIO_DIO1_PIN);

#elif   defined(USING_SX1278)
#ifndef CONFIG_RADIO_FREQ
#define CONFIG_RADIO_FREQ           433.0
#endif
#ifndef CONFIG_RADIO_OUTPUT_POWER
#define CONFIG_RADIO_OUTPUT_POWER   17
#endif
#ifndef CONFIG_RADIO_BW
#define CONFIG_RADIO_BW             125.0
#endif
SX1278 radio = new Module(RADIO_CS_PIN, RADIO_DIO0_PIN, RADIO_RST_PIN, RADIO_DIO1_PIN);

#elif   defined(USING_SX1262)
#ifndef CONFIG_RADIO_FREQ
#define CONFIG_RADIO_FREQ           850.0
#endif
#ifndef CONFIG_RADIO_OUTPUT_POWER
#define CONFIG_RADIO_OUTPUT_POWER   22
#endif
#ifndef CONFIG_RADIO_BW
#define CONFIG_RADIO_BW             125.0
#endif
SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);

#elif   defined(USING_SX1280)
#ifndef CONFIG_RADIO_FREQ
#define CONFIG_RADIO_FREQ           2400.0
#endif
#ifndef CONFIG_RADIO_OUTPUT_POWER
#define CONFIG_RADIO_OUTPUT_POWER   13
#endif
#ifndef CONFIG_RADIO_BW
#define CONFIG_RADIO_BW             203.125
#endif
SX1280 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);

#elif  defined(USING_SX1280PA)
#ifndef CONFIG_RADIO_FREQ
#define CONFIG_RADIO_FREQ           2400.0
#endif
#ifndef CONFIG_RADIO_OUTPUT_POWER
#define CONFIG_RADIO_OUTPUT_POWER   3           // PA Version power range : -18 ~ 3dBm
#endif
#ifndef CONFIG_RADIO_BW
#define CONFIG_RADIO_BW             203.125
#endif
SX1280 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);

#elif   defined(USING_SX1268)
#ifndef CONFIG_RADIO_FREQ
#define CONFIG_RADIO_FREQ           433.0
#endif
#ifndef CONFIG_RADIO_OUTPUT_POWER
#define CONFIG_RADIO_OUTPUT_POWER   22
#endif
#ifndef CONFIG_RADIO_BW
#define CONFIG_RADIO_BW             125.0
#endif
SX1268 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);

#elif   defined(USING_LR1121)

#define CONFIG_RADIO_FREQ           2450.0
#define CONFIG_RADIO_OUTPUT_POWER   LILYGO_RADIO_2G4_TX_POWER_LIMIT
#define CONFIG_RADIO_BW             125.0

LR1121 radio = new Module(RADIO_CS_PIN, RADIO_DIO9_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);

#ifdef USING_LR1121PA
static const uint32_t pa_version_rf_switch_dio_pins[] = {
    RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6, RADIOLIB_LR11X0_DIO7, RADIOLIB_LR11X0_DIO8, RADIOLIB_NC
};

static const Module::RfSwitchMode_t high_freq_switch_table[] = {
    { LR11x0::MODE_STBY,   { LOW,  LOW, LOW, LOW} },
    { LR11x0::MODE_TX,     { LOW,  LOW, LOW, HIGH} },
    { LR11x0::MODE_RX,     { LOW,  LOW, HIGH, LOW} },
    { LR11x0::MODE_TX_HP,  { LOW,  LOW, HIGH, LOW} },
    { LR11x0::MODE_TX_HF,  { LOW,  LOW, HIGH, LOW} },
    { LR11x0::MODE_GNSS,   { LOW,  LOW, LOW, HIGH} },
    { LR11x0::MODE_WIFI,   { LOW,  LOW, LOW, HIGH} },
    END_OF_MODE_TABLE,
};

static const Module::RfSwitchMode_t low_freq_switch_table[] = {
    { LR11x0::MODE_STBY,   { LOW,  LOW, LOW, LOW} },
    { LR11x0::MODE_TX,     { LOW,  HIGH, LOW, LOW} },
    { LR11x0::MODE_RX,     { HIGH, LOW, LOW, LOW} },
    { LR11x0::MODE_TX_HP,  { LOW,  HIGH, LOW, LOW} },
    { LR11x0::MODE_TX_HF,  { LOW,  LOW, LOW, LOW} },
    { LR11x0::MODE_GNSS,   { LOW,  LOW, LOW, LOW} },
    { LR11x0::MODE_WIFI,   { LOW,  LOW, LOW, LOW} },
    END_OF_MODE_TABLE,
};
#endif  // USING_LR1121PA
#endif  // Radio define end

// ---------------------------------------------------------------------
static String rssi = "0dBm";
static String snr = "0dB";
static String payload = "0";
volatile bool operationDone = false;
bool transmitMode = false;

void drawMain();          // deklaracija, definicija je na dnu fajla

void setRxMode() {
    digitalWrite(RADIO_TX_PIN, LOW);
    digitalWrite(RADIO_RX_PIN, HIGH);
}

void setTxMode() {
    digitalWrite(RADIO_TX_PIN, HIGH);
    digitalWrite(RADIO_RX_PIN, LOW);
}

void setFlag(void) {
    operationDone = true;
}

// ---------------------------------------------------------------------
void setup()
{
    
    // 1. ploca prva - inicijalizuje Serial, I2C i ekran
    setupBoards();
    Serial.printf("Flash: %u MB\n", ESP.getFlashChipSize() / (1024 * 1024));
    Serial.printf("RAM (heap): %u kB\n", ESP.getHeapSize() / 1024);
    Serial.printf("PSRAM: %u kB\n", ESP.getPsramSize() / 1024);
    Serial.printf("Cip: %s, rev %d\n", ESP.getChipModel(), ESP.getChipRevision());
    delay(500);

    // 2. tacno vreme preko WiFi-ja, pa se WiFi gasi da ne smeta LoRi
    Serial.print("WiFi: ");
    WiFi.begin(WIFI_SSID, WIFI_LOZINKA);
    for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
        Serial.print(".");
        delay(500);
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        configTime(3600, 3600, "pool.ntp.org");     // CET + letnje racunanje
        struct tm t;
        if (getLocalTime(&t, 10000)) {
            Serial.printf("Vreme: %02d.%02d.%d %02d:%02d\n",
                          t.tm_mday, t.tm_mon + 1, t.tm_year + 1900,
                          t.tm_hour, t.tm_min);
        } else {
            Serial.println("NTP nije odgovorio - detekcija nece biti tacna!");
        }
    } else {
        Serial.println("Nema WiFi - detekcija nece biti tacna!");
    }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    // 3. modeli
    if (!detekcija_init()) {
        Serial.println("Modeli se nisu ucitali - povecaj ARENA_SIZE u detekcija.h");
    }
    
    // 4. radio
    delay(1500);

#ifdef  RADIO_TCXO_ENABLE
    pinMode(RADIO_TCXO_ENABLE, OUTPUT);
    digitalWrite(RADIO_TCXO_ENABLE, HIGH);
#endif

    int state = radio.begin();
    printResult(state == RADIOLIB_ERR_NONE);

    Serial.printf("[%s]:", RADIO_TYPE_STR);
    Serial.print(F("Radio Initializing ... "));
    if (state == RADIOLIB_ERR_NONE) {
        Serial.println(F("success!"));
    } else {
        Serial.print(F("failed, code "));
        Serial.println(state);
        while (true);
    }

    // prekidi - mora POSLE radio.begin()
    radio.setPacketReceivedAction(setFlag);
    radio.setPacketSentAction(setFlag);

    if (radio.setFrequency(CONFIG_RADIO_FREQ) == RADIOLIB_ERR_INVALID_FREQUENCY) {
        Serial.println(F("Selected frequency is invalid for this module!"));
        while (true);
    }

    if (radio.setBandwidth(CONFIG_RADIO_BW) == RADIOLIB_ERR_INVALID_BANDWIDTH) {
        Serial.println(F("Selected bandwidth is invalid for this module!"));
        while (true);
    }

    if (radio.setSpreadingFactor(12) == RADIOLIB_ERR_INVALID_SPREADING_FACTOR) {
        Serial.println(F("Selected spreading factor is invalid for this module!"));
        while (true);
    }

    if (radio.setCodingRate(6) == RADIOLIB_ERR_INVALID_CODING_RATE) {
        Serial.println(F("Selected coding rate is invalid for this module!"));
        while (true);
    }

    if (radio.setSyncWord(0xAB) != RADIOLIB_ERR_NONE) {
        Serial.println(F("Unable to set sync word!"));
        while (true);
    }

    if (radio.setOutputPower(CONFIG_RADIO_OUTPUT_POWER) == RADIOLIB_ERR_INVALID_OUTPUT_POWER) {
        Serial.println(F("Selected output power is invalid for this module!"));
        while (true);
    }

#if !defined(USING_SX1280) && !defined(USING_LR1121) && !defined(USING_SX1280PA)
    if (radio.setCurrentLimit(140) == RADIOLIB_ERR_INVALID_CURRENT_LIMIT) {
        Serial.println(F("Selected current limit is invalid for this module!"));
        while (true);
    }
#endif

    if (radio.setPreambleLength(16) == RADIOLIB_ERR_INVALID_PREAMBLE_LENGTH) {
        Serial.println(F("Selected preamble length is invalid for this module!"));
        while (true);
    }

    if (radio.setCRC(false) == RADIOLIB_ERR_INVALID_CRC_CONFIGURATION) {
        Serial.println(F("Selected CRC is invalid for this module!"));
        while (true);
    }

#if  defined(USING_LR1121)
#if defined(USING_LR1121PA)
    if (CONFIG_RADIO_FREQ < 2400) {
        Serial.printf("LR1121 PA Version Using low frequency switch table for PA version\n");
        radio.setRfSwitchTable(pa_version_rf_switch_dio_pins, low_freq_switch_table);
    } else {
        Serial.printf("LR1121 PA Version Using high frequency switch table for PA version\n");
        radio.setRfSwitchTable(pa_version_rf_switch_dio_pins, high_freq_switch_table);
    }
#else
    Serial.println("LR1121 without PA Version");
    static const uint32_t rfswitch_dio_pins[] = {
        RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6,
        RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC
    };
    static const Module::RfSwitchMode_t rfswitch_table[] = {
        { LR11x0::MODE_STBY,   { LOW,  LOW  } },
        { LR11x0::MODE_RX,     { HIGH, LOW  } },
        { LR11x0::MODE_TX,     { LOW,  HIGH } },
        { LR11x0::MODE_TX_HP,  { LOW,  HIGH } },
        { LR11x0::MODE_TX_HF,  { LOW,  LOW  } },
        { LR11x0::MODE_GNSS,   { LOW,  LOW  } },
        { LR11x0::MODE_WIFI,   { LOW,  LOW  } },
        END_OF_MODE_TABLE,
    };
    radio.setRfSwitchTable(rfswitch_dio_pins, rfswitch_table);
#endif  // USING_LR1121PA

    radio.setTCXO(3.0);
#endif  // USING_LR1121

#ifdef USING_DIO2_AS_RF_SWITCH
#ifdef USING_SX1262
    if (radio.setDio2AsRfSwitch() != RADIOLIB_ERR_NONE) {
        Serial.println(F("Failed to set DIO2 as RF switch!"));
        while (true);
    }
#endif
#endif

#ifdef RADIO_RX_PIN
    radio.setRfSwitchPins(RADIO_RX_PIN, RADIO_TX_PIN);
#endif

#ifdef RADIO_SWITCH_PIN
    const uint32_t pins[] = {
        RADIO_SWITCH_PIN, RADIO_SWITCH_PIN, RADIOLIB_NC,
    };
    static const Module::RfSwitchMode_t table[] = {
        {Module::MODE_IDLE,  {0, 0} },
        {Module::MODE_RX,    {1, 0} },
        {Module::MODE_TX,    {0, 1} },
        END_OF_MODE_TABLE,
    };
    radio.setRfSwitchTable(pins, table);
#endif

#ifdef RADIO_CTRL
    Serial.println("Turn on LAN, Enter Rx mode.");
    digitalWrite(RADIO_CTRL, HIGH);
#endif

    delay(1000);

    Serial.print(F("Radio Starting to listen ... "));
    state = radio.startReceive();
    if (state == RADIOLIB_ERR_NONE) {
        Serial.println(F("success!"));
    } else {
        Serial.print(F("failed, code "));
        Serial.println(state);
    }

    drawMain();
}

// ---------------------------------------------------------------------
void loop()
{
    if (!operationDone) return;
    operationDone = false;

    // --- zavrseno slanje ACK-a: nazad u prijem ---
    if (transmitMode) {
        Serial.println("ACK sent");
        setRxMode();
        transmitMode = false;
        radio.startReceive();
        return;
    }

    // --- stigao paket ---
    String str;
    int state = radio.readData(str);

    if (state != RADIOLIB_ERR_NONE) {
        radio.startReceive();
        return;
    }

    Serial.print("Received: ");
    Serial.println(str);

    rssi    = String(radio.getRSSI()) + "dBm";
    snr     = String(radio.getSNR()) + "dB";
    payload = str;
    drawMain();

    String ack = "OK";

    if (str == "ERR") {
        Serial.println("Senzor nije odgovorio - uzorak preskocen");
    } else {
        int zarez = str.indexOf(',');
        if (zarez > 0) {
            float T  = str.substring(0, zarez).toFloat();
            float RH = str.substring(zarez + 1).toFloat();

            struct tm t;
            if (!getLocalTime(&t)) {
                Serial.println("Nema tacnog vremena - preskacem");
            } else {
                int tip = 0;
                bool anomalija = obradi(T, RH,
                                        t.tm_yday + 1,
                                        t.tm_mon + 1,
                                        t.tm_hour,
                                        &tip);
                if (anomalija) ack = "A" + String(tip);
            }
        } else {
            Serial.println("Neispravan format paketa");
        }
    }

    delay(50);
    setTxMode();
    transmitMode = true;
    radio.startTransmit(ack);
}

// ---------------------------------------------------------------------
void drawMain()
{
    if (disp) {
        disp->clearBuffer();
        disp->drawRFrame(0, 0, 128, 64, 5);
        disp->setFont(u8g2_font_pxplusibmvga8_mr);
        disp->setCursor(15, 20);
        disp->print("RX:");
        disp->setCursor(15, 35);
        disp->print("SNR:");
        disp->setCursor(15, 50);
        disp->print("RSSI:");

        disp->setFont(u8g2_font_crox1h_tr);
        disp->setCursor( U8G2_HOR_ALIGN_RIGHT(payload.c_str()) - 21, 20 );
        disp->print(payload);
        disp->setCursor( U8G2_HOR_ALIGN_RIGHT(snr.c_str()) - 21, 35 );
        disp->print(snr);
        disp->setCursor( U8G2_HOR_ALIGN_RIGHT(rssi.c_str()) - 21, 50 );
        disp->print(rssi);
        disp->sendBuffer();
    }
}

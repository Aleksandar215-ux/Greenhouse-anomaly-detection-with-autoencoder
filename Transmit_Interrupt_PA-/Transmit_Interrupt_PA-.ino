// Predajnik (plastenik) - DHT/BME senzor -> LoRa
// Zasnovano na RadioLib Transmit with Interrupts primeru

#include "LoRaBoards.h"
#include <RadioLib.h>
#include "senzor.h"

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
#define CONFIG_RADIO_OUTPUT_POWER   3
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
void drawMain();

static int transmissionState = RADIOLIB_ERR_NONE;
static volatile bool radioFlag = false;      // postavlja se i posle slanja i posle prijema
static String payload = "-";
static String odgovor = "-";

static uint32_t poslednjeSlanje = 0;
static bool cekamOdgovor = false;

void setFlag(void)
{
    radioFlag = true;
}

// ---------------------------------------------------------------------
void setup()
{
    setupBoards();
    delay(500);

    senzor_init();

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

    // oba prekida - salje se merenje i prima se potvrda
    radio.setPacketSentAction(setFlag);
    radio.setPacketReceivedAction(setFlag);

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
        radio.setRfSwitchTable(pa_version_rf_switch_dio_pins, low_freq_switch_table);
    } else {
        radio.setRfSwitchTable(pa_version_rf_switch_dio_pins, high_freq_switch_table);
    }
#else
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
#endif
    radio.setTCXO(3.0);
#endif

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
    digitalWrite(RADIO_CTRL, LOW);
#endif

    Serial.println("Predajnik spreman");
    Serial.print("Interval slanja: ");
    Serial.print(INTERVAL_MS / 1000);
    Serial.println(" s");

    drawMain();
    poslednjeSlanje = 0;      // prvo slanje odmah
}

// ---------------------------------------------------------------------
void loop()
{
    // --- radio nesto zavrsio ---
    if (radioFlag) {
        radioFlag = false;

        if (cekamOdgovor) {
            // stigla potvrda od prijemnika
            String odg;
            if (radio.readData(odg) == RADIOLIB_ERR_NONE && odg.length() > 0) {
                odgovor = odg;
                Serial.print("Potvrda: ");
                Serial.println(odg);
                if (odg.startsWith("A")) {
                    Serial.println("  -> prijemnik javlja ANOMALIJU");
                }
                cekamOdgovor = false;
                drawMain();
            }
        } else {
            // zavrseno slanje merenja -> predji u prijem i cekaj potvrdu
            if (transmissionState == RADIOLIB_ERR_NONE) {
                Serial.println("Poslato, cekam potvrdu...");
            } else {
                Serial.print("Slanje nije uspelo, kod ");
                Serial.println(transmissionState);
            }
            radio.startReceive();
            cekamOdgovor = true;
        }
    }

    // --- potvrda nije stigla u roku ---
    if (cekamOdgovor && millis() - poslednjeSlanje > 15000UL) {
        Serial.println("Nema potvrde (timeout)");
        odgovor = "?";
        cekamOdgovor = false;
        drawMain();
    }

    // --- vreme za novo merenje --- //poslednje slanje==0 u su
    if (!cekamOdgovor &&
        (poslednjeSlanje == 0 || millis() - poslednjeSlanje >= INTERVAL_MS)) {

        payload = senzor_paket();

        Serial.print("Saljem: ");
        Serial.println(payload);

        flashLed();
        drawMain();

        transmissionState = radio.startTransmit(payload);
        poslednjeSlanje = millis();
    }

    delay(10);
}

// ---------------------------------------------------------------------
void drawMain()
{
    if (disp) {
        disp->clearBuffer();
        disp->drawRFrame(0, 0, 128, 64, 5);

        disp->setFont(u8g2_font_pxplusibmvga8_mr);
        disp->setCursor(15, 25);
        disp->print("TX:");
        disp->setCursor(15, 45);
        disp->print("ACK:");

        disp->setFont(u8g2_font_crox1h_tr);
        disp->setCursor( U8G2_HOR_ALIGN_RIGHT(payload.c_str()) - 15, 25 );
        disp->print(payload);
        disp->setCursor( U8G2_HOR_ALIGN_RIGHT(odgovor.c_str()) - 15, 45 );
        disp->print(odgovor);
        disp->sendBuffer();
    }
}
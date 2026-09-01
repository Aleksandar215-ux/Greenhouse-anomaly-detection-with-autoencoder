import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import os

from sklearn.preprocessing import RobustScaler
from sklearn.metrics import precision_score
from sklearn.metrics import recall_score
from sklearn.metrics import f1_score
from sklearn.metrics import confusion_matrix
from sklearn.metrics import classification_report

from tensorflow.keras.models import Sequential
from tensorflow.keras.layers import Input, Dense
from tensorflow.keras.callbacks import EarlyStopping
import tensorflow as tf
import random

np.random.seed(42)
tf.random.set_seed(42)
random.seed(42)
#====================================================
#1. Ucitavanje podataka
#====================================================
data = pd.read_excel(r"D:/mikroklima/Microclimate monitoring in commercial tomato (Solanum Lycopersicum L.) greenhouse production and its effect on plant growth, yield and fruit quality dataset/T&RH/2020/"
                     r"CENTRAL.xlsx")
print(data.head())

data['Time'] = pd.to_datetime(data['Time'])
# CELA SEZONA - nema filtriranja po datumu
#====================================================
#2. Ciscenje
#====================================================
def clean(df):
    df = df.replace([np.inf, -np.inf], np.nan)
    df = df.dropna(subset=['T', 'RH'])

    df = df[
        (df['T'] > -20) &
        (df['T'] < 60) &
        (df['RH'] >= 0) &
        (df['RH'] <= 100)
    ]
    return df.reset_index(drop=True)

data = clean(data)
print("Broj uzoraka:", len(data))
print("Period:", data['Time'].min(), "-", data['Time'].max())
#====================================================
#3. Features
#====================================================
features = [
    'T',
    'RH',
    'hour_sin',
    'hour_cos',
    'doy',
    'T_dev',
    'RH_dev',
    'T_std',
    'RH_std',
]
#====================================================
#3.1 Trening podaci
#====================================================
train_data = data.copy()

train_data['hour'] = train_data['Time'].dt.hour
train_data['month'] = train_data['Time'].dt.month
train_data['doy'] = train_data['Time'].dt.dayofyear

train_data['hour_sin'] = np.sin(2 * np.pi * train_data['hour'] / 24)
train_data['hour_cos'] = np.cos(2 * np.pi * train_data['hour'] / 24)

# KLIMATOLOGIJA - ocekivana temperatura i vlaznost za taj trenutak
# sezonski nivo = prosek kroz 7 dana (2016 uzoraka po 5 min)
# dnevni ciklus = prosecno odstupanje za taj mesec i taj sat
train_data['T_sez'] = train_data['T'].rolling(2016, center=True, min_periods=1).mean()
train_data['T_cik'] = (train_data['T'] - train_data['T_sez']).groupby([train_data['month'], train_data['hour']]).transform('mean')
train_data['T_dev'] = train_data['T'] - train_data['T_sez'] - train_data['T_cik']

train_data['RH_sez'] = train_data['RH'].rolling(2016, center=True, min_periods=1).mean()
train_data['RH_cik'] = (train_data['RH'] - train_data['RH_sez']).groupby([train_data['month'], train_data['hour']]).transform('mean')
train_data['RH_dev'] = train_data['RH'] - train_data['RH_sez'] - train_data['RH_cik']

# koliko se senzor ljulja u zadnja 3h (36 uzoraka) - hvata zalepljen senzor
train_data['T_std'] = train_data['T'].rolling(36, min_periods=1).std()
train_data['RH_std'] = train_data['RH'].rolling(36, min_periods=1).std()

train_data = train_data.bfill()
print("T_dev std na cistim podacima:", round(train_data['T_dev'].std(), 2))
#====================================================
#3.2 Test podaci
#====================================================
test_data = data.copy()
#====================================================
# VESTACKE ANOMALIJE
# .iloc a ne .loc - .loc je ukljuciv po oba kraja pa menja 501 red umesto 500
#====================================================
y_true = np.zeros(len(test_data), dtype=int)

kol_T = test_data.columns.get_loc('T')
kol_RH = test_data.columns.get_loc('RH')

# Grejac
for s in [5000, 30000, 60000]:
    test_data.iloc[s:s + 500, kol_T] -= 10
    y_true[s:s + 500] = 1

# Ventilator
for s in [12000, 40000, 70000]:
    test_data.iloc[s:s + 300, kol_T] += 10
    test_data.iloc[s:s + 300, kol_RH] -= 15
    y_true[s:s + 300] = 1

# Drift senzora
for s in [20000, 55000]:
    test_data.iloc[s:s + 500, kol_T] += np.linspace(0, 20, 500)
    y_true[s:s + 500] = 1

# Zalepljen senzor
for s in [8000, 35000, 75000]:
    test_data.iloc[s:s + 200, kol_RH] = 60
    y_true[s:s + 200] = 1

test_data['RH'] = test_data['RH'].clip(0, 100) #ako ode ispod ili preko nule sa ubacenim anomalijama
print("Broj anomalija:", y_true.sum())
#====================================================
# ISTI FEATURES ZA TEST
#====================================================
test_data['hour'] = test_data['Time'].dt.hour
test_data['month'] = test_data['Time'].dt.month
test_data['doy'] = test_data['Time'].dt.dayofyear

test_data['hour_sin'] = np.sin(2 * np.pi * test_data['hour'] / 24)
test_data['hour_cos'] = np.cos(2 * np.pi * test_data['hour'] / 24)

# KLIMATOLOGIJA SE UZIMA IZ CISTIH PODATAKA, NE RACUNA SE PONOVO NA TESTU
# ako se racuna na testu, kvar ulazi u svoj prosek i delimicno se sakrije sam od sebe, da ne pokvaris podatke
test_data['T_sez'] = train_data['T_sez']
test_data['T_cik'] = train_data['T_cik']
test_data['T_dev'] = test_data['T'] - test_data['T_sez'] - test_data['T_cik']

test_data['RH_sez'] = train_data['RH_sez']
test_data['RH_cik'] = train_data['RH_cik']
test_data['RH_dev'] = test_data['RH'] - test_data['RH_sez'] - test_data['RH_cik']

test_data['T_std'] = test_data['T'].rolling(36, min_periods=1).std()
test_data['RH_std'] = test_data['RH'].rolling(36, min_periods=1).std()

test_data = test_data.bfill()
#====================================================
#4. Normalizacija
#====================================================
scaler = RobustScaler()
X_train = scaler.fit_transform(train_data[features])
X_test = scaler.transform(test_data[features])

print("Train shape:", X_train.shape)
print("Test shape:", X_test.shape)
#====================================================
#5. Autoencoder model + treniranje
# 3 modela sa razlicitim seedom, greske se usrednjavaju
#====================================================
early_stopping = EarlyStopping(monitor='val_loss',
                               patience=10,
                               restore_best_weights=True)

train_mse = np.zeros(len(X_train))
test_mse = np.zeros(len(X_test))
istorije = []

modeli = []      # <-- cuva se za izvoz na ESP32
tezine = []      # <-- cuva se za izvoz na ESP32

BROJ_MODELA = 3
for seed in range(BROJ_MODELA):
    print("\n=== Model", seed + 1, "/", BROJ_MODELA, "===")
    tf.keras.utils.set_random_seed(seed)

    model = Sequential([
        Input(shape=(len(features),)),

        Dense(24, activation='relu'),

        Dense(12, activation='relu'),

        Dense(4, activation='relu'),

        Dense(12, activation='relu'),

        Dense(24, activation='relu'),

        Dense(len(features), activation='linear')
    ])

    model.compile(optimizer='adam', loss='mse')

    history = model.fit(X_train, X_train,
                        validation_split=0.2,
                        epochs=120,
                        batch_size=256,
                        callbacks=[early_stopping],
                        verbose=1)
    istorije.append(history)
    modeli.append(model)

    X_train_pred = model.predict(X_train, verbose=0)
    X_test_pred = model.predict(X_test, verbose=0)

    # greska po feature-u
    E_train = np.power(X_train - X_train_pred, 2)
    E_test = np.power(X_test - X_test_pred, 2)

    # svaki feature se deli svojom prosecnom greskom, da sumoviti ne zaguse ostale
    w = 1.0 / np.mean(E_train, axis=0)
    tezine.append(w)

    train_mse += np.mean(E_train * w, axis=1)
    test_mse += np.mean(E_test * w, axis=1)
    PROZOR = 73  # 6h pri koraku od 5 min
     # --- ocena pojedinačnog modela ---
    e1 = np.mean(E_train * w, axis=1)
    e2 = np.mean(E_test * w, axis=1)
    e1s = pd.Series(e1).rolling(PROZOR, center=True, min_periods=1).median().values
    e2s = pd.Series(e2).rolling(PROZOR, center=True, min_periods=1).median().values
    yp1 = (e2s > np.percentile(e1s, 99.9)).astype(int)
    print(f"  model {seed}:  P={precision_score(y_true, yp1):.3f}"
          f"  R={recall_score(y_true, yp1):.3f}"
          f"  F1={f1_score(y_true, yp1):.3f}")

train_mse /= BROJ_MODELA
test_mse /= BROJ_MODELA

print("\nGreska rekonstrukcije na trening setu:", np.mean(train_mse))
print("Greska rekonstrukcije na test setu:", np.mean(test_mse))
#====================================================
#6. Zagladjivanje i threshold
# kvarovi traju satima, lazne uzbune su pojedinacni skokovi
#====================================================


train_mse = pd.Series(train_mse).rolling(PROZOR,center=True,  min_periods=1).median().values
test_mse = pd.Series(test_mse).rolling(PROZOR,center=True,  min_periods=1).median().values

threshold = np.percentile(train_mse, 99.9)
print("Threshold:", threshold)
#====================================================
#7. Detekcija anomalija
#====================================================
anomalies_treninga = train_mse > threshold
print("Broj anomalija u trening setu:", np.sum(anomalies_treninga))

anomalies_testa = test_mse > threshold
print("Broj anomalija u test setu:", np.sum(anomalies_testa))

y_pred = anomalies_testa.astype(int)
#====================================================
#8. Precision, recall, F1
#====================================================
precision = precision_score(y_true, y_pred)
recall = recall_score(y_true, y_pred)
f1 = f1_score(y_true, y_pred)

print("Precision:", round(precision, 3))
print("Recall:", round(recall, 3))
print("F1-score:", round(f1, 3))

cm = confusion_matrix(y_true, y_pred)
print(cm)
print(classification_report(y_true, y_pred))
#====================================================
#8.1 Koliko je uhvaceno po tipu kvara
#====================================================
kvarovi = [('Grejac', 5000, 500), ('Grejac', 30000, 500), ('Grejac', 60000, 500),
           ('Ventilator', 12000, 300), ('Ventilator', 40000, 300), ('Ventilator', 70000, 300),
           ('Drift', 20000, 500), ('Drift', 55000, 500),
           ('Zalepljen', 8000, 200), ('Zalepljen', 35000, 200), ('Zalepljen', 75000, 200)]

detektovano = 0
for naziv, poc, duz in kvarovi:
    udeo = y_pred[poc:poc + duz].mean()
    detektovano += int(y_pred[poc:poc + duz].any()) 
    print(naziv, poc, ":", round(udeo, 2))
print("Detektovano kvarova:", detektovano, "/", len(kvarovi))
#=====================================================
# 9.GRAFIK RECONSTRUCTION ERROR - treninga
# =====================================================

plt.figure(figsize=(15,6))

plt.plot(train_mse, label='Reconstruction Error', linewidth=0.8)

plt.axhline(
    y=threshold,
    color='red',
    linestyle='--',
    label='Threshold'
)

plt.title("Threshold and Reconstruction Error")

plt.xlabel("Sample")

plt.ylabel("MSE")

plt.legend()

plt.grid()

plt.show()

# =====================================================
# 9.1 GRAFIK ANOMALIJA - testa
# =====================================================

plt.figure(figsize=(15,6))

plt.plot(test_mse, label='MSE', linewidth=0.8)

plt.scatter(
    np.where(anomalies_testa)[0],
    test_mse[anomalies_testa],
    color='red',
    s=6,
    label='Anomalies'
)

plt.axhline(
    y=threshold,
    color='black',
    linestyle='--'
)

plt.title("Detected Anomalies in test data")

plt.legend()
plt.xlabel("Sample")
plt.grid()
plt.title("Detected anomalies on the evaluation set")
plt.xlabel("Sample")
plt.ylabel("Reconstruction error")
plt.legend(["Reconstruction error", "Detected anomalies", "Threshold"])
plt.savefig("anomalies.png", dpi=200, bbox_inches='tight')

plt.show()
#=====================================================
# 10. GRAFIK Temperature i anomalije
#=====================================================
plt.figure(figsize=(18,6))

plt.plot(test_data['Time'], test_data['T'], label='Temperature', linewidth=0.7)

plt.scatter(
    test_data['Time'][anomalies_testa],
    test_data['T'][anomalies_testa],
    color='red',
    s=8,
    label='Anomaly'
)

plt.legend()
plt.grid()
plt.title("Detected anomalies on temperature")
plt.show()
#=====================================================
# 11. GRAFIK Vlaznosti i anomalije
#=====================================================
plt.figure(figsize=(18,6))

plt.plot(test_data['Time'], test_data['RH'], label='Vlaznost', linewidth=0.7)

plt.scatter(
    test_data['Time'][anomalies_testa],
    test_data['RH'][anomalies_testa],
    color='red',
    s=8,
    label='Anomaly'
)

plt.legend()
plt.grid()
plt.title("Detected anomalies on humidity")
plt.show()
#=====================================================
# 12. Istorija treniranja
#=====================================================
plt.figure(figsize=(10,5))

for i, h in enumerate(istorije):
    plt.plot(h.history['loss'], label='train ' + str(i + 1))
    plt.plot(h.history['val_loss'], '--', label='validation ' + str(i + 1))

plt.legend()
plt.grid()
plt.title("Training History")

plt.show()
#=====================================================
# 13. IZVOZ ZA ESP32
#    generise folder esp32_izvoz sa .h fajlovima
#    kopirati sve .h pored .ino skice
#=====================================================
IZLAZ = 'esp32_izvoz'
os.makedirs(IZLAZ, exist_ok=True)

# --- 13.1 modeli -> C nizovi (bez xxd, radi i na Windowsu) -----------
ukupno = 0
for i, m in enumerate(modeli):
    tfl = tf.lite.TFLiteConverter.from_keras_model(m).convert()
    ukupno += len(tfl)
    with open(os.path.join(IZLAZ, f'model_data_{i}.h'), 'w') as f:
        f.write(f'// TFLite model {i} - {len(tfl)} bajtova\n#pragma once\n\n')
        f.write(f'alignas(8) const unsigned char model{i}[] = {{\n')
        for j in range(0, len(tfl), 16):
            f.write('  ' + ', '.join(f'0x{b:02x}' for b in tfl[j:j + 16]) + ',\n')
        f.write('};\n')
        f.write(f'const unsigned int model{i}_len = {len(tfl)};\n')
    print(f'  model_data_{i}.h   {len(tfl)} B')

# --- 13.2 klimatologija ---------------------------------------------
sez = train_data.groupby('doy')[['T_sez', 'RH_sez']].mean()
sez = sez.reindex(range(1, 367)).interpolate().bfill().ffill()

cik = train_data.groupby(['month', 'hour'])[['T_cik', 'RH_cik']].first()
cik = cik.reindex(pd.MultiIndex.from_product([range(1, 13), range(24)]))
nedostaju = sorted({m for m, h in cik[cik['T_cik'].isna()].index})
cik = cik.fillna(0.0)
if nedostaju:
    print("UPOZORENJE: nema podataka za mesece", nedostaju,
          "-> ti redovi su nule, iskljuci detekciju u tim mesecima")

with open(os.path.join(IZLAZ, 'klimatologija.h'), 'w') as f:
    f.write('// ocekivano = SEZ[dan_u_godini-1] + CIK[mesec-1][sat]\n#pragma once\n\n')
    for kol, ime in [('T_sez', 'T_SEZ'), ('RH_sez', 'RH_SEZ')]:
        f.write(f'const float {ime}[366] = {{\n  ')
        f.write(',\n  '.join(','.join(f'{v:.2f}f' for v in sez[kol][i:i + 12])
                             for i in range(0, 366, 12)))
        f.write('\n};\n\n')
    for kol, ime in [('T_cik', 'T_CIK'), ('RH_cik', 'RH_CIK')]:
        f.write(f'const float {ime}[12][24] = {{\n')
        for m in range(1, 13):
            red = ','.join(f'{cik.loc[(m, h), kol]:+.2f}f' for h in range(24))
            f.write(f'  {{{red}}},  // mesec {m}\n')
        f.write('};\n\n')

# --- 13.3 konstante --------------------------------------------------
with open(os.path.join(IZLAZ, 'konstante.h'), 'w') as f:
    f.write('#pragma once\n\n')
    f.write(f'#define BROJ_FEATURES {len(features)}\n')
    f.write(f'#define BROJ_MODELA   {BROJ_MODELA}\n')
    f.write('#define BAFER_STD     36    // 3h pri koraku od 5 min\n')
    f.write(f'#define BAFER_MEDIJAN {PROZOR}\n\n')
    f.write('// redosled: ' + ', '.join(features) + '\n\n')
    f.write('const float SCALER_CENTER[BROJ_FEATURES] = {'              #medijan robust scalera
            + ','.join(f'{v:.6f}f' for v in scaler.center_) + '};\n\n')
    f.write('const float SCALER_SCALE[BROJ_FEATURES] = {'              #interkvartilni opseg robust scalera
            + ','.join(f'{v:.6f}f' for v in scaler.scale_) + '};\n\n')
    f.write('const float W[BROJ_MODELA][BROJ_FEATURES] = {\n')
    for wi in tezine:
        f.write('  {' + ','.join(f'{v:.6f}f' for v in wi) + '},\n')
    f.write('};\n\n')
    f.write(f'const float THRESHOLD = {threshold:.6f}f;\n')

print("\nGenerisano u folderu:", os.path.abspath(IZLAZ))
for fn in sorted(os.listdir(IZLAZ)):
    print(f'   {fn:22s} {os.path.getsize(os.path.join(IZLAZ, fn)):>8d} B')
print(f'\nUkupno flash na ESP32: ~{(ukupno + (366 * 2 + 288 * 2) * 4) / 1024:.1f} KB')

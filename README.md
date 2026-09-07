A system for automatic detection of unusual microclimate conditions in a greenhouse, built around an autoencoder that runs entirely on an ESP32-S3 microcontroller.

Master's thesis, Faculty of Technical Sciences, University of Novi Sad.

What it does

A sensor unit inside the greenhouse measures air temperature and relative humidity every five minutes and transmits them over LoRa to a receiver unit. The receiver computes the deviation from the expected microclimate state, runs it through three autoencoders, and compares the result against a threshold. The detection result is sent back to the sensor unit.

The model is trained only on data representing normal operation, so no labeled fault examples are required. This also means it flags faults that were never anticipated at design time.

Core idea

The same measured value means different things at different times. An air temperature of 18 °C is unremarkable in January, but in August it signals a heater failure.

So instead of the raw value, the model is fed the deviation from the expected state , built from a seasonal and a daily component. The same deviation then carries the same meaning throughout the season, which lets a single model cover the entire monitoring period.

Removing these features drops recall from 0.855 to 0.587, making them the single most important element of the solution.

Results

Evaluated on a public dataset with four types of synthetically injected faults.

Metric	Value
Precision	0.910
Recall	0.855
F1-score	0.882
Faults detected	11 of 11
False alarm rate	0.41%
By fault type
Fault type	Position 1	Position 2	Position 3
Heater failure	1.00	1.00	1.00
Ventilation failure	1.00	1.00	1.00
Drift sensor	0.77	0.66	–
Stuck sensor	0.12	0.41	1.00
Ablation study
Configuration	Precision	Recall	F1-score
Ensemble of three networks	0.910	0.855	0.882
Best single network	0.908	0.868	0.887
Worst single network	0.910	0.754	0.825
Without deviation features	0.891	0.587	0.708
Causal smoothing	0.808	0.759	0.783

The last row reflects on-device conditions, where smoothing cannot use future samples.

Model

Autoencoder with layer sizes 9-24-12-4-12-24-9 , 1,189 trainable parameters.

The input vector holds nine features: the two measured values, sine and cosine of the hour, day of year, deviations from the expected state, and rolling standard deviations over three hours.

Three networks of identical structure are trained with different random initializations, and their reconstruction errors are averaged.

Memory footprint
Component	Size
Three neural network models	23.4 kB
Expected-state lookup tables	5.1 kB
Scaler parameters, weights, threshold	under 0.2 kB
Total flash	28 kB
Working buffer	12 kB RAM

On a device with 4 MB of flash, that is a small fraction of what is available.

Repository layout
python/     model training and export for the microcontroller
esp32/      firmware for the transmitter and receiver
thesis/     the thesis document
Getting started
Training
bash
pip install pandas numpy scikit-learn tensorflow matplotlib openpyxl
python python/autoenkoder_final.py

The script loads the data, builds the expected state, trains three models, evaluates detection, and exports the microcontroller files into esp32_izvoz.

Data

The public dataset used here:

Šalagovič, J. et al.: Microclimate monitoring in commercial tomato greenhouse production dataset. Mendeley Data, V2 (2024). https://doi.org/10.17632/tkbkzdt5nr.2

Download it and place it in python/before running the script.

Flashing the device

Copy the contents of esp32_izvozinto the receiver sketch folder and build with the Arduino IDE.

Required libraries: RadioLib , EloquentTinyML , Adafruit BME280 .

Fill in your Wi-Fi credentials in the receiver sketch before building — the clock is set once at startup from an NTP server.

Hardware
	
Microcontroller	ESP32-S3 on a LilyGO T3-S3 board
Sensor	BME280
Radio	LoRa, 2.4 GHz
Limitations

Slow persistent drifts go undetected, since they are absorbed into the expected state, which is computed over a seven-day window.

The fault type cannot be identified reliably. An autoencoder trained only on healthy data has no notion of individual fault classes.

The expected-state tables describe one specific greenhouse and cover 7 January to 2 November. Deploying elsewhere requires retraining on data from that site.

The reported accuracy refers to synthetic faults. Evaluating on real faults would require a longer deployment with ground-truth logging.

Prior work

This system builds on the device described in:
LoRa-greenhouse-monitoring
Aleksandar Popović

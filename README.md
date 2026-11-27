# Pengukur Kapasitansi — ESP32 + LCD I2C

Kapasitor rusak di laci, nilainya sudah tidak terbaca lagi? Alat kecil ini
mengukur kapasitansi langsung, tanpa multimeter mahal: pasang kapasitor,
nilainya tampil di LCD 16x2 dalam pF, nF, atau uF — otomatis. Rentang ukur
100 pF sampai 1000 uF, cocok buat memilah kapasitor bekas, mengecek
kapasitor elektrolit yang dicurigai kering, atau sekadar tahu nilai
kapasitor tanpa label di laci komponenmu.

Video penjelasan (60 detik):
link menyusul setelah upload

## Komponen

| Bagian | Keterangan |
|---|---|
| ESP32 DevKit | Otak alat, mengukur waktu isi RC |
| LCD 16x2 I2C (0x27) | Tampil nilai kapasitansi |
| Resistor 1M | Isi kapasitor kecil (100 pF - 100 nF) |
| Resistor 10k | Isi kapasitor menengah (100 nF - 100 uF) |
| Resistor 1k | Isi kapasitor besar (100 uF - 1000 uF) |
| Kapasitor yang diuji | 100 pF - 1000 uF |
| Adaptor USB 5V 2A | Catu daya |

## Wiring

| Dari | Ke |
|---|---|
| Kapasitor diuji kaki A | Simpul ukur (ESP32 GPIO34, ADC) |
| Kapasitor diuji kaki B | GND |
| Resistor 1M | GPIO25 ke simpul ukur |
| Resistor 10k | GPIO26 ke simpul ukur |
| Resistor 1k | GPIO32 ke simpul ukur |
| Pin buang muatan | GPIO27 ke simpul ukur |
| LCD SDA | ESP32 GPIO21 |
| LCD SCL | ESP32 GPIO22 |
| LCD VCC | 5V |
| LCD GND | GND |
| Adaptor USB 5V | ESP32 VIN |
| Semua GND | Digabung |

Skema lengkap: [schematic.png](schematic.png)

## Cara kerja

Kapasitor diisi lewat resistor, dan ESP32 mencatat berapa lama tegangan
di simpul ukur naik sampai 63% dari 3,3 V — itulah satu konstanta waktu,
tau. Karena tau = R x C, maka C = t / R. Ambang 63% dibaca lewat ADC
12-bit: nilai ADC 2588 dari 4095.

```
tau (detik) = R (ohm) x C (farad)
C = waktu isi / R
ambang ADC  = 0,632 x 4095 = 2588
```

Auto-range: ESP32 mencoba resistor 1M dulu (untuk kapasitor kecil).
Bila tegangan belum mencapai ambang dalam 1,5 detik, pindah ke resistor
10k, lalu 1k — supaya kapasitor besar terisi cukup cepat. Sebelum tiap
ukur, kapasitor dikosongkan dulu lewat pin buang muatan (GPIO27).

Contoh angka: kapasitor 10 nF lewat 1M terisi dalam 10 ms; 100 nF lewat
1M dalam 100 ms; 1 uF lewat 10k dalam 10 ms; 100 uF lewat 1k dalam
100 ms; 1000 uF lewat 1k dalam 1 detik. Hasil tampil di LCD dan dikirim
juga ke Serial Monitor (115200 baud).

## Batas ukur

| Hal | Nilai |
|---|---|
| Rentang | 100 pF - 1000 uF |
| Ambang ukur | 63% tegangan (ADC 2588) |
| Batas tunggu | 1,5 detik per resistor |
| Tampil pF | Di bawah 1000 pF |
| Tampil nF | 1000 pF - 1000 nF |
| Tampil uF | Di atas 1000 nF |
| Pin baca | GPIO34 (ADC 12-bit) |

Perhatian: kapasitor elektrolit HARUS dipasang dengan polaritas benar
(kaki + ke simpul ukur, kaki - ke GND) dan HARUS dikosongkan dulu sebelum
dipasang. Jangan ukur kapasitor yang masih terpasang di rangkaian
bertegangan — lepas dulu dari rangkaian. Kapasitor di bawah 100 pF
terlalu cepat terisi untuk diukur andal dengan metode ini.

## Konfigurasi di sketch

| Konstanta | Nilai bawaan | Arti |
|---|---|---|
| `PIN_SENSE` | 34 | Baca tegangan simpul ukur (ADC) |
| `PIN_ISI_1M` | 25 | Isi lewat resistor 1M |
| `PIN_ISI_10K` | 26 | Isi lewat resistor 10k |
| `PIN_ISI_1K` | 32 | Isi lewat resistor 1k |
| `PIN_BUANG` | 27 | Buang muatan kapasitor ke GND |
| `PIN_SDA_LCD` | 21 | SDA bus I2C |
| `PIN_SCL_LCD` | 22 | SCL bus I2C |
| `ALAMAT_LCD` | 0x27 | Alamat I2C LCD 16x2 |
| `AMBANG_ADC` | 2588 | Nilai ADC pada 63% tegangan |
| `BATAS_TUNGGU_MS` | 1500 | Batas tunggu tiap resistor |
| `BAUD_SERIAL` | 115200 | Kecepatan Serial Monitor |

## Compile (terverifikasi)

- Board: ESP32 DevKit (esp32:esp32:esp32), ESP32 core 4.0.0, arduino-cli 1.6.0
- Sketch: 324.721 byte (24%) dari 1.310.720 byte flash
- RAM: 24.704 byte (7%) dari 327.680 byte
- Library: LiquidCrystal_I2C 1.1.2 (lihat folder `libraries/`)

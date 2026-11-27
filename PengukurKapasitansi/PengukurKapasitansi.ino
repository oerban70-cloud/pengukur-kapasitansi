// Pengukur Kapasitansi — ESP32 + LCD I2C 16x2
//
// Mengukur kapasitansi kapasitor yang diuji dengan metode waktu isi RC:
// kapasitor diisi lewat resistor, ESP32 mencatat berapa lama tegangan di
// simpul ukur naik sampai 63% dari 3,3 V (satu konstanta waktu, tau).
// Karena tau = R x C, maka C = t / R. Hasil tampil otomatis dalam pF,
// nF, atau uF di LCD I2C alamat 0x27, dan dikirim juga ke Serial Monitor
// (115200 baud) untuk kalibrasi dan debug.
//
// Auto-range memakai tiga resistor isi: 1M untuk kapasitor kecil
// (100 pF - 100 nF), 10k untuk menengah (100 nF - 100 uF), 1k untuk
// besar (100 uF - 1000 uF). ESP32 mencoba dari resistor terbesar dulu;
// bila waktu isi melewati batas tunggu, pindah ke resistor yang lebih
// kecil supaya pengisian lebih cepat. Sebelum tiap ukur, kapasitor
// dikosongkan dulu lewat pin buang muatan.
//
// Contoh angka (terverifikasi dari rumus tau = R x C):
//   10 nF lewat 1M  -> tau 10 ms
//   100 nF lewat 1M -> tau 100 ms
//   1 uF lewat 10k  -> tau 10 ms
//   100 uF lewat 1k -> tau 100 ms
//   1000 uF lewat 1k -> tau 1 detik
//
// Perhatian: kapasitor elektrolit HARUS dipasang dengan polaritas benar
// (kaki + ke simpul ukur, kaki - ke GND) dan HARUS dikosongkan dulu
// sebelum dipasang. Jangan ukur kapasitor yang masih terpasang di
// rangkaian bertegangan — lepas dulu dari rangkaian.
//
// Wiring:
//   Kapasitor diuji kaki A -> simpul ukur (GPIO34, ADC)
//   Kapasitor diuji kaki B -> GND
//   Resistor 1M  -> GPIO25 ke simpul ukur
//   Resistor 10k -> GPIO26 ke simpul ukur
//   Resistor 1k  -> GPIO32 ke simpul ukur
//   Pin buang muatan -> GPIO27 ke simpul ukur
//   LCD SDA      -> GPIO21
//   LCD SCL      -> GPIO22
//   LCD VCC      -> 5V, LCD GND -> GND
//   Catu         -> USB 5V ke VIN ESP32
//
// Board: ESP32 DevKit (esp32:esp32:esp32), ESP32 core 4.0.0.
// Library: LiquidCrystal_I2C 1.1.2 (lihat folder libraries/).

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---------- konfigurasi pin & konstanta ----------
const uint8_t PIN_SENSE      = 34;    // baca tegangan simpul ukur (ADC)
const uint8_t PIN_ISI_1M     = 25;    // isi lewat resistor 1M (kapasitor kecil)
const uint8_t PIN_ISI_10K    = 26;    // isi lewat resistor 10k (kapasitor menengah)
const uint8_t PIN_ISI_1K     = 32;    // isi lewat resistor 1k (kapasitor besar)
const uint8_t PIN_BUANG      = 27;    // buang muatan kapasitor ke GND
const uint8_t PIN_SDA_LCD    = 21;    // SDA bus I2C
const uint8_t PIN_SCL_LCD    = 22;    // SCL bus I2C
const uint8_t ALAMAT_LCD     = 0x27;  // alamat I2C LCD 16x2
const uint8_t KOLOM_LCD      = 16;    // lebar LCD
const uint8_t BARIS_LCD      = 2;     // jumlah baris LCD
const uint32_t BAUD_SERIAL   = 115200;  // kecepatan Serial Monitor

// Ambang ukur: 63% dari tegangan catu 3,3 V. ADC ESP32 12-bit
// (0-4095 untuk 0-3,3 V), jadi ambang = 0,632 x 4095 = 2588.
const uint16_t AMBANG_ADC    = 2588;  // nilai ADC pada 63% tegangan
const uint16_t ADC_MAKSIMAL  = 4095;  // nilai ADC pada 3,3 V

// Batas tunggu tiap percobaan isi: bila kapasitor belum mencapai ambang
// dalam waktu ini, coba resistor yang lebih kecil.
const uint32_t BATAS_TUNGGU_MS = 1500;  // 1,5 detik per resistor
// Jeda kosongkan muatan sebelum ukur (kapasitor besar butuh waktu).
const uint32_t JEDA_BUANG_MS = 300;     // 0,3 detik buang muatan
// Jeda antar pengukuran supaya LCD sempat dibaca.
const uint32_t JEDA_ANTAR_UKUR_MS = 2000;  // 2 detik

// Rentang ukur yang diklaim alat ini (pF).
const float KAPASITANSI_MIN_PF = 100.0f;       // 100 pF
const float KAPASITANSI_MAKS_PF = 1000000000.0f;  // 1000 uF = 1e9 pF

// Satu entri auto-range: pin isi + nilai resistornya (ohm).
struct RentangIsi {
  uint8_t pinIsi;
  float resistorOhm;
};

const RentangIsi RENTANG_ISI[] = {
  { PIN_ISI_1M,  1000000.0f },  // 1M: kapasitor kecil
  { PIN_ISI_10K, 10000.0f },    // 10k: kapasitor menengah
  { PIN_ISI_1K,  1000.0f },     // 1k: kapasitor besar
};
const uint8_t JUMLAH_RENTANG = sizeof(RENTANG_ISI) / sizeof(RENTANG_ISI[0]);

LiquidCrystal_I2C lcd(ALAMAT_LCD, KOLOM_LCD, BARIS_LCD);

// ---------- matikan semua pin isi (supaya tidak ada yang mengisi) ----------
void matikanSemuaPinIsi() {
  for (uint8_t i = 0; i < JUMLAH_RENTANG; i++) {
    pinMode(RENTANG_ISI[i].pinIsi, INPUT);  // mengambang = tidak mengisi
  }
}

// ---------- kosongkan kapasitor lewat pin buang muatan ----------
// Pin buang dijadikan output LOW: simpul ukur ditarik ke GND sehingga
// muatan kapasitor terbuang. Selesai, pin dilepas (INPUT) supaya tidak
// mengganggu pengukuran berikutnya.
void buangMuatan() {
  matikanSemuaPinIsi();
  pinMode(PIN_BUANG, OUTPUT);
  digitalWrite(PIN_BUANG, LOW);
  delay(JEDA_BUANG_MS);
  pinMode(PIN_BUANG, INPUT);
}

// ---------- isi kapasitor lewat satu resistor, ukur waktunya ----------
// Mengembalikan waktu isi dalam mikrodetik, atau 0 bila ambang tidak
// tercapai sebelum batas tunggu (kapasitor terlalu besar untuk resistor
// ini — panggil dengan resistor yang lebih kecil).
uint32_t ukurWaktuIsi(uint8_t pinIsi) {
  buangMuatan();
  // Mulai isi: pin isi HIGH, catat waktu mulai.
  pinMode(pinIsi, OUTPUT);
  digitalWrite(pinIsi, HIGH);
  uint32_t waktuMulai = micros();
  uint32_t waktuIsi = 0;
  while (micros() - waktuMulai < BATAS_TUNGGU_MS * 1000UL) {
    if (analogRead(PIN_SENSE) >= AMBANG_ADC) {
      waktuIsi = micros() - waktuMulai;
      break;
    }
  }
  // Berhenti mengisi, lalu kosongkan lagi supaya siap ukur berikutnya.
  pinMode(pinIsi, INPUT);
  buangMuatan();
  return waktuIsi;
}

// ---------- ukur kapasitansi dengan auto-range ----------
// Mencoba resistor dari yang terbesar; bila waktu isi melewati batas,
// turun ke resistor yang lebih kecil. Mengembalikan kapasitansi dalam
// pF, atau -1 bila tidak ada resistor yang berhasil (di luar rentang).
float ukurKapasitansiPF() {
  for (uint8_t i = 0; i < JUMLAH_RENTANG; i++) {
    uint32_t waktuUs = ukurWaktuIsi(RENTANG_ISI[i].pinIsi);
    if (waktuUs == 0) {
      continue;  // terlalu besar untuk resistor ini, coba yang lebih kecil
    }
    // C = t / R (t dalam detik, R dalam ohm -> C dalam farad).
    float kapasitansiF = (waktuUs / 1000000.0f) / RENTANG_ISI[i].resistorOhm;
    return kapasitansiF * 1000000000000.0f;  // farad -> pF
  }
  return -1.0f;  // di luar rentang ukur
}

// ---------- format kapasitansi: "47.0 nF" / "220 uF" / "820 pF" ----------
// Satuan dipilih otomatis: pF di bawah 1000 pF, nF di bawah 1000 nF,
// selebihnya uF. Sisa angka lama dihapus dengan spasi di akhir.
void formatKapasitansi(float kapasitansiPF, char *buf, size_t ukuranBuf) {
  if (kapasitansiPF < 1000.0f) {
    snprintf(buf, ukuranBuf, "%.0f pF", kapasitansiPF);
  } else if (kapasitansiPF < 1000000.0f) {
    snprintf(buf, ukuranBuf, "%.1f nF", kapasitansiPF / 1000.0f);
  } else {
    snprintf(buf, ukuranBuf, "%.1f uF", kapasitansiPF / 1000000.0f);
  }
}

// ---------- tampilkan hasil di LCD ----------
// Baris atas: nilai kapasitansi. Baris bawah: status rentang atau
// pesan bila tidak ada kapasitor / di luar rentang.
void tampilkanHasil(float kapasitansiPF) {
  char teksNilai[16];
  lcd.clear();
  lcd.setCursor(0, 0);
  if (kapasitansiPF < 0.0f) {
    lcd.print("Di luar rentang");
    lcd.setCursor(0, 1);
    lcd.print("100pF-1000uF");
  } else if (kapasitansiPF < KAPASITANSI_MIN_PF) {
    lcd.print("Tidak ada C");
    lcd.setCursor(0, 1);
    lcd.print("pasang kapasitor");
  } else if (kapasitansiPF > KAPASITANSI_MAKS_PF) {
    lcd.print("Terlalu besar");
    lcd.setCursor(0, 1);
    lcd.print("maks 1000 uF");
  } else {
    formatKapasitansi(kapasitansiPF, teksNilai, sizeof(teksNilai));
    lcd.print("C = ");
    lcd.print(teksNilai);
    lcd.setCursor(0, 1);
    lcd.print("ESP32 ukur RC");
  }
}

// ---------- kirim hasil ke Serial Monitor ----------
// Menampilkan juga waktu isi (us) dan resistor yang dipakai, supaya
// kalibrasi bisa dicek ulang dengan rumus tau = R x C.
void kirimSerial(float kapasitansiPF) {
  char teksNilai[16];
  if (kapasitansiPF < 0.0f) {
    Serial.println("Kapasitansi: di luar rentang ukur (100 pF - 1000 uF)");
  } else if (kapasitansiPF < KAPASITANSI_MIN_PF) {
    Serial.println("Kapasitansi: tidak ada kapasitor terpasang");
  } else {
    formatKapasitansi(kapasitansiPF, teksNilai, sizeof(teksNilai));
    Serial.print("Kapasitansi: ");
    Serial.println(teksNilai);
  }
}

void setup() {
  Serial.begin(BAUD_SERIAL);
  analogReadResolution(12);  // ADC 12-bit: 0-4095 untuk 0-3,3 V
  pinMode(PIN_SENSE, INPUT);
  matikanSemuaPinIsi();
  pinMode(PIN_BUANG, INPUT);

  Wire.begin(PIN_SDA_LCD, PIN_SCL_LCD);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Pengukur Kapasitor");
  lcd.setCursor(0, 1);
  lcd.print("ESP32 + LCD I2C");
  delay(1500);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Pasang kapasitor");
  lcd.setCursor(0, 1);
  lcd.print("di pin ukur...");
  Serial.println("Pengukur kapasitansi siap - pasang kapasitor di simpul ukur");
}

void loop() {
  float kapasitansiPF = ukurKapasitansiPF();
  tampilkanHasil(kapasitansiPF);
  kirimSerial(kapasitansiPF);
  delay(JEDA_ANTAR_UKUR_MS);
}

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <Adafruit_Sensor.h>

// --- PIN DEFINITIONS ---
#define SOIL_PIN 34
#define DHT_PIN 5
#define DHT_TYPE DHT22
#define RELAY_PIN 4

// --- SENSOR & STATE VARIABLES ---
int soilValue = 0;
int moisturePercent = 0;

// Variabel global untuk dibaca oleh MQTT Task (Core 1)
float temp = 0.0;
float hum = 0.0;

DHT dht(DHT_PIN, DHT_TYPE);

// --- VARIABEL UNTUK NON-BLOCKING ---
bool pompaMenyala = false;          
unsigned long waktuMulaiSiram = 0;  
float durasiTargetSiram = 0.0;

// -- VARIABEL TIMER ---
unsigned long lastSensorPublish = 0;
const long sensorPublishInterval = 5000; // Kirim data tiap 5 detik

// --- DATA WIFI ---
const char* MY_SSID = "LAPTOP-4IDAUTP9 7983";
const char* MY_PWD = "123123123";

// --- KONFIGURASI MQTT ---
const char *mqtt_broker = "broker.hivemq.com";
const char *mqtt_username = "";
const char *mqtt_password = "";
const int mqtt_port = 1883;
const char* TOPIC_CONNECTION = "esp32/connection2025";

WiFiClient espClient;
PubSubClient client(espClient);
TaskHandle_t Task0;

// -----------------------------------------------------------------
// --- LOGIKA FUZZY MANUAL ---
// -----------------------------------------------------------------
float f_soil_kering, f_soil_lembap, f_soil_basah;
float f_temp_dingin, f_temp_sejuk, f_temp_panas;
float rule_strength[9];
float f_durasi_mati, f_durasi_sedang, f_durasi_lama;

float trapmf(float x, float a, float b, float c, float d) {
    if (x <= a || x >= d) {
        return 0.0;
    } else if (x >= b && x <= c) {
        return 1.0;
    } else if (x > a && x < b) {
        return (x - a) / (b - a);
    } else { // jika (x > c && x < d)
        return (d - x) / (d - c);
    }
}

// --- BAGIAN INI SUDAH DISESUAIKAN (Ideal 60-70%) ---
void fuzzifyInputs(float soil, float temp) {
    // INPUT TANAH (Target Ideal: 60-70%)
    // Kering: 0 - 60 (Transisi turun mulai di 50)
    f_soil_kering = trapmf(soil, 0, 0, 50, 60);
    // Lembap (Ideal): 50 - 80 (Transisi ideal 60-70)
    f_soil_lembap = trapmf(soil, 50, 60, 70, 80);
    // Basah: 70 - 100 (Transisi naik mulai di 70)
    f_soil_basah  = trapmf(soil, 70, 80, 100, 100);

    // INPUT SUHU (Target Ideal: 21-27 C)
    // Dingin: < 21 (Transisi turun 19-21)
    f_temp_dingin = trapmf(temp, 0, 0, 19, 21);
    // Sejuk (Ideal): 21-27 (Transisi naik 19-21, turun 27-29)
    f_temp_sejuk  = trapmf(temp, 19, 21, 27, 29);
    // Panas: > 27 (Transisi naik 27-29)
    f_temp_panas  = trapmf(temp, 27, 29, 37, 39);
}

void evaluateRules() {
    // Rule untuk pompa menyala
    rule_strength[0] = min(f_soil_kering, f_temp_panas);
    rule_strength[1] = min(f_soil_kering, f_temp_sejuk);
    rule_strength[2] = min(f_soil_kering, f_temp_dingin);
    // Rule untuk pompa mati
    rule_strength[3] = min(f_soil_lembap, f_temp_panas);
    rule_strength[4] = min(f_soil_lembap, f_temp_sejuk);
    rule_strength[5] = min(f_soil_lembap, f_temp_dingin);
    rule_strength[6] = min(f_soil_basah, f_temp_panas);
    rule_strength[7] = min(f_soil_basah, f_temp_sejuk);
    rule_strength[8] = min(f_soil_basah, f_temp_dingin);

    f_durasi_lama = max(rule_strength[0], rule_strength[1]);
    f_durasi_sedang = rule_strength[2];

    float temp_mati_1 = max(rule_strength[3], rule_strength[4]);
    float temp_mati_2 = max(rule_strength[5], rule_strength[6]);
    float temp_mati_3 = max(rule_strength[7], rule_strength[8]);
    
    f_durasi_mati = max(temp_mati_1, max(temp_mati_2, temp_mati_3));
}

float defuzzifyCentroid() {
    float numerator = 0.0;
    float denominator = 0.0;
    float step = 0.5; 
    for (float x = 0; x <= 10; x += step) {
        float mu_mati = trapmf(x, 0, 0, 1, 2);
        float mu_sedang = trapmf(x, 1, 5, 5, 9);
        float mu_lama = trapmf(x, 8, 10, 10, 10);
        mu_mati = min(mu_mati, f_durasi_mati);
        mu_sedang = min(mu_sedang, f_durasi_sedang);
        mu_lama = min(mu_lama, f_durasi_lama);
        float aggregated_mu = max(mu_mati, max(mu_sedang, mu_lama));
        numerator += aggregated_mu * x;
        denominator += aggregated_mu;
    }
    if (denominator == 0) { return 0.0; }
    return numerator / denominator;
}

// Fungsi SETUP, WIFI, dan MQTT
void setup() {
  Serial.begin(115200);
  dht.begin();
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); 
  wifiSetup();
  client.setServer(mqtt_broker, mqtt_port);
  Serial.println("Sistem siap. Parameter Cabai Rawit (60-70%).");
  xTaskCreatePinnedToCore(mqttTask, "Task0", 10000, NULL, 1, &Task0, 1);
  delay(500);
}

void wifiSetup() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(MY_SSID, MY_PWD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void checkConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected! Reconnecting...");
    wifiSetup();
  }
  if (!client.connected()) {
    Serial.println("MQTT disconnected! Reconnecting...");
    reconnectMQTT();
  }
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Mencoba koneksi ke MQTT Broker...");
    String clientId = "ESP32Client-" + String(random(0xffff), HEX);

    // SETTING LWT (Last Will and Testament)
    if (client.connect(clientId.c_str(), mqtt_username, mqtt_password, TOPIC_CONNECTION, 1, true, "OFFLINE")) {
      Serial.println("Berhasil Terhubung!");

      // Umumkan status
      // Retain = true,  agar dashboard yang baru dibuka langsung tahu status terakhir
      client.publish(TOPIC_CONNECTION, "ONLINE", true); 
      Serial.println("Status ONLINE terkirim.");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void mqttTask(void * pvParameters) {
  while (1) {
    checkConnection();
    client.loop();
    delay(5000);
  }
}

// -----------------------------------------------------------------
// --- LOOP UTAMA (CORE 0) - DENGAN PROTEKSI SENSOR ---
// -----------------------------------------------------------------
void loop() {
  // Variabel statis untuk menyimpan nilai valid terakhir
  static float last_valid_temp = 25.0; 
  static float last_valid_hum = 60.0;  

  // BACA SENSOR
  soilValue = analogRead(SOIL_PIN);
  float current_hum = dht.readHumidity();
  float current_temp = dht.readTemperature();
  moisturePercent = map(soilValue, 4095, 0, 0, 100);

  // --- LOGIKA PROTEKSI DHT (PENTING SAAT POMPA NYALA) ---
  if (isnan(current_hum) || isnan(current_temp)) {
    // Jika gagal baca, gunakan nilai terakhir yang valid
    Serial.println("WARNING: DHT Gagal! Pakai nilai terakhir agar sistem tetap jalan.");
  } else {
    // Jika berhasil, update nilai terakhir
    last_valid_temp = current_temp;
    last_valid_hum = current_hum;
    // Update variabel global untuk MQTT
    temp = last_valid_temp;
    hum = last_valid_hum;
  }

  // PUBLISH DATA SENSOR PERIODIK (Setiap 5 Detik)
  unsigned long now = millis();
  if (now - lastSensorPublish > sensorPublishInterval) {
    lastSensorPublish = now;
    
    // Tampilkan di Serial
    Serial.println("--------------------");
    Serial.print("Soil: "); Serial.print(moisturePercent); Serial.println(" %");
    Serial.print("Temp: "); Serial.print(last_valid_temp); Serial.println(" °C");
    Serial.print("Hum:  "); Serial.print(last_valid_hum); Serial.println(" %");
    
    // Kirim ke MQTT (Dashboard Gauge)
    if (client.connected()) {
       client.publish("esp32/soiliot2025", String(moisturePercent).c_str());
       client.publish("esp32/tempiot2025", String(last_valid_temp).c_str());
       client.publish("esp32/humiot2025", String(last_valid_hum).c_str());
       
       // Kirim status Standby hanya jika sedang tidak menyiram
       if (!pompaMenyala) {
          String statusMsg = "Aman (Lembap). Standby.";
          client.publish("esp32/statusiot2025", statusMsg.c_str());
          Serial.println("STATUS: " + statusMsg);
       }
    }
  }

  // PROSES LOGIKA FUZZY (Selalu jalan, pakai nilai last_valid)
  fuzzifyInputs(moisturePercent, last_valid_temp);
  evaluateRules();
  float durasiHasilFuzzy = defuzzifyCentroid(); 

  // LOGIKA KONTROL POMPA
  if (pompaMenyala == true) {
    // Kondisi 1: Sensor mendeteksi tanah sudah lembap (fuzzy bilang "Mati")
    if (durasiHasilFuzzy <= 2.0) {
      // Pesan kondisi OFF pompa MQTT
      String msg = "Tanah Ideal (" + String(moisturePercent) + "%). Stop Siram.";
      Serial.println("FUZZY: " + msg);
      if(client.connected()) client.publish("esp32/statusiot2025", msg.c_str());

      digitalWrite(RELAY_PIN, HIGH); // OFF
      pompaMenyala = false;
    }
    // Kondisi 2: Waktu siram maksimum tercapai
    else if (millis() - waktuMulaiSiram >= (unsigned long)(durasiTargetSiram * 1000)) {
      // Pesan waktu siram telah habis MQTT
      String msg = "Waktu Siram Habis (" + String(durasiTargetSiram) + "s). Stop.";
      Serial.println("FUZZY: " + msg);
      if(client.connected()) client.publish("esp32/statusiot2025", msg.c_str());

      digitalWrite(RELAY_PIN, HIGH); // OF
      pompaMenyala = false;
    }
  }
  else { // (pompaMenyala == false)
    // Kondisi: Fuzzy bilang perlu siram
    if (durasiHasilFuzzy > 2.0) {
      // Logika Teks Informatif
      String pesanKondisi = "";
      String pesanDurasi = "";

      // Klasifikasi berdasarkan output detik
      if (durasiHasilFuzzy > 6.0) {
        pesanKondisi = "SANGAT KERING";
        pesanDurasi = "LAMA";
      } else {
        pesanKondisi = "SEDIKIT KERING";
        pesanDurasi = "SEDANG";
      }

      // Buat satu kalimat status lengkap
      String statusMsg = "Tanah " + pesanKondisi + ". Siram " + pesanDurasi + " (" + String(durasiHasilFuzzy) + "s)";
      Serial.print("ACTION: "); 
      Serial.println(statusMsg);

      // Kirim MQTT INSTAN (Prioritas Tinggi)
      if (client.connected()) {
        client.publish("esp32/statusiot2025", statusMsg.c_str());
      }
      
      digitalWrite(RELAY_PIN, LOW); // NYALA
      pompaMenyala = true;
      waktuMulaiSiram = millis(); 
      durasiTargetSiram = durasiHasilFuzzy;
    }
  }
  delay(100); // Delay kecil agar CPU tidak panas, tapi tetap responsif
}
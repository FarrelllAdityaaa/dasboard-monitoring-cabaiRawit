const express = require('express');
const app = express();
const http = require('http').createServer(app);
const io = require('socket.io')(http);
const mqtt = require('mqtt');
const path = require('path');

// Setup Web Server
app.use(express.static(path.join(__dirname, 'public'))); // Folder untuk file HTML

// Setup MQTT (Koneksi ke HiveMQ)
const mqttClient = mqtt.connect('mqtt://broker.hivemq.com:1883');

const TOPIC_SOIL = 'esp32/soiliot2025';
const TOPIC_TEMP = 'esp32/tempiot2025';
const TOPIC_HUM  = 'esp32/humiot2025';
const TOPIC_STAT = 'esp32/statusiot2025';
const TOPIC_CONN = 'esp32/connection2025';

mqttClient.on('connect', () => {
    console.log('Terhubung ke HiveMQ Broker!');
    // Subscribe ke semua topik
    mqttClient.subscribe([TOPIC_SOIL, TOPIC_TEMP, TOPIC_HUM, TOPIC_STAT, TOPIC_CONN]);
});

// Saat terima pesan MQTT -> Teruskan ke Browser via Socket.io
mqttClient.on('message', (topic, message) => {
    const msgString = message.toString();
    // console.log(`Terima: ${topic} -> ${msgString}`);

    // Kirim ke frontend
    if (topic === TOPIC_SOIL) io.emit('soilData', msgString);
    if (topic === TOPIC_TEMP) io.emit('tempData', msgString);
    if (topic === TOPIC_HUM)  io.emit('humData', msgString);
    if (topic === TOPIC_STAT) io.emit('statusData', msgString);

    if (topic === TOPIC_CONN) {
        console.log(`STATUS KONEKSI ESP32 BERUBAH: ${msgString}`);
        io.emit('connectionData', msgString); // Kirim event baru 'connectionData'
    }
});

// Jalankan Server
http.listen(3001, () => {
    console.log('Dashboard berjalan di http://localhost:3001');
});
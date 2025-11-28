const socket = io();

// Elemen HTML
const soilEl = document.getElementById("soilVal");
const tempEl = document.getElementById("tempVal");
const humEl = document.getElementById("humVal");
const statusEl = document.getElementById("statusText");
const connDotEl = document.getElementById("connDot");
const connTextEl = document.getElementById("connText");

socket.on("connectionData", (status) => {
  if (status === "ONLINE") {
    // Jika Online: Dot Hijau Kedip, Teks Hijau
    connDotEl.className = "status-dot dot-online";
    connTextEl.innerText = "ESP32 Aktif & Terkoneksi";
    connTextEl.classList.remove("text-gray-500", "text-red-500");
    connTextEl.classList.add("text-green-600");
  } else {
    // Jika Offline: Dot Merah Diam, Teks Merah
    connDotEl.className = "status-dot dot-offline";
    connTextEl.innerText = "ESP32 OFFLINE / Terputus!";
    connTextEl.classList.remove("text-gray-500", "text-green-600");
    connTextEl.classList.add("text-red-500");
  }
});

socket.on("soilData", (data) => {
  soilEl.innerText = data;
  // Ubah string ke angka agar bisa dibandingkan
  const val = parseFloat(data);
  soilEl.classList.remove(
    "text-slate-800",
    "text-red-600",
    "text-amber-500",
    "text-green-600"
  );

  // Logika Warna Tanah
  if (val < 50) {
    // Sangat Kering -> MERAH
    soilEl.classList.add("text-red-600");
  } else if (val >= 50 && val < 60) {
    // Sedikit Kering -> KUNING KECOKLATAN (Amber)
    soilEl.classList.add("text-amber-500");
  } else {
    // Lembap / Aman -> HIJAU
    soilEl.classList.add("text-green-600");
  }
});

socket.on("tempData", (data) => {
  tempEl.innerText = data;
  const val = parseFloat(data);

  tempEl.classList.remove(
    "text-slate-800",
    "text-red-600",
    "text-green-600",
    "text-blue-600"
  );

  // Logika Warna Suhu (Target Ideal: 21-27)
  if (val > 27) {
    // Panas -> MERAH
    tempEl.classList.add("text-red-600");
  } else if (val >= 21 && val <= 27) {
    // Ideal -> HIJAU
    tempEl.classList.add("text-green-600");
  } else {
    // Dingin (< 21) -> BIRU
    tempEl.classList.add("text-blue-600");
  }
});

socket.on("humData", (data) => {
  humEl.innerText = data;
});

socket.on("statusData", (data) => {
  statusEl.innerText = data;

  // Logika Ganti Warna Background Status
  // Class Tailwind untuk warna background
  if (
    data.includes("ON") ||
    data.includes("Siram") ||
    data.includes("KERING") ||
    data.includes("SANGAT")
  ) {
    // Mode Menyiram / Sangat Kering -> MERAH
    statusEl.className =
      "text-xl md:text-2xl font-bold text-white bg-red-500 p-4 rounded-lg text-center transition-colors duration-500 shadow-lg shadow-red-200";
  } else if (data.includes("SEDIKIT")) {
    // Mode Sedikit Kering -> KUNING/AMBER
    statusEl.className =
      "text-xl md:text-2xl font-bold text-white bg-amber-500 p-4 rounded-lg text-center transition-colors duration-500 shadow-lg shadow-amber-200";
  } else {
    // Mode Standby -> HIJAU
    statusEl.className =
      "text-xl md:text-2xl font-bold text-white bg-green-600 p-4 rounded-lg text-center transition-colors duration-500 shadow-lg shadow-green-200";
  }
});

#include <Arduino.h>
#include "knob.h"

// ==========================================
// KONFIGURASI MODEL FISIKA (Berdasarkan Paper)
// ==========================================

// 1. Konversi Unit
// Paper menggunakan milimeter (mm), Knob menggunakan derajat (deg).
// Kita asumsikan 1 putaran (360 deg) = 20 mm kedalaman jarum.
const float DEG_TO_MM = 20.0f / 360.0f; 

// 2. Parameter Stiffness (Fase Pre-Puncture) 
// Rumus: f(x) = a1*x + a2*x^2
// Nilai disesuaikan untuk skala motor kecil (bukan sapi asli yang butuh >2N)
const float K_STIFFNESS_A1 = 0.010f; // Koefisien Linear
const float K_STIFFNESS_A2 = 0.002f; // Koefisien Non-linear (rasa "keras" di akhir)

// 3. Parameter Puncture (Tusukan) 
// Batas gaya (dalam skala PWM 0.0 - 1.0) di mana kulit "jebol"
const float PUNCTURE_THRESHOLD = 0.65f; 

// 4. Parameter Insertion (Fase Post-Puncture) 
// f_total = f_friction + f_cutting
const float F_CUTTING       = 0.15f;  // Gaya konstan untuk membelah daging [cite: 247]
const float B_VISCOUS       = 0.002f; // Gesekan fluida (tergantung kecepatan) [cite: 164]
const float F_STATIC_FRICTION = 0.05f; // Gesekan awal (stiction)

// ==========================================
// STATE MACHINE
// ==========================================
enum NeedleState {
  STATE_AIR,       // Di udara
  STATE_CAPSULE,   // Menekan kulit (stiffness)
  STATE_INSERTED   // Di dalam jaringan (friction + cutting)
};

NeedleState currentState = STATE_AIR;

// Titik referensi sudut di mana "kulit" dimulai (menggunakan unwrapped angle untuk multi-turn)
float surfaceAngleStartUnwrapped = 0.0f; 
bool isCalibrated = false;

void setup() {
  Serial.begin(115200);
  delay(500);

  // Init library knob (akan melakukan homing ke 180 derajat)
  knob_init(); 
  
  Serial.println("=== Simulation Start ===");
  Serial.println("Putar knob ke posisi awal, lalu diamkan 2 detik untuk kalibrasi 'Permukaan Kulit'.");
  
  // Kalibrasi sederhana: Posisi setelah homing dianggap "0 mm" (permukaan kulit)
  // Kita beri waktu user memegang knob
  delay(2000); 
  float dump1, currentAngleUnwrapped, dump2;
  knob_get_status(&dump1, &currentAngleUnwrapped, &dump2);
  surfaceAngleStartUnwrapped = currentAngleUnwrapped;
  isCalibrated = true;
  
  Serial.print("Surface Calibrated at (unwrapped): ");
  Serial.println(surfaceAngleStartUnwrapped);
}

void loop() {
  if (!isCalibrated) return;

  // --- 1. BACA SENSOR ---
  float angleWrapped, angleUnwrapped, velocity;
  // Menggunakan fungsi baru yang kita tambahkan di knob.cpp
  knob_get_status(&angleWrapped, &angleUnwrapped, &velocity);

  // Hitung kedalaman penetrasi (x) dalam mm
  // Positif = masuk ke dalam kulit (jika memutar ke kanan dari titik start)
  // Menggunakan unwrapped angle untuk menghindari reset setiap 360 derajat
  float depth_deg = angleUnwrapped - surfaceAngleStartUnwrapped;

  float depth_mm = depth_deg * DEG_TO_MM;
  float force_cmd = 0.0f;

  // --- 2. HITUNG GAYA BERDASARKAN STATE (MODEL PAPER) ---
  
  switch (currentState) {
    
    // --- KASUS A: DI LUAR KULIT ---
    case STATE_AIR:
      if (depth_mm > 0) {
        // Mulai menyentuh kulit
        currentState = STATE_CAPSULE;
      }
      force_cmd = 0.0f; // Tidak ada gaya
      break;

    // --- KASUS B: MENEKAN KAPSUL (PRE-PUNCTURE) ---
    case STATE_CAPSULE:
      if (depth_mm <= 0) {
        currentState = STATE_AIR; // Kembali ke udara
        force_cmd = 0.0f;
      } else {
        // Model Non-Linear Spring: F = a1*x + a2*x^2 
        float f_stiffness = (K_STIFFNESS_A1 * depth_mm) + (K_STIFFNESS_A2 * depth_mm * depth_mm);
        force_cmd = f_stiffness;

        // Cek Puncture Event (Jebol)
        if (force_cmd > PUNCTURE_THRESHOLD) {
          Serial.println(">>> PUNCTURE EVENT! (Pop) <<<");
          currentState = STATE_INSERTED;
        }
      }
      break;

    // --- KASUS C: DI DALAM JARINGAN (INSERTION) ---
    case STATE_INSERTED:
      if (depth_mm <= 0) {
        currentState = STATE_AIR; // Ditarik keluar sepenuhnya
        force_cmd = 0.0f;
      } else {
        // Model: F = F_cutting + F_friction 
        
        // 1. Cutting Force (Konstan, hanya saat bergerak masuk/positif)
        // Paper bilang cutting force konstan [cite: 246]
        float f_cut = (velocity > 0) ? F_CUTTING : 0.0f; 

        // 2. Friction (Viscous + Static) 
        // Viscous: sebanding dengan kecepatan
        float f_friction = velocity * B_VISCOUS;
        
        // Static (Coulomb): Tambahan gaya gesek dasar
        if (fabs(velocity) > 0.1f) {
           f_friction += (velocity > 0) ? F_STATIC_FRICTION : -F_STATIC_FRICTION;
        }

        force_cmd = f_cut + f_friction;
        
        // Pastikan gaya saat di dalam tidak melebihi gaya puncture (agar terasa "jatuh")
        // Paper menyebutkan "sudden drop in force" setelah puncture 
        if (force_cmd > PUNCTURE_THRESHOLD * 0.8f) {
           force_cmd = PUNCTURE_THRESHOLD * 0.8f;
        }
      }
      break;
  }

  // --- 3. KIRIM TORSI KE MOTOR ---
  
  // PENTING: Gaya Reaksi (Haptic) harus berlawanan dengan arah gerakan jarum.
  // Jika kita menekan (positif depth), motor harus mendorong balik (negatif torque).
  // Namun, rumus di atas menghitung BESAR gaya resistansi.
  
  float motor_output = 0.0f;

  if (currentState == STATE_CAPSULE) {
    // Pegas selalu mendorong balik ke permukaan (negatif)
    motor_output = -1.0f * force_cmd; 
  } 
  else if (currentState == STATE_INSERTED) {
    // Gesekan selalu berlawanan arah kecepatan
    // Rumus f_friction di atas sudah mengikuti tanda kecepatan, 
    // jadi kita perlu me-negasikannya untuk menjadi gaya perlawanan.
    // Cutting force juga melawan arah masuk.
    motor_output = -1.0f * force_cmd;
  }

  // Kirim perintah raw torque
  knob_set_torque(motor_output);

  // --- 4. DEBUGGING ---
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 50) {
    // Format Serial Plotter: Depth, Force, State*Scale
    Serial.print("Depth_mm:"); Serial.print(depth_mm);
    Serial.print(" Force_cmd:"); Serial.print(abs(motor_output)); // Plot magnitude
    Serial.print(" State:"); Serial.println(currentState * 0.5); // Skala state agar terlihat di grafik
    lastPrint = millis();
  }
  
  // Loop delay agar konsisten dengan loop knob.cpp
  delay(2); 
}
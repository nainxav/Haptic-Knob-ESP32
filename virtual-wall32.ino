// ============================================================
// Needle Insertion Haptic Simulation – ESP32 + Haptic Knob
//
// Force model based on:
//   Okamura, Simone & O'Leary (2004)
//     "Force modeling for needle insertion into soft tissue"
//     IEEE Trans. Biomedical Eng. 51(10), pp. 1707–1716
//
//   Contact model: Mahvash & Dupont (2010)
//     Nonlinear spring + Maxwell viscoelastic branch
//     f_tip = a2·δ² + a1·δ + K(δ)·δ_k
//
// Total axial force:  f = f_stiffness + f_cutting + f_friction
// Phases: Pre-rupture → Rupture (pop) → Cutting → Relaxation
// Multi-layer: Skin → Fat → Muscle, each with own parameters
// ============================================================

#include <Arduino.h>
#include "knob.h"

// ==================== UNIT CONVERSION ====================
// 1 full knob rotation (360 deg) maps to 20 mm needle depth
const float DEG_TO_MM = 20.0f / 360.0f;

// ==================== TISSUE LAYER MODEL ====================
struct TissueLayer {
  float depthStartMm;    // cumulative depth where this layer begins
  float thicknessMm;     // layer thickness (mm)

  // Mahvash-Dupont contact model (pre-rupture stiffness)
  //   f_tip = a2·δ² + a1·δ + K(δ)·δ_k
  float a1;              // linear stiffness coefficient
  float a2;              // quadratic stiffness coefficient
  float Kprime;          // Maxwell spring factor: K(δ) = K'·δ
  float tau;             // viscoelastic time constant (s): τ = D'/K'

  // Fracture thresholds
  float Fr;              // rupture force (triggers puncture "pop")
  float Fc;              // cutting force (constant, post-rupture)

  // Runtime state (reset on extraction)
  bool  punctured;
  float deltaK;          // Maxwell damper internal displacement
  float integDeltaK;     // integral of δ_k for Euler update
};

#define NUM_LAYERS 3

TissueLayer layers[NUM_LAYERS] = {
  //                start  thick   a1      a2      K'      τ       Fr     Fc
  /* Skin   */  {   0.0f,  2.0f,  0.08f,  0.12f,  0.20f,  0.04f,  0.55f, 0.08f,  false, 0, 0 },
  /* Fat    */  {   2.0f,  8.0f,  0.012f, 0.002f, 0.03f,  0.10f,  0.22f, 0.06f,  false, 0, 0 },
  /* Muscle */  {  10.0f, 25.0f,  0.018f, 0.004f, 0.06f,  0.06f,  0.35f, 0.12f,  false, 0, 0 },
};

// ==================== SHAFT FRICTION (Coulomb + Viscous) ====================
// Friction increases with insertion depth (more shaft-tissue contact area)
const float MU_SHAFT     = 0.003f;    // Coulomb friction per mm of depth
const float B_VISCOUS    = 0.0015f;   // viscous friction coefficient
const float STICTION_F   = 0.025f;    // static friction (stiction) threshold

// ==================== GLOBAL STATE ====================
float surfaceAngleStartUnwrapped = 0.0f;
bool  isCalibrated = false;

// ==================== CONTACT MODEL ====================
// Mahvash-Dupont discrete update:
//   δ_k(n+1) = [ δ(n+1) − (1/τ)·∫δ_k ] / (1 + Ts/τ)
//   f_tip    = a2·δ² + a1·δ + K'·δ·δ_k
float computeContactForce(TissueLayer& L, float delta, float dt) {
  if (delta <= 0.0f) {
    L.deltaK = 0.0f;
    L.integDeltaK = 0.0f;
    return 0.0f;
  }

  float ratio = dt / L.tau;
  L.deltaK = (delta - L.integDeltaK / L.tau) / (1.0f + ratio);
  L.integDeltaK += L.deltaK * dt;

  float f = L.a2 * delta * delta
          + L.a1 * delta
          + L.Kprime * delta * L.deltaK;

  return (f > 0.0f) ? f : 0.0f;
}

// ==================== FIND ACTIVE LAYER ====================
int findLayerIndex(float depthMm) {
  for (int i = NUM_LAYERS - 1; i >= 0; i--) {
    if (depthMm >= layers[i].depthStartMm) return i;
  }
  return -1;
}

// ==================== RESET ALL LAYERS ====================
void resetLayers() {
  for (int i = 0; i < NUM_LAYERS; i++) {
    layers[i].punctured    = false;
    layers[i].deltaK       = 0.0f;
    layers[i].integDeltaK  = 0.0f;
  }
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(500);

  knob_init();

  Serial.println("=== Needle Insertion Simulation ===");
  Serial.println("Model: Okamura (2004) + Mahvash-Dupont contact");
  Serial.println("Layers: Skin / Fat / Muscle");

  delay(2000);

  float dump1, unwrapped, dump2;
  knob_get_status(&dump1, &unwrapped, &dump2);
  surfaceAngleStartUnwrapped = unwrapped;
  isCalibrated = true;
  resetLayers();

  Serial.print("Surface calibrated at: ");
  Serial.println(surfaceAngleStartUnwrapped, 1);
}

// ==================== MAIN LOOP ====================
void loop() {
  if (!isCalibrated) return;

  // ---- Read sensor ----
  float angleWrapped, angleUnwrapped, velocity;
  knob_get_status(&angleWrapped, &angleUnwrapped, &velocity);

  float depth_deg = angleUnwrapped - surfaceAngleStartUnwrapped;
  float depth_mm  = depth_deg * DEG_TO_MM;
  float vel_mm    = velocity  * DEG_TO_MM;     // mm/s
  const float dt  = 0.005f;                     // ~200 Hz

  float f_tip     = 0.0f;
  float f_friction = 0.0f;
  int   stateId   = 0;   // 0 = AIR, 1 = DEFORM (pre-rupture), 2 = CUT
  int   layerIdx  = -1;

  if (depth_mm <= 0.0f) {
    // ======== AIR ========
    stateId = 0;
    resetLayers();

  } else {
    // ======== INSIDE TISSUE ========
    layerIdx = findLayerIndex(depth_mm);
    if (layerIdx < 0) layerIdx = 0;
    TissueLayer& L = layers[layerIdx];

    float localDepth = depth_mm - L.depthStartMm;

    if (L.punctured) {
      // ---- CUTTING PHASE (Phase III) ----
      // Constant tip force: Fc = Rf · wc (fracture toughness × crack width)
      stateId = 2;
      f_tip = L.Fc;

      // Viscoelastic relaxation: when velocity ≈ 0, cutting force decays
      if (fabsf(vel_mm) < 0.3f) {
        float relaxFactor = 0.92f;   // per-sample decay
        f_tip *= relaxFactor;
      }

    } else {
      // ---- DEFORMATION PHASE (Phase I) ----
      // Nonlinear viscoelastic contact model
      stateId = 1;
      f_tip = computeContactForce(L, localDepth, dt);

      // ---- RUPTURE CHECK (Phase II) ----
      // Force drop: Fr → Fc ("pop-through" effect)
      if (f_tip >= L.Fr) {
        L.punctured = true;
        f_tip = L.Fc;
        stateId = 2;
        L.deltaK      = 0.0f;
        L.integDeltaK = 0.0f;
      }
    }

    // ---- SHAFT FRICTION (along entire inserted length) ----
    // Coulomb friction proportional to contact area (≈ depth)
    // Viscous friction proportional to velocity
    float f_coulomb = MU_SHAFT * depth_mm;
    float f_viscous = B_VISCOUS * vel_mm;

    if (fabsf(vel_mm) > 0.5f) {
      // Dynamic friction: Coulomb + viscous, direction opposes velocity
      f_friction = f_coulomb + fabsf(f_viscous);
      if (vel_mm < 0.0f) f_friction = -f_friction;  // extraction direction
    } else {
      // Stiction zone: small resistive force
      f_friction = (f_coulomb < STICTION_F) ? 0.0f : f_coulomb * 0.3f;
    }
  }

  // ---- TOTAL HAPTIC OUTPUT ----
  // Reaction force opposes user motion (negative of computed force)
  float motor_output = -(f_tip + f_friction);

  if (motor_output >  1.0f) motor_output =  1.0f;
  if (motor_output < -1.0f) motor_output = -1.0f;

  knob_set_torque(motor_output);

  // ---- SERIAL OUTPUT (for visualization) ----
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 50) {
    lastPrint = millis();
    // Format: Depth_mm:X Force_cmd:Y State:Z Layer:W FTip:A FFric:B
    Serial.print("Depth_mm:");  Serial.print(depth_mm, 2);
    Serial.print(" Force_cmd:"); Serial.print(fabsf(motor_output), 4);
    Serial.print(" State:");     Serial.print(stateId * 0.5f, 1);
    Serial.print(" Layer:");     Serial.print(layerIdx);
    Serial.print(" FTip:");      Serial.print(f_tip, 4);
    Serial.print(" FFric:");     Serial.println(f_friction, 4);
  }

  delay(2);
}

# 2-DOF Pantograph Haptic Device

Two-degree-of-freedom haptic device using a 5-bar pantograph mechanism driven by two haptic knobs (ESP32 + AS5600 + DC motor each). The browser-based visualization handles all force computation and sends torque commands back to the motors.

## Architecture

```
┌─────────────┐  USB Serial   ┌─────────────────────────────────┐  USB Serial  ┌─────────────┐
│  ESP32 Left │ ◄───────────► │  Browser (pantograph3d.html)    │ ◄──────────► │ ESP32 Right │
│  encoder →  │  A:<angle>,v  │                                 │  A:<angle>,v │  ← encoder  │
│  ← motor    │  T:<torque>   │  FK → Forces → J^T → Torques   │  T:<torque>  │    motor →  │
└─────────────┘               └─────────────────────────────────┘              └─────────────┘
```

**PC is the brain**: Both ESP32 boards run identical firmware. They only read the encoder and drive the motor. All kinematics, force models, and torque computation happen in the browser.

## Folder Structure

```
pantograph/
├── esp-knob/          # Flash this to BOTH ESP32 boards
│   ├── esp-knob.ino   # Main Arduino sketch
│   ├── knob.h         # Knob library header
│   └── knob.cpp       # Knob library (AS5600 + motor driver)
├── viz/
│   └── pantograph3d.html  # 3D visualization (open in Chrome/Edge)
└── README.md
```

## Serial Protocol

**ESP32 → PC** (200 Hz):
```
A:<unwrapped_angle_deg>,<velocity_deg_s>\n
```

**PC → ESP32** (~100 Hz):
```
T:<torque_cmd>\n        // torque in range [-1.0, 1.0]
```

## Hardware Setup

Each knob unit needs:
| Component | Pin |
|-----------|-----|
| Motor PWM | IO18 |
| Motor AIN1 | IO19 |
| Motor AIN2 | IO23 |
| AS5600 SDA | IO21 |
| AS5600 SCL | IO22 |

Flash `esp-knob/esp-knob.ino` to **both** ESP32 boards using Arduino IDE or PlatformIO.

## Pantograph Geometry (configurable in HTML)

| Parameter | Default | Description |
|-----------|---------|-------------|
| `d` | 38 mm | Half motor spacing |
| `L1`, `L2` | 50 mm | Proximal link lengths |
| `L3`, `L4` | 70 mm | Distal link lengths |
| `leftRestRad` | 60° | Left motor rest angle |
| `rightRestRad` | 120° | Right motor rest angle |
| `torqueScale` | 0.005 | Force-to-motor-command gain |

## Force Model

The visualization implements the same multi-layer needle insertion model as the 1-DOF version:

- **Okamura (2004)**: Force = F_tip + F_friction
- **Mahvash-Dupont (2010)**: Viscoelastic contact model for pre-rupture
- **3 tissue layers**: Skin (2mm), Fat (8mm), Muscle (25mm)
- **Lateral friction**: Proportional to insertion depth
- **Workspace walls**: Soft spring boundaries

Forces are mapped to motor torques using the **Jacobian transpose** method:
```
τ = J^T · F
```

## Usage

1. Flash firmware to both ESP32s
2. Open `viz/pantograph3d.html` in Chrome or Edge
3. Click **Connect Left ESP32** → select the left knob's COM port
4. Click **Connect Right ESP32** → select the right knob's COM port
5. The visualization will display the pantograph mechanism, tissue, and force feedback

**Without hardware**: Use arrow keys to simulate (↑↓ = depth, ←→ = lateral). Press R to reset.

## Calibration

When each ESP32 connects, the visualization records the current angle as the zero offset. Make sure the pantograph is in its rest position before connecting. Adjust `leftScale` / `rightScale` (negate if rotation direction is wrong) and `leftRestRad` / `rightRestRad` in the HTML config section.

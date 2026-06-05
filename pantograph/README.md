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

## Dummy, Hardware, and Hybrid Modes

The visualization supports three operating modes per side (Left / Right):

| Mode | Badge | Input | Telemetry |
|------|-------|-------|-----------|
| **Dummy** | `L/R: DUMMY` | Arrow keys | Simulated `A:angle,vel` @ 20 Hz |
| **Hardware** | `L/R: HARDWARE` | Physical knob | Real serial from ESP32 |
| **Hybrid** | One dummy, one hardware | Mixed | Dummy side logs `A:...`, hardware side logs real data |

### Without any hardware (full dummy)

1. Open `viz/pantograph3d.html` in Chrome or Edge
2. Both sides show `DUMMY` badge
3. Use `↑↓←→` to move the pantograph end-effector
4. Serial monitor shows simulated `A:angle,velocity` for both sides
5. Force model and 3D rendering run normally

### With hardware (full hardware)

1. Flash `esp-knob/esp-knob.ino` to both ESP32 boards
2. Click **Connect Left ESP32** → select left knob COM port
3. Click **Connect Right ESP32** → select right knob COM port
4. Badges change to `HARDWARE`; keyboard input disabled for connected sides
5. Browser computes torques and sends `T:torque` back to both motors

### Hybrid (one real, one simulated)

Useful for development when only one ESP32 is available:

1. Connect one side only (e.g. Left)
2. Left reads real encoder; Right stays in dummy mode
3. Arrow keys control only the unconnected (dummy) side
4. Torque commands sent only to the connected motor

Click the connect button again to **disconnect** and return that side to dummy mode.

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

## Parallel Computing Implementation

The pantograph system is a **distributed parallel computing** architecture with three concurrent execution domains:

### Domain 1: ESP32 Workers (×2, parallel)

Each ESP32 runs an independent ~200 Hz control loop:

```
loop() {
  read encoder (AS5600)     ─┐
  send A:angle,velocity       │ ~5 ms period
  receive T:torque            │ runs on both
  drive motor (TB6612FNG)    ─┘ boards in parallel
}
```

The two boards operate **fully in parallel** — no inter-ESP32 communication. They are symmetric workers connected only through the PC coordinator.

### Domain 2: Browser Async I/O (parallel serial reads)

`pantograph3d.html` runs two independent async serial read loops:

```javascript
// Left and right run concurrently via JavaScript event loop
readSerialLoop(leftPort,  'left');   // async task 1
readSerialLoop(rightPort, 'right');  // async task 2
```

Each loop:
- Decodes incoming bytes asynchronously (non-blocking)
- Parses `A:angle,velocity` lines
- Updates shared `state.theta1/theta2` without blocking the render loop

Torque writes are also parallelized — `sendTorques()` fires `T:torque` to both ports independently with per-port write locks to prevent serial contention.

### Domain 3: Browser Compute + Render (main thread)

The animation loop (`requestAnimationFrame`) runs at ~60 FPS:

```
simulateInput(dt)     // keyboard for dummy sides only
updateFK()            // forward kinematics (5-bar linkage)
computeForces(dt)     // Okamura + Mahvash-Dupont tissue model
sendTorques()         // J^T mapping → motor commands (~100 Hz)
render (Three.js)     // 3D scene update
```

This is the **compute-intensive master** node that fuses data from both workers, solves the pantograph FK/Jacobian, and distributes torque commands back.

### Timing diagram

```
Time ──────────────────────────────────────────────────────►

ESP32 L:  |read|send|read|send|read|send|read|send|  (~200 Hz)
ESP32 R:  |read|send|read|send|read|send|read|send|  (~200 Hz)
Browser:  |────FK+Force+Render────|────FK+Force+Render────|  (~60 Hz)
          |─T─|─T─|─T─|─T─|─T─|─T─|─T─|─T─|─T─|─T─|        (~100 Hz torque)
```

### Why parallel matters here

- **Latency**: Serial I/O (~115200 baud) must not block the 16 ms render budget
- **Throughput**: Two encoders at 200 Hz = 400 angle samples/sec to process
- **Scalability**: Pattern extends to N knobs (see `visualization.py` thread-per-device for 4-knob desktop viz)
- **Fault isolation**: One ESP32 disconnecting doesn't crash the other side (hybrid dummy mode)

## Calibration

When each ESP32 connects, the visualization records the current angle as the zero offset. Make sure the pantograph is in its rest position before connecting. Adjust `leftScale` / `rightScale` (negate if rotation direction is wrong) and `leftRestRad` / `rightRestRad` in the HTML config section.

## Usage Quick Start

1. Flash firmware to both ESP32s (or skip for dummy-only testing)
2. Open `viz/pantograph3d.html` in Chrome or Edge
3. Move with arrow keys in dummy mode, or connect ESP32s for hardware
4. Press **R** to reset pantograph to rest position

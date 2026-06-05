# Haptic Knob Needle Insertion Simulation (ESP32)

**Video Demo:**
[Watch the System Demonstration Video via Instagram Reels!](https://www.instagram.com/reel/DZNVUA0p1P7/?utm_source=ig_web_copy_link&igsh=MzRlODBiNWFlZA==)

**Code documentation:**
[Check the documentation folder here!](https://kizzuato.github.io/NiceKnob-Documentation/)

## System Interface Snippet

The web browser based three dimensional visualization displays the tissue layers, the needle position, the Head Up Display (HUD) projection, and the force graph in real time.

| First Screenshot | Second Screenshot |
|:------------:|:------------:|
| ![Visualization 1](images/Visualization1.png) | ![Visualization 2](images/Visualization2.png) |

## Project Overview

This project presents a physics based haptic feedback simulation system that specifically mimics the sensation of needle insertion penetrating through various layers of biological tissue. The developers designed this system based on previously published biomechanics research. The force model we implement is capable of reproducing six distinct insertion phases. These phases encompass the penetration through three main tissue layers, where each layer possesses unique and specific mechanical characteristics.

| Phase | Detailed Description |
|-------|-------------|
| **Air** | The system does not provide any force resistance at all when the needle is located above the tissue surface. |
| **Deformation (Prerupture)** | The user will experience nonlinear viscoelastic resistance as the tissue undergoes shape changes or deformation resulting from the needle tip pressure. |
| **Rupture** | The system generates an instantaneous force drop exactly at the moment the needle tip successfully penetrates or tears the tissue layer. |
| **Cutting (Postrupture)** | The system maintains a constant force at the needle tip as the needle moves to cut and penetrate deeper into the tissue. |
| **Relaxation** | The haptic force will decay slowly when the user stops the needle movement inside the tissue. |
| **Extraction** | The system provides a friction force in the opposite direction to resist the outward withdrawal movement of the needle. |

## Parallel Computing Architecture (ESP32 Multicore System)

This project relies heavily on the parallel computing paradigm to ensure the physics simulation runs deterministically and precisely without encountering bottlenecks from the communication interface processes. The ESP32 microcontroller features a Dual Core processor architecture. The developers utilize the FreeRTOS real time operating system to explicitly divide the computational workload into two distinct processor cores.

This parallel computing approach resolves the bottleneck issues that frequently occur in single core microcontrollers. If the system calculates physics and sends serial data sequentially, latency will occur and ruin the haptic sensation. By employing parallel computing, the process of reading sensors and computing force feedback runs independently from the process of sending communication logs to the web browser.

```mermaid
graph TD
    subgraph "Mikrokontroler ESP32"
        subgraph "Core 1: Loop Kendali Haptik (Prioritas Tinggi)"
            A[Membaca Data Sensor Enkoder I2C] --> B[Menghitung Kedalaman Posisi Jarum]
            B --> C[Mengevaluasi Model Fisika Okamura]
            C --> D[Menghasilkan Sinyal PWM Motor]
    subgraph "ESP32 Microcontroller"
        subgraph "Core 1: Haptic Control Loop (High Priority)"
            A[Read I2C Encoder Sensor Data] --> B[Calculate Needle Position Depth]
            B --> C[Evaluate Okamura Physics Model]
            C --> D[Generate Motor PWM Signal]
        end
        subgraph "Core 0: Komunikasi Serial (Prioritas Rendah)"
            E[Menerima Data Fisika via FreeRTOS Queue] --> F[Memformat Data Menjadi String Serial]
            F --> G[Mengirim Data ke Komputer]
        subgraph "Core 0: Serial Communication (Low Priority)"
            E[Receive Physics Data via FreeRTOS Queue] --> F[Format Data Into Serial String]
            F --> G[Send Data to Computer]
        end
    end
    D -->|"Sinyal Kendali"| Motor["Motor DC Haptik"]
    G -->|"Visualisasi Log"| Browser["Peramban Web 3D"]
    D -->|"Control Signal"| Motor["Haptic DC Motor"]
    G -->|"Log Visualization"| Browser["3D Web Browser"]
```

## Haptic Physics Model

The developers base the computational force calculations on the Okamura force decomposition combined with the Mahvash Dupont contact model. Mathematically, the system computes the total axial force through the following equation:

```text
Total Axial Force = Stiffness Force + Cutting Force + Friction Force
```

### Contact Model (Prerupture Phase)

The system utilizes the Mahvash and Dupont nonlinear spring combined with a Maxwell viscoelastic branch. The following equations represent the needle tip force calculation:

```text
Tip Force = a2 * delta^2 + a1 * delta + K(delta) * delta_k
K(delta) = K' * delta_k (stiffness dependent on deformation level)
tau = D'/K' (viscoelastic time constant)
```

The Maxwell branch in this model serves to provide viscoelastic relaxation. With this mechanism, the force experienced by the user will decay naturally when the user stops pushing the needle.

### Fracture Mechanism (From Rupture to Cutting)

When the needle tip force reaches the rupture threshold (Fr), the biological tissue will undergo penetration. The system simulates this event through an instantaneous force drop towards a constant cutting force level (Fc).

```text
Penetration Event: Tip force drops instantly from Fr to Fc
Cutting: Tip force = Fc = Rf * wc (fracture toughness multiplied by crack width)
```

### Shaft Friction (Coulomb and Viscous Friction)
The friction value will increase proportionally as the needle insertion depth increases, because the contact area between the needle shaft and the tissue becomes larger.

```text
Friction Force = (mu_shaft * depth) + (B_viscous * velocity) + f_stiction
```

### Physical Tissue Layer Parameters

| Tissue Layer | Spatial Depth | a1 Value | a2 Value | K' Value | tau Time (seconds) | Fr Force (Rupture) | Fc Force (Cutting) |
|-------|-------|------|------|------|-------|--------------|--------------|
| **Skin** | 0 to 2 millimeters | 0.08 | 0.12 | 0.20 | 0.04 | 0.55 | 0.08 |
| **Fat** | 2 to 10 millimeters | 0.012 | 0.002 | 0.03 | 0.10 | 0.22 | 0.06 |
| **Muscle** | 10 to 35 millimeters | 0.018 | 0.004 | 0.06 | 0.06 | 0.35 | 0.12 |

The developers design each layer to possess distinct tearing sensation characteristics. The skin features a rigid property with strong rupture resistance. The fat feels softer and highly compliant to pressure. The muscle possesses a fibrous structure that provides an intermediate level of resistance.

## Hardware Requirements

This system requires the integration of several specific hardware components to function optimally:

1.  **ESP32 Development Board**: Acts as the primary computational brain that processes the physics algorithms and controls other modules.
2.  **DC Motor with H Bridge Driver**: The developers recommend using the TB6612FNG driver module or similar modules capable of controlling the motor rotation direction and speed precisely.
    *   You must connect the PWMA pin to the IO18 pin on the ESP32.
    *   You must connect the AIN1 pin to the IO19 pin on the ESP32.
    *   You must connect the AIN2 pin to the IO23 pin on the ESP32.
3.  **AS5600 Magnetic Rotary Encoder**: This sensor communicates through the I2C interface protocol to read the rotation angle precisely, which the system converts into needle depth.
    *   You must connect the SDA pin to the IO21 pin on the ESP32.
    *   You must connect the SCL pin to the IO22 pin on the ESP32.
4.  **Rotary Knob Mechanism**: The user must install a physical rotary knob connected directly to the motor shaft to provide a mechanical interaction interface for the user.

## Detailed Installation and Configuration Guide

Users must follow these installation procedures sequentially and carefully to configure the hardware and software of this simulation system.

### 1. ESP32 Firmware Flashing Procedure

1.  **Download and Install Arduino IDE**: Users need to download the latest version of Arduino IDE from the official Arduino website and install it on their computer operating system.
2.  **Configure ESP32 Board**: Open the *File* menu, then select *Preferences*. In the *Additional Boards Manager URLs* field, users must enter the link `https://dl.espressif.com/dl/package_esp32_index.json` to download the ESP32 development board library.
3.  **Install ESP32 Library**: Open the *Tools* menu, navigate to *Board*, then select *Boards Manager*. Perform a search using the keyword "esp32" published by Espressif Systems, then click the install button to complete the board configuration process.
4.  **Open Main File**: Open the file named `virtual-wall32.ino` located in the root directory of this project using the Arduino IDE application.
5.  **Select Board and Communication Port**: Open the *Tools* menu, set the *Board* option to the ESP32 variant that you use (for example, DOIT ESP32 DEVKIT V1). Next, select the appropriate communication port (COM Port) that corresponds to the USB cable connection path to the ESP32 microcontroller.
6.  **Code Uploading Process**: Click the *Upload* button on the Arduino IDE and wait for the screen instructions until the code compilation and data flashing process into the ESP32 memory finishes completely.

### 2. Running the Three Dimensional Visualization System

1.  **Use a Web Serial Supported Browser**: Open the `visualization3d.html` file exclusively using the Google Chrome or Microsoft Edge browser. The developers mandate the use of these browsers because the system fluidity relies heavily on the Web Serial Application Programming Interface (Web Serial API) functionality, which is not natively available in all web browsers.
2.  **Serve Files via Local Server (Highly Recommended)**: To allow the web browser to load all assets perfectly and avoid Cross Origin Resource Sharing (CORS) policy issues, users are highly advised to run this interface through a local web server. Users can utilize the Live Server extension in the Visual Studio Code software, or use a standalone web server stack application such as Laragon or XAMPP. Open the interface through the local address, for example: `http://localhost/Haptic-Knob-ESP32/virtual-wall32/visualization3d.html`.
3.  **Physical Device Connection**: After the web interface opens perfectly, click the "Connect ESP32" button available on the interface. The web browser will display a serial port confirmation pop up dialog box. Select the serial port that physically connects to the user's ESP32 device, then confirm by pressing the Connect button.

### 3. Classic Python Visualization Configuration (Optional Approach)

For users who prioritize using a Python script based visualization environment, users are welcome to configure and execute the following instruction steps through the command terminal window.

1.  **Build a Virtual Environment**: Execute the command `python -m venv venv` in the terminal to create a Python installation distribution environment isolated from the main system.
2.  **Virtual Environment Activation Process**: If the user operates on the Windows operating system platform, run the script execution command `venv\Scripts\activate`.
3.  **Install Dependency Modules**: Run the package processing command `pip install -r requirements.txt` to download and configure all programming library modules required by this Python visualization system.
4.  **Run the Visualization Program**: Execute the command `python visualization.py` to activate the visualization graphical interface.

## System Control Mechanism

### Three Dimensional Visualization Interface Controls on Browser

The visualization system provides a set of spatial navigation control functions to facilitate users in precisely monitoring every stage of the simulation process.

*   **Hold and Drag Left Mouse Button**: Users perform this action to rotate the camera perspective orientation around the center of the three dimensional object.
*   **Scroll Mouse Wheel**: Users manipulate the mouse wheel to control the magnification or reduction level of the visual focus on the insertion area.
*   **Up and Down Arrow Navigation Keys**: Users utilize these keys specifically to simulate the translational movement of the needle in virtual space when the mechanical hardware system is not connected.
*   **Keyboard Shortcut Key R**: Users press this key to reinitialize the needle position coordinates back to the outermost skin surface point.
*   **Connect ESP32 Interface Button**: Users press this screen button to trigger the browser application programming interface dialog to open the serial communication line with 
the hardware microcontroller.

### Classic Python Visualization Navigation Controls
*   **Up and Down Arrow Navigation Keys**: Users press the up and down navigation keys to force a shift in the simulated needle pushing direction.
*   **Keyboard Shortcut Key R**: Users press the R key to reset the tool calibration spatial metric calculations.
*   **Keyboard Shortcut Key ESC**: Users press the Escape key to send an execution cancellation command and terminate the Python program instantly.

## System Serial Communication Protocol
The ESP32 microcontroller actively and constantly transmits status update data packets through the serial interface line with a modulation speed of 115200 baud at a stable frequency of 20 Hertz. The data encapsulation format takes the form of a character string sequence with the following property value structure format:

`Depth_mm:12.34 Force_cmd:0.1823 State:1.0 Layer:1 FTip:0.0600 FFric:0.0370`

The developers parse and define each segment of the data packet variables as follows:
*   `Depth_mm` indicates the translational depth calculation of the proximal needle insertion represented in millimeter units. A numerical value of 0 specifically indicates that the needle is resting exactly at the boundary of the outermost skin tissue surface.
*   `Force_cmd` represents the aggregate magnitude of output power distributed to the actuator motor, normalized within a decimal value range of 0 to 1.
*   `State` describes the quantitative parameter of the needle tip physical interaction cycle with the environment. A numerical value of 0 symbolizes that the needle is moving freely in the air medium. A value of 0.5 defines that the tissue is in the prerupture phase or basic shape deformation process. A value of 1.0 confirms that the needle tip is currently penetrating the biological tissue fiber cutting phase constantly.
*   `Layer` denotes the real time positional coordinates of the spatial layer of the medium tissue where the needle tip currently resides. The system utilizes a value of -1 for the free air medium, an index of 0 for the epidermal skin tissue, an index of 1 for the subcutaneous fat tissue, and an index of 2 for the muscle mass formation.
*   `FTip` informs the derivative value of the computational calculation of the dynamic force resistance specifically excited affecting the needle tip contact surface area.
*   `FFric` informs the derivative value of the cumulative kinetic friction fixation calculation which naturally provides linear movement blocking resistance across the entire surface area of the outer needle shaft.

## Physical Device Initiation and Calibration Procedure

System users are strictly required to execute the initial initialization calibration procedure. This operational step serves to reestablish the cartesian reference zero point of the motor mechanical instrument before the experimental execution takes place.

1.  Provide adequate electrical input power to all device circuit board components. The mechanical knob instrument will autonomously rotate its axis to search for and lock onto the spatial center reference position within a 180 degree geometric rotation limit.
2.  The user must hold the central knob in a state of total equilibrium or stability for a full observation duration period of two consecutive seconds.
3.  The computational system will independently record this rotational resting position trace immediately, which it will then articulate as the absolute vertical zero coordinate boundary at the equator of the simulated outer skin surface.
4.  The user can directly manipulate the mechanical rotary rotation of the knob device clockwise to produce a linear penetration pushing force simulation of the needle tip entry, and manipulate the rotation torque in the counter clockwise direction to create a linear extraction force pulling effect of the entire needle body exiting.

## Technical Issue Troubleshooting Analysis Guide

### Three Dimensional Visualization Device Refuses to Open Connection Session

Users must first reflect on their browser specifications and ensure they are utilizing a modern web browser product such as Google Chrome or Microsoft Edge. Users should remember that the Web Serial API access interface protocol is highly critical and absolute for the communication line to occur successfully. If complaints persist, inspect and validate the conductor connection on the USB cable line while confirming that the bridge portability communication driver package library (such as the CH340 IC or CP210x IC chipset) has been loaded correctly by the operating system core kernel.

### DC Motor Actuator Fails to Implement Mechanical Feedback

Users need to actively verify the compatibility level of the electronic wiring assembly schematic path so that the hardware pin structure does not conflict with the program code definition block inside the `knob.cpp` component file. Carefully ensure that the wiring structure of pins IO18, IO19, and IO23 integrates completely into the port block of the motor control driver board module, alongside ensuring that interface pins IO21 and IO22 have established solid communication with the port pins of the I2C protocol rotational encoder module. Users must also ensure the power supply energy source provides sufficient electrical current supply (Amperes) essential to meet the operational energy hunger of the high performance DC motor.

### Force Feedback Resistance Curvature Curve Feels Anomalous or Inaccurate

Users of this research project always receive unlimited modification privileges to retune the physical mathematical coefficients of each anatomical tissue layer simulation environment model. These variable settings are organized neatly in the memory block index of the `layers[]` array inside the main source code `virtual-wall32.ino`. For advanced users, we also recommend iteratively executing tuning modifications to manipulate the macro variable calculation constant values `MU_SHAFT` and the `B_VISCOUS` range to create a manifestation of the shaft friction pulling resistance reality that is as natural as possible.

## Bibliography and Academic Research References

This advanced haptic interface simulation system was methodologically engineered and built based on the empirical foundation of respected scientific literature examining the realm of computational haptic control and tissue biomechanical dynamics system modeling:
*   Okamura, A. M., Simone, C., & O'Leary, M. D. (2004). *Force modeling for needle insertion into soft tissue*. IEEE Trans. Biomedical Engineering, 51(10), 1707-1716.
*   Mahvash, M. & Dupont, P. E. (2010). *Mechanics of dynamic needle insertion into a biological material*. IEEE Trans. Biomedical Engineering, 57(4), 934-943.
*   Delbos, B., Chalard, R., Lelevé, A., & Moreau, R. (2024). *A generalized tracking wall approach to the haptic simulation of tip forces during needle insertion*. IEEE Trans. Haptics, 18(1), 110-123.

## Copyright and Usage Distribution License Determination

The academics along with the entire structure of the research development division unit dedicate this open source engineering project entirely with the noble purpose of advancing the fundamental educational agenda, supporting exploratory knowledge transfer, and collectively ensuring the success of advanced research programs without any tendencies or elements of commercialization.

**Tim Pengembang:**
* IFAC 2026 Team - Niceknob ITENAS
**Development Team:**
* IFAC 2026 Team Niceknob ITENAS
* Zakhwa Aliya (152024032)
* Dzakiyya Puteri Aulia (152024127)
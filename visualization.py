"""
Haptic Knob Needle Insertion Visualization
OpenGL visualization for needle insertion simulation
Requires two haptic knobs per hand
"""

import pygame
from pygame.locals import *
from OpenGL.GL import *
from OpenGL.GLU import *
import serial
import serial.tools.list_ports
import threading
import math
import time

# ================= CONFIGURATION =================
WINDOW_WIDTH = 1200
WINDOW_HEIGHT = 700
BAUDRATE = 115200

# Physics conversion (from ESP32 code)
DEG_TO_MM = 20.0 / 360.0
SURFACE_OFFSET = 0.0  # Will be calibrated

# Needle parameters
NEEDLE_LENGTH = 120.0
NEEDLE_TIP_LENGTH = 15.0
NEEDLE_WIDTH = 3.0

# Flesh layer parameters (mm from surface)
SKIN_THICKNESS = 2.0
FAT_THICKNESS = 8.0
MUSCLE_THICKNESS = 25.0

# Colors (RGBA)
COLOR_SKIN = (0.96, 0.80, 0.69, 1.0)
COLOR_FAT = (1.0, 0.95, 0.6, 1.0)
COLOR_MUSCLE = (0.7, 0.2, 0.2, 1.0)
COLOR_DEEP_TISSUE = (0.5, 0.15, 0.15, 1.0)
COLOR_NEEDLE = (0.75, 0.75, 0.78, 1.0)
COLOR_NEEDLE_TIP = (0.85, 0.85, 0.88, 1.0)
COLOR_AIR = (0.9, 0.95, 1.0, 1.0)

# ================= GLOBAL STATE =================
class KnobState:
    def __init__(self, name):
        self.name = name
        self.depth_mm = 0.0
        self.velocity = 0.0
        self.state = 0  # 0=AIR, 1=CAPSULE, 2=INSERTED
        self.force = 0.0
        self.angle = 0.0
        self.last_angle = None
        self.calibrated = False
        self.surface_angle = 0.0

# Two knobs per hand, two hands
left_hand_knob1 = KnobState("Left Hand - Knob 1")
left_hand_knob2 = KnobState("Left Hand - Knob 2")
right_hand_knob1 = KnobState("Right Hand - Knob 1")
right_hand_knob2 = KnobState("Right Hand - Knob 2")

# Active knobs list (will be populated with connected devices)
active_knobs = []
serial_connections = []

# Simulation mode (if no hardware connected)
simulation_mode = False
sim_time = 0.0

# ================= SERIAL FUNCTIONS =================
def find_arduino_ports():
    """Find all connected Arduino/ESP32 devices"""
    ports = serial.tools.list_ports.comports()
    found = []
    for p in ports:
        if "USB" in p.description or "UART" in p.description or "CP210" in p.description or "CH340" in p.description:
            found.append(p.device)
    return found

def parse_serial_line(line, knob_state):
    """Parse serial data from ESP32"""
    try:
        if "Depth_mm:" in line:
            parts = line.split()
            for p in parts:
                if p.startswith("Depth_mm:"):
                    knob_state.depth_mm = float(p.replace("Depth_mm:", ""))
                elif p.startswith("Force_cmd:"):
                    knob_state.force = float(p.replace("Force_cmd:", ""))
                elif p.startswith("State:"):
                    state_val = float(p.replace("State:", ""))
                    knob_state.state = int(state_val / 0.5)
        elif "V=" in line:
            # Alternative format from original main.py
            for p in line.split():
                if p.startswith("V="):
                    V = float(p.replace("V=", ""))
                    if knob_state.last_angle is None:
                        knob_state.last_angle = V
                        knob_state.surface_angle = V
                        knob_state.calibrated = True
                    else:
                        dV = V - knob_state.last_angle
                        knob_state.last_angle = V
                        knob_state.angle += dV * 120.0
                        
                        # Calculate depth
                        depth_deg = knob_state.angle - knob_state.surface_angle
                        knob_state.depth_mm = depth_deg * DEG_TO_MM
                        
                        # Determine state based on depth
                        if knob_state.depth_mm <= 0:
                            knob_state.state = 0  # AIR
                        elif knob_state.depth_mm < SKIN_THICKNESS + FAT_THICKNESS:
                            knob_state.state = 1  # CAPSULE
                        else:
                            knob_state.state = 2  # INSERTED
    except Exception as e:
        pass

def serial_reader(ser, knob_state):
    """Thread function to read serial data"""
    while True:
        try:
            line = ser.readline().decode(errors="ignore").strip()
            if line:
                parse_serial_line(line, knob_state)
        except Exception as e:
            print(f"Serial error ({knob_state.name}): {e}")
            time.sleep(0.1)

def init_serial_connections():
    """Initialize serial connections to all found devices"""
    global simulation_mode, active_knobs, serial_connections
    
    ports = find_arduino_ports()
    
    if not ports:
        print("[INFO] No Arduino/ESP32 devices found. Running in SIMULATION mode.")
        print("[INFO] Press UP/DOWN arrows to simulate needle movement.")
        simulation_mode = True
        active_knobs = [left_hand_knob1]  # Use one knob for simulation
        return
    
    knob_list = [left_hand_knob1, left_hand_knob2, right_hand_knob1, right_hand_knob2]
    
    for i, port in enumerate(ports[:4]):  # Max 4 devices
        try:
            ser = serial.Serial(port, BAUDRATE, timeout=0.1)
            serial_connections.append(ser)
            knob = knob_list[i]
            active_knobs.append(knob)
            
            # Start reader thread
            t = threading.Thread(target=serial_reader, args=(ser, knob), daemon=True)
            t.start()
            
            print(f"[INFO] Connected {knob.name} to {port}")
        except Exception as e:
            print(f"[ERROR] Could not connect to {port}: {e}")
    
    if not active_knobs:
        print("[INFO] No devices connected. Running in SIMULATION mode.")
        simulation_mode = True
        active_knobs = [left_hand_knob1]

# ================= OPENGL DRAWING =================
def draw_rounded_rect(x, y, width, height, radius, color):
    """Draw a rounded rectangle"""
    glColor4f(*color)
    
    # Main rectangle
    glBegin(GL_QUADS)
    glVertex2f(x + radius, y)
    glVertex2f(x + width - radius, y)
    glVertex2f(x + width - radius, y + height)
    glVertex2f(x + radius, y + height)
    glEnd()
    
    # Left strip
    glBegin(GL_QUADS)
    glVertex2f(x, y + radius)
    glVertex2f(x + radius, y + radius)
    glVertex2f(x + radius, y + height - radius)
    glVertex2f(x, y + height - radius)
    glEnd()
    
    # Right strip
    glBegin(GL_QUADS)
    glVertex2f(x + width - radius, y + radius)
    glVertex2f(x + width, y + radius)
    glVertex2f(x + width, y + height - radius)
    glVertex2f(x + width - radius, y + height - radius)
    glEnd()
    
    # Corners
    segments = 10
    for corner in [(x + radius, y + radius, 180, 270),
                   (x + width - radius, y + radius, 270, 360),
                   (x + width - radius, y + height - radius, 0, 90),
                   (x + radius, y + height - radius, 90, 180)]:
        cx, cy, start, end = corner
        glBegin(GL_TRIANGLE_FAN)
        glVertex2f(cx, cy)
        for i in range(segments + 1):
            angle = math.radians(start + (end - start) * i / segments)
            glVertex2f(cx + radius * math.cos(angle), cy + radius * math.sin(angle))
        glEnd()

def draw_flesh_cross_section(x_offset, y_offset, width, height, depth_mm):
    """Draw a cross-section of flesh layers"""
    scale = height / (SKIN_THICKNESS + FAT_THICKNESS + MUSCLE_THICKNESS + 10)
    
    # Background (air)
    glColor4f(*COLOR_AIR)
    glBegin(GL_QUADS)
    glVertex2f(x_offset, y_offset + height)
    glVertex2f(x_offset + width, y_offset + height)
    glVertex2f(x_offset + width, y_offset + height - 20)
    glVertex2f(x_offset, y_offset + height - 20)
    glEnd()
    
    layer_y = y_offset + height - 20
    
    # Skin layer
    skin_height = SKIN_THICKNESS * scale
    glColor4f(*COLOR_SKIN)
    glBegin(GL_QUADS)
    glVertex2f(x_offset, layer_y)
    glVertex2f(x_offset + width, layer_y)
    glVertex2f(x_offset + width, layer_y - skin_height)
    glVertex2f(x_offset, layer_y - skin_height)
    glEnd()
    
    # Draw skin texture (dots)
    glColor4f(0.9, 0.7, 0.6, 1.0)
    glPointSize(2)
    glBegin(GL_POINTS)
    for i in range(int(width / 8)):
        for j in range(max(1, int(skin_height / 4))):
            px = x_offset + 4 + i * 8 + (j % 2) * 4
            py = layer_y - 2 - j * 4
            if py > layer_y - skin_height:
                glVertex2f(px, py)
    glEnd()
    
    layer_y -= skin_height
    
    # Fat layer
    fat_height = FAT_THICKNESS * scale
    glColor4f(*COLOR_FAT)
    glBegin(GL_QUADS)
    glVertex2f(x_offset, layer_y)
    glVertex2f(x_offset + width, layer_y)
    glVertex2f(x_offset + width, layer_y - fat_height)
    glVertex2f(x_offset, layer_y - fat_height)
    glEnd()
    
    # Fat texture (circles)
    glColor4f(1.0, 0.9, 0.5, 0.5)
    for i in range(int(width / 15)):
        for j in range(max(1, int(fat_height / 12))):
            cx = x_offset + 8 + i * 15 + (j % 2) * 7
            cy = layer_y - 6 - j * 12
            if cy > layer_y - fat_height + 4:
                draw_circle(cx, cy, 4, 8)
    
    layer_y -= fat_height
    
    # Muscle layer
    muscle_height = MUSCLE_THICKNESS * scale
    glColor4f(*COLOR_MUSCLE)
    glBegin(GL_QUADS)
    glVertex2f(x_offset, layer_y)
    glVertex2f(x_offset + width, layer_y)
    glVertex2f(x_offset + width, layer_y - muscle_height)
    glVertex2f(x_offset, layer_y - muscle_height)
    glEnd()
    
    # Muscle fibers
    glColor4f(0.6, 0.15, 0.15, 1.0)
    glLineWidth(1)
    glBegin(GL_LINES)
    for i in range(int(width / 6)):
        lx = x_offset + 3 + i * 6
        glVertex2f(lx, layer_y - 2)
        glVertex2f(lx, layer_y - muscle_height + 2)
    glEnd()
    
    layer_y -= muscle_height
    
    # Deep tissue
    remaining_height = layer_y - y_offset
    if remaining_height > 0:
        glColor4f(*COLOR_DEEP_TISSUE)
        glBegin(GL_QUADS)
        glVertex2f(x_offset, layer_y)
        glVertex2f(x_offset + width, layer_y)
        glVertex2f(x_offset + width, y_offset)
        glVertex2f(x_offset, y_offset)
        glEnd()
    
    # Return the y-coordinate of the surface for needle positioning
    return y_offset + height - 20

def draw_circle(cx, cy, radius, segments=16):
    """Draw a filled circle"""
    glBegin(GL_TRIANGLE_FAN)
    glVertex2f(cx, cy)
    for i in range(segments + 1):
        angle = 2 * math.pi * i / segments
        glVertex2f(cx + radius * math.cos(angle), cy + radius * math.sin(angle))
    glEnd()

def draw_needle(x, surface_y, depth_mm, width, angle_deg=0):
    """Draw a needle at given position and depth"""
    scale = 4.0  # Visual scale for depth
    
    # Needle tip position (negative depth means above surface)
    tip_y = surface_y - (depth_mm * scale)
    
    # Needle body extends upward from tip
    body_top_y = tip_y + NEEDLE_LENGTH
    
    # Calculate needle points with slight angle
    angle_rad = math.radians(angle_deg)
    dx = math.sin(angle_rad) * NEEDLE_LENGTH
    
    # Needle shaft (metallic)
    glColor4f(*COLOR_NEEDLE)
    half_width = NEEDLE_WIDTH / 2
    
    glBegin(GL_QUADS)
    glVertex2f(x - half_width, body_top_y)
    glVertex2f(x + half_width, body_top_y)
    glVertex2f(x + half_width + dx * 0.1, tip_y + NEEDLE_TIP_LENGTH)
    glVertex2f(x - half_width + dx * 0.1, tip_y + NEEDLE_TIP_LENGTH)
    glEnd()
    
    # Needle tip (triangular)
    glColor4f(*COLOR_NEEDLE_TIP)
    glBegin(GL_TRIANGLES)
    glVertex2f(x - half_width, tip_y + NEEDLE_TIP_LENGTH)
    glVertex2f(x + half_width, tip_y + NEEDLE_TIP_LENGTH)
    glVertex2f(x, tip_y)
    glEnd()
    
    # Needle highlight (reflection)
    glColor4f(1.0, 1.0, 1.0, 0.3)
    glBegin(GL_QUADS)
    glVertex2f(x - half_width + 1, body_top_y - 5)
    glVertex2f(x - half_width + 2, body_top_y - 5)
    glVertex2f(x - half_width + 2, tip_y + NEEDLE_TIP_LENGTH + 5)
    glVertex2f(x - half_width + 1, tip_y + NEEDLE_TIP_LENGTH + 5)
    glEnd()
    
    # Hub (needle holder)
    hub_height = 25
    hub_width = 20
    glColor4f(0.3, 0.5, 0.8, 1.0)  # Blue plastic
    glBegin(GL_QUADS)
    glVertex2f(x - hub_width/2, body_top_y)
    glVertex2f(x + hub_width/2, body_top_y)
    glVertex2f(x + hub_width/2, body_top_y + hub_height)
    glVertex2f(x - hub_width/2, body_top_y + hub_height)
    glEnd()
    
    # Hub detail
    glColor4f(0.2, 0.4, 0.7, 1.0)
    glLineWidth(2)
    glBegin(GL_LINE_LOOP)
    glVertex2f(x - hub_width/2, body_top_y)
    glVertex2f(x + hub_width/2, body_top_y)
    glVertex2f(x + hub_width/2, body_top_y + hub_height)
    glVertex2f(x - hub_width/2, body_top_y + hub_height)
    glEnd()

def draw_hand_label(x, y, text, knob_state):
    """Draw hand label and state info"""
    # State colors
    state_colors = {
        0: (0.5, 0.8, 1.0, 1.0),   # AIR - light blue
        1: (1.0, 0.8, 0.3, 1.0),   # CAPSULE - orange
        2: (1.0, 0.3, 0.3, 1.0)    # INSERTED - red
    }
    state_names = {0: "AIR", 1: "CAPSULE", 2: "INSERTED"}
    
    # Draw state indicator circle
    color = state_colors.get(knob_state.state, (0.5, 0.5, 0.5, 1.0))
    glColor4f(*color)
    draw_circle(x - 15, y + 8, 8, 16)
    
    # Draw depth bar
    bar_width = 100
    bar_height = 10
    max_depth = 30.0
    
    # Background
    glColor4f(0.3, 0.3, 0.3, 1.0)
    glBegin(GL_QUADS)
    glVertex2f(x, y - 20)
    glVertex2f(x + bar_width, y - 20)
    glVertex2f(x + bar_width, y - 20 + bar_height)
    glVertex2f(x, y - 20 + bar_height)
    glEnd()
    
    # Fill based on depth
    fill_width = min(bar_width, max(0, (knob_state.depth_mm / max_depth) * bar_width))
    glColor4f(*color)
    glBegin(GL_QUADS)
    glVertex2f(x, y - 20)
    glVertex2f(x + fill_width, y - 20)
    glVertex2f(x + fill_width, y - 20 + bar_height)
    glVertex2f(x, y - 20 + bar_height)
    glEnd()

def draw_text_overlay(font, text, x, y, color=(255, 255, 255)):
    """Draw text using pygame font (overlay method)"""
    text_surface = font.render(text, True, color)
    text_data = pygame.image.tostring(text_surface, "RGBA", True)
    
    glWindowPos2d(x, y)
    glDrawPixels(text_surface.get_width(), text_surface.get_height(),
                 GL_RGBA, GL_UNSIGNED_BYTE, text_data)

def init_opengl():
    """Initialize OpenGL settings"""
    glEnable(GL_BLEND)
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
    glClearColor(0.15, 0.15, 0.2, 1.0)
    
    # Setup 2D projection
    glMatrixMode(GL_PROJECTION)
    glLoadIdentity()
    gluOrtho2D(0, WINDOW_WIDTH, 0, WINDOW_HEIGHT)
    glMatrixMode(GL_MODELVIEW)
    glLoadIdentity()

def update_simulation():
    """Update simulation mode values"""
    global sim_time
    sim_time += 0.016  # Approx 60 FPS
    
    # Keyboard control in simulation mode is handled in main loop
    pass

def main():
    """Main function"""
    global simulation_mode
    
    # Initialize pygame and OpenGL
    pygame.init()
    pygame.display.set_mode((WINDOW_WIDTH, WINDOW_HEIGHT), DOUBLEBUF | OPENGL)
    pygame.display.set_caption("Haptic Knob - Needle Insertion Simulation")
    
    # Initialize font
    pygame.font.init()
    font_large = pygame.font.SysFont('Arial', 24)
    font_small = pygame.font.SysFont('Arial', 16)
    
    # Initialize serial connections
    init_serial_connections()
    
    # Initialize OpenGL
    init_opengl()
    
    clock = pygame.time.Clock()
    running = True
    
    # Panel dimensions
    panel_width = 280
    panel_height = 350
    panel_margin = 30
    panel_y = 150
    
    while running:
        # Event handling
        for event in pygame.event.get():
            if event.type == QUIT:
                running = False
            elif event.type == KEYDOWN:
                if event.key == K_ESCAPE:
                    running = False
                elif event.key == K_r:
                    # Reset calibration
                    for knob in active_knobs:
                        knob.depth_mm = 0.0
                        knob.surface_angle = knob.angle
                        knob.state = 0
        
        # Handle continuous key presses for simulation
        if simulation_mode:
            keys = pygame.key.get_pressed()
            for knob in active_knobs:
                if keys[K_UP]:
                    knob.depth_mm = max(-10, knob.depth_mm - 0.5)
                if keys[K_DOWN]:
                    knob.depth_mm = min(35, knob.depth_mm + 0.5)
                
                # Update state based on depth
                if knob.depth_mm <= 0:
                    knob.state = 0
                elif knob.depth_mm < SKIN_THICKNESS + FAT_THICKNESS:
                    knob.state = 1
                else:
                    knob.state = 2
        
        # Clear screen
        glClear(GL_COLOR_BUFFER_BIT)
        
        # Calculate panel positions based on number of active knobs
        num_knobs = len(active_knobs)
        total_width = num_knobs * panel_width + (num_knobs - 1) * panel_margin
        start_x = (WINDOW_WIDTH - total_width) / 2
        
        # Draw each knob's visualization
        for i, knob in enumerate(active_knobs):
            panel_x = start_x + i * (panel_width + panel_margin)
            
            # Draw panel background
            glColor4f(0.2, 0.2, 0.25, 0.9)
            glBegin(GL_QUADS)
            glVertex2f(panel_x - 10, panel_y - 10)
            glVertex2f(panel_x + panel_width + 10, panel_y - 10)
            glVertex2f(panel_x + panel_width + 10, panel_y + panel_height + 60)
            glVertex2f(panel_x - 10, panel_y + panel_height + 60)
            glEnd()
            
            # Draw flesh cross-section
            surface_y = draw_flesh_cross_section(
                panel_x, panel_y, 
                panel_width, panel_height,
                knob.depth_mm
            )
            
            # Draw needle
            needle_x = panel_x + panel_width / 2
            draw_needle(needle_x, surface_y, knob.depth_mm, panel_width)
            
            # Draw state indicator
            draw_hand_label(panel_x + 30, panel_y + panel_height + 45, knob.name, knob)
        
        # Draw title and info using pygame text overlay
        glColor4f(1, 1, 1, 1)
        
        # Render text to pygame surface, then blit
        title_text = "Haptic Knob - Needle Insertion Simulation"
        draw_text_overlay(font_large, title_text, 
                         int((WINDOW_WIDTH - font_large.size(title_text)[0]) / 2), 
                         WINDOW_HEIGHT - 50)
        
        mode_text = "[SIMULATION MODE] Use UP/DOWN arrows" if simulation_mode else f"[CONNECTED] {len(active_knobs)} device(s)"
        draw_text_overlay(font_small, mode_text,
                         int((WINDOW_WIDTH - font_small.size(mode_text)[0]) / 2),
                         WINDOW_HEIGHT - 80)
        
        # Draw legend
        legend_y = 100
        legend_x = 20
        
        # State legend
        states = [
            ("AIR", (0.5, 0.8, 1.0)),
            ("CAPSULE (Skin)", (1.0, 0.8, 0.3)),
            ("INSERTED", (1.0, 0.3, 0.3))
        ]
        
        for j, (state_name, color) in enumerate(states):
            glColor4f(*color, 1.0)
            draw_circle(legend_x + 10, legend_y - j * 25, 6, 12)
            draw_text_overlay(font_small, state_name, legend_x + 25, legend_y - j * 25 - 8, 
                            (int(color[0]*255), int(color[1]*255), int(color[2]*255)))
        
        # Draw depth values for each knob
        for i, knob in enumerate(active_knobs):
            panel_x = start_x + i * (panel_width + panel_margin)
            depth_text = f"Depth: {knob.depth_mm:.1f} mm"
            state_text = ["AIR", "CAPSULE", "INSERTED"][knob.state]
            
            draw_text_overlay(font_small, knob.name, int(panel_x), panel_y - 30, (200, 200, 200))
            draw_text_overlay(font_small, depth_text, int(panel_x), panel_y - 50, (255, 255, 255))
            draw_text_overlay(font_small, f"State: {state_text}", int(panel_x + 120), panel_y - 50, (255, 255, 255))
        
        # Controls info
        draw_text_overlay(font_small, "Press R to reset | ESC to quit", 20, 20, (150, 150, 150))
        
        # Update display
        pygame.display.flip()
        clock.tick(60)
    
    # Cleanup
    for ser in serial_connections:
        ser.close()
    pygame.quit()

if __name__ == "__main__":
    main()

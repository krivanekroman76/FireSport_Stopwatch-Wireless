from build123d import *
from ocp_vscode import show

# 1. Přesné parametry plošného spoje (FR-4 50x70)
board_width = 50.0
board_length = 70.0
board_thickness = 1.6
hole_pitch = 2.54 
cols = 18
rows = 24
hole_diameter = 1.0 

mount_hole_diameter = 3.2 
mount_offset_x = 40.0 / 2 
mount_offset_y = 65.0 / 2 

batt_mount_hole_spacing = 54.00 
batt_mount_hole_dia     = 3.00  
batt_pin_hole_spacing   = 72.00 
batt_pin_hole_dia       = 1.80  

# === PARAMETRY KRABIČKY A DYNAMICKÁ VÝŠKA ===
box_width = 90.0
box_length = 90.0
wall_thickness = 3.0
standoff_height = 5.0

# Výpočet celkové výšky (přidáno 6 mm pro senzor pod tlačítky)
battery_diameter = 18.5
lid_clearance = 2.0
extra_sensor_clearance = 6.0 
internal_height = standoff_height + board_thickness + 3.0 + battery_diameter + lid_clearance + extra_sensor_clearance
box_height = wall_thickness + internal_height 

# === PARAMETRY TLAČÍTEK ===
button_spacing = 22.0          
button_start_offset_y = 22.0   

# === PARAMETRY PRO VYPÍNAČ A JEHO SLOUPEČKY ===
switch_standoff_h = 3.0 
switch_hole_pitch = 16.0 
switch_insert_dia = 3.6  

# === PARAMETRY ZVUKOVÉHO SENZORU KY-037 ===
ky_pcb_length = 35.0        # Délka plošného spoje senzoru
ky_pcb_width = 15.0         # Šířka plošného spoje senzoru
ky_pcb_thickness = 1.6      # Tloušťka desky (standardně 1.6 mm)

ky_mic_diameter = 9.5       # Průměr stříbrného válečku mikrofonu
ky_mic_length = 8.0         # Jak moc mikrofon trčí dopředu
ky_mic_center_z = 4.75      # Výška středu mikrofonu od povrchu PCB (změř šuplerou!)

ky_standoff_h = 2.0         # Jak vysoko nad dnem krabičky má senzor sedět
ky_hole_spacing = 20.0      # Vzdálenost montážních sloupků (od středu do středu)

# === ZAROVNÁNÍ ===
pcb_shift_x = -3.0 
holder_pos_x = -mount_offset_x 
actual_holder_y = (mount_offset_y - (batt_mount_hole_spacing / 2)) - 1.8
esp_pos_x =  hole_pitch

# --- A. Samotná deska ---
with BuildPart() as pcb:
    Box(board_width, board_length, board_thickness, align=(Align.CENTER, Align.CENTER, Align.CENTER))
    with GridLocations(hole_pitch, hole_pitch, cols, rows):
        Cylinder(radius=hole_diameter/2, height=board_thickness * 3, mode=Mode.SUBTRACT)
    with Locations(
        (mount_offset_x, mount_offset_y, 0),
        (-mount_offset_x, mount_offset_y, 0),
        (-mount_offset_x, -mount_offset_y, 0),
        (mount_offset_x, -mount_offset_y, 0)
    ):
        Cylinder(radius=mount_hole_diameter/2, height=board_thickness * 3, mode=Mode.SUBTRACT)

# --- B. Lišty pro ESP8266 ---
esp_header_pitch = 9 * hole_pitch 
with BuildPart() as esp_headers:
    with Locations((esp_header_pitch/2, 0, 0), (-esp_header_pitch/2, 0, 0)):
        Box(2.54, 15 * hole_pitch, 8.5, align=(Align.CENTER, Align.CENTER, Align.MIN)) 

# --- C. TP4056 Modul ---
with BuildPart() as tp4056:
    Box(17, 26, 4, align=(Align.CENTER, Align.CENTER, Align.MIN)) 
    with Locations((0, -13 - 1.5, 0)): 
        Box(9, 6, 3.2, align=(Align.CENTER, Align.CENTER, Align.MIN))

# --- D. Šroubovací terminály ---
with BuildPart() as terminals:
    Box(9, 20, 12, align=(Align.CENTER, Align.CENTER, Align.MIN)) 

# === E. DRŽÁK BATERIE 18650 ===
with BuildPart() as holder:
    Box(21.0, 75.0, 3.0, align=(Align.CENTER, Align.CENTER, Align.MIN)) 
    with Locations((0, 53.7/2, 0), (0, -53.7/2, 0)):
        Cylinder(radius=3.5/2, height=3, align=(Align.CENTER, Align.CENTER, Align.MIN), mode=Mode.SUBTRACT)
    with Locations((0, 75/2 - 1.5, 3), (0, -75/2 + 1.5, 3)):
        Box(21.0, 3.0, 18.0, align=(Align.CENTER, Align.CENTER, Align.MIN)) 
    Box(21.0, 75.0, 10.5, align=(Align.CENTER, Align.CENTER, Align.MIN))
    Box(19.0, 73.0, 10.5, align=(Align.CENTER, Align.CENTER, Align.MIN), mode=Mode.SUBTRACT)

# --- F. Pin headery (2x3) ---
with BuildPart() as wire_headers:
    Box(2.54*2, 2.54 * 3, 8.5, align=(Align.CENTER, Align.CENTER, Align.MIN))

# --- H. 4x 7-Segmentový displej ---
with BuildPart() as seven_seg:
    Box(30, 14, 6.3, align=(Align.CENTER, Align.CENTER, Align.MIN))

# --- I. Malý posuvný vypínač (SS-12D00G3) ---
with BuildPart() as slide_switch:
    Box(6.0, 5.5, 12.7, align=(Align.CENTER, Align.CENTER, Align.MIN)) 
    with Locations((0, -2.75 - 4.0, 12.7/2)): 
        Box(3.0, 8.0, 3.0, align=(Align.CENTER, Align.CENTER, Align.CENTER))

# --- J. LED diody (přesunuto na boční panel k tlačítkům) ---
with BuildPart() as panel_leds:
    with Locations((0, 0, 0), (0, button_spacing, 0), (0, 2*button_spacing, 0)):
        Cylinder(radius=5/2, height=5, align=(Align.CENTER, Align.CENTER, Align.MIN))
        with Locations((0, 0, 5)):
            Sphere(radius=5/2)

# --- K. Tlačítka PBS-33B ---
with BuildPart() as push_button:
    with Locations((0, 0, 0)):
        Cylinder(radius=12/2, height=18, align=(Align.CENTER, Align.CENTER, Align.MIN)) 
        with Locations((0, 0, 18)):
            Cylinder(radius=17.5/2, height=2.6, align=(Align.CENTER, Align.CENTER, Align.MIN)) 
            with Locations((0, 0, 2.6)):
                Cylinder(radius=11.8/2, height=5.5, align=(Align.CENTER, Align.CENTER, Align.MIN))

# --- N. Zvukový senzor KY-037 (Plně parametrický) ---
with BuildPart() as ky037:
    # Hlavní PCB
    Box(ky_pcb_length, ky_pcb_width, ky_pcb_thickness, align=(Align.CENTER, Align.CENTER, Align.MIN))
    
    # Mikrofon (orientovaný dopředu k pravé stěně)
    with Locations(Location((ky_pcb_length/2, 0, ky_pcb_thickness), (0, 0, 0))):
        Cylinder(radius=ky_mic_diameter/2, height=ky_mic_length, align=(Align.CENTER, Align.CENTER, Align.MIN))
    
    # Potenciometr (modrý trimr)
    with Locations((-ky_pcb_length/2 + 10, 0, ky_pcb_thickness)):
        Box(9, 4, 9, align=(Align.CENTER, Align.CENTER, Align.MIN))
        
    # Piny vzadu (4 pin header)
    with Locations((-ky_pcb_length/2 + 1.5, 0, ky_pcb_thickness)):
        Box(2.54, 10, 8, align=(Align.CENTER, Align.CENTER, Align.MIN))

# === VÝPOČTY POZIC (Souřadnice) ===
z_floor = wall_thickness
inner_bottom_y = -box_length/2 + wall_thickness
edge_y = inner_bottom_y 
switch_center_x = 27
switch_center_z = z_floor + 1.0 

btn_wall_x = box_width/2 - wall_thickness - 18.0 
btn_z = wall_thickness + (internal_height / 2) + 2.0 # Mírně zvednuto kvůli senzoru
led_z = btn_z + 9.0 # LEDky o 9 mm nad středem tlačítek

btn1_y = inner_bottom_y + button_start_offset_y
btn2_y = btn1_y + button_spacing
btn3_y = btn2_y + button_spacing

# === POZICE A DÍRA PRO KY-037 V KRABIČCE ===
# Tento blok patří tam, kde se počítají souřadnice (sekce "VÝPOČTY POZIC")
ky_x = box_width/2 - wall_thickness - (ky_pcb_length/2) - 2.0 # Mikrofon 2mm od vnitřní stěny
ky_y = btn2_y # Vycentrovaný uprostřed pod tlačítky
ky_mic_z = z_floor + ky_standoff_h + ky_pcb_thickness + ky_mic_center_z # Z souřadnice pro dírku ve stěně

# === L. KRABIČKA - Základna ===
with BuildPart() as base_enclosure:
    with BuildSketch():
        Rectangle(box_width, box_length)
        fillet(vertices(), radius=4.0)
    extrude(amount=box_height)
    
    with BuildSketch(Location((0, 0, wall_thickness))):
        Rectangle(box_width - 2*wall_thickness, box_length - 2*wall_thickness)
        fillet(vertices(), radius=2.0)
    extrude(amount=box_height, mode=Mode.SUBTRACT)
    
    # Sloupky pro PCB
    with Locations(
        (pcb_shift_x + mount_offset_x, mount_offset_y, wall_thickness),
        (pcb_shift_x - mount_offset_x, mount_offset_y, wall_thickness),
        (pcb_shift_x - mount_offset_x, -mount_offset_y, wall_thickness),
        (pcb_shift_x + mount_offset_x, -mount_offset_y, wall_thickness)
    ):
        Cylinder(radius=3.0, height=standoff_height, align=(Align.CENTER, Align.CENTER, Align.MIN))
        Cylinder(radius=1.2, height=standoff_height, align=(Align.CENTER, Align.CENTER, Align.MIN), mode=Mode.SUBTRACT)

    # Rohové sloupky pro víko
    with Locations(
        (box_width/2 - 4.5, box_length/2 - 4.5, wall_thickness),
        (-box_width/2 + 4.5, box_length/2 - 4.5, wall_thickness),
        (-box_width/2 + 4.5, -box_length/2 + 4.5, wall_thickness),
        (box_width/2 - 4.5, -box_length/2 + 4.5, wall_thickness)
    ):
        Cylinder(radius=3.5, height=box_height - wall_thickness - 2.0, align=(Align.CENTER, Align.CENTER, Align.MIN))
        Cylinder(radius=1.2, height=box_height, align=(Align.CENTER, Align.CENTER, Align.MIN), mode=Mode.SUBTRACT)

    # Horizontální sloupky pro vypínač
    with Locations(
        Location((switch_center_x - switch_hole_pitch/2, edge_y + switch_standoff_h/2, switch_center_z + 12.7/2), (90, 0, 0)),
        Location((switch_center_x + switch_hole_pitch/2, edge_y + switch_standoff_h/2, switch_center_z + 12.7/2), (90, 0, 0))
    ):
        Cylinder(radius=3.5, height=switch_standoff_h)
        Cylinder(radius=switch_insert_dia/2, height=switch_standoff_h, mode=Mode.SUBTRACT)

    # Sloupky pro zvukový senzor KY-037
    with Locations(
        (ky_x - ky_hole_spacing/2, ky_y, wall_thickness),
        (ky_x + ky_hole_spacing/2, ky_y, wall_thickness)
    ):
        Cylinder(radius=3.0, height=ky_standoff_h, align=(Align.CENTER, Align.CENTER, Align.MIN))
        Cylinder(radius=1.2, height=ky_standoff_h, align=(Align.CENTER, Align.CENTER, Align.MIN), mode=Mode.SUBTRACT)

    # DÍRY SKRZ STĚNU (Pro tlačítka, LED a mikrofon)
    with Locations(
        Location((box_width/2, btn1_y, btn_z), (0, 90, 0)),
        Location((box_width/2, btn2_y, btn_z), (0, 90, 0)),
        Location((box_width/2, btn3_y, btn_z), (0, 90, 0))
    ):
        Cylinder(radius=12.2/2, height=wall_thickness*3, mode=Mode.SUBTRACT, align=(Align.CENTER, Align.CENTER, Align.CENTER))

    with Locations(
        Location((box_width/2, btn1_y, led_z), (0, 90, 0)),
        Location((box_width/2, btn2_y, led_z), (0, 90, 0)),
        Location((box_width/2, btn3_y, led_z), (0, 90, 0))
    ):
        Cylinder(radius=5.2/2, height=wall_thickness*3, mode=Mode.SUBTRACT, align=(Align.CENTER, Align.CENTER, Align.CENTER))

    with Locations(Location((box_width/2, ky_y, ky_mic_z), (0, 90, 0))):
        Cylinder(radius=3.5, height=wall_thickness*3, mode=Mode.SUBTRACT, align=(Align.CENTER, Align.CENTER, Align.CENTER))

# === M. KRABIČKA - Víko ===
with BuildPart() as top_lid:
    with BuildSketch():
        Rectangle(box_width, box_length)
        fillet(vertices(), radius=4.0)
    extrude(amount=2.0)
    
    with Locations(
        (box_width/2 - 4.5, box_length/2 - 4.5, 2.0),
        (-box_width/2 + 4.5, box_length/2 - 4.5, 2.0),
        (-box_width/2 + 4.5, -box_length/2 + 4.5, 2.0),
        (box_width/2 - 4.5, -box_length/2 + 4.5, 2.0)
    ):
        Cylinder(radius=3.4/2, height=2.0, align=(Align.CENTER, Align.CENTER, Align.MAX), mode=Mode.SUBTRACT)
        Cone(bottom_radius=3.4/2, top_radius=6.2/2, height=1.4, align=(Align.CENTER, Align.CENTER, Align.MAX), mode=Mode.SUBTRACT)

# === POZICOVÁNÍ SOUČÁSTEK DO KRABIČKY ===
z_pcb_bottom = z_floor + standoff_height
z_surface = z_pcb_bottom + board_thickness

loc_pcb       = Location((pcb_shift_x, 0, z_pcb_bottom + board_thickness/2)) 
loc_holder    = Location((pcb_shift_x + holder_pos_x, actual_holder_y, z_surface))
loc_esp       = Location((pcb_shift_x + esp_pos_x, -(hole_pitch * rows/2)/2+4, z_surface))

terminal_y = -(hole_pitch * rows/2)/2 - hole_pitch - 1.4 + (4 * hole_pitch)
terminal_x = pcb_shift_x + esp_pos_x + 7 * hole_pitch
loc_terminals = Location((terminal_x, terminal_y, z_surface))

led_y = terminal_y + (20/2) + 5
loc_wires = Location((terminal_x, led_y, z_surface))

seg_x = pcb_shift_x + board_width/2 - (30/2) - 1.0 
seg_y = actual_holder_y + (75.0 / 2) - (14.0 / 2)
loc_7seg = Location((seg_x, seg_y, box_height-5)) 

loc_tp4056    = Location((0, edge_y + 13, z_floor))
loc_switch    = Location((switch_center_x, edge_y + switch_standoff_h + 2.75, switch_center_z)) 

loc_btn1 = Location((btn_wall_x, btn1_y, btn_z), (0, 90, 0))
loc_btn2 = Location((btn_wall_x, btn2_y, btn_z), (0, 90, 0))
loc_btn3 = Location((btn_wall_x, btn3_y, btn_z), (0, 90, 0))

# LEDky kousek zapuštěné ve stěně nad tlačítky
loc_panel_leds = Location((box_width/2 - wall_thickness - 10.0, btn1_y - 10.0, led_z), (0, 0, 0)) 
loc_ky037 = Location((ky_x + 10.0, ky_y + 14.0, z_floor + ky_standoff_h), (0, 0, 270))

loc_lid = Location((0, 0, box_height))
loc_enclosure = Location((0, 0, 0))

show(
    loc_enclosure * base_enclosure.part,
    loc_lid * top_lid.part,
    loc_pcb * pcb.part,
    loc_esp * esp_headers.part,
    loc_tp4056 * tp4056.part,
    loc_terminals * terminals.part,
    loc_holder * holder.part,
    loc_wires * wire_headers.part,
    loc_7seg * seven_seg.part,
    loc_switch * slide_switch.part,
    loc_panel_leds * panel_leds.part,
    loc_btn1 * push_button.part,
    loc_btn2 * push_button.part,
    loc_btn3 * push_button.part,
    loc_ky037 * ky037.part,
    names  = ["Základna krabičky", "Víko", "PCB", "ESP8266", "TP4056", "Terminály", "Držák 18650", "LED Headery", "7-Segment", "Vypínač", "LED u tlačítek", "Tlačítko 1", "Tlačítko 2", "Tlačítko 3", "KY-037 Senzor"],
    colors = ["#cccccc", "#eeeeee", "#058d17", "black", "#070338", "blue", "gray", "black", "#aa0000", "#111111", "yellow", "blue", "red", "green", "#cc3333"],
    alphas = [0.4, 0.4, 0.9, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.9, 1.0, 1.0, 1.0, 1.0] 
)
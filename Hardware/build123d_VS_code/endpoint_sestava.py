from build123d import *
from ocp_vscode import show
import sys

# ==========================================
# ==========================================
# --- HLAVNÍ PARAMETRY MODELU ---
# ==========================================
# ==========================================

# --- 1. GLOBÁLNÍ A TISKOVÉ PARAMETRY ---
overshoot = 1.0              # Bezpečný přesah pro čisté vyřezávání děr (matematická nutnost CADu)
tol_edge = 0.5               # Tolerance pro vyhledávání hran (při zaoblování)
cut_oversize = 50.0          # Velikost pracovního "sekáčku" pro zaříznutí přesahujících uší

wall = 2.5                   # Základní tloušťka stěn krabiček
base_th = 3.0                # Tloušťka dna krabiček a kotevních uší
lid_th = 3.0                 # Základní tloušťka vík
corner_r = 5.0               # Poloměr vnějších rohů krabiček

# --- 2. SPOJOVACÍ MATERIÁL (Šrouby, Inzerty) ---
# Mosazné inzerty (zálisky)
ins_d, ins_h = 4.2, 5.5      # Průměr a výška díry pro zatavení inzertu
ins_cone_h = 0.6             # Výška náběžného zkosení (usnadňuje zavádění)
ins_cone_r_ext = 0.5         # Rozšíření poloměru na vršku inzertu

# Šrouby M3 (Víka, Uši krabiček)
m3_pass_r = 3.4 / 2          # Poloměr průchozí díry pro závit M3 (volnější)
m3_head_r = 6.5 / 2          # Poloměr vybrání pro hlavičku šroubu M3
m3_head_h = 1.6              # Výška/hloubka zapuštění hlavičky M3 (zkosení)
m3_lid_hole_r = 1.8          # Vnitřní poloměr dírky ve víku (3.6mm průměr)

# Šrouby M4 (Montážní plát)
m4_pass_r = 4.5 / 2          # Poloměr průchozí díry pro závit M4
m4_head_r = 8.5 / 2          # Poloměr zapuštění pro hlavičku M4
m4_head_h = 2.5              # Hloubka zapuštění hlavičky M4

# --- 3. PARAMETRY ZÁMKŮ VÍKA (LIP) ---
lip_clearance = 0.2          # Vůle mezi víkem a vnitřní stěnou krabičky (aby to šlo zavřít)
lip_width = 1.4              # Tloušťka samotného lemu víka
lip_height = 4.5             # Jak hluboko zasahuje víko do krabičky
lip_chamfer = 0.4            # Zkosení hrany víka pro snadné nasazení

# --- 4. KONEKTORY GX12 ---
gx12_spacing = 18.0          # Rozteč mezi dvěma konektory
gx12_hole_r = 11.8 / 2       # Hlavní poloměr závitu konektoru
gx12_flat_dist = 5.4         # Vzdálenost "D" plošky od středu (zabraňuje protáčení)
gx12_cut_w = 15.0            # Šířka pomocného kvádru pro výřez D-plošky
gx12_cut_h = 5.0             # Výška pomocného kvádru pro výřez D-plošky

# --- 5. PODPĚRNÉ SLOUPKY (BOSSES) ---
boss_r = 4.0                 # Poloměr nosných sloupků pro víka
sup_total_h = 15.0           # Celková výška vyvýšených podpěr (left/right boss)
boss_z_chamfer = 7.9         # Zkosení vyvýšené podpěry (výztuha)
boss_z_fillet = 3.99         # Zaoblení vyvýšené podpěry
pcb_boss_r = 3.5             # Poloměr plastového sloupku pod PCB
pcb_hole_r = 1.0             # Poloměr dírky pro vrut do PCB
pcb_standoff_h = 5.0         # Výška podložení PCB od dna
pcb_hole_depth = 5.0         # Jak hluboko jde šroub do sloupku PCB

# ==========================================
# --- SPECIFICKÉ PARAMETRY JEDNOTLIVÝCH DÍLŮ ---
# ==========================================

# A) OBROVITÁNSKÁ KRABIČKA (Battery box)
w, l, h_box = 50.0, 100.0, 35.0
batt_tab_r = 5.0             # Rádius montážních uší
batt_tab_extend = 15.0       # Jak daleko od dírky sahá hmota ucha směrem ke krabičce
batt_fillet_r = 5.0          # Zaoblení přechodu uší do krabičky

dx, dy = (w/2 - 1.5), (l/2 - 1.5) 
screw_locs = [(dx, dy), (-dx, dy), (dx, -dy), (-dx, -dy), (dx, 0), (-dx, 0)]

batt_x, batt_y_offset, batt_y_hole = 11.5, 7.0, 53.7 / 2
batt_locs = [
    (-batt_x, batt_y_hole + batt_y_offset), (-batt_x, -batt_y_hole + batt_y_offset),  
    (batt_x, batt_y_hole + batt_y_offset), (batt_x, -batt_y_hole + batt_y_offset)     
]

# B) LOGICKÁ KRABIČKA
log_w, log_l, log_h = 42.0, 180.0, 32.0  
tube_offset = 30.0
target_shift_y = 32.0 + wall + tube_offset  # led_l + wall + ...
log_tab_r = 5.0              # Rádius kotevních uší logic boxu
log_tab_chamfer = 7.0        # Velikost zkosení/výztuhy uší
log_under_chamfer = 7.0      # Velikost zkosení "v podpaží" (vyhlazení rohu)
log_lid_fillet = 2.5         # Zaoblení vyčnívajících uší na víku
log_tab_width = 10.0         # Šířka kotevního ucha

# Výpočet pozic Logic Boxu
grid_pitch = 15.0  
log_global_y = round(target_shift_y / grid_pitch) * grid_pitch
log_tab_y_top = 75.0 - log_global_y 
log_tab_y_bottom = log_tab_y_top - 75.0 
log_tab_x = 37.5
log_tab_locs = [
    (log_tab_x, log_tab_y_top), (-log_tab_x, log_tab_y_top), 
    (log_tab_x, log_tab_y_bottom), (-log_tab_x, log_tab_y_bottom)
]

pcb_w, pcb_l = 34.0, 146.0
pcb_y_center = 12.0
pcb_hole_dy = (pcb_l / 2)
log_pcb_locs = [
    (pcb_w/4, pcb_y_center + pcb_hole_dy - 32.0 - 10), 
    (-pcb_w/4, pcb_y_center - pcb_hole_dy + 62.0 + 10)
]

# C) MONTÁŽNÍ PLÁT (Grid Plate)
mount_th = 6.0 
mount = 85.0
mount_corner_holes = [(mount, mount), (-mount, mount), (mount, -mount), (-mount, -mount)] 
mount_holes_x, mount_holes_y = 6, 6 
tab_x, tab_y = ((mount_holes_x-1)*grid_pitch)/2, ((mount_holes_y-1)*grid_pitch)/2 
tab_locs = [(tab_x, tab_y), (-tab_x, tab_y), (tab_x, -tab_y), (-tab_x, -tab_y)]

# ==========================================
# POSUNY PRO SESTAVU (Nutné pro výpočet chytrého plátu)
# ==========================================
batt_shift_x = -37.5
batt_shift_y = 7.5

# ==========================================
# DRŽÁKY KONEKTORU (Zvýšené sloupky)
# ==========================================
with BuildPart() as right_boss:
    Box(boss_r*2, boss_r*2, sup_total_h, align=(Align.CENTER, Align.CENTER, Align.MIN))
    chamfer(right_boss.edges().filter_by(Axis.Y).group_by(Axis.X)[0].sort_by(Axis.Z)[0], length=boss_z_chamfer)
    fillet(right_boss.edges().filter_by(Axis.Z).group_by(Axis.X)[0], radius=boss_z_fillet)

with BuildPart() as left_boss:
    Box(boss_r*2, boss_r*2, sup_total_h, align=(Align.CENTER, Align.CENTER, Align.MIN))
    chamfer(left_boss.edges().filter_by(Axis.Y).group_by(Axis.X)[-1].sort_by(Axis.Z)[0], length=boss_z_chamfer)
    fillet(left_boss.edges().filter_by(Axis.Z).group_by(Axis.X)[-1], radius=boss_z_fillet)

# ==========================================
# 1. OBROVITÁNSKÁ KRABIČKA S UŠIMA (BATTERY BOX)
# ==========================================
with BuildPart() as box:
    # Základna a uši
    with BuildSketch():
        RectangleRounded(w + 2*wall, l + 2*wall, radius=corner_r)
        for loc in tab_locs:
            c1 = Location(loc) * Circle(radius=batt_tab_r, mode=Mode.PRIVATE)
            anchor_x = loc[0] - batt_tab_extend if loc[0] > 0 else loc[0] + batt_tab_extend
            c2 = Location((anchor_x, loc[1])) * Circle(radius=batt_tab_r, mode=Mode.PRIVATE)
            add(make_hull(c1.edges() + c2.edges()))       
    extrude(amount=base_th)

    # Obvodové stěny
    with BuildSketch(box.faces().sort_by(Axis.Z)[-1]):
        RectangleRounded(w + 2*wall, l + 2*wall, radius=corner_r)
        RectangleRounded(w, l, radius=corner_r - wall, mode=Mode.SUBTRACT)
    extrude(amount=h_box - base_th)

    # Zaoblení uší k tělu krabičky
    stena_x = (w / 2) + wall 
    
    # A) Zaoblení uší "k podložce" (svislé hrany, osa Z)
    z_edges = box.edges().filter_by(Axis.Z).filter_by_position(Axis.Z, 0, base_th)
    inner_z_edges = z_edges.filter_by_position(Axis.X, stena_x - tol_edge, stena_x + tol_edge) + \
                    z_edges.filter_by_position(Axis.X, -stena_x - tol_edge, -stena_x + tol_edge)
    valid_z_edges = [e for e in inner_z_edges if e.length > 1.0]
    if valid_z_edges: fillet(valid_z_edges, radius=batt_fillet_r)
        
    # B) Zaoblení uší "ke stěně" (shora, hrany v ose Y)
    y_edges_base = box.edges().filter_by(Axis.Y).filter_by_position(Axis.Z, base_th, base_th)
    root_edges = y_edges_base.filter_by_position(Axis.X, stena_x - tol_edge, stena_x + tol_edge) + \
                 y_edges_base.filter_by_position(Axis.X, -stena_x - tol_edge, -stena_x + tol_edge)
    valid_root_edges = [e for e in root_edges if e.length > 1.0]
    if valid_root_edges: fillet(valid_root_edges, radius=batt_fillet_r) 

    # Sloupky v krabičce
    for i, loc in enumerate(screw_locs):
        if i < 4:
            with Locations((loc[0], loc[1], base_th)):
                Cylinder(radius=boss_r, height=h_box - base_th, align=(Align.CENTER, Align.CENTER, Align.MIN))
        else:
            with Locations((loc[0], loc[1], h_box - sup_total_h)):
                if loc[0] > 0: add(right_boss.part)
                else: add(left_boss.part)
                
    # Díry na inzerty ve sloupcích
    with Locations(screw_locs):
        with Locations((0, 0, h_box + overshoot)):
            Cylinder(radius=ins_d/2, height=ins_h + overshoot, align=(Align.CENTER, Align.CENTER, Align.MAX), mode=Mode.SUBTRACT)
            Cone(bottom_radius=ins_d/2, top_radius=ins_d/2 + ins_cone_r_ext, height=ins_cone_h + overshoot, align=(Align.CENTER, Align.CENTER, Align.MAX), mode=Mode.SUBTRACT)

    # Díry v montážních uších pro přišroubování k podložce (M3)
    with Locations(tab_locs):
        with Locations((0, 0, base_th + overshoot)):
            Cylinder(radius=m3_pass_r, height=base_th + overshoot * 2, align=(Align.CENTER, Align.CENTER, Align.MAX), mode=Mode.SUBTRACT)
            Cone(bottom_radius=m3_pass_r, top_radius=m3_head_r, height=m3_head_h, align=(Align.CENTER, Align.CENTER, Align.MAX), mode=Mode.SUBTRACT)

    # Držáky pro baterii
    with Locations(batt_locs):
        with Locations((0, 0, base_th)):
            Cylinder(radius=4.0, height=5.0, align=(Align.CENTER, Align.CENTER, Align.MIN))
            with Locations((0, 0, 5.0 + overshoot)):
                Cylinder(radius=1.25, height=5.0 + overshoot, align=(Align.CENTER, Align.CENTER, Align.MAX), mode=Mode.SUBTRACT)

    # Otvor pro konektor GX12
    conn_z = base_th + h_box/2  
    conn_plane = Plane(origin=(0, -l/2, conn_z), x_dir=(1, 0, 0), z_dir=(0, -1, 0))
    with BuildSketch(conn_plane): 
        Circle(gx12_hole_r) 
        with Locations((0, gx12_flat_dist + overshoot/2)): Rectangle(gx12_cut_w, gx12_cut_h + overshoot, align=(Align.CENTER, Align.MIN), mode=Mode.SUBTRACT) 
        with Locations((0, -gx12_flat_dist - overshoot/2)): Rectangle(gx12_cut_w, gx12_cut_h + overshoot, align=(Align.CENTER, Align.MAX), mode=Mode.SUBTRACT) 
    extrude(amount=wall + 5.0, both=True, mode=Mode.SUBTRACT)

# ==========================================
# 2. TENKÉ VÍKO
# ==========================================
with BuildPart() as lid:
    with BuildSketch():
        RectangleRounded(w + 2*wall, l + 2*wall, radius=corner_r)
    extrude(amount=lid_th)
    
    # Zámek víka
    with BuildSketch(mode=Mode.PRIVATE) as inner_profile:
        RectangleRounded(w, l, radius=corner_r - wall)
        with Locations(screw_locs):
            Circle(boss_r, mode=Mode.SUBTRACT) 
            
    with BuildSketch(lid.faces().sort_by(Axis.Z)[0]):
        offset(inner_profile.sketch, amount=-lip_clearance)
        offset(inner_profile.sketch, amount=-(lip_clearance + lip_width), mode=Mode.SUBTRACT)
    extrude(amount=lip_height)
    lip_bottom_face = lid.faces().sort_by(Axis.Z)[0]
    chamfer(lip_bottom_face.edges(), length=lip_chamfer)

    # Díry na šrouby M3 ve víku
    with Locations(screw_locs):
        Cylinder(radius=m3_lid_hole_r, height=10.0, align=(Align.CENTER, Align.CENTER, Align.CENTER), mode=Mode.SUBTRACT)
        with Locations((0, 0, lid_th + overshoot)):
            Cone(bottom_radius=m3_lid_hole_r, top_radius=m3_pass_r, height=m3_head_h + overshoot, align=(Align.CENTER, Align.CENTER, Align.MAX), mode=Mode.SUBTRACT)

# ==========================================
# 3. UNIVERZÁLNÍ ČTVERCOVÝ PLÁT (11x11 Grid, okraje + střed)
# ==========================================
grid_span = 10*grid_pitch/2   # Rozpětí od středu do kraje (celkem 100 mm)

unique_holes = set()
# A) 3 svislé sloupce X
for x in [-grid_span, 0.0, grid_span]:
    for y in range(int(-grid_span), int(grid_span) + 1, int(grid_pitch)):
        unique_holes.add((float(x), float(y)))

# B) 3 vodorovné řady (na Y = -50, 0, 50)
for y in [-grid_span, 0.0, grid_span]:
    for x in range(int(-grid_span), int(grid_span) + 1, int(grid_pitch)):
        unique_holes.add((float(x), float(y)))
        
unique_holes = list(unique_holes)

# Extrémně malý okraj – pouze 6 mm k obalení krajních inzertů
pad = 6.0 
plate_size = (grid_span * 2) + 2 * pad

with BuildPart() as mount_plate:
    # 1. Hlavní tělo plátu 
    with BuildSketch():
        RectangleRounded(plate_size, plate_size, radius=corner_r)
    extrude(amount=mount_th)
    
    # 2. Dírky pro mosazné inzerty (kříž)
    with Locations(unique_holes):
        with Locations((0, 0, mount_th + overshoot)):
            Cylinder(radius=ins_d/2, height=ins_h + overshoot, align=(Align.CENTER, Align.CENTER, Align.MAX), mode=Mode.SUBTRACT)

    # 3. Kotevní M4 dírky přesně uprostřed 4 vzniklých oken
    m4_hidden_holes = [
        (-plate_size/4, -plate_size/4),
        (-plate_size/4, plate_size/4),
        (plate_size/4, -plate_size/4),
        (plate_size/4, plate_size/4)
    ]
    with Locations(m4_hidden_holes):
        Cylinder(radius=m4_pass_r, height=mount_th + overshoot * 2, mode=Mode.SUBTRACT)
        # Zapuštění pro hlavičku šroubu M4
        with Locations((0, 0, mount_th + overshoot)):
            Cone(bottom_radius=m4_pass_r, top_radius=m4_head_r, height=m4_head_h + overshoot, align=(Align.CENTER, Align.CENTER, Align.MAX), mode=Mode.SUBTRACT)
                                    
# ==========================================
# 4. LOGICKÁ KRABIČKA 
# ==========================================
with BuildPart() as logic_box:
    # 1. Dno krabičky
    with BuildSketch():
        RectangleRounded(log_w + 2*wall, log_l + 2*wall, radius=corner_r)
    extrude(amount=base_th)
    
    # 2. Obvodové stěny
    with BuildSketch(logic_box.faces().sort_by(Axis.Z)[-1]):
        RectangleRounded(log_w + 2*wall, log_l + 2*wall, radius=corner_r)
        RectangleRounded(log_w, log_l, radius=corner_r - wall, mode=Mode.SUBTRACT)
    extrude(amount=log_h - base_th)

    # 3. Přidání uší
    log_stena_x = (log_w / 2) + wall 
    with BuildSketch(Plane(origin=(0, 0, log_h))):
        for loc in log_tab_locs:
            c1 = Location(loc) * Circle(radius=log_tab_r, mode=Mode.PRIVATE)
            sign = 1 if loc[0] > 0 else -1
            anchor_x = sign * (log_stena_x - wall / 2)
            c2 = Location((anchor_x, loc[1])) * Rectangle(1.0, log_tab_width, mode=Mode.PRIVATE)
            add(make_hull(c1.edges() + c2.edges()))
    extrude(amount=-base_th) 

    # 4. Zkosení uší ke stěně
    z_spodek_usi = log_h - base_th
    hrany_y = logic_box.edges().filter_by(Axis.Y).filter_by_position(Axis.Z, z_spodek_usi - 0.1, z_spodek_usi + 0.1)
    styk_hrany = hrany_y.filter_by_position(Axis.X, log_stena_x - 0.1, log_stena_x + 0.1) + \
                 hrany_y.filter_by_position(Axis.X, -log_stena_x - 0.1, -log_stena_x + 0.1)
    try:
        valid_styk = [e for e in styk_hrany if e.length > 1.0]
        if valid_styk: chamfer(valid_styk, length=log_tab_chamfer)
    except: pass

    # 5. Zkosení podpaží
    log_z_edges = logic_box.edges().filter_by(Axis.Z)
    log_inner_z_edges = log_z_edges.filter_by_position(Axis.X, log_stena_x - 1, log_stena_x + 1) + \
                        log_z_edges.filter_by_position(Axis.X, -log_stena_x - 1, -log_stena_x + 1)
    log_inner_z_edges = log_inner_z_edges.filter_by_position(Axis.Z, z_spodek_usi - log_tab_chamfer - 1.0, log_h + 0.1)
    try:
        valid_inner = [e for e in log_inner_z_edges if e.length > 1.0]
        if valid_inner: chamfer(valid_inner, length=log_under_chamfer)
    except: pass

    # 6. Odstranění přesahů uší (Zarovnání ořezem)
    cut_x = log_stena_x + log_tab_chamfer + 0.1 
    with Locations((cut_x + cut_oversize/2, 0, log_h/2)):
        Box(cut_oversize, log_l + cut_oversize, log_h + cut_oversize, mode=Mode.SUBTRACT)
    with Locations((-cut_x - cut_oversize/2, 0, log_h/2)):
        Box(cut_oversize, log_l + cut_oversize, log_h + cut_oversize, mode=Mode.SUBTRACT)

    # 7. Sloupky a vnitřek
    log_screw_dx, log_screw_dy = log_w/2 - 1.5, log_l/2 - 1.5
    log_screw_locs_corners = [
        (log_screw_dx, log_screw_dy), (-log_screw_dx, log_screw_dy), 
        (log_screw_dx, -log_screw_dy), (-log_screw_dx, -log_screw_dy)
    ]
    log_screw_locs_mid = [(log_screw_dx, 30), (-log_screw_dx, 30), (log_screw_dx, -30), (-log_screw_dx, -30)]
    log_screw_locs = log_screw_locs_corners + log_screw_locs_mid
    
    with Locations(log_screw_locs_corners):
        with Locations((0, 0, base_th)):
            Cylinder(radius=boss_r, height=log_h - base_th, align=(Align.CENTER, Align.CENTER, Align.MIN))

    for loc in log_screw_locs_mid:
        with Locations((loc[0], loc[1], log_h - sup_total_h)):
            if loc[0] > 0: add(right_boss.part)
            else: add(left_boss.part)

    with Locations(log_screw_locs):
        with Locations((0, 0, log_h + overshoot)):
            Cylinder(radius=ins_d/2, height=ins_h + overshoot, align=(Align.CENTER, Align.CENTER, Align.MAX), mode=Mode.SUBTRACT)
            Cone(bottom_radius=ins_d/2, top_radius=ins_d/2 + ins_cone_r_ext, height=ins_cone_h + overshoot, align=(Align.CENTER, Align.CENTER, Align.MAX), mode=Mode.SUBTRACT)

    # Sloupky pro PCB
    with Locations(log_pcb_locs):
        with Locations((0, 0, base_th)):
            Cylinder(radius=pcb_boss_r, height=pcb_standoff_h, align=(Align.CENTER, Align.CENTER, Align.MIN))
            with Locations((0, 0, pcb_standoff_h + overshoot)):
                Cylinder(radius=pcb_hole_r, height=pcb_hole_depth + overshoot, align=(Align.CENTER, Align.CENTER, Align.MAX), mode=Mode.SUBTRACT)

    # Otvory pro GX12
    log_conn_z = base_th + log_h/2 
    log_conn_plane = Plane(origin=(0, -log_l/2, log_conn_z), x_dir=(1, 0, 0), z_dir=(0, -1, 0))
    with BuildSketch(log_conn_plane):
        with Locations((-gx12_spacing/2, 0), (gx12_spacing/2, 0)):
            Circle(gx12_hole_r)
            with Locations((0, gx12_flat_dist + overshoot / 2)): Rectangle(gx12_cut_w + overshoot, gx12_cut_h + overshoot, align=(Align.CENTER, Align.MIN), mode=Mode.SUBTRACT)
            with Locations((0, -gx12_flat_dist - overshoot / 2)): Rectangle(gx12_cut_w + overshoot, gx12_cut_h + overshoot, align=(Align.CENTER, Align.MAX), mode=Mode.SUBTRACT)
    extrude(amount=wall + 5.0, both=True, mode=Mode.SUBTRACT)

# ==========================================
# 5. ZADNÍ VÍKO LOGICKÉ KRABIČKY
# ==========================================
with BuildPart() as logic_lid:
    with BuildSketch():
        RectangleRounded(log_w + 2*wall, log_l + 2*wall, radius=corner_r)
        # Profil pro zakrytí zkosených uší krabičky
        for loc in log_tab_locs:
            c1 = Location(loc) * Circle(radius=log_tab_r, mode=Mode.PRIVATE)
            anchor_x = log_stena_x if loc[0] > 0 else -log_stena_x
            c2 = Location((anchor_x, loc[1])) * Circle(radius=log_tab_r, mode=Mode.PRIVATE)
            add(make_hull(c1.edges() + c2.edges()))
    extrude(amount=lid_th)
    
    # Zaoblení přečnívajících uší
    try:
        log_root_edges = logic_lid.edges().filter_by(Axis.Z).filter_by_position(Axis.X, log_stena_x - 2, log_stena_x + 2) + \
                         logic_lid.edges().filter_by(Axis.Z).filter_by_position(Axis.X, -log_stena_x - 2, -log_stena_x + 2)
        if log_root_edges: fillet(log_root_edges, radius=log_lid_fillet)
    except: pass

    # Dírky v uších (M3)
    with Locations(log_tab_locs):
        Cylinder(radius=m3_pass_r, height=lid_th + overshoot*4, mode=Mode.SUBTRACT)

    # Zámek víka
    with BuildSketch(mode=Mode.PRIVATE) as log_inner_profile:
        RectangleRounded(log_w, log_l, radius=corner_r - wall)
        with Locations(log_screw_locs):
            Circle(boss_r, mode=Mode.SUBTRACT) 
            
    with BuildSketch(logic_lid.faces().sort_by(Axis.Z)[-1]):
        offset(log_inner_profile.sketch, amount=-lip_clearance)
        offset(log_inner_profile.sketch, amount=-(lip_clearance + lip_width), mode=Mode.SUBTRACT)
    extrude(amount=lip_height)
    
    log_lip_top_face = logic_lid.faces().sort_by(Axis.Z)[-1]
    chamfer(log_lip_top_face.edges(), length=lip_chamfer)
            
    # Dírky ve víku pro sloupky (M3)
    with Locations(log_screw_locs):
        Cylinder(radius=m3_lid_hole_r, height=lid_th + 5.0 + overshoot*2, mode=Mode.SUBTRACT)
        with Locations((0, 0, -overshoot)):
            Cone(bottom_radius=m3_pass_r, top_radius=m3_lid_hole_r, height=m3_head_h + overshoot, align=(Align.CENTER, Align.CENTER, Align.MIN), mode=Mode.SUBTRACT)

# ==========================================
# 6. DUMMY MODELY (Jen pro náhled, neměníme)
# ==========================================
with BuildPart() as holder:
    Box(21.0, 75.0, 3.0, align=(Align.CENTER, Align.CENTER, Align.MIN)) 
    with Locations((0, 53.7/2, 0), (0, -53.7/2, 0)):
        Cylinder(radius=3.5/2, height=3, align=(Align.CENTER, Align.CENTER, Align.MIN), mode=Mode.SUBTRACT)
    with Locations((0, 75/2 - 1.5, 3), (0, -75/2 + 1.5, 3)):
        Box(21.0, 3.0, 18.0, align=(Align.CENTER, Align.CENTER, Align.MIN)) 
    Box(21.0, 75.0, 10.5, align=(Align.CENTER, Align.CENTER, Align.MIN))
    Box(19.0, 73.0, 10.5, align=(Align.CENTER, Align.CENTER, Align.MIN), mode=Mode.SUBTRACT)

with BuildPart() as conn:
    out_wall_y = -l/2 - wall
    cp_out = Plane(origin=(0, out_wall_y, conn_z), x_dir=(1, 0, 0), z_dir=(0, -1, 0))
    with BuildSketch(cp_out): Circle(15/2)
    extrude(amount=2) 
    with BuildSketch(cp_out):
        Circle(11.5/2) 
        with Locations((0, 5.25)): Rectangle(15, 5, align=(Align.CENTER, Align.MIN), mode=Mode.SUBTRACT)
        with Locations((0, -5.25)): Rectangle(15, 5, align=(Align.CENTER, Align.MAX), mode=Mode.SUBTRACT)   
    extrude(amount=-11) 

with BuildPart() as pcb_dummy:
    with BuildSketch():
        Rectangle(34, 146)
    extrude(amount=1.6)
    pcb_bottom = -73.0 
    with Locations((0, pcb_bottom + 62/2, 1.6)):
        Box(30, 62, 12, align=(Align.CENTER, Align.CENTER, Align.MIN))
    with Locations((0, pcb_bottom + 117 + 32/2, 1.6)):
        Box(32, 32, 16, align=(Align.CENTER, Align.CENTER, Align.MIN))

    # ==========================================
    # 7. ASSEMBLY ZOBRAZENÍ (Pro OCP CAD Viewer)
    # ==========================================
    loc_logic_flip = Location((log_tab_x, log_global_y, log_h + lid_th), (0, 180, 0))

if __name__ == "__main__":
    # Bezpečné zavolání zobrazení (aby skript nespadl mimo VSCode)
    if 'show' in globals():
        show(
            Location((0, 0, -mount_th)) * mount_plate.part, 
            Location((batt_shift_x, batt_shift_y, 0)) * box.part,
            Location((-batt_x + batt_shift_x, batt_y_offset + batt_shift_y, base_th + 5)) * holder.part,
            Location((batt_x + batt_shift_x, batt_y_offset + batt_shift_y, base_th + 5)) * holder.part,
            Location((batt_shift_x, batt_shift_y, 0)) * conn.part,
            Location((batt_shift_x, batt_shift_y, h_box)) * lid.part,
            
            Location((log_tab_x, log_global_y, 0)) * logic_lid.part, 
            loc_logic_flip * logic_box.part,
            loc_logic_flip * Location((0, pcb_y_center, base_th + pcb_standoff_h)) * pcb_dummy.part,

            names=[
                "Montazni_Plat", "Battery_box", "Levy_Drzak", "Pravy_Drzak", "Konektor", "Tenke_Viko", 
                "Logic_viko_Pruhledne", "Logic_box", "Dummy_PCB"
            ],
            colors=[
                "#9b59b6", "#e6b800", "#333333", "#333333", "#ff3333", "#555555", 
                "#a2d2ff", "#4ea8de", "#00ff00"
            ],
            alphas=[0.8, 0.4, 0.9, 0.9, 1.0, 0.8, 0.4, 0.5, 1.0] 
        )

    # ==========================================
    # 8. INTERAKTIVNÍ EXPORT DO SOUBORŮ (Po spuštění v terminálu)
    # ==========================================
    # Seznam tiskutelných dílů pro export
    export_parts = {
        "1_Battery_Box": box.part,
        "2_Battery_Lid": lid.part,
        "3_Logic_Box": logic_box.part,
        "4_Logic_Lid": logic_lid.part,
        "5_Mount_Plate": mount_plate.part
    }

    print("\n" + "="*50)
    print("  MODELOVÁNÍ DOKONČENO! Přejít k exportu?")
    print("="*50)

    # Ošetříme pád, pokud by to někdo spouštěl v prostředí bez terminálu
    try:
        volba = input("Chceš spustit interaktivní uložení dílů do STL/STEP? (y/n): ").strip().lower()

        if volba == 'y':
            print("\nOdpověz 'y' pro uložení dílu, nebo cokoliv jiného pro přeskočení.")
            for nazev, objekt in export_parts.items():
                ulozit = input(f"Uložit {nazev} ? (y/n): ").strip().lower()
                if ulozit == 'y':
                    # TADY JE ZMĚNA: Používáme samostatné funkce místo metod objektu
                    export_step(objekt, f"{nazev}.step")
                    export_stl(objekt, f"{nazev}.stl")
                    print(f"  ✅ Uloženo: {nazev}.step a {nazev}.stl")
                else:
                    print(f"  ⏭️ Přeskočeno: {nazev}")
            print("\nExport dokončen. Soubory najdeš ve složce s tímto skriptem.")
        else:
            print("Export přeskočen.")
    except EOFError:
        print("Běžíš v prostředí bez interaktivní konzole. Export vynechán.")
from build123d import *
from ocp_vscode import show

# ==========================================
# 1. ROZMĚRY KRABIČKY (ORIENTACE DLE UŽIVATELE)
# ==========================================
box_w = 120.0  # Šířka stěny (osa X)
box_l = 120.0  # Délka stěny (osa Y)
box_h = 60.0   # Výška krabičky (osa Z)
wall = 3.0     # Tlouščka stěny
corner_r = 5.0 # Vnější zaoblení rohů
overshoot = 1.0

# Vnější uši jsou na LEVÉ a PRAVÉ straně (osa X)
tab_w = 87.57
tab_total_w = 147.93
tab_x_ext = (tab_total_w - box_w) / 2 

# Oválné otvory v uších
slot_l = 30.0
slot_w = 5.3
slot_x = 128.6 / 2                        
slot_y = 15.5 / 2 + slot_l / 2  # Středy oválů od osy X              

# Sloupky pro šrouby víka M4 (104x104 mm)
lid_screw_dist = 104.0
lid_boss_r = 8.97                         
lid_hole_r = 1.8                          

# Spodní montážní nálitky krabičky (rozteč 97mm v X, 35mm v Y)
boss_outer_r = 8.0 / 2                    
boss_inner_r = 3.2 / 2  # Průměr 3.2mm pro původní nálitky krabičky                  
boss_h = 5.0                              
boss_col_x = 97.0 / 2                     # X = ±48.5 mm (Levá a pravá řada)
boss_row_y = 70.0 / 2                     # Y = ±35.0 mm (Horní a dolní řada)

# ==========================================
# ROZMĚRY PRO MOSAZNÉ ZÁVITOVÉ VLOŽKY (HEAT INSERTS)
# ==========================================
# Standardní otvor pro M3 heat insert bývá průměr 4.0 až 4.2 mm.
# Zde nastaveno na 4.2 mm (poloměr 2.1 mm). Uprav si dle svých vložek.
insert_r = 3.6 / 2  

# ==========================================
# 2. MODELOVÁNÍ KRABIČKY
# ==========================================
with BuildPart() as box:
    # A) ZÁKLADNA A UŠI
    with BuildSketch() as base_sk:
        RectangleRounded(box_w, box_l, corner_r)
        # Pravé zkosené ucho
        Polygon([
            (box_w/2 - 0.5, -106.0/2), (147.93/2, -tab_w/2),
            (147.93/2, tab_w/2), (box_w/2 - 0.5, 106.0/2)
        ], mode=Mode.ADD)
        # Levé zkosené ucho
        Polygon([
            (-box_w/2 + 0.5, -106.0/2), (-147.93/2, -tab_w/2),
            (-147.93/2, tab_w/2), (-box_w/2 + 0.5, 106.0/2)
        ], mode=Mode.ADD)
    extrude(amount=wall)

    # B) VYŘÍZNUTÍ 4 OVÁLŮ DO UŠÍ (Otočené o 90 stupňů)
    with BuildSketch(Plane.XY):
        with Locations(
            (slot_x, slot_y), (slot_x, -slot_y),
            (-slot_x, slot_y), (-slot_x, -slot_y)
        ):
            SlotOverall(slot_l, slot_w, rotation=90) 
    extrude(amount=wall + overshoot, both=True, mode=Mode.SUBTRACT)

    # C) OBVODOVÉ STĚNY KRABIČKY
    with BuildSketch(Plane(origin=(0, 0, wall))):
        RectangleRounded(box_w, box_l, corner_r)
        RectangleRounded(box_w - 2*wall, box_l - 2*wall, max(0.1, corner_r - wall), mode=Mode.SUBTRACT)
    extrude(amount=box_h - wall)

    # D) ROHOVÉ SLOUPKY PRO VÍKO
    with BuildPart() as corners:
        screw_locs = [
            (lid_screw_dist/2, lid_screw_dist/2), (-lid_screw_dist/2, lid_screw_dist/2),
            (lid_screw_dist/2, -lid_screw_dist/2), (-lid_screw_dist/2, -lid_screw_dist/2)
        ]
        with Locations(screw_locs):
            with Locations((0, 0, wall)):
                Cylinder(radius=lid_boss_r, height=box_h - wall, align=(Align.CENTER, Align.CENTER, Align.MIN))
        with BuildSketch():
            RectangleRounded(box_w, box_l, corner_r)
        extrude(amount=box_h, mode=Mode.INTERSECT)
    
    add(corners.part)
    with Locations(screw_locs):
        with Locations((0, 0, box_h)):
            Cylinder(radius=lid_hole_r, height=20, align=(Align.CENTER, Align.CENTER, Align.MAX), mode=Mode.SUBTRACT)

    # E) 6 MONTÁŽNÍCH NÁLITKŮ NA DNĚ (Levá řada X=-48.5, Pravá řada X=+48.5)
    with Locations(
        (-boss_col_x, boss_row_y), (-boss_col_x, 0), (-boss_col_x, -boss_row_y),
        (boss_col_x, boss_row_y), (boss_col_x, 0), (boss_col_x, -boss_row_y)
    ):
        with Locations((0, 0, wall)):
            Cylinder(radius=boss_outer_r, height=boss_h, align=(Align.CENTER, Align.CENTER, Align.MIN))
            with Locations((0, 0, boss_h)):
                Cylinder(radius=boss_inner_r, height=boss_h + overshoot, align=(Align.CENTER, Align.CENTER, Align.MAX), mode=Mode.SUBTRACT)


# ==========================================
# 3. MODELOVÁNÍ MOSTOVÝCH STRUKTUR (PRO TISK)
# ==========================================
bridge_thick = 4.0        
bridge_w = 12.0           
bridge_total_len = 107.0  # Délka přes nálitky na X = ±48.5

# Pozice X pro dva držáky baterií vedle sebe na levé straně
bat1_x = -33.0
bat2_x = -10.0
# Pozice X pro otvory plošného spoje na pravé straně
pcb1_x = 15.0
pcb2_x = 35.0

# --- A) HORNÍ MOST (Pozice Y = +35 mm) ---
with BuildPart() as horni_most:
    with BuildSketch(): 
        RectangleRounded(bridge_total_len, bridge_w, 3.0)
    extrude(amount=bridge_thick)
    
    # Krajní otvory pro stažení mostu k nálitkům (Centrované na středovou výšku Z = bridge_thick / 2)
    with Locations((-boss_col_x, 0, bridge_thick / 2), (boss_col_x, 0, bridge_thick / 2)):
        Cylinder(radius=1.7, height=bridge_thick + overshoot, mode=Mode.SUBTRACT) # Průchozí díra skrz naskrz
        with Locations((0, 0, bridge_thick / 2)):
            Cylinder(radius=3.2, height=2.0, align=(Align.CENTER, Align.CENTER, Align.MAX), mode=Mode.SUBTRACT) # Zapuštění hlavy z vrchu
            
    # 4 otvory pro HEAT INSERTY M3 (Skrz naskrz celý most, centrované na Z = bridge_thick / 2)
    with Locations(
        (bat1_x, 0, bridge_thick / 2), (bat2_x, 0, bridge_thick / 2), 
        (pcb1_x, 0, bridge_thick / 2), (pcb2_x, 0, bridge_thick / 2)
    ):
        Cylinder(radius=insert_r, height=bridge_thick + overshoot, mode=Mode.SUBTRACT)


# --- B) PROSTŘEDNÍ MOST (Pozice Y = 0 mm, s rozšířeným výstupkem dolů) ---
with BuildPart() as prostredni_most:
    with BuildSketch():
        # Základní rovný nosník
        RectangleRounded(bridge_total_len, bridge_w, 3.0)
        # Výstupek dolů (-Y) pro spodní úchyty baterií (rozteč 53.7 mm)
        with Locations((-22.0, -10.0)): 
            RectangleRounded(42.0, 26.0, 3.0)
    extrude(amount=bridge_thick)
    
    # Krajní otvory pro stažení mostu k nálitkům krabičky (Centrované na Z = bridge_thick / 2)
    with Locations((-boss_col_x, 0, bridge_thick / 2), (boss_col_x, 0, bridge_thick / 2)):
        Cylinder(radius=1.7, height=bridge_thick + overshoot, mode=Mode.SUBTRACT)
        with Locations((0, 0, bridge_thick / 2)):
            Cylinder(radius=3.2, height=2.0, align=(Align.CENTER, Align.CENTER, Align.MAX), mode=Mode.SUBTRACT)
            
    # Otvory pro HEAT INSERTY M3 pro baterie (posun Y = -18.7 mm, skrz naskrz, centrované na Z = bridge_thick / 2)
    with Locations((bat1_x, -18.7, bridge_thick / 2), (bat2_x, -18.7, bridge_thick / 2)):
        Cylinder(radius=insert_r, height=bridge_thick + overshoot, mode=Mode.SUBTRACT)
        
    # Otvory pro HEAT INSERTY M3 pro PCB (v ose mostu Y = 0, skrz naskrz, centrované na Z = bridge_thick / 2)
    with Locations((pcb1_x, 0, bridge_thick / 2), (pcb2_x, 0, bridge_thick / 2)):
        Cylinder(radius=insert_r, height=bridge_thick + overshoot, mode=Mode.SUBTRACT)


# ==========================================
# 4. MODEL DRŽÁKU BATERIÍ 18650 (PRO VIZUALIZACI)
# ==========================================
with BuildPart() as holder:
    Box(21.0, 75.0, 3.0, align=(Align.CENTER, Align.CENTER, Align.MIN)) 
    with Locations((0, 53.7/2, 0), (0, -53.7/2, 0)):
        Cylinder(radius=3.5/2, height=5, align=(Align.CENTER, Align.CENTER, Align.MIN), mode=Mode.SUBTRACT)
    with Locations((0, 75/2 - 1.5, 3), (0, -75/2 + 1.5, 3)):
        Box(21.0, 3.0, 18.0, align=(Align.CENTER, Align.CENTER, Align.MIN)) 
    Box(21.0, 75.0, 10.5, align=(Align.CENTER, Align.CENTER, Align.MIN))
    Box(19.0, 73.0, 10.5, align=(Align.CENTER, Align.CENTER, Align.MIN), mode=Mode.SUBTRACT)


# ==========================================
# UMÍSTĚNÍ DÍLŮ DO PROSTORU PRO PREVIEW
# ==========================================
most_horni_pos = horni_most.part.moved(Location((0, boss_row_y, wall + boss_h)))
most_prostredni_pos = prostredni_most.part.moved(Location((0, 0, wall + boss_h)))

holder_y = boss_row_y - (53.7 / 2) # Y = 35 - 26.85 = 8.15 mm
holder_z = wall + boss_h + bridge_thick

holder_1_pos = holder.part.moved(Location((bat1_x, holder_y, holder_z)))
holder_2_pos = holder.part.moved(Location((bat2_x, holder_y, holder_z)))

# Spuštění náhledu v OCP VS Code
if __name__ == "__main__":
    if 'show' in globals():
        show( 
            box,
            most_horni_pos, most_prostredni_pos,
            holder_1_pos, holder_2_pos,
            names=["Krabicka_Vigan", "Horni_Most_M3", "Prostredni_Most_M3", "Baterie_Drzak_1", "Baterie_Drzak_2"],
            colors=["#7f8c8d", "#0984e3", "#00b894", "#e1b12c", "#e1b12c"],
            alphas=[0.4, 1.0, 1.0, 0.4, 0.4]
        )
    
    export_parts = {
        "1_Straight": horni_most.part,
        "2_Bulk": prostredni_most.part
    }
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
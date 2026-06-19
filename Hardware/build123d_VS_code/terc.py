from build123d import *
from ocp_vscode import show

def get_terc_parts():
    """
    Funkce vygeneruje a vrátí jednotlivé komponenty terče pro vizualizaci.
    Všechny rozměry jsou v mm.
    """
    # --- PARAMETRY TERČE ---
    prof = 30         # Rozměr železného profilu (např. jekl 30x30 mm)
    w = 500           # Šířka rámu a štítu (50 cm)
    d = 500           # Hloubka spodní základny (50 cm)
    h_hole = 1600     # Výška středu díry od země (160 cm)
    shield_s = 500    # Rozměr čtvercového štítu (50 x 50 cm)
    hole_d = 50       # Průměr díry ve štítu (5 cm)
    
    # Vypočtené hodnoty
    h_total = h_hole + shield_s/2  # Celková výška sloupků (1850 mm)
    shelf_z = h_hole - shield_s/2  # Výška poličky pod terčem (1350 mm)

    parts = {}

    # --- 1. RÁM (KONSTRUKCE) ---
    with BuildPart() as frame:
        # Přední sloupky
        with Locations(((w/2 - prof/2, 0, h_total/2), (-w/2 + prof/2, 0, h_total/2))):
            Box(prof, prof, h_total)
        
        # Spodní nohy (směřující dozadu)
        with Locations(((w/2 - prof/2, -d/2, prof/2), (-w/2 + prof/2, -d/2, prof/2))):
            Box(prof, d, prof)
            
        # Zadní spodní propojka
        with Locations((0, -d + prof/2, prof/2)):
            Box(w, prof, prof)
            
        # Propojka pod poličkou
        with Locations((0, 0, shelf_z - prof/2)):
            Box(w, prof, prof)

        # Horní přední spojnice (na straně štítu)
        with Locations((0, 0, h_total - prof/2)):
            Box(w, prof, prof)

        # Dolní přední spojnice (na straně štítu, těsně u země)
        with Locations((0, 0, prof/2)):
            Box(w, prof, prof)

        # Diagonální vzpěry
        for x_pos in [w/2 - prof/2, -w/2 + prof/2]:
            p1 = Vector(x_pos, -d + prof/2, prof/2)
            p2 = Vector(x_pos, 0, shelf_z - prof/2)
            direction = (p2 - p1).normalized()
            length = (p2 - p1).length
            center = p1 + direction * (length / 2)
            
            with BuildSketch(Plane(origin=center, z_dir=direction)):
                Rectangle(prof, prof)
            extrude(amount=length/2, both=True)
            
    parts["Ram"] = frame.part

    # --- 2. PŘEDNÍ ŠTÍT ---
    with BuildPart() as shield:
        # Tloušťka 4mm. Zadní stěna štítu je na Y = 15.
        with Locations((0, prof/2 + 2, h_hole)): 
            Box(shield_s, 4, shield_s) 
            Cylinder(radius=hole_d/2, height=10, rotation=(90, 0, 0), mode=Mode.SUBTRACT)
    parts["Stit"] = shield.part

    # --- 3. SKLOPNÝ TERČ ---
    with BuildPart() as target_plate:
        # Parametry terče a přidružených hran pro snadnou úpravu
        t_w = 100         # Šířka hlavního plechu
        t_h = 150         # Výška hlavního plechu
        t_t = 5           # Tloušťka hlavního plechu
        
        edge_depth = 50  # Délka hran (jak moc zasahují do hloubky/dozadu)
        edge_thick = 5    # "Malá šířka" - tloušťka plechu hran
        
        # 1. Hlavní čelní plech (vycentrován na střed díry)
        with Locations((0, -5, h_hole)):
            Box(t_w, t_t, t_h) 
            
        # 2. Horní hrana
        # Zarovnaná s horním okrajem a s přední lící hlavního plechu, sahá dozadu
        with Locations((0, -5 - edge_depth/2 + t_t/2, h_hole + t_h/2 - edge_thick/2)):
            Box(t_w, edge_depth, edge_thick)
            
        # 3. Boční hrany (levá a pravá)
        # Zarovnané s vnějšími okraji plechu, sahají dozadu
        with Locations(
            (-t_w/2 + edge_thick/2, -5 - edge_depth/2 + t_t/2, h_hole+15), # Levá
            (t_w/2 - edge_thick/2,  -5 - edge_depth/2 + t_t/2, h_hole+15)  # Pravá
        ):
            Box(edge_thick, edge_depth, t_h-30)
            
    parts["Sklopka"] = target_plate.part

    # --- 4. Kyvadlo (mechanické potvrzení sestřelení) ---
    with BuildPart() as sensor:
        # Výpočty pozice boxu
        # Levá trubka rámu končí na X = -235. Střed je X = 0.
        box_l = 205
        box_x_center = -box_l / 2  # cca -117.5
        box_z_center = h_total - 75  # 1750 (pod vrchní trubkou)
        
        # Y pozice: Štít má zadní stěnu na Y=15. 
        # Chceme mezeru 15 mm, přední hrana boxu bude na Y=0.
        # Při hloubce boxu 40 mm bude jeho střed na Y=-20.
        box_y_center = -prof-10
        box_depth = 40
        box_height = 50

        # Hlavní tělo boxu
        with Locations((box_x_center, box_y_center, box_z_center)):
            Box(box_l, box_depth, box_height)
        
        # Upevňovací válec s mezerou (čep) uprostřed boxu
        with Locations((box_x_center, (15 - 30)/2, box_z_center)):
            Cylinder(radius=8, height=30, rotation=(90, 0, 0))

        # Úzký L-výstupek na pravém konci (X = 0, přímo pod středem díry)
        # 1. Vertikální část (padá dolů k terči)
        arm_drop = 120 # Rozdíl mezi boxem (1750) a terčem (1600)
        with Locations((-5, box_y_center, box_z_center - arm_drop/4-10)):
            # OPRAVA: Výška Boxu je arm_drop, ne arm_drop/2
            Box(20, 20, arm_drop+5)

    parts["Kyvadlo"] = sensor.part

    return parts

# Spuštění pro kontrolu sestavy (Kombinovaná vizualizace)
if __name__ == "__main__":
    t_parts = get_terc_parts()
    
    # 1. Společné posunutí pro celou elektroniku (Zvoleno místo nad středovou příčkou)
    # Z-osa = 1350 odpovídá výšce shelf_z v terči
    elec_base_loc = Location((0, -200, 1350))

    if 'show' in globals():
        show(
            # --- 1. DÍLY TERČE ---
            t_parts["Ram"], 
            t_parts["Stit"], 
            t_parts["Sklopka"], 
            t_parts["Kyvadlo"],
            
            # --- 2. DÍLY ELEKTRONIKY (S aplikovaným posunem k terči) ---
            elec_base_loc * Location((0, 0, -mount_th)) * mount_plate.part, 
            elec_base_loc * Location((batt_shift_x, batt_shift_y, 0)) * box.part,
            elec_base_loc * Location((-batt_x + batt_shift_x, batt_y_offset + batt_shift_y, base_th + 5)) * holder.part,
            elec_base_loc * Location((batt_x + batt_shift_x, batt_y_offset + batt_shift_y, base_th + 5)) * holder.part,
            elec_base_loc * Location((batt_shift_x, batt_shift_y, 0)) * conn.part,
            elec_base_loc * Location((batt_shift_x, batt_shift_y, h_box)) * lid.part,
            elec_base_loc * Location((log_tab_x, log_global_y, 0)) * logic_lid.part, 
            elec_base_loc * loc_logic_flip * logic_box.part,
            elec_base_loc * loc_logic_flip * Location((0, pcb_y_center, base_th + pcb_standoff_h)) * pcb_dummy.part,

            # --- 3. PARAMETRY ZOBRAZENÍ ---
            names=[
                # Terč
                "Ram", "Stit", "Sklopka", "Kyvadlo",
                # Elektronika
                "Montazni_Plat", "Battery_box", "Levy_Drzak", "Pravy_Drzak", "Konektor", "Tenke_Viko", 
                "Logic_viko_Pruhledne", "Logic_box", "Dummy_PCB"
            ],
            colors=[
                # Terč
                "#222222", "#4a708b", "#333333", "#ff6600",
                # Elektronika
                "#9b59b6", "#e6b800", "#333333", "#333333", "#ff3333", "#555555", 
                "#a2d2ff", "#4ea8de", "#00ff00"
            ],
            alphas=[
                # Terč
                1.0, 1.0, 1.0, 1.0,
                # Elektronika
                0.8, 0.4, 0.9, 0.9, 1.0, 0.8, 0.4, 0.5, 1.0
            ] 
        )
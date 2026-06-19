from build123d import *
from ocp_vscode import show

# Importujeme funkci z tvého souboru terc.py
from terc import get_terc_parts

# Importujeme vše z tvého souboru elektronika.py 
# (přejmenujeme si to na 'el', ať se nám to lépe píše)
import endpoint_sestava as el

# 1. Načteme díly terče
t_parts = get_terc_parts()

# 2. Určíme, kam chceme elektroniku na terči umístit
# Pozice: X=110 (doprava), Y=15 (na štítu), Z=1710 (nahoru), Rotace: (90, 0, 0)
elec_base_loc = Location((138, -15, 1738), (90, 0, 0))

# 3. Zobrazíme vše dohromady
if 'show' in globals():
    show(
        # --- DÍLY TERČE ---
        t_parts["Ram"], 
        t_parts["Stit"], 
        t_parts["Sklopka"], 
        t_parts["Kyvadlo"],
        
        # --- DÍLY ELEKTRONIKY (natáhlé ze souboru 'el') ---
        # Přidáme k nim elec_base_loc, aby se celá sestava posunula na terč
        elec_base_loc * Location((0, 0, -el.mount_th)) * el.mount_plate.part, 
        elec_base_loc * Location((el.batt_shift_x, el.batt_shift_y, 0)) * el.box.part,
        elec_base_loc * Location((-el.batt_x + el.batt_shift_x, el.batt_y_offset + el.batt_shift_y, el.base_th + 5)) * el.holder.part,
        elec_base_loc * Location((el.batt_x + el.batt_shift_x, el.batt_y_offset + el.batt_shift_y, el.base_th + 5)) * el.holder.part,
        elec_base_loc * Location((el.batt_shift_x, el.batt_shift_y, 0)) * el.conn.part,
        elec_base_loc * Location((el.batt_shift_x, el.batt_shift_y, el.h_box)) * el.lid.part,
        elec_base_loc * Location((el.log_tab_x, el.log_global_y, 0)) * el.logic_lid.part, 
        elec_base_loc * el.loc_logic_flip * el.logic_box.part,
        elec_base_loc * el.loc_logic_flip * Location((0, el.pcb_y_center, el.base_th + el.pcb_standoff_h)) * el.pcb_dummy.part,

        # --- PARAMETRY ZOBRAZENÍ ---
        names=[
            "Ram", "Stit", "Sklopka", "Kyvadlo",
            "Montazni_Plat", "Battery_box", "Levy_Drzak", "Pravy_Drzak", "Konektor", "Tenke_Viko", 
            "Logic_viko", "Logic_box", "Dummy_PCB"
        ],
        colors=[
            "#222222", "#4a708b", "#333333", "#ff6600",
            "#9b59b6", "#e6b800", "#333333", "#333333", "#ff3333", "#555555", 
            "#a2d2ff", "#4ea8de", "#00ff00"
        ],
        alphas=[
            1.0, 1.0, 1.0, 1.0,
            0.8, 0.4, 0.9, 0.9, 1.0, 0.8, 0.4, 0.5, 1.0
        ] 
    )
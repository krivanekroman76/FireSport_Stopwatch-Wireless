import customtkinter as ctk
import os
import json
import serial
import serial.tools.list_ports
from dotenv import load_dotenv
import firebase_admin
from firebase_admin import credentials, db
import threading
import random
import time
from tkinter import filedialog
from tkinter import messagebox

# Načtení konfigurace
load_dotenv()
try:
    cred = credentials.Certificate(os.getenv('FIREBASE_KEY_PATH'))
    if not firebase_admin._apps:
        firebase_admin.initialize_app(cred, {'databaseURL': os.getenv('FIREBASE_DB_URL')})
except Exception as e:
    print(f"Firebase error: {e}")
    
class RaceManager(ctk.CTk):
    def __init__(self):
        super().__init__()
        self.title("XPC-MMA Pro Race Control")
        self.geometry("1300x900")

        # Stavové proměnné
        self.lanes_count = ctk.IntVar(value=2)
        self.attempts_count = ctk.IntVar(value=1)
        self.teams_list = [] 
        self.current_team_index = 0
        self.current_attempt_index = 0
        self.active_race_name = "Default Race"
        
        # Serial port
        self.serial_port = None
        self.is_simulated = ctk.BooleanVar(value=True)
        
        self.cat_colors = {"Muži": "#3498db", "Ženy": "#e74c3c", "Dorost": "#9b59b6"}
        
        # Layout
        self.grid_columnconfigure(1, weight=1)
        self.grid_rowconfigure(0, weight=1)

        # Navigation
        self.navigation_frame = ctk.CTkFrame(self, corner_radius=0, width=200)
        self.navigation_frame.grid(row=0, column=0, sticky="nsew")
        self.navigation_frame.grid_propagate(False)
        
        ctk.CTkLabel(self.navigation_frame, text="MENU", font=("Arial", 16, "bold")).pack(pady=20)
        ctk.CTkButton(self.navigation_frame, text="Settings", command=self.show_settings).pack(pady=10, padx=10)
        ctk.CTkButton(self.navigation_frame, text="Race Dashboard", command=self.show_dashboard).pack(pady=10, padx=10)
        ctk.CTkButton(self.navigation_frame, text="Leaderboard", command=self.show_leaderboard).pack(pady=10, padx=10)

        # Main Content Frames
        self.settings_frame = ctk.CTkScrollableFrame(self, corner_radius=0, fg_color="transparent")
        self.dashboard_frame = ctk.CTkFrame(self, corner_radius=0, fg_color="transparent")
        self.leaderboard_frame = ctk.CTkScrollableFrame(self, corner_radius=0, fg_color="transparent")
        
        # Inicializace obsahu
        self.setup_settings()
        self.setup_dashboard()
        self.setup_leaderboard()
        
        # Defaultní stránka
        self.show_settings()

        # Start serial background thread
        threading.Thread(target=self.serial_reader, daemon=True).start()

    def log(self, message):
        timestamp = time.strftime('%H:%M:%S')
        msg = f"[{timestamp}] {message}"
        if hasattr(self, 'debug_console'):
            self.debug_console.insert("end", msg + "\n")
            self.debug_console.see("end")

    # --- NAVIGATION ---
    def show_settings(self):
        self.dashboard_frame.grid_forget()
        self.leaderboard_frame.grid_forget()
        self.settings_frame.grid(row=0, column=1, sticky="nsew")

    def show_dashboard(self):
        self.settings_frame.grid_forget()
        self.leaderboard_frame.grid_forget()
        self.dashboard_frame.grid(row=0, column=1, sticky="nsew")
        self.update_lane_buttons()

    def show_leaderboard(self):
        self.settings_frame.grid_forget()
        self.dashboard_frame.grid_forget()
        self.leaderboard_frame.grid(row=0, column=1, sticky="nsew")
        self.refresh_leaderboard()

    # --- SETTINGS PAGE ---
    def setup_settings(self):
        ctk.CTkLabel(self.settings_frame, text="System Settings", font=("Arial", 24, "bold")).pack(pady=20)
        
        # COM Port Selection
        port_frame = ctk.CTkFrame(self.settings_frame)
        port_frame.pack(pady=10, padx=20, fill="x")
        
        ctk.CTkSwitch(port_frame, text="Simulation Mode", variable=self.is_simulated).pack(side="left", padx=20)
        self.port_menu = ctk.CTkOptionMenu(port_frame, values=["Scanning..."])
        self.port_menu.pack(side="left", padx=10)
        ctk.CTkButton(port_frame, text="Connect", command=self.connect_serial, width=100).pack(side="left", padx=5)
        self.refresh_ports()

        # Race Config
        self.race_name_entry = ctk.CTkEntry(self.settings_frame, placeholder_text="Race Name", width=400)
        self.race_name_entry.pack(pady=10)
        
        ctk.CTkLabel(self.settings_frame, text="Lanes:").pack()
        self.lane_value_label = ctk.CTkLabel(self.settings_frame, text="Selected: 2")
        self.lane_value_label.pack()
        self.lane_slider = ctk.CTkSlider(self.settings_frame, from_=1, to=4, number_of_steps=3, variable=self.lanes_count, command=self.update_lane_display)
        self.lane_slider.pack(pady=5)
        
        ctk.CTkLabel(self.settings_frame, text="Attempts:").pack()
        self.attempts_value_label = ctk.CTkLabel(self.settings_frame, text="Selected: 1")
        self.attempts_value_label.pack()
        self.attempts_slider = ctk.CTkSlider(self.settings_frame, from_=1, to=4, number_of_steps=3, variable=self.attempts_count, command=self.update_attempts_display)
        self.attempts_slider.pack(pady=5)

        # Team Entry
        entry_f = ctk.CTkFrame(self.settings_frame, fg_color="transparent")
        entry_f.pack(pady=10)
        self.team_entry = ctk.CTkEntry(entry_f, placeholder_text="Team Name", width=200)
        self.team_entry.pack(side="left", padx=5)
        self.category_menu = ctk.CTkOptionMenu(entry_f, values=["Muži", "Ženy", "Dorost"])
        self.category_menu.pack(side="left", padx=5)
        ctk.CTkButton(entry_f, text="Add Team", command=self.add_team, fg_color="#1f6aa5").pack(side="left", padx=5)
        
        self.teams_scroll = ctk.CTkScrollableFrame(self.settings_frame, width=500, height=200)
        self.teams_scroll.pack(pady=10)

        # Control Buttons
        btn_f = ctk.CTkFrame(self.settings_frame, fg_color="transparent")
        btn_f.pack(pady=20)
        ctk.CTkButton(btn_f, text="CLEAR TEAMS", command=self.clear_teams, fg_color="#c0392b").pack(side="left", padx=10)
        ctk.CTkButton(btn_f, text="SAVE CONFIG", command=self.save_to_json, fg_color="#555").pack(side="left", padx=10)
        ctk.CTkButton(btn_f, text="LOAD CONFIG", command=self.load_from_json, fg_color="#555").pack(side="left", padx=10)
        
        ctk.CTkButton(self.settings_frame, text="START / UPDATE RACE", fg_color="#28a745", font=("Arial", 16, "bold"), height=50, command=self.confirm_and_start).pack(pady=10)

    def update_lane_display(self, value):
        self.lane_value_label.configure(text=f"Selected: {int(value)}")
        
    def update_attempts_display(self, value):
        self.attempts_value_label.configure(text=f"Selected: {int(value)}")

    def clear_teams(self):
        if messagebox.askyesno("Clear", "Opravdu smazat všechny týmy?"):
            self.teams_list = []
            self.refresh_settings_list()

    def add_team(self):
        name = self.team_entry.get()
        if name:
            self.teams_list.append({
                "id": len(self.teams_list) + 1,
                "name": name,
                "category": self.category_menu.get(),
                "status": "idle",
                "attempts_data": [], # Vygeneruje se při Startu
                "best_time": 0
            })
            self.team_entry.delete(0, 'end')
            self.refresh_settings_list()

    def refresh_settings_list(self):
        for widget in self.teams_scroll.winfo_children(): widget.destroy()
        for t in self.teams_list:
            ctk.CTkLabel(self.teams_scroll, text=f"#{t['id']} {t['name']} ({t['category']})").pack(anchor="w", padx=10)

    # --- DASHBOARD PAGE ---
    def setup_dashboard(self):
        self.table_label = ctk.CTkLabel(self.dashboard_frame, text="Live Race Table", font=("Arial", 20, "bold"))
        self.table_label.pack(pady=10)

        self.table_container = ctk.CTkScrollableFrame(self.dashboard_frame, width=1050, height=400)
        self.table_container.pack(pady=10, padx=20)

        ctrl_frame = ctk.CTkFrame(self.dashboard_frame)
        ctrl_frame.pack(pady=10, padx=20, fill="x")
        
        self.active_label = ctk.CTkLabel(ctrl_frame, text="READY", font=("Arial", 18, "bold"))
        self.active_label.pack(side="left", padx=20, pady=10)

        self.sim_button_frame = ctk.CTkFrame(ctrl_frame, fg_color="transparent")
        self.sim_button_frame.pack(side="right", padx=10)
        
        ctk.CTkButton(ctrl_frame, text="NP (Invalid)", fg_color="#c0392b", command=self.set_np, width=80).pack(side="right", padx=10)

        self.debug_console = ctk.CTkTextbox(self.dashboard_frame, height=150, width=1050, font=("Courier New", 12))
        self.debug_console.pack(pady=10, padx=20)

    # --- LEADERBOARD PAGE ---
    def setup_leaderboard(self):
        self.lb_title = ctk.CTkLabel(self.leaderboard_frame, text="Live Standings", font=("Arial", 24, "bold"))
        self.lb_title.pack(pady=20)
        self.lb_container = ctk.CTkFrame(self.leaderboard_frame, fg_color="transparent")
        self.lb_container.pack(fill="both", expand=True)

    # --- SERIAL & LOGIC ---
    def serial_reader(self):
        while True:
            if not self.is_simulated.get() and self.serial_port and self.serial_port.is_open:
                try:
                    line = self.serial_port.readline().decode('utf-8').strip()
                    if line:
                        self.log(f"RX: {line}")
                        if ":" in line:
                            parts = line.split(":")
                            lane = int(parts[0].replace("L", ""))
                            ms = int(parts[1])
                            self.after(0, self.process_hit, lane, ms)
                except: pass
            time.sleep(0.01)

    def connect_serial(self):
        try:
            port = self.port_menu.get()
            self.serial_port = serial.Serial(port, 115200, timeout=0.1)
            self.log(f"Connected to {port}")
            self.is_simulated.set(False)
        except Exception as e:
            messagebox.showerror("Serial Error", str(e))

    def refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        if ports: self.port_menu.configure(values=ports)
        self.after(5000, self.refresh_ports)

    def process_hit(self, lane, ms):
        if self.current_team_index >= len(self.teams_list): return

        team = self.teams_list[self.current_team_index]
        attempt = team["attempts_data"][self.current_attempt_index]
        
        lane_key = str(lane)
        if lane_key in attempt["lanes"]: return 

        attempt["lanes"][lane_key] = ms
        self.log(f"HIT: {team['name']} | L{lane} | {ms/1000:.2f}s")

        # --- PŘIDÁNO: Live sync do Firebase pro aktuální pokus ---
        try:
            db.reference('current_race').update({
                "team": team["name"],
                "category": team["category"],
                "attempt": self.current_attempt_index + 1,
                f"lane_{lane}": ms
            })
        except Exception as e:
            self.log(f"Firebase Update Error: {e}")

        if len(attempt["lanes"]) == self.lanes_count.get():
            attempt["final_time"] = max(attempt["lanes"].values())
            valid_times = [a["final_time"] for a in team["attempts_data"] if a["final_time"] > 0]
            team["best_time"] = min(valid_times) if valid_times else 0

            # --- PŘIDÁNO: Permanentní zápis výsledku do Firebase ---
            try:
                r_name = self.active_race_name.replace(" ", "_")
                db.reference(f"races/{r_name}/{team['category']}/{team['name']}").set({
                    "start_no": team['id'],
                    "best_time": team["best_time"],
                    "attempts": team["attempts_data"]
                })
            except Exception as e:
                self.log(f"Firebase Archive Error: {e}")

            # Switch Logic (ponechat beze změny...)
            if self.current_team_index < len(self.teams_list) - 1:
                self.current_team_index += 1
            else:
                if self.current_attempt_index < self.attempts_count.get() - 1:
                    self.current_team_index = 0
                    self.current_attempt_index += 1
                else:
                    self.current_team_index = len(self.teams_list)

            self.update_statuses()
            self.update_active_team_display()
            self.refresh_table()
            
    def set_np(self):
        if self.current_team_index < len(self.teams_list):
            for l in range(1, self.lanes_count.get() + 1):
                self.process_hit(l, 999999)

    def simulate_hit(self, lane):
        self.process_hit(lane, random.randint(14000, 22000))

    def update_lane_buttons(self):
        for widget in self.sim_button_frame.winfo_children(): widget.destroy()
        for i in range(1, self.lanes_count.get() + 1):
            ctk.CTkButton(self.sim_button_frame, text=f"Sim L{i}", command=lambda l=i: self.simulate_hit(l), width=80).pack(side="left", padx=2)

    def update_statuses(self):
        for i, team in enumerate(self.teams_list):
            if i < self.current_team_index: team["status"] = "done"
            elif i == self.current_team_index: team["status"] = "running"
            elif i <= self.current_team_index + 2: team["status"] = "preparing"
            else: team["status"] = "idle"

    def update_active_team_display(self):
        if self.current_team_index < len(self.teams_list):
            team = self.teams_list[self.current_team_index]
            self.active_label.configure(text=f"NOW: {team['name']} | Attempt: {self.current_attempt_index + 1}", 
                                        text_color=self.cat_colors.get(team['category']))
        else:
            self.active_label.configure(text="FINISHED", text_color="green")

    # --- TABLE REFRESH ---
    def refresh_table(self):
        for widget in self.table_container.winfo_children(): widget.destroy()
        att_cnt = self.attempts_count.get()
        lanes_cnt = self.lanes_count.get()
        headers = ["Status", "#", "Cat", "Team Name", "TOP RESULT"]
        for a in range(1, att_cnt + 1):
            for l in range(1, lanes_cnt + 1): headers.append(f"P{a}-L{l}")
            headers.append(f"Res {a}")

        for i, h in enumerate(headers):
            ctk.CTkLabel(self.table_container, text=h, font=("Arial", 10, "bold")).grid(row=0, column=i, padx=5, pady=5)

        for idx, team in enumerate(self.teams_list):
            status_color = {"running": "#1f6aa5", "preparing": "#e67e22", "done": "#2ecc71", "idle": "gray"}.get(team["status"], "white")
            ctk.CTkLabel(self.table_container, text=team["status"].upper(), text_color=status_color).grid(row=idx+1, column=0)
            ctk.CTkLabel(self.table_container, text=str(team['id'])).grid(row=idx+1, column=1)
            ctk.CTkLabel(self.table_container, text=team['category'], text_color=self.cat_colors.get(team['category'])).grid(row=idx+1, column=2)
            ctk.CTkLabel(self.table_container, text=team['name'], font=("Arial", 11, "bold")).grid(row=idx+1, column=3)
            
            b_val = team['best_time']
            b_str = "NP" if b_val >= 999999 else (f"{b_val/1000:.2f}s" if b_val > 0 else "--")
            ctk.CTkLabel(self.table_container, text=b_str, text_color="#f1c40f").grid(row=idx+1, column=4)

            curr_col = 5
            for a_idx in range(att_cnt):
                if a_idx < len(team["attempts_data"]):
                    att = team["attempts_data"][a_idx]
                    for l_idx in range(1, lanes_cnt + 1):
                        l_val = att["lanes"].get(str(l_idx), 0)
                        ctk.CTkLabel(self.table_container, text=f"{l_val/1000:.2f}" if l_val > 0 else "-").grid(row=idx+1, column=curr_col)
                        curr_col += 1
                    f_val = att["final_time"]
                    ctk.CTkLabel(self.table_container, text=f"{f_val/1000:.2f}s" if f_val > 0 else "--").grid(row=idx+1, column=curr_col)
                    curr_col += 1

    def refresh_leaderboard(self):
        for widget in self.lb_container.winfo_children(): widget.destroy()
        for cat in ["Muži", "Ženy", "Dorost"]:
            cat_teams = [t for t in self.teams_list if t["category"] == cat]
            sorted_teams = sorted(cat_teams, key=lambda x: (x["best_time"] == 0, x["best_time"]))
            if not sorted_teams: continue

            ctk.CTkLabel(self.lb_container, text=f"--- {cat.upper()} ---", font=("Arial", 18, "bold"), text_color=self.cat_colors[cat]).pack(pady=10)
            table = ctk.CTkFrame(self.lb_container)
            table.pack(pady=5, padx=20, fill="x")
            
            for rank, team in enumerate(sorted_teams, 1):
                bg = "transparent"
                if team["best_time"] > 0 and team["best_time"] < 999999:
                    if rank == 1: bg = "#f1c40f"
                    elif rank == 2: bg = "#bdc3c7"
                    elif rank == 3: bg = "#cd7f32"
                
                row = ctk.CTkFrame(table, fg_color=bg, corner_radius=0)
                row.pack(fill="x", pady=1)
                time_str = "NP" if team["best_time"] >= 999999 else (f"{team['best_time']/1000:.2f}s" if team["best_time"] > 0 else "--")
                ctk.CTkLabel(row, text=f"{rank}.", width=40).pack(side="left", padx=10)
                ctk.CTkLabel(row, text=team["name"], font=("Arial", 12, "bold"), width=200).pack(side="left", padx=10)
                ctk.CTkLabel(row, text=time_str, width=100).pack(side="right", padx=10)

    # --- SAVE / LOAD ---
    def save_to_json(self):
        file_path = filedialog.asksaveasfilename(defaultextension=".json", filetypes=[("JSON", "*.json")])
        if file_path:
            data = {"race_name": self.race_name_entry.get(), "lanes": self.lanes_count.get(), 
                    "attempts": self.attempts_count.get(), "teams": self.teams_list}
            with open(file_path, "w", encoding="utf-8") as f:
                json.dump(data, f, ensure_ascii=False, indent=4)
            messagebox.showinfo("Saved", "OK")

    def load_from_json(self):
        file_path = filedialog.askopenfilename(filetypes=[("JSON", "*.json")])
        if file_path:
            with open(file_path, "r", encoding="utf-8") as f:
                data = json.load(f)
            self.race_name_entry.delete(0, 'end')
            self.race_name_entry.insert(0, data.get("race_name", ""))
            self.lanes_count.set(data.get("lanes", 2))
            self.attempts_count.set(data.get("attempts", 1))
            self.teams_list = data.get("teams", [])
            self.update_lane_display(self.lanes_count.get())
            self.update_attempts_display(self.attempts_count.get())
            self.refresh_settings_list()
            messagebox.showinfo("Loaded", "OK")

    def confirm_and_start(self):
        self.active_race_name = self.race_name_entry.get()
        if not self.active_race_name:
            messagebox.showwarning("Varování", "Zadejte název závodu!")
            return
        if not self.teams_list: 
            messagebox.showwarning("Varování", "Seznam týmů je prázdný!")
            return
        
        for team in self.teams_list:
            if not team.get("attempts_data") or len(team["attempts_data"]) != self.attempts_count.get():
                team["attempts_data"] = [{"lanes": {}, "final_time": 0} for _ in range(self.attempts_count.get())]
        
        self.current_team_index = 0
        self.current_attempt_index = 0

        # --- PŘIDÁNO: Reset Firebase pro nový závod ---
        try:
            db.reference('current_race').set({
                "status": "running",
                "race_name": self.active_race_name,
                "lanes_count": self.lanes_count.get(),
                "team": "Ready...",
                "lane_1": 0, "lane_2": 0, "lane_3": 0, "lane_4": 0
            })
        except Exception as e:
            self.log(f"Firebase Reset Error: {e}")

        self.update_statuses()
        self.show_dashboard()
        self.refresh_table()
        self.update_active_team_display()

if __name__ == "__main__":
    app = RaceManager()
    app.mainloop()
import tkinter as tk
from tkinter import ttk, Scale
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import serial.tools.list_ports
import threading
import time

# Fenêtre principale
window = tk.Tk()
window.title("Interface Temps Réel - ObCP Accéléromètre")

# Variables globales
ser = None
connected = False
x = 0
update_interval_sec = 1  # Intervalle par défaut (en secondes)
data = {'x': [], 'y': [], 'z': []}

# Fonction pour détecter les ports série
def detect_serial_ports():
    available_ports = [port.device for port in serial.tools.list_ports.comports()]
    port_combobox['values'] = available_ports

# Fonction pour se connecter au port série
def connect_to_serial():
    global ser, connected
    selected_port = port_combobox.get()
    try:
        ser = serial.Serial(selected_port, baudrate=115200, timeout=1)
        status_label.config(text=f"Connecté à {selected_port}", foreground="green")
        connected = True
        threading.Thread(target=read_serial, daemon=True).start()
    except Exception as e:
        status_label.config(text=f"Erreur de connexion : {e}", foreground="red")

# Fonction pour se déconnecter du port série
def disconnect_from_serial():
    global ser, connected
    connected = False
    if ser:
        ser.close()
    status_label.config(text="Déconnecté", foreground="red")

# Fonction pour lire les données série
def read_serial():
    global x
    while connected:
        try:
            ser.write(b"a\n")
            data_received = ser.readline().decode('utf-8').strip()
            print(f"Reçu : {data_received}")  # Debug

            if "Acceleration" in data_received:
                parts = data_received.replace("=", " ").split()
                if "X" in parts and "Y" in parts and "Z" in parts:
                    x_val = float(parts[parts.index("X") + 1])
                    y_val = float(parts[parts.index("Y") + 1])
                    z_val = float(parts[parts.index("Z") + 1])

                    data['x'].append(x_val)
                    data['y'].append(y_val)
                    data['z'].append(z_val)

                    x += 1
                    if len(data['x']) > 20:  # Limiter à 20 points
                        for key in data:
                            data[key] = data[key][-20:]

                    update_graph()
        except Exception as e:
            print(f"Erreur série : {e}")

        time.sleep(update_interval_sec)

# Mise à jour du graphique
def update_graph():
    realtime_ax.clear()
    realtime_ax.plot(range(x - len(data['x']) + 1, x + 1), data['x'], label='X', color='red')
    realtime_ax.plot(range(x - len(data['y']) + 1, x + 1), data['y'], label='Y', color='green')
    realtime_ax.plot(range(x - len(data['z']) + 1, x + 1), data['z'], label='Z', color='blue')

    realtime_ax.set_xlabel("Temps (s)")
    realtime_ax.set_ylabel("Accélérations (g)")
    realtime_ax.legend(loc="upper left")
    realtime_ax.grid(True)
    realtime_canvas.draw()

# Mise à jour de l'intervalle
def update_interval(val):
    global update_interval_sec
    update_interval_sec = float(val) / 10

# Interface utilisateur
serial_frame = tk.LabelFrame(window, text="Port Série")
serial_frame.grid(row=0, column=0, padx=10, pady=5, sticky="w")

port_combobox = ttk.Combobox(serial_frame, values=[], width=20)
port_combobox.grid(row=0, column=0, padx=5, pady=5)

refresh_button = ttk.Button(serial_frame, text="Actualiser", command=detect_serial_ports)
refresh_button.grid(row=0, column=1, padx=5, pady=5)

connect_button = ttk.Button(serial_frame, text="Connecter", command=connect_to_serial)
connect_button.grid(row=0, column=2, padx=5, pady=5)

disconnect_button = ttk.Button(serial_frame, text="Déconnecter", command=disconnect_from_serial)
disconnect_button.grid(row=0, column=3, padx=5, pady=5)

status_label = ttk.Label(serial_frame, text="Déconnecté", foreground="red")
status_label.grid(row=0, column=4, padx=5, pady=5)

realtime_frame = tk.LabelFrame(window, text="Graphique Temps Réel")
realtime_frame.grid(row=1, column=0, padx=10, pady=5, sticky="w")

interval_slider = Scale(realtime_frame, from_=1, to=10, orient="horizontal", label="Intervalle (s)",
                        command=lambda val: update_interval(val))
interval_slider.set(update_interval_sec * 10)
interval_slider.grid(row=0, column=0)

# Configuration du graphique
realtime_fig = Figure(figsize=(6, 3))
realtime_ax = realtime_fig.add_subplot(111)
realtime_ax.set_xlabel("Temps (s)")
realtime_ax.set_ylabel("Accélérations (g)")
realtime_canvas = FigureCanvasTkAgg(realtime_fig, master=realtime_frame)
realtime_canvas_widget = realtime_canvas.get_tk_widget()
realtime_canvas_widget.grid(row=1, column=0)

# Initialisation
detect_serial_ports()
window.mainloop()

import tkinter as tk
from tkinter import ttk, Scale
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import serial.tools.list_ports
import threading
import time

# Fenêtre principale
window = tk.Tk()
window.title("Interface Temps Réel - ObCP Accéléromètre et Température")

# Variables globales
ser = None
connected = False
x = 0
update_interval_acc = 1  # Intervalle de rafraîchissement pour les accélérations (en secondes)
update_interval_temp = 1  # Intervalle de rafraîchissement pour la température (en secondes)
data = {'x': [], 'y': [], 'z': [], 'temp': []}
pwm_values = {'R': 0, 'v': 0, 'B': 0, 'M': 0}  # PWM initial pour rouge, vert, bleu, général

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
        threading.Thread(target=read_acceleration, daemon=True).start()
        threading.Thread(target=read_temperature, daemon=True).start()
    except Exception as e:
        status_label.config(text=f"Erreur de connexion : {e}", foreground="red")

# Fonction pour se déconnecter du port série
def disconnect_from_serial():
    global ser, connected
    connected = False
    if ser:
        ser.close()
    status_label.config(text="Déconnecté", foreground="red")

# Fonction pour lire les accélérations
def read_acceleration():
    global x
    while connected:
        try:
            ser.write(b"a\n")
            data_received = ser.readline().decode('utf-8').strip()
            print(f"Reçu (Accélérations) : {data_received}")  # Debug

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
                        for key in ['x', 'y', 'z']:
                            data[key] = data[key][-20:]

                    update_acc_graph()
        except Exception as e:
            print(f"Erreur lecture accélérations : {e}")

        time.sleep(update_interval_acc)

# Fonction pour lire la température
def read_temperature():
    while connected:
        try:
            ser.write(b"T\n")
            data_received = ser.readline().decode('utf-8').strip()
            print(f"Reçu (Température) : {data_received}")  # Debug

            if "T =" in data_received:
                parts = data_received.split("=")
                temp_val = float(parts[1].strip())
                data['temp'].append(temp_val)

                if len(data['temp']) > 20:  # Limiter à 20 points
                    data['temp'] = data['temp'][-20:]

                update_temp_graph()
        except Exception as e:
            print(f"Erreur lecture température : {e}")

        time.sleep(update_interval_temp)

# Mise à jour des graphiques
def update_acc_graph():
    realtime_ax.clear()
    realtime_ax.plot(range(x - len(data['x']) + 1, x + 1), data['x'], label='X', color='red')
    realtime_ax.plot(range(x - len(data['y']) + 1, x + 1), data['y'], label='Y', color='green')
    realtime_ax.plot(range(x - len(data['z']) + 1, x + 1), data['z'], label='Z', color='blue')
    realtime_ax.set_xlabel("Temps (s)")
    realtime_ax.set_ylabel("Accélérations (g)")
    realtime_ax.legend(loc="upper left")
    realtime_ax.grid(True)
    realtime_canvas.draw()

def update_temp_graph():
    temp_ax.clear()
    temp_ax.plot(range(len(data['temp'])), data['temp'], label='Température', color='orange')
    temp_ax.set_xlabel("Temps (s)")
    temp_ax.set_ylabel("Température (°C)")
    temp_ax.legend(loc="upper left")
    temp_ax.grid(True)
    temp_canvas.draw()

# Fonction pour envoyer les commandes PWM
def update_pwm(channel, value):
    pwm_values[channel] = int(value)
    if connected and ser:
        command = f"{channel}{pwm_values[channel]}\n"
        ser.write(command.encode('utf-8'))
        print(f"Envoyé : {command}")  # Debug

# Fonction pour mettre à jour les intervalles
def update_acc_interval(val):
    global update_interval_acc
    update_interval_acc = float(val)

def update_temp_interval(val):
    global update_interval_temp
    update_interval_temp = float(val)

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

# Cadre des sliders PWM
pwm_frame = tk.LabelFrame(window, text="Contrôle des PWM")
pwm_frame.grid(row=1, column=0, padx=10, pady=5, sticky="w")

for idx, channel in enumerate(['R', 'v', 'B', 'M']):
    slider = Scale(pwm_frame, from_=0, to=65535, orient="horizontal",
                   label=f"PWM {channel}", command=lambda val, ch=channel: update_pwm(ch, val))
    slider.set(pwm_values[channel])
    slider.grid(row=0, column=idx)

# Slider pour intervalles
interval_frame = tk.LabelFrame(window, text="Réglage des Intervalles")
interval_frame.grid(row=2, column=0, padx=10, pady=5, sticky="w")

acc_slider = Scale(interval_frame, from_=0.1, to=5, resolution=0.1, orient="horizontal",
                   label="Intervalle Accélérations (s)", command=update_acc_interval)
acc_slider.set(update_interval_acc)
acc_slider.grid(row=0, column=0)

temp_slider = Scale(interval_frame, from_=0.1, to=5, resolution=0.1, orient="horizontal",
                    label="Intervalle Température (s)", command=update_temp_interval)
temp_slider.set(update_interval_temp)
temp_slider.grid(row=0, column=1)

# Graphiques côte à côte
graph_frame = tk.Frame(window)
graph_frame.grid(row=3, column=0, padx=10, pady=5, sticky="w")

# Graphique accélérations
realtime_fig = Figure(figsize=(5, 3))
realtime_ax = realtime_fig.add_subplot(111)
realtime_canvas = FigureCanvasTkAgg(realtime_fig, master=graph_frame)
realtime_canvas_widget = realtime_canvas.get_tk_widget()
realtime_canvas_widget.grid(row=0, column=0, padx=5)

# Graphique température
temp_fig = Figure(figsize=(5, 3))
temp_ax = temp_fig.add_subplot(111)
temp_canvas = FigureCanvasTkAgg(temp_fig, master=graph_frame)
temp_canvas_widget = temp_canvas.get_tk_widget()
temp_canvas_widget.grid(row=0, column=1, padx=5)

# Initialisation
detect_serial_ports()
window.mainloop()

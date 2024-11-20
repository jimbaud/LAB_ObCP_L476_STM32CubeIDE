import tkinter as tk
from tkinter import ttk, scrolledtext
import serial.tools.list_ports
import serial
import threading
from tkinter import Scale

# Variables globales
ser = None  # Objet de port série
connected = False  # Indicateur d'état de connexion

# Fonction pour détecter les ports série disponibles
def detect_serial_ports():
    available_ports = [port.device for port in serial.tools.list_ports.comports()]
    port_combobox['values'] = available_ports

# Fonction appelée lors de la connexion au port série
def connect_to_serial():
    global ser, connected
    selected_port = port_combobox.get()
    
    try:
        # Ouvrir le port série sélectionné
        ser = serial.Serial(selected_port, baudrate=9600)
        status_label.config(text=f"Connecté au port {selected_port}", fg="green")
        connected = True

        # Définir une fonction pour lire et afficher les données reçues
        def read_serial():
            while connected:
                data = ser.readline().decode('utf-8')
                received_text.insert(tk.END, data)  # Afficher les données reçues dans la zone de texte
                received_text.see(tk.END)  # Faire défiler vers le bas pour afficher les dernières données
                window.update_idletasks()  # Mettre à jour l'interface graphique
        
        # Lancer la fonction de lecture des données série dans un thread
        thread = threading.Thread(target=read_serial)
        thread.daemon = True
        thread.start()
        
    except serial.SerialException as e:
        status_label.config(text=f"Erreur de connexion : {str(e)}", fg="red")

# Fonction pour se déconnecter du port série
def disconnect_from_serial():
    global ser, connected
    if ser:
        ser.close()
        connected = False
        status_label.config(text="Déconnecté", fg="red")

# Fonction pour envoyer des données sur le port série
def send_data(data):
    if connected:
        termination = termination_combobox.get()
        
        if termination == "Line Feed":
            data += "\n"
        elif termination == "Carriage Return":
            data += "\r"
        
        ser.write(data.encode('utf-8'))  # Envoyer les données sur le port série

# Fonction pour mettre à jour la taille de la police de la zone de texte
def update_font_size():
    font_size = font_size_combobox.get()
    received_text.configure(font=("TkDefaultFont", font_size))

# Créer une fenêtre
window = tk.Tk()
window.title("Interface avec Port Série")

# Cadre pour les éléments de connexion
connection_frame = tk.Frame(window)
connection_frame.pack()

# Libellé (label) pour la détection des ports
label = tk.Label(connection_frame, text="Sélectionnez un port série :")
label.grid(row=0, column=0, padx=5, pady=5, sticky="w")

# Liste déroulante (combobox) pour les ports série
port_combobox = ttk.Combobox(connection_frame, values=[], width=20)
port_combobox.grid(row=0, column=1, padx=5, pady=5)

# Bouton pour détecter les ports série disponibles
detect_button = tk.Button(connection_frame, text="Actualiser les ports", command=detect_serial_ports)
detect_button.grid(row=0, column=2, padx=5, pady=5)

# Bouton de connexion au port série
connect_button = tk.Button(connection_frame, text="Connecter", command=connect_to_serial)
connect_button.grid(row=0, column=3, padx=5, pady=5)

# Bouton de déconnexion du port série
disconnect_button = tk.Button(connection_frame, text="Déconnecter", command=disconnect_from_serial)
disconnect_button.grid(row=0, column=4, padx=5, pady=5)

# Libellé pour afficher l'état de la connexion
status_label = tk.Label(connection_frame, text="Déconnecté", fg="red")
status_label.grid(row=0, column=5, padx=5, pady=5)

# Cadre pour les éléments de la zone de réception
received_frame = tk.Frame(window)
received_frame.pack(padx=10, pady=10)

# Zone de texte avec défilement pour afficher les données reçues
received_text = scrolledtext.ScrolledText(received_frame, height=10, width=40)
received_text.grid(row=0, column=0, padx=5, pady=5, columnspan=3)

# Cadre pour les éléments d'envoi de données
send_frame = tk.Frame(window)
send_frame.pack(padx=10, pady=10)

# Libellé pour "Caractère de terminaison"
termination_label = tk.Label(send_frame, text="Caractère de terminaison :")
termination_label.grid(row=0, column=0, padx=5, pady=5, sticky="w")

# Liste déroulante pour sélectionner le caractère de terminaison
termination_combobox = ttk.Combobox(send_frame, values=["Aucun", "Line Feed", "Carriage Return"])
termination_combobox.set("Aucun")
termination_combobox.grid(row=0, column=1, padx=5, pady=5)

# Libellé pour "Taille de la police"
font_size_label = tk.Label(send_frame, text="Taille de la police :")
font_size_label.grid(row=1, column=0, padx=5, pady=5, sticky="w")

# Liste déroulante pour sélectionner la taille de la police
font_size_combobox = ttk.Combobox(send_frame, values=["10", "12", "14", "16", "18", "20"])
font_size_combobox.set("12")
font_size_combobox.grid(row=1, column=1, padx=5, pady=5)

# Sliders pour rouge, vert et bleu
red_slider = Scale(send_frame, from_=0, to=65535, orient="horizontal", label="Rouge", bg="red")
green_slider = Scale(send_frame, from_=0, to=65535, orient="horizontal", label="Vert", bg="green")
blue_slider = Scale(send_frame, from_=0, to=65535, orient="horizontal", label="Bleu", bg="blue")

red_slider.grid(row=2, column=0, padx=5, pady=5)
green_slider.grid(row=2, column=1, padx=5, pady=5)
blue_slider.grid(row=2, column=2, padx=5, pady=5)

# Bouton "Envoyer" pour chaque couleur
red_button = tk.Button(send_frame, text="Envoyer Rouge", command=lambda: send_data(f"r{red_slider.get()}\r"))
green_button = tk.Button(send_frame, text="Envoyer Vert", command=lambda: send_data(f"v{green_slider.get()}\r"))
blue_button = tk.Button(send_frame, text="Envoyer Bleu", command=lambda: send_data(f"b{blue_slider.get()}\r"))

red_button.grid(row=3, column=0, padx=5, pady=5)
green_button.grid(row=3, column=1, padx=5, pady=5)
blue_button.grid(row=3, column=2, padx=5, pady=5)

# Cadre pour la console d'envoi
console_frame = tk.Frame(window)
console_frame.pack(padx=10, pady=10)

# Libellé pour la console d'envoi
console_label = tk.Label(console_frame, text="Console d'envoi :")
console_label.grid(row=0, column=0, padx=5, pady=5, sticky="w")

# Zone de texte pour afficher les données envoyées
console_text = scrolledtext.ScrolledText(console_frame, height=5, width=40)
console_text.grid(row=1, column=0, padx=5, pady=5, columnspan=3)

# Lancer la détection initiale des ports série disponibles
detect_serial_ports()

# Lancer la boucle principale de l'interface graphique
window.mainloop()

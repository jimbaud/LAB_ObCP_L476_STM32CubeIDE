import tkinter as tk
from tkinter import ttk, scrolledtext
import serial.tools.list_ports

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
        import threading
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

# Fonction appelée lors de l'envoi de données
def send_data():
    if connected:
        data_to_send = send_entry.get()
        termination = termination_combobox.get()
        
        if termination == "Line Feed":
            data_to_send += "\n"
        elif termination == "Carriage Return":
            data_to_send += "\r"
        
        ser.write(data_to_send.encode('utf-8'))  # Envoyer les données sur le port série
        send_entry.delete(0, tk.END)  # Effacer le champ de saisie

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

# Champ de saisie pour envoyer des données
send_entry = tk.Entry(send_frame, width=30)
send_entry.grid(row=2, column=0, padx=5, pady=5, columnspan=2)

# Libellé pour "Envoie"
send_label = tk.Label(send_frame, text="Envoie :")
send_label.grid(row=2, column=2, padx=5, pady=5, sticky="e")

# Bouton "Envoyer"
send_button = tk.Button(send_frame, text="Envoyer", command=send_data)
send_button.grid(row=2, column=3, padx=5, pady=5)

# Bouton pour mettre à jour la taille de la police
update_font_button = tk.Button(send_frame, text="Mettre à jour la taille de la police", command=update_font_size)
update_font_button.grid(row=1, column=2, padx=5, pady=5, columnspan=2)

# Lancer la détection initiale des ports série disponibles
detect_serial_ports()

# Lancer la boucle principale de l'interface graphique
window.mainloop()

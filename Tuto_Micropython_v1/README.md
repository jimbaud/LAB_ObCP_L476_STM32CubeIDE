# Guide : Configurer et Programmer l'OBCP avec MicroPython

Ce guide explique comment flasher et configurer votre carte OBCP pour développer en Python avec MicroPython. Toutes les étapes nécessaires pour préparer votre environnement sont détaillées, ainsi que des ressources pour approfondir vos connaissances.

---

## **1. Préparation de la carte : Flash du firmware**

### Étape 1 : Passer la carte en mode *prog*
- **Action :** Mettre la carte en mode "prog" (positionner le cavalier ou basculer l'interrupteur selon votre modèle).
- **Connecter :** Brancher la carte à l'ordinateur via USB et l'allumer.

---

### Étape 2 : Télécharger et installer `dfu-util`
- **Outil :** [dfu-util](http://dfu-util.sourceforge.net/) est un utilitaire de flash pour les périphériques compatibles DFU (Device Firmware Upgrade).  
- **Téléchargez :** La version Windows du logiciel, ou utilisez un gestionnaire de paquets comme `brew` sur macOS ou `apt` sur Linux :
  ```bash
  sudo apt install dfu-util # Pour Linux
  ```

---

### Étape 3 : Naviguer dans le répertoire `dfu-util`
- **Ouvrez un terminal/console** (Windows : `cmd` ou PowerShell). 
- **Accédez au dossier contenant l’exécutable `dfu-util` :**
  ```bash
  cd Downloads/Tuto_Micropython_v1/dfu-util-0.9-win64/dfu-util-0.9-win64
  ```

---

### Étape 4 : Flasher le firmware
- **Commande de base :**
  ```bash
  dfu-util --alt 0 -D chemin_vers_fichier_firmware.dfu
  ```
- **Exemple :**
  ```bash
  dfu-util --alt 0 -D C:\Users\natha\Downloads\Tuto_Micropython_v1\NUCLEO_L476RG-20211006-unstable-v1.17-74-gd42cba0d2.dfu
  ```
- **Note :** Adaptez le chemin pour pointer vers le fichier `.dfu` spécifique à votre carte.

---

### Étape 5 : Repasser en mode *run*
- **Action :** Mettre la carte hors tension, passer en mode "run", et rallumer la carte.
- **Résultat :** La carte apparaît comme une clé USB contenant plusieurs fichiers, notamment :
  - `boot.py` : Script lancé au démarrage de la carte.
  - `main.py` : Script principal où écrire votre programme.

---

## **2. Écrire votre programme en Python**

### Utilisation des GPIO
- Déclarez les broches en utilisant leurs noms MicroPython (ex. : `"PA0"`) visibles sur le schéma électrique de la carte.
- Exemple d’utilisation des broches pour contrôler une LED :
  ```python
  from machine import Pin
  led = Pin("PA0", Pin.OUT)  # Déclare la broche PA0 comme sortie
  led.on()                  # Allume la LED
  led.off()                 # Éteint la LED
  ```

---

### **Attention : Sauvegarde des fichiers**
- Lors d’une déconnexion brutale (*sans éjecter le périphérique*), les fichiers `boot.py` et `main.py` peuvent être effacés.
- **Conseil :** Sauvegardez toujours une copie locale de vos fichiers avant de déconnecter la carte.

---

## **3. Environnements de Développement**

### Utiliser **PyCharm**
- **Configuration directe :** PyCharm peut interagir avec votre carte via le port série pour coder directement en Python.
- **Ressource :** Consultez [la documentation PyCharm](https://www.jetbrains.com/help/pycharm/) pour configurer un interpréteur externe.

### Utiliser **PuTTY**
- **Fonctionnalité :** Outil de communication série pour tester rapidement la carte, par exemple pour allumer une LED ou recevoir des logs.
- **Installation :** Téléchargez [PuTTY ici](https://www.chiark.greenend.org.uk/~sgtatham/putty/latest.html).
- **Configuration :** Configurez la connexion série (ex. : port COM utilisé par la carte) pour interagir avec elle.

---

## **4. Documentation complémentaire**
- **Documentation officielle MicroPython :** [MicroPython Docs](https://docs.micropython.org/en/latest/)
- **Outils et librairies MicroPython :** [MicroPython Libraries](https://docs.micropython.org/en/latest/library/index.html)
- **Exemples de projets MicroPython :**
  - [MicroPython sur GitHub](https://github.com/micropython/micropython)
  - [Tutoriels MicroPython](https://learn.adafruit.com/micropython-basics)

---

## **5. Conseils pour aller plus loin**
- **Approfondir :**
  - [Tutoriels ST sur les cartes Nucleo](https://www.st.com/content/st_com/en/products/development-tools/software-development-tools/stm32-software-development-tools/stm32-embedded-software/stm32cube-mcu-packages/stm32cube-nucleo-expansion-packages.html)
  - [Configuration avancée MicroPython](https://micropython-on-esp8266.readthedocs.io/en/latest/advanced.html)
- **Communauté :**
  - [Forum officiel MicroPython](https://forum.micropython.org/)
  - [Stack Overflow](https://stackoverflow.com/questions/tagged/micropython)

Avec ce guide, vous devriez être en mesure de configurer et d’utiliser votre carte Nucleo avec MicroPython pour des projets embarqués en Python. 🎉

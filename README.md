---

# **ObCP - Objet Connecté Personnel**

<div align="center">
  <img src="./Images/ObCP_3Diso.png" alt="Vue 3D ObCP" width="700"/>
  <p><em>Vue 3D de l'ObCP</em></p>
</div>

Le projet ObCP (Objet Connecté Personnel) est une plateforme de développement basée sur le microcontrôleur STM32L476RG et les modules Bluetooth SPBTLE-RF (avant 2024) ou BlueNRG-M0 (à partir de 2024). Elle permet de prototyper des objets connectés intelligents et personnalisés.

---

## 📘 **Introduction**
Ce dépôt propose une série de projets d’exemple destinés à la carte **ObCP**. Chaque répertoire contient un projet STM32CubeIDE complet ainsi qu'un sous-répertoire `bin/` contenant les fichiers binaires prêts à être flashés directement sur la carte.  

Ces projets permettent d'explorer diverses fonctionnalités telles que :
- ⚡ **PWM**  
- 🔌 **GPIOs**  
- 📡 **Bluetooth Low Energy (BLE)**  
- 🚦 **LEDs multicolores**  
- 📡 **Communication I2C, SPI** 

---

## 📁 **Structure du dépôt**

```
/
├── ObCP_projet_1/                 # Dossier contenant un projet exemple complet
│   ├── .settings/                 # Fichiers de configuration STM32CubeIDE
│   ├── Core/                      # Code source du projet (main.c, stm32l4xx_it.c, etc.)
│   ├── Drivers/                   # Drivers HAL et CMSIS pour STM32
│   ├── bin/                       # Fichiers binaires prêts à flasher (hex, bin, elf)
│   └── ObCP_projet_1.ioc          # Fichier de configuration CubeMX
│
├── ObCP_projet_2/                 # Un autre projet exemple
│   └── bin/                       # Fichiers binaires prêts à flasher
│
├── README.md                      # Ce fichier de présentation
└── Images/                        # Images et captures d'écran de la configuration
```

Chaque dossier **ObCP_projet_x** est un projet complet que vous pouvez importer dans **STM32CubeIDE**. Vous y trouverez également un dossier **bin/** contenant les fichiers binaires (".bin", ".hex", ".elf") prêts à être flashés sur la carte.

---

## 🚀 **Comment importer un projet dans STM32CubeIDE ?**

1️⃣ **A l'aide de l'explorateur de fichier, copiez le dossier du projet dans votre dossier de workspace CubeIDE.**  
2️⃣ **Lancez STM32CubeIDE** et accédez à **File → Import**.  
3️⃣ Choisissez **Existing Projects into Workspace** et cliquez sur **Next**.  
4️⃣ Cliquez sur **Browse** et sélectionnez le dossier du projet (par exemple, `ObCP_projet_1/`).  
5️⃣ Assurez-vous que le projet est bien coché, puis cliquez sur **Finish**.  

Vous verrez maintenant le projet importé dans l’explorateur de projets de STM32CubeIDE.  

---
## ⚙️ **Génération du fichier .bin dans STM32CubeIDE**

Pour flasher le programme directement sur la carte, il est souvent utile de générer le fichier binaire **.bin** (ou **.hex**). Voici comment configurer STM32CubeIDE pour générer automatiquement ces fichiers.  

1️⃣ **Accédez aux paramètres de génération d'artefacts binaires** :  
- Clic droit sur le nom du projet dans l'explorateur de projets.  
- Sélectionnez **Properties** (Propriétés).  

2️⃣ **Accédez à la section Build Settings** :  
- Allez dans **C/C++ Build → Settings**.  
- Dans l'onglet *Tool Settings**, recherchez la section **MCU Post build outputs**.  

3️⃣ **Activez la génération du fichier binaire** :  
- Dans la liste, cochez **Convert to binary file** et **Convert to intel Hex file**, ajoutez la ligne suivante :  
 <div align="center"> <img src="./Images/Bin_Hex_generation_config.png" alt="Configuration de génération .bin" width="800"/> <p><em>Configuration de la génération automatique des fichiers .bin et .hex</em></p> </div>
  Cette commande transforme le fichier ELF généré en fichier **.bin** et **.hex**.  

4️⃣ **Validez et appliquez les modifications** :  
- Cliquez sur **Apply and Close**.  

5️⃣ **Compilez le projet** :  
- Faites un clic droit sur le projet et sélectionnez **Build Project**.  
- Le fichier **.bin** sera généré dans le répertoire **/Debug/** ou **/Release/** selon la configuration de votre build.  

---

## 🛠️ **Fonctionnalités principales**

### 🧩 **Composants principaux**
- **STM32L476RG** : Puissance de traitement, faible consommation, parfait pour les applications IoT.  
- **SPBTLE-RF / BlueNRG-M0** : Module Bluetooth Low Energy.  

### ⚙️ **Caractéristiques matérielles**
- **PWM** : Contrôle de charges (jusqu’à 2,5W sous 5V).  
- **Bouton poussoir utilisateur** : Interface utilisateur simple.  
- **Batterie Lithium** : Alimentation autonome portable.  
- **LED RGB** : Indication des états de fonctionnement.  
- **Capteur LIS3D** : Mesure d'accélérations sur trois axes.  

---

## 🔥 **Installation et utilisation**

### ⚡ **Avec STM32CubeProgrammer**

1️⃣ **Téléchargez et installez** [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html).  
2️⃣ **Connectez votre ObCP** en mode "Prog" (SW4) puis appuyez sur "Reset" (SW3).  

<div align="center">
  <img src="./Images/Inter_Prog.png" alt="Mode Prog" width="250"/> <img src="./Images/Reset.png" alt="Reset" width="250"/>
  <p><em>Mode "Prog" et bouton "Reset"</em></p>
</div>

3️⃣ **Flashez le programme** :  Le port USB doit à présent être detecté comme actif pour la programmation. Effacer le STM32 (Bouton Full chip erase de l'onglet Erase flash memory) puis ouvrez l'emplacement du fichier binaire (.elf/.bin/.hex) à l'aide du bouton Browse et enfin cliquez sur Start programming pour charger le programme dans le STM32 via l'interface USB dans CubeProgrammer. Si vous utilisez le fichier BIN, l'adresse de début est 0x08000000.

<div align="center">
  <img src="./Images/Config_CubeProg.png" alt="Configuration de CubeProgrammer" width="800"/>
  <p><em>Configuration de CubeProgrammer</em></p>
</div>

4️⃣ **Repassez en mode "Run"** et appuyez sur **Reset (SW3)** pour démarrer le programme.

<div align="center">
  <img src="./Images/Inter_Run.png" alt="Mode Run" width="250"/> <img src="./Images/Reset.png" alt="Reset" width="250"/>
  <p><em>Mode "Run" et bouton "Reset"</em></p>
</div>

---

### 🛠️ **Avec STM32CubeIDE**

#### Configuration avec STM32CubeIDE et CubeProgrammer
<div align="center">
  <img src="./Images/Config_CubeIDE.png" alt="Configuration STM32CubeIDE" width="800"/>
  <p><em>Configuration de STM32CubeIDE pour téléversement en DFU</em></p>
</div>

1. Téléchargez et installez [STM32CubeIDE](https://www.st.com/en/development-tools/stm32cubeide.html) et [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html).
2. Configurez STM32CubeIDE pour téléverser directement en mode DFU via la CLI de CubeProgrammer :
   - **Location** : `C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe`
   - **Working directory** : `${workspace_loc}${project_path}\Debug`
   - **Arguments** : `-c port=usb1 -d ${project_name}.elf -s`
3. Placez SW4 en mode "Prog", appuyez sur SW3 "Reset", puis démarrez le téléversement.

<div align="center">
  <img src="./Images/Inter_Prog.png" alt="Mode Prog" width="250"/> <img src="./Images/Reset.png" alt="Reset" width="250"/>
  <p><em>Mode "Prog" et bouton "Reset"</em></p>
</div>

5. Une fois le téléversement terminé, repassez en mode "Run" et redémarrez avec SW3.

<div align="center">
  <img src="./Images/Inter_Run.png" alt="Mode Run" width="250"/> <img src="./Images/Reset.png" alt="Reset" width="250"/>
  <p><em>Mode "Run" et bouton "Reset"</em></p>
</div>

---

## 📘 **Documentation supplémentaire**

- **STM32L476RG** : [Documentation officielle](https://www.st.com/en/microcontrollers-microprocessors/stm32l476rg.html).
- **BlueNRG-M0** : [Fiche technique](https://www.st.com/en/wireless-connectivity/bluenrg-m0.html).

---

## 📄 **Schéma et implantation des composants**

<div align="center">
  <img src="./Images/ObCP_3DTOP.png" alt="Implantation des composants" width="700"/>
  <p><em>Implantation des composants de l'ObCP</em></p>
</div>

<div align="center">
  <img src="./Images/Schema_ObCP_V24_Page1.png" alt="Schéma Page 1" width="700"/>
  <p><em>Schéma électrique - Page 1</em></p>
</div>

<div align="center">
  <img src="./Images/Schema_ObCP_V24_Page2.png" alt="Schéma Page 2" width="700"/>
  <p><em>Schéma électrique - Page 2</em></p>
</div>

<div align="center">
  <img src="./Images/BOM_ObCP_V24.png" alt="Nomenclature" width="700"/>
  <p><em>Nomenclature complète de l'ObCP</em></p>
</div>

---

**Note :** Ce projet est destiné à des fins de développement et d'apprentissage. Utilisez-le à vos propres risques.

--- 

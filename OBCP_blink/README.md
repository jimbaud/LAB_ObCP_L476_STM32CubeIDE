# OBCP_blink: Programme pour le Clignotement de la LED Verte sur l'OBCP

Ce projet, disponible sur le repository GitHub, est un exemple simple illustrant comment contrôler une LED sur l'OBCP (STM32 L476RG) avec STM32CubeIDE. Le programme fait clignoter la LED verte de la carte toutes les secondes. 

---

## Fonctionnalités
1. **Clignotement de la LED Verte :**  
   Utilise la fonction `HAL_GPIO_TogglePin` pour faire clignoter la LED verte connectée à la carte.  

2. **Intégration Bluetooth Low Energy (BLE) :**  
   Le code inclut la configuration et le traitement des processus Bluetooth via le module BLE intégré (BlueNRG-MS).  

3. **Architecture modulaire :**  
   La gestion de la LED, du Bluetooth et d'autres fonctionnalités est organisée de manière modulaire dans des fichiers séparés (`Bluetooth_low_energy.h`, `Driver.h`).  

---

## Configuration matérielle
1. **Carte utilisée :** STM32L476RG (OBCP).  
2. **LED connectée à :** `Led_Verte_GPIO_Port` et `Led_Verte_Pin`.  
3. **Horloge configurée pour le fonctionnement optimal.**  

---

## Fonctionnement du Code
- La LED verte clignote toutes les secondes grâce à `HAL_GPIO_TogglePin` et un délai défini avec `HAL_Delay(1000)`.  

---

## Ressources et Références
- [Documentation officielle STM32CubeIDE](https://www.st.com/en/development-tools/stm32cubeide.html)  
- [Documentation STM32 HAL](https://www.st.com/en/embedded-software/stm32cube-mcu-packages.html)  
- [Tutoriels Bluetooth Low Energy STM32](https://www.st.com/en/embedded-software/stsw-bluenrg-ms.html)  

---

## Contact
Pour toute question ou contribution, n'hésitez pas à ouvrir une issue ou à soumettre une pull request sur le repository.  
Auteur : **Joel Imbaud**  
Institution : **SUPMICROTECH ENSMM**  


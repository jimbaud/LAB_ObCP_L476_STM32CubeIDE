# BLE UART Service for STM32

Ce projet implémente un service Bluetooth Low Energy (BLE) pour les microcontrôleurs STM32, permettant la communication sans fil via BLE en utilisant un service UART pour envoyer et recevoir des données. Le programme gère la connexion, les notifications, et l'échange de données entre un périphérique STM32 et un autre appareil compatible BLE.

## Fonctionnalités principales
- Ajout d'un service BLE avec les caractéristiques TX (Transmission) et RX (Réception) pour envoyer et recevoir des données.
- Prise en charge de la communication BLE en mode UART.
- Notifications BLE activées pour recevoir des données à distance.
- Utilisation du port USB pour la transmission de données via CDC (Communication Device Class).

## Commandes possibles

| Commande                  | Description                                                                 |
|---------------------------|-----------------------------------------------------------------------------|
| `R` ou `r` suivi d'un entier de 0 à 65535 | Contrôle de la LED rouge via PWM, exemple : `r50` pour une intensité de 50.  |
| `V` ou `v` suivi d'un entier de 0 à 65535 | Contrôle de la LED verte via PWM, exemple : `v50`.                          |
| `B` ou `b` suivi d'un entier de 0 à 65535 | Contrôle de la LED bleue via PWM, exemple : `b50`.                          |
| `M` ou `m` suivi d'un entier de 0 à 65535 | Contrôle du PWM général, exemple : `m50`.                                   |
| `A` ou `a`                  | Lecture de l'accéléromètre, retourne les valeurs X, Y et Z.                |
| `T` ou `t`                  | Lecture de la température.                                                  |
| `cls`                       | Effacement de l'écran du terminal USB.                                      |
| `start timer`               | Démarrage du timer 1s (BLE).                                                |
| `stop timer`                | Arrêt du timer 1s (BLE).                                                   |
| `#rst`                      | Réinitialisation de toutes les fonctions.                                   |
| `?`                         | Affiche l'aide avec la liste des commandes.                                 |

## Explication du port COM USB et du BLE UART

Le programme utilise le port USB pour établir une connexion avec le périphérique STM32, qui sert de passerelle pour les données échangées via le service BLE. Une fois que le périphérique est connecté à un autre appareil BLE (comme un smartphone ou un ordinateur), les données peuvent être envoyées et reçues en utilisant les caractéristiques définies dans le service UART BLE :

1. **Port COM USB** : La connexion USB sert à la communication entre le microcontrôleur STM32 et un terminal USB (par exemple, un PC). Le périphérique STM32 transmet l'adresse générée pour la communication BLE, ainsi que l'état de la connexion.
2. **BLE comme UART** : Le service BLE implémente un canal UART pour la transmission et la réception des données. Les données sont envoyées à travers la caractéristique TX (Transmission) et reçues via la caractéristique RX (Réception). Le BLE agit ici comme un remplacement sans fil pour une connexion série traditionnelle.

### Utilisation

1. Branchez votre périphérique STM32 à votre ordinateur via USB.
2. Lancez une application de terminal compatible BLE (comme nRF Connect ou BLE Scanner).
3. Cherchez le périphérique STM32 dans les périphériques disponibles.
4. Connectez-vous et commencez à échanger des données via les caractéristiques TX/RX.

## Dépendances

- STM32CubeMX pour la configuration de la puce STM32.
- HAL (Hardware Abstraction Layer) pour l'accès aux périphériques matériels.
- BlueNRG pour la gestion du BLE sur STM32.

## Exemple de commandes

Voici un exemple d'utilisation des commandes dans le programme :

### Contrôle des LED via PWM
- Pour ajuster la LED rouge à une intensité de 50 : `r50`
- Pour ajuster la LED verte à une intensité de 50 : `v50`
- Pour ajuster la LED bleue à une intensité de 50 : `b50`

### Lecture des capteurs
- Pour lire les données de l'accéléromètre : `a`
- Pour lire la température : `t`

### Commandes additionnelles
- Pour démarrer un timer de 1 seconde : `start timer`
- Pour arrêter le timer : `stop timer`
- Pour réinitialiser toutes les fonctions : `#rst`

## Auteurs

- Joel IMBAUD

## Licence

Ce projet est sous licence MIT - voir le fichier [LICENSE](LICENSE) pour plus de détails.

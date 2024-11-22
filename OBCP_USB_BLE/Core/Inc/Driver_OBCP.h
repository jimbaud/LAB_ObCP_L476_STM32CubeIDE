/*
 * Driver_OBCP.h
 *
 *  Créé le : 20 nov. 2024
 *  Auteur   : Joel IMBAUD
 *  SUPMICROTECH ENSMM www.supmicrotech.fr
 */

#ifndef INC_DRIVER_OBCP_H_
#define INC_DRIVER_OBCP_H_

/* Includes ------------------------------------------------------------------*/
#include <includes.h>

/* Variables externes --------------------------------------------------------*/
extern uint8_t Acc_timer;                     // Drapeau pour le minuteur d'accéléromètre
extern uint8_t user_button_pressed;           // Drapeau pour le bouton utilisateur
extern uint32_t* USB_length;                  // Longueur des données USB reçues
extern uint8_t* UsbBuffer;                    // Buffer des données USB reçues
extern int iusb;                              // Index pour les données USB reçues caractère par caractère
extern char UsbBuffer2[10];                   // Buffer temporaire pour les commandes USB
extern uint8_t RxUSB_OK;                      // Drapeau pour indiquer que les données USB sont prêtes
extern int8_t USB_Connected;                  // Indique si la connexion USB est établie
extern volatile uint8_t user_button_init_state; // État initial du bouton utilisateur
extern char help[];                           // Message d'aide pour les commandes

/* Prototypes de fonctions ---------------------------------------------------*/

/**
 * @brief Réinitialise toutes les fonctions liées au PWM et arrête le minuteur 1 seconde.
 */
void reset_OBCP_functions(void);

/**
 * @brief Gestionnaire d'interruption pour le bouton utilisateur.
 * @param GPIO_Pin Broche GPIO déclenchant l'interruption.
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin);

/**
 * @brief Gestionnaire d'interruption du minuteur périodique (1s).
 * @param htim Pointeur vers le handle du timer.
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim);

/**
 * @brief Envoie une chaîne de caractères via BLE (paquets de 20 caractères max).
 * @param data_buffer Buffer contenant les données à envoyer.
 */
void Printf_BLE(char* data_buffer);

/**
 * @brief Gestion des événements utilisateur (bouton, minuteur, etc.).
 */
void User_Process(void);

/**
 * @brief Traite les tâches en arrière-plan pour le module BLE.
 */
void Bluetooh_LE_Process(void);

/**
 * @brief Traite les commandes reçues via BLE.
 * @param data_buffer Buffer contenant les données reçues.
 */
void SendDataBLEToUSB(uint8_t* data_buffer);

/**
 * @brief Traite les commandes reçues via USB.
 * @param data_buffer Buffer contenant les données reçues.
 */
void USBCmd(uint8_t* data_buffer);

/**
 * @brief Gère les données USB reçues (par paquets ou caractère par caractère).
 */
void SendDataUSBToBLE(void);

#endif /* INC_DRIVER_OBCP_H_ */

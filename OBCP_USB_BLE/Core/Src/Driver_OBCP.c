/*
 * Driver_OBCP.c
 *
 *  Created on: 20 nov. 2024
 *      Author: Joel IMBAUD
 *      SUPMICROTECH ENSMM www.supmicrotech.fr
 */

/* Includes ------------------------------------------------------------------*/
#include <includes.h>  // Inclusion des fichiers d'en-tête nécessaires pour le projet

/* Private variables ---------------------------------------------------------*/
// Variables privées utilisées dans le code
uint8_t Acc_timer = 0;  // Timer pour l'accéléromètre
uint8_t user_button_pressed = 0;  // Flag pour détecter si le bouton utilisateur est pressé
uint32_t* USB_length = 0;  // Pointeur pour la longueur de données USB
uint8_t* UsbBuffer;  // Pointeur pour le tampon de données USB
int iusb = 0;  // Index pour le tampon USB
char UsbBuffer2[10];  // Tampon pour recevoir les données USB
uint8_t RxUSB_OK = 0;  // Flag indiquant si les données USB sont prêtes à être lues
int8_t USB_Connected;  // Indicateur de connexion USB


/* Volatile type is used to avoid optimization on a certain variable (useful with interrupts) */
// Le mot-clé volatile est utilisé pour éviter l'optimisation du compilateur pour certaines variables liées aux interruptions
volatile uint8_t user_button_init_state = 1;  // État initial du bouton utilisateur

/* Function prototypes -----------------------------------------------*/
// Déclaration des prototypes des fonctions utilisées plus tard

// Fonction qui réinitialise tous les PWM à zéro et arrête le timer de 1s
void reset_OBCP_functions(void)
{
	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0);  // Désactivation du PWM pour le canal 1 de TIM3
	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);  // Désactivation du PWM pour le canal 3 de TIM3
	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, 0);  // Désactivation du PWM pour le canal 4 de TIM3
	__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, 0);  // Désactivation du PWM pour le canal 3 de TIM1
	HAL_TIM_Base_Stop_IT(&htim7);  // Arrêt du timer 7
}

// Fonction d'interruption pour le bouton poussoir
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
	if (GPIO_Pin == BLUE_BUTTON_PIN) {  // Si l'interruption provient du bouton utilisateur
		/* Set the User Button flag */
		user_button_pressed = 1;  // Le bouton a été pressé
	}
}


// Fonction pour envoyer des données via BLE par paquets de 20 caractères
void Printf_BLE(char* data_buffer)
{
	size_t length = strlen(data_buffer);  // Longueur des données à envoyer
	int modulo20 = length % 20;  // Calcul du reste pour la dernière portion
	int number20 = length / 20;  // Nombre de paquets de 20 caractères
	char Temp[20];  // Tampon pour 20 caractères
	int i = 0;
	int time;

	// Envoi des paquets de données par 20 caractères
	for (i = 0; i < number20; i++)
	{
		strncpy(Temp, &data_buffer[i*20], 20);  // Copie 20 caractères dans Temp
		time = HAL_GetTick();  // Temps actuel
		// Attente d'une réponse Hal_ok avec un timeout de 10ms
		while(sendDataDef((uint8_t*)Temp, 20))
		{
			if (HAL_GetTick() - time > 10) break;  // Timeout de 10ms
		}
		time = HAL_GetTick();
		// Si 5 paquets envoyés, on attend 20ms pour vider le tampon interne BLE
		if (i % 5 == 0)
		{
			while((HAL_GetTick() - time) < 20 );
		}
	}
	// Envoi du dernier paquet si la longueur des données n'est pas un multiple de 20
	strncpy(Temp, &data_buffer[20 * i], modulo20);
	sendDataDef((uint8_t*)Temp, modulo20);
}

// Fonction de traitement de l'utilisateur, exécutée périodiquement
void User_Process(void)
{

	// Si la connexion BLE est en mode connectable, on établit une connexion
	if (set_connectable)
	{
		Make_Connection();  // Établir la connexion avec l'appareil distant
		set_connectable = FALSE;  // Désactive le mode connectable
		user_button_init_state = HAL_GPIO_ReadPin(BLUE_BUTTON_PORT, BLUE_BUTTON_PIN);  // Lire l'état du bouton
	}

	/* Check if the User Button has been pushed */
	if (user_button_pressed)
	{
		/* Software debouncing (likely not needed with the nucleo board) */
		HAL_Delay(50);  // Délai pour le debouncing logiciel
		/* Wait until the User Button is released */
		while (HAL_GPIO_ReadPin(BLUE_BUTTON_PORT, BLUE_BUTTON_PIN) == !user_button_init_state);
		/* Debouncing */
		HAL_Delay(50);  // Nouvelle période de debouncing

		/* Print on virtual com port USB */
		if (USB_Connected)
		{
			/* Envoi d'un message à l'USB si connecté */
			CDC_Transmit_FS((uint8_t*)"Push button pressed\n\r", strlen("Push button pressed\n\r"));
		}
		if (BLE_connected)
		{
			/* Envoi d'un message via BLE si connecté */
			Printf_BLE("Push button pressed\n\r");
		}
		/* Reset the User Button flag */
		user_button_pressed = 0;  // Réinitialisation du flag du bouton utilisateur
	}
	/* Check the timer flag every second with this config */

}

// Fonction pour gérer les commandes reçues via BLE
void SendDataBLEToUSB(uint8_t* data_buffer) {

	if (USB_Connected)
	{
		/* Envoi d'un message à l'USB si connecté */
		CDC_Transmit_FS((uint8_t*)data_buffer, strlen((const char*)data_buffer));
	}
}

// Traitement du tampon USB en fonction de la réception des données
void SendDataUSBToBLE(void)
{
	 if (RxUSB_OK && BLE_connected) {
	        Printf_BLE((char*)UsbBuffer);  // Envoi via BLE si connecté
	        memset(UsbBuffer, 0, 1);  // Nettoyage du tampon
	        RxUSB_OK = 0;  // Réinitialisation du flag
	    }

}

// Fonction de tâche en arrière-plan pour le Bluetooth Low Energy
void Bluetooh_LE_Process(void)
{
	User_Process();  // Traitement des actions utilisateur
	SendDataUSBToBLE();
	hci_user_evt_proc();  // Traitement des événements HCI
}




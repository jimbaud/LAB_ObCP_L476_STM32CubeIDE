
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

// Chaîne de caractères pour afficher l'aide à l'utilisateur
char help[] = "Type command : \n\r"
		"- R or r led and integer from 0 to 65535 like r50 for red led\n\r"
		"- V or v led and integer from 0 to 65535 like v50 for green\n\r"
		"- B or b led and integer from 0 to 65535 like b50 for red\n\r"
		"- M or m and integer from 0 to 65535 like m50 for pwm output\n\r"
		"- A or a like a to read accelerometer\n\r"
		"- T or t like t to read temperature\n\r"
		"- cls to clear screen (USB)\n\r"
		"- start timer or stop timer (BLE)\n\r"
		"- #rst to reset functions\n\r";

/* Config accelerometer variable */
// Définition des variables pour l'accéléromètre
LIS3DSH_DataRaw myData;  // Données brutes de l'accéléromètre
LIS3DSH_DataScaled MyDataScale;  // Données de l'accéléromètre mises à l'échelle

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

// Fonction d'interruption pour le timer 1s
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
	Acc_timer = 1;  // Met à jour le flag de timer pour l'accéléromètre
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
	char Temp[20] = "";  // Tampon pour les données à envoyer

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
			sendDataDef((uint8_t*)"PButton pressed\n\r", strlen("PButton pressed\n\r"));
		}
		/* Reset the User Button flag */
		user_button_pressed = 0;  // Réinitialisation du flag du bouton utilisateur
	}
	/* Check the timer flag every second with this config */
	if (Acc_timer)
	{
		// Si connecté via BLE, envoie des données d'accéléromètre et de température
		if (BLE_connected)
		{
			MyDataScale = LIS3DSH_GetDataScaled();  // Lecture des données mises à l'échelle
			sprintf(Temp, "X%.2fY%.2fZ%.2f\n\r", MyDataScale.x, MyDataScale.y, MyDataScale.z);
			sendDataDef((uint8_t*)Temp, strlen(Temp));  // Envoi des données via BLE
			sprintf(Temp, "T = %i\n\r", LIS3DSH_GetTemperature());  // Lecture de la température
			sendDataDef((uint8_t*)Temp, strlen(Temp));  // Envoi de la température via BLE
		}
		/* Reset the 1s timer flag */
		Acc_timer = 0;  // Réinitialisation du timer
	}

	if (USB_Connected == 1)
	{
		/* Détection de la connexion USB et envoi d'un message BLE si connecté */
		if (BLE_connected)
		{
			sendDataDef((uint8_t*)"USB Connected\n\r", strlen("USB Connected\n\r"));
			// Incrémentation de USB_Connected pour éviter la répétition du message
			USB_Connected = 2;
		}
	}
}

// Fonction de tâche en arrière-plan pour le Bluetooth Low Energy
void Bluetooh_LE_Process(void)
{
	User_Process();  // Traitement des actions utilisateur
	hci_user_evt_proc();  // Traitement des événements HCI
}

// Fonction pour traiter les commandes reçues via BLE ou USB
void processCommand(uint8_t* data_buffer, char interface) {
	char commande;  // Variable pour stocker la commande
	int valeur = 0;  // Variable pour stocker la valeur de la commande
	char Temp[64] = "";  // Tampon pour stocker les réponses

	sscanf((char*)data_buffer, "%c%i", &commande, &valeur);  // Lecture de la commande et de la valeur

	// Traitement des commandes de contrôle
	switch (commande) {
	case 'r':
	case 'R':
		__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, valeur);  // Contrôle du LED rouge via PWM
		snprintf(Temp, sizeof(Temp), "Red = %i\n\r", valeur);
		break;
	case 'v':
	case 'V':
		__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, valeur);  // Contrôle du LED verte via PWM
		snprintf(Temp, sizeof(Temp), "Green = %i\n\r", valeur);
		break;
	case 'b':
	case 'B':
		__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, valeur);  // Contrôle du LED bleue via PWM
		snprintf(Temp, sizeof(Temp), "Blue = %i\n\r", valeur);
		break;
	case 'm':
	case 'M':
		__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, valeur);  // Contrôle du PWM général
		snprintf(Temp, sizeof(Temp), "PWM = %i\n\r", valeur);
		break;
	case 'a': // Commande pour lire l'accéléromètre
	case 'A':
		MyDataScale = LIS3DSH_GetDataScaled();  // Lecture des données mises à l'échelle de l'accéléromètre
		snprintf(Temp, sizeof(Temp), "Acceleration : X=%.2f Y=%.2f Z=%.2f\n\r", MyDataScale.x, MyDataScale.y, MyDataScale.z);
		break;
	case 't': // Commande pour lire la température
	case 'T':
		snprintf(Temp, sizeof(Temp), "T = %i\n\r", LIS3DSH_GetTemperature());  // Lecture de la température
		break;
	case '?': // Commande d'aide
		if (interface == 'B') {
			Printf_BLE(help);  // Envoi de l'aide via BLE
		} else if (interface == 'U') {
			CDC_Transmit_FS((uint8_t*)help, strlen(help));  // Envoi de l'aide via USB
		}
		return;
	default: // Commandes spécifiques
		if (strncmp((char*)data_buffer, "start timer", 11) == 0) {
			HAL_TIM_Base_Start_IT(&htim7);  // Démarrage du timer 1s
		} else if (strncmp((char*)data_buffer, "stop timer", 10) == 0) {
			HAL_TIM_Base_Stop_IT(&htim7);  // Arrêt du timer 1s
		} else if (strncmp((char*)data_buffer, "#rst", 4) == 0) {
			reset_OBCP_functions();  // Réinitialisation des fonctions
		} else if (interface == 'U' && strncmp((char*)data_buffer, "cls", 3) == 0) {
			CDC_Transmit_FS((uint8_t*)"\e[1;1H\e[2J", strlen("\e[1;1H\e[2J"));  // Effacement de l'écran USB
		} else {
			snprintf(Temp, sizeof(Temp), "False command\n\r");  // Commande incorrecte
		}
		break;
	}

	// Envoi des réponses via BLE ou USB
	if (interface == 'B') {
		sendDataDef((uint8_t*)Temp, strlen(Temp));  // Envoi via BLE
		if (USB_Connected) {
			CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));  // Envoi aussi via USB si connecté
		}
	} else if (interface == 'U') {
		CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));  // Envoi via USB
		if (BLE_connected) {
			Printf_BLE(Temp);  // Envoi via BLE si connecté
		}
	}
}

// Fonction pour gérer les commandes reçues via BLE
void BLE_command_received(uint8_t* data_buffer) {
	processCommand(data_buffer, 'B');  // Traite la commande via BLE
}

// Fonction pour gérer les commandes reçues via USB
void USBCmd(uint8_t* data_buffer) {
	processCommand(data_buffer, 'U');  // Traite la commande via USB
}

// Traitement du tampon USB en fonction de la réception des données
void receiveDataUSB(void)
{
	// Les caractères sont envoyés par paquets via le terminal
	if(RxUSB_OK && USB_length[0] > 1)
	{
		RxUSB_OK = 0;
		USBCmd(UsbBuffer);  // Traite la commande
	}
	// Les caractères sont envoyés un par un par le terminal
	if(RxUSB_OK && (USB_length[0] == 1))
	{
		RxUSB_OK = 0;
		UsbBuffer2[iusb] = UsbBuffer[0];  // Sauvegarde du caractère reçu
		iusb++;
		// Vérifie si le dernier caractère est un retour chariot (CR) ou saut de ligne (LF)
		if(UsbBuffer2[iusb-1] == 0x0D)
		{
			iusb = 0;  // Réinitialise l'index
			USBCmd((uint8_t*)UsbBuffer2);  // Traite la commande reçue
		}
	}
}

/*
 * Driver.c
 *
 *  Created on: 03 oct. 2023
 *      Author: Joel IMBAUD
 *      SUPMICROTECH ENSMM www.supmicrotech.fr
 */

/* Inclusions de fichiers d'en-tête ------------------------------------------*/
#include "Driver.h"

/* Variables privées --------------------------------------------------------*/

uint8_t user_button_pressed = 0;

/* Le type "volatile" est utilisé pour éviter l'optimisation d'une certaine variable (utile avec les interruptions) */
volatile uint8_t user_button_init_state = 1;

/* Prototypes de fonctions -----------------------------------------------*/

// Fonction d'interruption pour le bouton-poussoir
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
	if (GPIO_Pin == B1_Pin) {
		/* Définit le drapeau du bouton utilisateur */
		user_button_pressed = 1;
	}
}

void Printf_BLE(char*  data_buffer)
{
	size_t length = strlen(data_buffer);
	int modulo20 = length % 20;
	int number20 = length / 20;
	char Temp[20];
	int i = 0;
	int time;
	// Impression sur BLE par paquets de 20 caractères, qui est la charge utile maximale pour UART BLE
	for (i = 0; i < number20 ; i++)
	{
		strncpy(Temp, &data_buffer[i * 20], 20);
		time = HAL_GetTick();
		// Attendez le feedback Hal_ok (0x0) pour continuer avec un délai de 10 ms
		while(sendDataDef((uint8_t*)Temp, 20))
		{
			if (HAL_GetTick() - time > 10) break;
		}
		time = HAL_GetTick();
		// Tous les 128 caractères envoyés, attendez 20 ms pour vider le tampon interne BLE de 128
		if (i % 5 == 0)
		{
			while((HAL_GetTick() - time) < 20 );
		}
	}
	strncpy(Temp, &data_buffer[20 * i], modulo20);
	sendDataDef((uint8_t*)Temp, modulo20);
}

void User_Process(void)
{
	if (set_connectable)
	{
		/* Établir une connexion avec un périphérique distant */
		Make_Connection();
		set_connectable = FALSE;
		user_button_init_state = HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin);
	}

	/* Vérifier si le bouton utilisateur a été enfoncé */
	if (user_button_pressed)
	{
		/* Anti-rebond logiciel (probablement pas nécessaire avec la carte Nucleo) */
		HAL_Delay(50);
		/* Attendre que le bouton utilisateur soit relâché */
		while (HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin) == !user_button_init_state);
		/* Anti-rebond */
		HAL_Delay(50);

		/* Imprimer sur le port virtuel COM BLE */

		if (BLE_connected)
		{
			/* Envoyer une commande au périphérique distant BLE si connecté */
			sendDataDef((uint8_t*)"BPpressed\n\r", strlen("BPpressed\n\r"));
		}
		/* Réinitialiser le drapeau du bouton utilisateur */
		user_button_pressed = 0;
	}
}

/*
 * Tâche de fond BlueNRG-MS
 */
void Bluetooh_LE_Process(void)
{
	User_Process();
	hci_user_evt_proc();
}

void BLE_command_received(uint8_t* data_buffer)
{
	if (data_buffer[0] == '1')
	{
		HAL_GPIO_WritePin(Led_Verte_GPIO_Port, Led_Verte_Pin, GPIO_PIN_SET);
	}
	if (data_buffer[0] == '0')
	{
		HAL_GPIO_WritePin(Led_Verte_GPIO_Port, Led_Verte_Pin, GPIO_PIN_RESET);
	}
}

/****FIN DU FICHIER****/

/*
 * Driver_OBCP.c
 *
 *  Created on: 20 nov. 2022
 *      Author: Joel IMBAUD
 *      SUPMICROTECH ENSMM www.supmicrotech.fr
 */

/* Includes ------------------------------------------------------------------*/
#include <includes.h>

/* Private variables ---------------------------------------------------------*/
uint8_t Acc_timer = 0;
uint8_t user_button_pressed = 0;
uint32_t* USB_length =0;
uint8_t* UsbBuffer;
int iusb = 0;
char UsbBuffer2[10];
uint8_t RxUSB_OK = 0;
int8_t USB_Connected;

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
LIS3DSH_DataRaw myData;
LIS3DSH_DataScaled MyDataScale;

/* Volatile type is used to avoid optimization on a certain variable (useful with interrupts) */
volatile uint8_t user_button_init_state = 1;

/* Function prototypes -----------------------------------------------*/

//Reset all the PWM to zero, disable 1s timer
void reset_OBCP_functions(void)
{
	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0);
	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);
	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, 0);
	__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, 0);
	HAL_TIM_Base_Stop_IT(&htim7);
}

//Interrupt function for the pushbutton
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
	if (GPIO_Pin == BLUE_BUTTON_PIN) {
		/* Set the User Button flag */
		user_button_pressed = 1;
	}
}

//Interrupt function for the 1s timer
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
	Acc_timer = 1;
}

void Printf_BLE(char*  data_buffer)
{
	size_t length = strlen(data_buffer);
	int modulo20 = length%20;
	int number20 = length/20;
	char Temp[20];
	int i=0;
	int time;
	//Print on BLE by 20 characters packets which is the max payload for BLE UART
	for (i = 0; i < number20 ; i++)
	{
		strncpy(Temp, &data_buffer[i*20], 20);
		time = HAL_GetTick();
		//Wait the Hal_ok (0x0) feedback to continue with a 10 ms time out
		while(sendDataDef((uint8_t*)Temp, 20))
		{
			if (HAL_GetTick()-time > 10) break;
		}
		time = HAL_GetTick();
		//All the 128 char sent wait 20ms to empty the internal BLE buffer of 128
		if (i%5 == 0)
		{
			while((HAL_GetTick() - time) < 20 );
		}
	}
	strncpy(Temp, &data_buffer[20*i], modulo20);
	sendDataDef((uint8_t*)Temp, modulo20);
}

void User_Process(void)
{
	char Temp[20]="";
	if (set_connectable)
	{
		/* Establish connection with remote device */
		Make_Connection();
		set_connectable = FALSE;
		user_button_init_state = HAL_GPIO_ReadPin(BLUE_BUTTON_PORT, BLUE_BUTTON_PIN);
	}

	/* Check if the User Button has been pushed */
	if (user_button_pressed)
	{
		/* Software debouncing (likely not needed with the nucleo board) */
		HAL_Delay(50);
		/* Wait until the User Button is released */
		while (HAL_GPIO_ReadPin(BLUE_BUTTON_PORT, BLUE_BUTTON_PIN) == !user_button_init_state);
		/* Debouncing */
		HAL_Delay(50);
		/* Print on virtual com port USB */
		if (USB_Connected)
		{
			/* Send a command to the VCP USB if connected */
			CDC_Transmit_FS((uint8_t*)"Push button pressed\n\r", strlen("Push button pressed\n\r"));
		}
		if (BLE_connected)
		{
			/* Send a command to the BLE remote device if connected */
			sendDataDef((uint8_t*)"PButton pressed\n\r", strlen("PButton pressed\n\r"));
		}
		/* Reset the User Button flag */
		user_button_pressed = 0;
	}
	/* Check the timer flag every second with this config */
	if (Acc_timer)
	{
		if (BLE_connected)
		{
			// Send accelerometer and temperature values to BLE UART if connected
			MyDataScale = LIS3DSH_GetDataScaled();
			sprintf(Temp, "X%.2fY%.2fZ%.2f\n\r", MyDataScale.x, MyDataScale.y, MyDataScale.z);
			sendDataDef((uint8_t*)Temp, strlen(Temp));
			sprintf(Temp, "T = %i\n\r", LIS3DSH_GetTemperature());
			sendDataDef((uint8_t*)Temp, strlen(Temp));
		}
		/* Reset the 1s timer flag */
		Acc_timer = 0;
	}

	if (USB_Connected == 1)
	{
		/* Detect USB connexion and send a command to the BLE remote device if connected */
		if (BLE_connected)
		{
			sendDataDef((uint8_t*)"USB Connected\n\r", strlen("USB Connected\n\r"));
			//Increment USB_connected to avoid repetition
			USB_Connected = 2;
		}
	}
}
/*
 * BlueNRG-MS background task
 */
void Bluetooh_LE_Process(void)
{
	User_Process();
	hci_user_evt_proc();
}
/* Function about PWM timer on BLE command received */
void BLE_command_received(uint8_t* data_buffer)
{
	char commande;
	int valeur = 0;
	char Temp[20]="";
	sscanf((char*)data_buffer, "%c%i", &commande, &valeur);
	switch(commande)
	{
	case 'R':
	case 'r':
		__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, valeur);
		sprintf(Temp, "R%i\n\r", valeur);
		sendDataDef((uint8_t*)Temp, strlen(Temp));
		if (USB_Connected)
		{
			sprintf(Temp, "BLE Red = %i\n\r", valeur);
			CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
		}
		break;
	case 'V':
	case 'v':
		__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, valeur);
		sprintf(Temp, "G%i\n\r", valeur);
		sendDataDef((uint8_t*)Temp, strlen(Temp));
		if (USB_Connected)
		{
			sprintf(Temp, "BLE Green = %i\n\r", valeur);
			CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
		}
		break;
	case 'B':
	case 'b':
		__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, valeur);
		sprintf(Temp, "B%i\n\r", valeur);
		sendDataDef((uint8_t*)Temp, strlen(Temp));
		if (USB_Connected)
		{
			sprintf(Temp, "BLE Blue = %i\n\r", valeur);
			CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
		}
		break;
	case 'M':
	case 'm':
		__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, valeur);
		sprintf(Temp, "PWM%i\n\r", valeur);
		sendDataDef((uint8_t*)Temp, strlen(Temp));
		if (USB_Connected)
		{
			sprintf(Temp, "BLE PWM = %i\n\r", valeur);
			CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
		}
		break;
	case 'A':
	case 'a':
		MyDataScale = LIS3DSH_GetDataScaled();
		sprintf(Temp, "X%.2fY%.2fZ%.2f\n\r", MyDataScale.x, MyDataScale.y, MyDataScale.z);
		sendDataDef((uint8_t*)Temp, strlen(Temp));
		break;
	case 'T':
	case 't':
		sprintf(Temp, "T = %i\n\r", LIS3DSH_GetTemperature());
		sendDataDef((uint8_t*)Temp, strlen(Temp));
		break;
	case '?':
		Printf_BLE(help);
		break;

	default:
		if(strncmp((char*)data_buffer, "start timer", 11) == 0)
		{
			HAL_TIM_Base_Start_IT(&htim7);
			break;
		}
		if(strncmp((char*)data_buffer, "stop timer", 10) == 0)
		{
			HAL_TIM_Base_Stop_IT(&htim7);
			break;
		}
		if(strncmp((char*)data_buffer, "#rst", 4) == 0)
		{
			reset_OBCP_functions();
			break;
		}
		sendDataDef((uint8_t*)"False command\n\r", strlen("False command\n\r"));
		break;
	}
}

/* Function about PWM timer on virtual com port USB command received */
void USBCmd(uint8_t* data_buffer)
{
	char commande;
	int valeur = 0;
	char Temp[64]="";
	char TempBLE[20]="";
	sscanf((char*)data_buffer, "%c%i", &commande, &valeur);
	switch(commande)
	{
	case 'R':
	case 'r':
		__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, valeur);
		sprintf(Temp, "Red = %i\n\r", valeur);
		CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
		if (BLE_connected)
		{
			sprintf(TempBLE, "USB Red = %i\n\r", valeur);
			sendDataDef((uint8_t*)TempBLE, strlen(TempBLE));
		}
		break;
	case 'V':
	case 'v':
		__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, valeur);
		sprintf(Temp, "Green = %i\n\r", valeur);
		CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
		if (BLE_connected)
		{
			sprintf(TempBLE, "USB Green = %i\n\r", valeur);
			sendDataDef((uint8_t*)TempBLE, strlen(TempBLE));
		}
		break;
	case 'B':
	case 'b':
		__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, valeur);
		sprintf(Temp, "Blue = %i\n\r", valeur);
		CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
		if (BLE_connected)
		{
			sprintf(TempBLE, "USB Blue = %i\n\r", valeur);
			sendDataDef((uint8_t*)TempBLE, strlen(TempBLE));
		}
		break;
	case 'M':
	case 'm':
		__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, valeur);
		sprintf(Temp, "PWM = %i\n\r", valeur);
		CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
		if (BLE_connected)
		{
			sprintf(TempBLE, "USB PWM = %i\n\r", valeur);
			sendDataDef((uint8_t*)TempBLE, strlen(TempBLE));
		}
		break;
	case 'A':
	case 'a':
		MyDataScale = LIS3DSH_GetDataScaled();
		sprintf(Temp, "Acceleration : X = %.4f  Y = %.4f  Z = %.4f  \n\r", MyDataScale.x, MyDataScale.y, MyDataScale.z);
		CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
		break;
	case 'T':
	case 't':
		sprintf(Temp, "T = %i\n\r", LIS3DSH_GetTemperature());
		CDC_Transmit_FS((uint8_t*)Temp, strlen(Temp));
		break;
	case '?':
		while (CDC_Transmit_FS((uint8_t*)help, strlen(help)));
		break;

	default:
		if(strncmp((char*)data_buffer, "cls", 3) == 0)
		{
			// Clear the console screen with \e[1;1H\e[2J command (depending the terminal console)
			CDC_Transmit_FS((uint8_t*)"\e[1;1H\e[2J", strlen("\e[1;1H\e[2J"));
			break;
		}
		if(strncmp((char*)data_buffer, "#rst", 4) == 0)
		{
			reset_OBCP_functions();
			break;
		}
		CDC_Transmit_FS((uint8_t*)"False command\n\r", strlen("False command\n\r"));
		break;
	}
}
//Treatment of USB buffer depending if data arriving by packet or one by one
void receiveDataUSB(void)
{
	// Characters are sent by packets within the terminal.
	if(RxUSB_OK && USB_length[0] > 1)
	{
		RxUSB_OK = 0;
		USBCmd(UsbBuffer);
	}
	//The characters are sent by the terminal one by one
	if(RxUSB_OK && (USB_length[0] == 1))
	{
		RxUSB_OK = 0;
		UsbBuffer2[iusb] = UsbBuffer[0];
		iusb++;
		// Tests if the last character received is carriage return (CR) 0X0D (or line feed (LF) 0X0A) depending on the terminal config
		if(UsbBuffer2[iusb-1] == 0x0D)
		{
			iusb=0;
			USBCmd((uint8_t*)UsbBuffer2);
		}
	}
}

/****END OF FILE****/

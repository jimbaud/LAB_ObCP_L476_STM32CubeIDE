// inspired from https://github.com/MYaqoobEmbedded/STM32-Tutorials/tree/master/Tutorial%2027%20-%20Motion%203-Axis%20Accelerometer%20LIS3DSH
/*
Library:					Accelerometer - LIS3DH
 *      Author: Joel IMBAUD
 *      SUPMICROTECH ENSMM www.supmicrotech.fr
 */

//Header files
#include "includes.h"

//SPI Chip Select
#define _LIS3DHS_CS_ENBALE		HAL_GPIO_WritePin(ACC_CS_GPIO_Port, ACC_CS_Pin, GPIO_PIN_RESET);
#define _LIS3DHS_CS_DISABLE		HAL_GPIO_WritePin(ACC_CS_GPIO_Port, ACC_CS_Pin, GPIO_PIN_SET);

//Library variables

//1. SPI handle
static SPI_HandleTypeDef accSPI_Handle;
//2. Sensitivity value
static float lis3dsh_Sensitivity = LIS3DSH_SENSITIVITY_0_06G;
//3. bias variables
static float __X_Bias = 0.0f;
static float __Y_Bias = 0.0f;
static float __Z_Bias = 0.0f;
//4. scaling variables
static float __X_Scale = 1.0f;
static float __Y_Scale = 1.0f;
static float __Z_Scale = 1.0f;

//Functions definitions
//Private functions
//1. Write IO
void LIS3DSH_WriteIO(uint8_t reg, uint8_t *dataW, uint8_t size)
{
	uint8_t spiReg = reg;
	//Enable CS
	_LIS3DHS_CS_ENBALE;
	//set register value
	HAL_SPI_Transmit(&accSPI_Handle, &spiReg, 1, 10);
	//Transmit data
	HAL_SPI_Transmit(&accSPI_Handle, dataW, size, 10);
	//Disable CS
	_LIS3DHS_CS_DISABLE;
}
//2. Read IO
void LIS3DSH_ReadIO(uint8_t reg, uint8_t *dataR, uint8_t size)
{
	uint8_t spiBuf[4];
	spiBuf[0] = reg | 0xC0;//0x80;
	//Enable CS
	_LIS3DHS_CS_ENBALE;
	//set register value
	HAL_SPI_Transmit(&accSPI_Handle, spiBuf, 1, 10);

	//Transmit data
	HAL_SPI_Receive(&accSPI_Handle, spiBuf, size, 10);

	//Disable CS
	_LIS3DHS_CS_DISABLE;

	for(uint8_t i=0; i<(size&0x3); i++)
	{
		dataR[i] = spiBuf[i];
	}
}

//1. Accelerometer initialize function
void LIS3DSH_Init(SPI_HandleTypeDef *accSPI, LIS3DSH_InitTypeDef *accInitDef)
{
	uint8_t spiData = 0;
	memcpy(&accSPI_Handle, accSPI, sizeof(*accSPI));
	//** 1. Enable Axes and Output Data Rate **//
	//Set CTRL REG1 settings value
	spiData |= (accInitDef->enableAxes);	//Enable Axes
	spiData |= (accInitDef->dataRate);		//Output Data Rate
	//Write to accelerometer
	LIS3DSH_WriteIO(LIS3DSH_CTRL_REG1_ADDR, &spiData, 1);

	//** 2. Full-Scale selection, high resolution, self test and 4-wire SPI **//
	spiData = 0;
	spiData |= (accInitDef->fullScale | 0x88); // 0x88 for BDU = 1  (see doc p19) and HR = 1 (see doc p37)
	//Write to accelerometer
	LIS3DSH_WriteIO(LIS3DSH_CTRL_REG4_ADDR, &spiData, 1);

	// Configure register for internal temperature activating
	spiData = 0xC0;
	LIS3DSH_WriteIO(LIS3DH_TEMP_CFG_REG, &spiData, 1); // see lis3dh doc p19

	switch(accInitDef->fullScale)
	{
	case LIS3DSH_FULLSCALE_2:
		lis3dsh_Sensitivity = LIS3DSH_SENSITIVITY_0_06G;
		break;

	case LIS3DSH_FULLSCALE_4:
		lis3dsh_Sensitivity = LIS3DSH_SENSITIVITY_0_12G;
		break;

	case LIS3DSH_FULLSCALE_8:
		lis3dsh_Sensitivity = LIS3DSH_SENSITIVITY_0_24G;
		break;

	case LIS3DSH_FULLSCALE_16:
		lis3dsh_Sensitivity = LIS3DSH_SENSITIVITY_0_73G;
		break;
	}

}
//2. Get Accelerometer raw data
LIS3DSH_DataRaw LIS3DSH_GetDataRaw(void)
{
	uint8_t spiBuf[2];
	LIS3DSH_DataRaw tempDataRaw;

	//Read X data
	LIS3DSH_ReadIO(LIS3DSH_OUT_X_L_ADDR, spiBuf, 2);
	tempDataRaw.x = ((spiBuf[1] << 8) + spiBuf[0]);

	//Read Y data
	LIS3DSH_ReadIO(LIS3DSH_OUT_Y_L_ADDR, spiBuf, 2);
	tempDataRaw.y = ((spiBuf[1] << 8) + spiBuf[0]);

	//Read Z data
	LIS3DSH_ReadIO(LIS3DSH_OUT_Z_L_ADDR, spiBuf, 2);
	tempDataRaw.z = ((spiBuf[1] << 8) + spiBuf[0]);

	return tempDataRaw;

}
//3. Get Accelerometer mg data
LIS3DSH_DataScaled LIS3DSH_GetDataScaled(void)
{
	//Read raw data
	LIS3DSH_DataRaw tempRawData = LIS3DSH_GetDataRaw();
	//Scale data and return 
	LIS3DSH_DataScaled tempScaledData;
	tempScaledData.x = ((tempRawData.x * lis3dsh_Sensitivity * __X_Scale) + 0.0f - __X_Bias)/1000;
	tempScaledData.y = ((tempRawData.y * lis3dsh_Sensitivity * __Y_Scale) + 0.0f - __Y_Bias)/1000;
	tempScaledData.z = ((tempRawData.z * lis3dsh_Sensitivity * __Z_Scale) + 0.0f - __Z_Bias)/1000;

	return tempScaledData;
}

//** Calibration functions **//
//1. Set X-Axis calibrate
void LIS3DSH_X_calibrate(float x_min, float x_max)
{
	__X_Bias = (x_max+x_min)/2.0f;
	__X_Scale = (2*1000)/(x_max - x_min);
}
//2. Set Y-Axis calibrate
void LIS3DSH_Y_calibrate(float y_min, float y_max)
{
	__Y_Bias = (y_max+y_min)/2.0f;
	__Y_Scale = (2*1000)/(y_max - y_min);
}
//3. Set Z-Axis calibrate
void LIS3DSH_Z_calibrate(float z_min, float z_max)
{
	__Z_Bias = (z_max+z_min)/2.0f;
	__Z_Scale = (2*1000)/(z_max - z_min);
}

int LIS3DSH_GetTemperature(void)
{

	uint8_t spiBuf[2];
	int temperature;

	LIS3DSH_ReadIO(LIS3DH_OUT_ADC3_L, spiBuf, 2);
	temperature = (int8_t)(spiBuf[1]) + 25;

	return temperature;
}


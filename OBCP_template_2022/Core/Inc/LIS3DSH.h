/*
Library:					Accelerometer - LIS3DH
 *      Author: Joel IMBAUD
 *      SUPMICROTECH ENSMM www.supmicrotech.fr
 */

//Header files
#include <includes.h>

//List of Defines
//1. LIS3DHS registers addresses
#define LIS3DSH_WHO_AM_I_ADDR                0x0F
#define LIS3DSH_OFF_X_ADDR                   0x10
#define LIS3DSH_OFF_Y_ADDR                   0x11
#define LIS3DSH_OFF_Z_ADDR                   0x12

#define LIS3DSH_CTRL_REG1_ADDR               0x20
#define LIS3DSH_CTRL_REG2_ADDR               0x21
#define LIS3DSH_CTRL_REG3_ADDR               0x22
#define LIS3DSH_CTRL_REG4_ADDR               0x23
#define LIS3DSH_CTRL_REG5_ADDR               0x24
#define LIS3DSH_CTRL_REG6_ADDR               0x25

#define LIS3DSH_STATUS_ADDR                  0x27

#define LIS3DSH_OUT_X_L_ADDR                 0x28
#define LIS3DSH_OUT_X_H_ADDR                 0x29
#define LIS3DSH_OUT_Y_L_ADDR                 0x2A
#define LIS3DSH_OUT_Y_H_ADDR                 0x2B
#define LIS3DSH_OUT_Z_L_ADDR                 0x2C
#define LIS3DSH_OUT_Z_H_ADDR                 0x2D
#define LIS3DH_OUT_ADC3_L					 0x0C
#define LIS3DH_OUT_ADC3_H   				 0x0D
#define LIS3DH_TEMP_CFG_REG                  0x1F

//2. Data rate
#define LIS3DSH_DATARATE_POWERDOWN           ((uint8_t)0x00)  /* Power Down Mode*/
#define LIS3DSH_DATARATE_1             		 ((uint8_t)0x10)  /* 1 Hz Normal Mode */
#define LIS3DSH_DATARATE_10                	 ((uint8_t)0x20)  /* 10  Hz Normal Mode */
#define LIS3DSH_DATARATE_25                  ((uint8_t)0x30)  /* 25    Hz Normal Mode */
#define LIS3DSH_DATARATE_50                  ((uint8_t)0x40)  /* 50    Hz Normal Mode */
#define LIS3DSH_DATARATE_100                 ((uint8_t)0x50)  /* 100   Hz Normal Mode */
#define LIS3DSH_DATARATE_200                 ((uint8_t)0x60)  /* 200   Hz Normal Mode */
#define LIS3DSH_DATARATE_400                 ((uint8_t)0x70)  /* 400   Hz Normal Mode */

//3. Full scale
#define LIS3DSH_FULLSCALE_2                  ((uint8_t)0x00)  /* 2 g  */
#define LIS3DSH_FULLSCALE_4                  ((uint8_t)0x10)  /* 4 g  */
#define LIS3DSH_FULLSCALE_8                  ((uint8_t)0x20)  /* 8 g  */
#define LIS3DSH_FULLSCALE_16                 ((uint8_t)0x30)  /* 16 g */

//4. Enabled axis
#define LIS3DSH_X_ENABLE                     ((uint8_t)0x01)
#define LIS3DSH_Y_ENABLE                     ((uint8_t)0x02)
#define LIS3DSH_Z_ENABLE                     ((uint8_t)0x04)
#define LIS3DSH_XYZ_ENABLE                   ((uint8_t)0x07)

//5. Sensitivity values (Based on Full-Scale)
#define LIS3DSH_SENSITIVITY_0_06G            0.06  /* 0.06 mg/digit*/
#define LIS3DSH_SENSITIVITY_0_12G            0.12  /* 0.12 mg/digit*/
#define LIS3DSH_SENSITIVITY_0_24G            0.24  /* 0.24 mg/digit*/
#define LIS3DSH_SENSITIVITY_0_73G            0.73  /* 0.73 mg/digit*/

//Typedefs
//1. Accelerometer Configuration struct
typedef struct 
{
	uint8_t dataRate;
	uint8_t fullScale;
	uint8_t enableAxes;
}LIS3DSH_InitTypeDef;

//2. Accelerometer raw data
typedef struct 
{
	int16_t x;
	int16_t y;
	int16_t z;
}LIS3DSH_DataRaw;

//3. Accelerometer mg data (scaled data)
typedef struct
{
	float x;
	float y;
	float z;
}LIS3DSH_DataScaled;

//Functions prototypes
//Private functions
//1. Write IO
void LIS3DSH_WriteIO(uint8_t reg, uint8_t *dataW, uint8_t size);
//2. Read IO
void LIS3DSH_ReadIO(uint8_t reg, uint8_t *dataR, uint8_t size);

//1. Accelerometer initialise function
void LIS3DSH_Init(SPI_HandleTypeDef *accSPI, LIS3DSH_InitTypeDef *accInitDef);
//2. Get Accelerometer raw data
LIS3DSH_DataRaw LIS3DSH_GetDataRaw(void);
//3. Get Accelerometer mg data
LIS3DSH_DataScaled LIS3DSH_GetDataScaled(void);

//** Calibration functions **//
//1. Set X-Axis calibrate
void LIS3DSH_X_calibrate(float x_min, float x_max);
//2. Set Y-Axis calibrate
void LIS3DSH_Y_calibrate(float y_min, float y_max);
//3. Set Z-Axis calibrate
void LIS3DSH_Z_calibrate(float z_min, float z_max);
// Get internal temperature
int LIS3DSH_GetTemperature(void);

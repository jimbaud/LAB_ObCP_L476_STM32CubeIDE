/** @file main.h
 * @brief Sous-ensemble HAL factice utilisé seulement pour les tests PC du tactile.
 * Les valeurs de bits reproduisent les champs contrôlés par les pilotes. Les fonctions
 * sont implémentées dans test_touch_bus.c. Ce fichier ne remplace jamais la HAL sur STM32.
 * Les petits handles conservent uniquement les champs effectivement lus par les tests.
 */
#ifndef TEST_MOCK_HAL_H
#define TEST_MOCK_HAL_H

#include <stdint.h>

/** @brief Résultats HAL nécessaires aux scénarios, sans la bibliothèque STM32 réelle. */
typedef enum {
    HAL_OK, /**< Transaction simulée réussie. */
    HAL_ERROR, /**< Erreur volontairement injectée. */
} HAL_StatusTypeDef;
/** @brief Niveaux GPIO, zéro sélectionnant le tactile SPI. */
typedef enum {
    GPIO_PIN_RESET, /**< Niveau bas. */
    GPIO_PIN_SET, /**< Niveau haut. */
} GPIO_PinState;
/** @brief Le pilote tactile ne consulte ici que le registre CR1 du SPI. */
typedef struct {
    uint32_t CR1; /**< Champs de fréquence, phase, polarité et activation simulés. */
} SPI_TypeDef;
/** @brief Handle minimal partageant la même écriture d’accès que la HAL. */
typedef struct {
    SPI_TypeDef *Instance; /**< Adresse des registres SPI simulés. */
} SPI_HandleTypeDef;
/** @brief Handle I²C minimal pour tester le bit BUSY. */
typedef struct {
    uint32_t flags; /**< État virtuel du bus. */
} I2C_HandleTypeDef;

/* Identifiants de port et broche symboliques. Les tests vérifient qu’ils sont utilisés.
 * Les champs SPI reproduisent les positions des bits touchés par stmpe610.c.
 * Les macros d’accès opèrent sur les registres simulés comme leurs équivalents HAL. */
#define TOUCH_SPI_CS_GPIO_Port 1U
#define TOUCH_SPI_CS_Pin 0x0200U
#define SPI_CR1_CPHA UINT32_C(0x01)
#define SPI_CR1_CPOL UINT32_C(0x02)
#define SPI_CR1_BR UINT32_C(0x38)
#define SPI_CR1_SPE UINT32_C(0x40)
#define SPI_BAUDRATEPRESCALER_64 UINT32_C(0x28)
#define SPI_PHASE_1EDGE UINT32_C(0)
#define SPI_PHASE_2EDGE SPI_CR1_CPHA
#define I2C_FLAG_BUSY UINT32_C(1)
#define I2C_MEMADD_SIZE_8BIT UINT32_C(1)
#define READ_BIT(reg, bits) ((reg) & (bits))
#define MODIFY_REG(reg, mask, value) ((reg) = ((reg) & ~(mask)) | (value))
#define __HAL_SPI_DISABLE(spi) ((spi)->Instance->CR1 &= ~SPI_CR1_SPE)
#define __HAL_I2C_GET_FLAG(i2c, flag) ((i2c)->flags & (flag))

extern SPI_HandleTypeDef hspi1;
extern I2C_HandleTypeDef hi2c1;
/* Implémentations documentées dans test_touch_bus.c. Les délais utilisent le temps
 * virtuel, les buffers reçoivent les réponses synthétiques propres au scénario. */
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t ms);
void HAL_GPIO_WritePin(unsigned port, uint16_t pin, GPIO_PinState state);
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *spi, uint8_t *tx, uint8_t *rx,
                                          uint16_t count, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *i2c, uint16_t addr, uint16_t reg,
                                  uint16_t regsize, uint8_t *rx, uint16_t count, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *i2c, uint16_t addr, uint8_t *tx,
                                         uint16_t count, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *i2c, uint16_t addr, uint8_t *rx,
                                        uint16_t count, uint32_t timeout);

#endif

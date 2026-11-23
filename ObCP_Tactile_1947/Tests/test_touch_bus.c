/** @file test_touch_bus.c
 * @brief Banc pédagogique sur PC avec faux matériel et assertions.
 * Une assertion exprime le résultat attendu. SAN active ASan/UBSan dans Tests/Makefile.
 */
#include "obcp_panel.h"
#include "obcp_touch.h"
#include "main.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/** @brief Matériel virtuel choisi par chaque scénario du banc tactile. */
typedef enum {
    ABSENT, /**< Aucun contrôleur ne répond. */
    CAPACITIVE, /**< Registres FT62xx sur I²C. */
    RESISTIVE_I2C, /**< Conversions et pression TSC2007 sur I²C. */
    RESISTIVE_SPI, /**< Registres et FIFO STMPE610 sur SPI. */
    OTHER_I2C, /**< Périphérique qui répond, mais avec un format de conversion incorrect. */
} Fixture;
/* Registres HAL minimaux et état du bus. selected suit le niveau de CS,
 * lcd_busy interdit un accès tactile pendant le transfert TFT.
 * tick et conversion_started vérifient que l’ADC reçoit son temps de stabilisation.
 * Les compteurs mesurent les transactions réellement demandées par le pilote. */
static Fixture fixture;
static SPI_TypeDef registers;
SPI_HandleTypeDef hspi1 = {&registers};
I2C_HandleTypeDef hi2c1;
static bool lcd_busy, selected, pen_down, bus_error;
static uint32_t tick, conversion_started, legacy_phase;
static uint8_t command, legacy_ctrl, fifo_count, fifo_byte;
static unsigned spi_transactions, tsc_commands;
enum { TFT_SETTINGS = 0x10 }; /* Prescaler /8, mode 0. */

/**
 * @brief Réinitialiser les registres, les compteurs et le type de contrôleur du bus simulé.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 * @param[in] hardware Famille de contrôleur à émuler.
 */
static void prepare(Fixture hardware)
{
    fixture = hardware;
    registers.CR1 = TFT_SETTINGS;
    hi2c1.flags = 0;
    lcd_busy = selected = pen_down = bus_error = false;
    legacy_phase = SPI_PHASE_2EDGE;
    legacy_ctrl = fifo_count = fifo_byte = 0;
    spi_transactions = tsc_commands = 0;
    tick = 0;
}

/**
 * @brief Renvoyer la disponibilité simulée du SPI pour vérifier que le tactile attend le TFT.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 * @return true si l’opération simulée réussit.
 */
bool lcd_spi_available(void) { return !lcd_busy; }
/**
 * @brief Lire la base de temps virtuelle avancée explicitement par les scénarios.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 * @return Tick virtuel courant, en millisecondes.
 */
uint32_t HAL_GetTick(void) { return tick; }
/**
 * @brief Avancer le temps virtuel sans suspendre réellement le processus de test.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 * @param[in] ms Durée en millisecondes virtuelles.
 */
void HAL_Delay(uint32_t ms) { tick += ms; }

/**
 * @brief Vérifier la sélection et la désélection du STMPE610 sans chevauchement avec le TFT.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 * @param[in] port Port GPIO symbolique du faux matériel.
 * @param[in] pin Masque de broche de sélection tactile.
 * @param[in] state Niveau de sortie demandé.
 */
void HAL_GPIO_WritePin(unsigned port, uint16_t pin, GPIO_PinState state)
{
    assert(port == TOUCH_SPI_CS_GPIO_Port && pin == TOUCH_SPI_CS_Pin);
    assert(!lcd_busy && selected == (state == GPIO_PIN_SET));
    selected = state == GPIO_PIN_RESET;
}

/**
 * @brief Émuler les registres et la FIFO STMPE610 en contrôlant le mode SPI et la sélection.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 * @param[in] spi Handle SPI simulé.
 * @param[in] tx Octets de commande émis par le pilote.
 * @param[out] rx Tampon recevant la réponse simulée.
 * @param[in] count Nombre d’octets de la transaction.
 * @param[in] timeout Délai maximal HAL en millisecondes, contrôlé par assertion.
 * @return HAL_OK ou HAL_ERROR selon le scénario.
 */
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *spi, uint8_t *tx, uint8_t *rx,
                                          uint16_t count, uint32_t timeout)
{
    assert(spi == &hspi1 && selected && !lcd_busy && timeout <= 2U);
    assert((registers.CR1 & SPI_CR1_BR) == SPI_BAUDRATEPRESCALER_64);
    ++spi_transactions;
    memset(rx, 0xFF, count);
    if (bus_error) {
        return HAL_ERROR;
    }
    if (fixture != RESISTIVE_SPI || (registers.CR1 & SPI_CR1_CPHA) != legacy_phase) {
        return HAL_OK;
    }
    if (count == 2U) {
        if (tx[0] == 0x40U) {
            legacy_ctrl = tx[1];
        }
        if (tx[0] == 0x4BU && tx[1] == 1U) {
            fifo_count = fifo_byte = 0;
        }
        return HAL_OK;
    }
    assert(count == 3U && tx[1] == 0U && tx[2] == 0U);
    switch (tx[0]) {
    case 0x80: rx[2] = 0x08; break;
    case 0x81: rx[2] = 0x11; break;
    case 0xC0: rx[2] = (uint8_t)(legacy_ctrl | (pen_down ? 0x80U : 0U)); break;
    case 0xCC: rx[2] = fifo_count; break;
    case 0xD7: {
        static const uint8_t point[] = {0x12, 0x34, 0x56, 0x3C};
        assert(fifo_count > 0U);
        rx[2] = point[fifo_byte++];
        if (fifo_byte == 4U) {
            fifo_byte = 0;
            --fifo_count;
        }
        break;
    }
    default: assert(false); break;
    }
    return HAL_OK;
}

/**
 * @brief Émuler l’identifiant et le premier contact du FT62xx à l’adresse 0x38.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 * @param[in] i2c Handle I²C simulé.
 * @param[in] addr Adresse I²C décalée au format attendu par la HAL.
 * @param[in] reg Adresse de registre à émuler.
 * @param[in] regsize Largeur d’adresse de registre HAL.
 * @param[out] rx Tampon recevant la réponse simulée.
 * @param[in] count Nombre d’octets de la transaction.
 * @param[in] timeout Délai maximal HAL en millisecondes, contrôlé par assertion.
 * @return HAL_OK ou HAL_ERROR selon le scénario.
 */
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *i2c, uint16_t addr, uint16_t reg,
                                  uint16_t regsize, uint8_t *rx, uint16_t count, uint32_t timeout)
{
    assert(i2c == &hi2c1 && addr == (0x38U << 1) && regsize == I2C_MEMADD_SIZE_8BIT && timeout <= 4U);
    if (fixture != CAPACITIVE || bus_error) {
        return HAL_ERROR;
    }
    if (reg == 0xA3U) {
        assert(count == 1U);
        rx[0] = 0x06U;
    } else {
        assert(reg == 0x02U && count == 5U);
        const uint8_t touch[] = {1, 0, 99, 1, 0x20};
        memcpy(rx, touch, sizeof(touch));
    }
    return HAL_OK;
}

/**
 * @brief Mémoriser une commande TSC2007 et son instant de départ pour contrôler l’attente de conversion.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 * @param[in] i2c Handle I²C simulé.
 * @param[in] addr Adresse I²C décalée au format attendu par la HAL.
 * @param[in] tx Octets de commande émis par le pilote.
 * @param[in] count Nombre d’octets de la transaction.
 * @param[in] timeout Délai maximal HAL en millisecondes, contrôlé par assertion.
 * @return HAL_OK ou HAL_ERROR selon le scénario.
 */
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *i2c, uint16_t addr, uint8_t *tx,
                                         uint16_t count, uint32_t timeout)
{
    assert(i2c == &hi2c1 && addr == (0x48U << 1) && count == 1U && timeout <= 2U);
    if ((fixture != RESISTIVE_I2C && fixture != OTHER_I2C) || bus_error) {
        return HAL_ERROR;
    }
    command = tx[0];
    ++tsc_commands;
    conversion_started = tick;
    return HAL_OK;
}

/**
 * @brief Émuler une conversion 12 bits, la pression ou une réponse de format volontairement incorrect.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 * @param[in] i2c Handle I²C simulé.
 * @param[in] addr Adresse I²C décalée au format attendu par la HAL.
 * @param[out] rx Tampon recevant la réponse simulée.
 * @param[in] count Nombre d’octets de la transaction.
 * @param[in] timeout Délai maximal HAL en millisecondes, contrôlé par assertion.
 * @return HAL_OK ou HAL_ERROR selon le scénario.
 */
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *i2c, uint16_t addr, uint8_t *rx,
                                        uint16_t count, uint32_t timeout)
{
    assert(i2c == &hi2c1 && addr == (0x48U << 1) && count == 2U && timeout <= 2U);
    if (bus_error) {
        return HAL_ERROR;
    }
    assert(tick - conversion_started >= (command == 0U ? 1U : 2U));
    uint16_t value = 0;
    switch (command) {
    case 0x00: value = 810; break;
    case 0xC4: value = 2330; break;
    case 0xD4: value = 540; break;
    case 0xE4: value = pen_down ? 500 : 0; break;
    case 0xF4: value = pen_down ? 880 : 4095; break;
    default: assert(false); break;
    }
    rx[0] = (uint8_t)(value >> 4);
    rx[1] = (uint8_t)(value << 4);
    if (fixture == OTHER_I2C) {
        rx[1] |= 1U;
    }
    return HAL_OK;
}

/**
 * @brief Rappeler le pilote jusqu’à une observation complète en avançant le tick de manière contrôlée.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 * @param[in] device Contrôleur déjà reconnu par le sondage.
 * @return Observation tactile brute terminée.
 */
static RawTouch complete_sample(const TouchDevice *device)
{
    RawTouch sample;
    for (unsigned i = 0; i < 25U; ++i) {
        TouchRead state = touch_read(device, &sample);
        if (state == TOUCH_READ_OK) {
            return sample;
        }
        assert(state == TOUCH_READ_WAIT);
        ++tick;
    }
    assert(false);
    return (RawTouch){0};
}

/**
 * @brief Exécuter les assertions du banc PC, puis produire les captures si un dossier est fourni.
 *
 * Les assertions signalent le premier écart. Ce code est exécuté sur PC,
 * avec des ports simulés et sans accès à une carte STM32.
 * @return Zéro si tous les scénarios atteignent leur fin.
 */
int main(void)
{
    TouchDevice device;
    RawTouch sample;
    prepare(ABSENT);
    lcd_busy = true;
    assert(!touch_probe(&device) && spi_transactions == 0U);
    lcd_busy = false;
    assert(touch_probe(&device) && device.controller == TOUCH_CTRL_NONE);

    prepare(CAPACITIVE);
    assert(touch_probe(&device) && device.controller == TOUCH_CTRL_FT62XX && device.id == 6U);
    assert(spi_transactions == 0U && tsc_commands == 0U);
    sample = complete_sample(&device);
    assert(sample.down && sample.point.x == 99U && sample.point.y == 288U);

    prepare(OTHER_I2C);
    assert(touch_probe(&device) && device.controller == TOUCH_CTRL_NONE);

    prepare(RESISTIVE_I2C);
    assert(touch_probe(&device) && device.controller == TOUCH_CTRL_TSC2007);
    pen_down = true;
    tick = UINT32_MAX - 1U;
    sample = complete_sample(&device);
    assert(sample.down && sample.point.x == 2330U && sample.point.y == 540U);
    assert(sample.z1 == 500U && sample.z2 == 880U && command == 0U);
    pen_down = false;
    sample = complete_sample(&device);
    assert(!sample.down && sample.z1 == 0U && sample.z2 == 4095U);
    tick += 2;
    assert(touch_read(&device, &sample) == TOUCH_READ_WAIT);
    tick += 2;
    bus_error = true;
    assert(touch_read(&device, &sample) == TOUCH_READ_ERROR);

    for (unsigned mode = 0; mode < 2U; ++mode) {
        prepare(RESISTIVE_SPI);
        legacy_phase = mode == 0U ? SPI_PHASE_1EDGE : SPI_PHASE_2EDGE;
        assert(touch_probe(&device) && device.controller == TOUCH_CTRL_STMPE610 && device.id == 0x0811U);
        assert(registers.CR1 == TFT_SETTINGS && !selected && tsc_commands == 0U);
        lcd_busy = true;
        unsigned before = spi_transactions;
        assert(touch_read(&device, &sample) == TOUCH_READ_WAIT && spi_transactions == before);
        lcd_busy = false;
        pen_down = true;
        assert(touch_read(&device, &sample) == TOUCH_READ_EMPTY);
        fifo_count = 12U;
        sample = complete_sample(&device);
        assert(sample.down && sample.point.x == 0x123U && sample.point.y == 0x456U);
        assert(sample.z1 == 0x3CU && fifo_count == 0U);
        assert(registers.CR1 == TFT_SETTINGS && !selected);
        pen_down = false;
        assert(!complete_sample(&device).down);
        bus_error = true;
        assert(touch_read(&device, &sample) == TOUCH_READ_ERROR);
        assert(registers.CR1 == TFT_SETTINGS && !selected);
    }
    puts("BUS TACTILE FT62xx, STMPE610 modes 0/1, TSC2007, absence, erreur et SPI partage");
    return 0;
}

/**
 ******************************************************************************
 * @file    oa_board.c
 * @brief   Interface matérielle de l'ObCP pour le jeu : bus, sélections,
 *          DMA de l'écran, temps, LED, bouton et liaison série.
 *
 *  Seul fichier du jeu qui appelle la HAL. Les poignées (hspi1, hi2c1,
 *  htim2...) et les noms de broches (LCD_CS_Pin...) viennent du code généré
 *  par CubeMX (spi.h, i2c.h, tim.h, usart.h, main.h).
 *
 *  Partage de SPI1 : l'écran ILI9341 et le tactile résistif STMPE610 du
 *  shield sont sur le même bus. Pendant un envoi DMA vers l'écran, toute
 *  transaction SPI1 est refusée (false) : l'appelant réessaie au tour suivant.
 *  La fréquence et le mode SPI sont reconfigurés seulement bus libre.
 *
 *  SPDX-License-Identifier: MIT
 ******************************************************************************
 */
#include "oa_board.h"
#include "main.h"
#include "i2c.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"

/** Sélection (CS, active à l'état bas) de chaque périphérique SPI, dans
 *  l'ordre de OaDevice : écran, tactile STMPE610, accéléromètre, EEPROM. */
static GPIO_TypeDef *const cs_port[] = {LCD_CS_GPIO_Port, TOUCH_CS_GPIO_Port, ACC_CS_GPIO_Port,
                                        EEPROM_CS_GPIO_Port};
static const uint16_t cs_pin[] = {LCD_CS_Pin, TOUCH_CS_Pin, ACC_CS_Pin, EEPROM_CS_Pin};

static volatile bool display_busy, display_fault;
static uint32_t display_started, lcd_hz = 10000000;

static void select(OaDevice d, bool on) {
    HAL_GPIO_WritePin(cs_port[d], cs_pin[d], on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

/** @brief Adapte diviseur, CPOL/CPHA et sens d'un SPI, sans dépasser max_hz.
 *  @return false si le bus est occupé ou si HAL_SPI_Init échoue. */
static bool configure(SPI_HandleTypeDef *s, uint32_t max_hz, unsigned mode, bool tx_only) {
    if (HAL_SPI_GetState(s) != HAL_SPI_STATE_READY)
        return false;
    uint32_t clock = s->Instance == SPI1 ? HAL_RCC_GetPCLK2Freq() : HAL_RCC_GetPCLK1Freq();
    unsigned shift = 0;
    while (shift < 7 && clock / (2u << shift) > max_hz)
        shift++;
    uint32_t br = shift << 3, cpol = mode & 2 ? SPI_POLARITY_HIGH : SPI_POLARITY_LOW,
             cpha = mode & 1 ? SPI_PHASE_2EDGE : SPI_PHASE_1EDGE;
    uint32_t direction = tx_only ? SPI_DIRECTION_1LINE : SPI_DIRECTION_2LINES;
    if (s->Init.BaudRatePrescaler == br && s->Init.CLKPolarity == cpol &&
        s->Init.CLKPhase == cpha && s->Init.Direction == direction)
        return true;
    s->Init.BaudRatePrescaler = br;
    s->Init.CLKPolarity = cpol;
    s->Init.CLKPhase = cpha;
    s->Init.Direction = direction;
    return HAL_SPI_Init(s) == HAL_OK;
}

/* ---- Temps ---------------------------------------------------------------- */

void oa_board_start_timebase(void) {
    (void)HAL_TIM_Base_Start(&htim2);
}

uint32_t oa_board_us(void) {
    return __HAL_TIM_GET_COUNTER(&htim2);
}

uint32_t oa_board_ms(void) {
    return HAL_GetTick();
}

void oa_board_delay_ms(uint32_t ms) {
    HAL_Delay(ms);
}

static void delay_us(void *ctx, uint32_t us) {
    (void)ctx;
    uint32_t begin = oa_board_us();
    while (oa_board_us() - begin < us) {
    }
}

/* ---- Transactions des pilotes (OaIo) -------------------------------------- */

static bool spi(void *ctx, OaDevice d, const uint8_t *tx, uint8_t *rx, size_t n, uint32_t hz,
                unsigned mode) {
    (void)ctx;
    if (d > OA_DEV_EEPROM || !n || n > 65535 || !tx || !rx)
        return false;
    SPI_HandleTypeDef *s = d == OA_DEV_ACCEL ? &hspi3 : (d == OA_DEV_EEPROM ? &hspi2 : &hspi1);
    if (s == &hspi1 && display_busy)
        return false;
    if (!configure(s, hz, mode, false))
        return false;
    select(d, true);
    bool ok = HAL_SPI_TransmitReceive(s, (uint8_t *)tx, rx, (uint16_t)n, 5) == HAL_OK;
    select(d, false);
    return ok;
}

/* Les adresses I2C sont données sur 7 bits ; la HAL les attend décalées. */
static bool i2c_write(void *ctx, uint8_t address, const uint8_t *p, size_t n) {
    (void)ctx;
    if (n > 255)
        return false;
    uint16_t addr = (uint16_t)(address << 1);
    if (!n)
        return HAL_I2C_IsDeviceReady(&hi2c1, addr, 1, 3) == HAL_OK;
    return HAL_I2C_Master_Transmit(&hi2c1, addr, (uint8_t *)p, (uint16_t)n, 5) == HAL_OK;
}

static bool i2c_read(void *ctx, uint8_t address, uint8_t *p, size_t n) {
    (void)ctx;
    return n <= 255 &&
           HAL_I2C_Master_Receive(&hi2c1, (uint16_t)(address << 1), p, (uint16_t)n, 5) == HAL_OK;
}

OaIo oa_board_io(void) {
    return (OaIo){NULL, spi, i2c_write, i2c_read, delay_us};
}

/* ---- Écran ---------------------------------------------------------------- */

bool oa_board_display_busy(void) {
    if (display_busy && oa_board_us() - display_started > 200000) {
        (void)HAL_SPI_Abort(&hspi1);
        select(OA_DEV_LCD, false);
        display_busy = false;
        display_fault = true;
    }
    return display_busy;
}

bool oa_board_display_error(void) {
    bool fault = display_fault;
    display_fault = false;
    return fault;
}

void oa_board_lcd_fast(bool fast) {
    lcd_hz = fast ? 40000000 : 10000000;
}

bool oa_board_lcd_command(uint8_t command, const uint8_t *p, size_t n) {
    if (oa_board_display_busy() || n > 65535 || !configure(&hspi1, 10000000, 0, false))
        return false;
    select(OA_DEV_LCD, true);
    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_RESET); /* D/C = 0 : commande */
    bool ok = HAL_SPI_Transmit(&hspi1, &command, 1, 5) == HAL_OK;
    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET); /* D/C = 1 : données */
    if (ok && n)
        ok = HAL_SPI_Transmit(&hspi1, (uint8_t *)p, (uint16_t)n, 10) == HAL_OK;
    select(OA_DEV_LCD, false);
    return ok;
}

bool oa_board_lcd_dma(unsigned x, unsigned y, unsigned w, unsigned h, const uint8_t *bytes) {
    if (!w || !h || x + w > 320 || y + h > 240 || w * h * 2 > 5120 || oa_board_display_busy())
        return false;
    uint8_t col[4] = {(uint8_t)(x >> 8), (uint8_t)x, (uint8_t)((x + w - 1) >> 8),
                      (uint8_t)(x + w - 1)};
    uint8_t row[4] = {(uint8_t)(y >> 8), (uint8_t)y, (uint8_t)((y + h - 1) >> 8),
                      (uint8_t)(y + h - 1)};
    /* Fenêtre d'écriture (CASET, PASET) puis écriture en mémoire (RAMWR). */
    if (!oa_board_lcd_command(0x2A, col, 4) || !oa_board_lcd_command(0x2B, row, 4) ||
        !oa_board_lcd_command(0x2C, NULL, 0))
        return false;
    /* Au-delà de 24 MHz, le STM32 doit être en émission seule. Le mode
     * unidirectionnel utilise PA7/MOSI ; les lectures reviendront en duplex. */
    if (!configure(&hspi1, lcd_hz, 0, true))
        return false;
    select(OA_DEV_LCD, true);
    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);
    display_started = oa_board_us();
    display_busy = true;
    if (HAL_SPI_Transmit_DMA(&hspi1, (uint8_t *)bytes, (uint16_t)(w * h * 2)) != HAL_OK) {
        display_busy = false;
        display_fault = true;
        select(OA_DEV_LCD, false);
        return false;
    }
    return true;
}

/* Rappels de la HAL, appelés en interruption (DMA1 canal 3) à la fin ou en
 * cas d'erreur d'un envoi : ils ne font que libérer l'écran. */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *s) {
    if (s->Instance == SPI1) {
        select(OA_DEV_LCD, false);
        display_busy = false;
    }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *s) {
    if (s->Instance == SPI1) {
        select(OA_DEV_LCD, false);
        display_fault = true;
        display_busy = false;
    }
}

/* ---- Bouton, LED, liaison série ------------------------------------------- */

bool oa_board_button_raw(void) {
    return HAL_GPIO_ReadPin(USER_BUTTON_GPIO_Port, USER_BUTTON_Pin) == GPIO_PIN_RESET;
}

bool oa_board_button(bool *pressed) {
    static bool raw_previous, stable;
    static uint32_t changed;
    bool raw = oa_board_button_raw();
    if (raw != raw_previous) {
        raw_previous = raw;
        changed = HAL_GetTick();
    }
    *pressed = false;
    if (raw != stable && HAL_GetTick() - changed >= 25) {
        stable = raw;
        *pressed = stable;
    }
    return stable;
}

void oa_board_led(bool r, bool g, bool b) {
    HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, r ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, g ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin, b ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

int oa_board_uart_getchar(void) {
    /* Lecture directe des drapeaux, sans attente : un débordement (ORE) laissé
     * par des octets non lus est effacé pour que la réception continue. */
    if (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_ORE))
        __HAL_UART_CLEAR_FLAG(&huart2, UART_CLEAR_OREF);
    if (!__HAL_UART_GET_FLAG(&huart2, UART_FLAG_RXNE))
        return -1;
    return (int)(huart2.Instance->RDR & 0xFFu);
}

void oa_board_fatal(void) {
    __disable_irq();
    /* L'erreur peut survenir avant MX_GPIO_Init() (horloge HSE absente...) :
     * la broche de la LED rouge est alors configurée ici. */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef led = {0};
    led.Pin = LED_RED_Pin;
    led.Mode = GPIO_MODE_OUTPUT_PP;
    led.Pull = GPIO_NOPULL;
    led.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_RED_GPIO_Port, &led);
    HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_SET);
    for (;;) {
    }
}

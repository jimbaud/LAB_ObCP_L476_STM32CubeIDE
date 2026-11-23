/**
 * @file stmpe610.c
 * @brief Tactile résistif historique sur le bus SPI1 partagé.
 * @ingroup obcp_drivers
 *
 * Le pilote sauvegarde prescaler, polarité et phase du SPI avant son accès à 1 MHz, puis
 * les restaure pour le TFT à 8 MHz. Il lit au plus huit points FIFO par passage et utilise
 * le dernier point reçu.
 *
 * @par Repères de lecture
 * Les coordonnées écran sont en pixels. Les durées suffixées ms sont en
 * millisecondes. Les adresses du service de stockage sont des offsets en octets.
 */
/** @var spi_phase
 * @brief Phase SPI reconnue lors du sondage du STMPE610, à réappliquer aux acquisitions.
 */

#include "obcp_panel.h"
#include "touch_drivers.h"

#include "main.h"
#include "spi.h"

/** @brief Constantes privées de ce module, avec leurs unités. */
enum {
    STMPE_ID = 0x0811, /**< Signature 16 bits attendue du STMPE610. */
    STMPE_SYS_CTRL2 = 0x04, /**< Adresse du registre d’activation des blocs ADC et tactile. */
    STMPE_INT_CTRL = 0x09, /**< Adresse de configuration globale des interruptions du contrôleur. */
    STMPE_INT_EN = 0x0A, /**< Adresse du masque des sources d’interruption. */
    STMPE_INT_STA = 0x0B, /**< Adresse des événements à acquitter. */
    STMPE_ADC1 = 0x20, /**< Adresse des paramètres de conversion ADC. */
    STMPE_ADC2 = 0x21, /**< Adresse de la fréquence ADC. */
    STMPE_CTRL = 0x40, /**< Adresse de contrôle du tactile et du bit de contact. */
    STMPE_CFG = 0x41, /**< Adresse du délai de stabilisation et de la moyenne tactile. */
    STMPE_FIFO_STA = 0x4B, /**< Adresse de commande et d’état de la FIFO tactile. */
    STMPE_FIFO_SIZE = 0x4C, /**< Adresse du nombre de points présents dans la FIFO. */
    STMPE_FRACTION_Z = 0x56, /**< Adresse de répartition des bits de pression. */
    STMPE_DATA = 0x57, /**< Adresse d’accès non incrémenté aux coordonnées et à la pression. */
    STMPE_DRIVE = 0x58, /**< Adresse du courant de commande de la dalle. */
    STMPE_TOUCHED = 0x80, /**< Masque du bit de présence d’un contact. */
    STMPE_FIFO_RESET = 0x01, /**< Masque du bit de remise à zéro de la FIFO. */
    STMPE_MAX_POINTS = 8, /**< Au plus huit points retirés de la FIFO par appel. */
};

static uint32_t spi_phase;

/**
 * @brief Changer temporairement fréquence et phase du SPI1.
 *
 * Le périphérique est désactivé avant la modification des bits BR, CPHA et CPOL. Le masque
 * préserve les autres réglages matériels.
 * @param[in] settings Bits BR, CPHA et CPOL du SPI à appliquer.
 * @return Anciennes valeurs des bits modifiés, à restaurer après la transaction.
 */
static uint32_t set_spi(uint32_t settings)
{
    /* Sauvegarder uniquement les bits modifiés permet de rendre au TFT le même SPI1, après la transaction tactile à 1 MHz. */
    const uint32_t mask = SPI_CR1_BR | SPI_CR1_CPHA | SPI_CR1_CPOL;
    uint32_t previous = READ_BIT(hspi1.Instance->CR1, mask);
    __HAL_SPI_DISABLE(&hspi1);
    MODIFY_REG(hspi1.Instance->CR1, mask, settings);
    return previous;
}

/**
 * @brief Échanger un message sous la sélection tactile D8.
 *
 * Le TFT doit déjà avoir libéré SPI1. La sélection tactile est abaissée pour l'échange,
 * puis remontée même si la HAL signale une erreur.
 * @param[in] tx Octets à émettre, au moins len ou count cases selon la signature.
 * @param[out] rx Tampon de réception, de même capacité que le nombre d’octets échangés.
 * @param[in] count Nombre d’octets de l’opération ou de la transaction.
 * @return true si l'échange SPI se termine correctement.
 */
static bool transaction(uint8_t *tx, uint8_t *rx, uint16_t count)
{
    HAL_GPIO_WritePin(TOUCH_SPI_CS_GPIO_Port, TOUCH_SPI_CS_Pin, GPIO_PIN_RESET);
    HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(&hspi1, tx, rx, count, 2);
    HAL_GPIO_WritePin(TOUCH_SPI_CS_GPIO_Port, TOUCH_SPI_CS_Pin, GPIO_PIN_SET);
    return status == HAL_OK;
}

/**
 * @brief Lire un registre STMPE610 avec son octet factice.
 *
 * Le bit de lecture et l'adresse sont émis en premier. La valeur utile arrive au troisième
 * octet du protocole utilisé par ce composant.
 * @param[in] reg Adresse du registre du composant.
 * @param[out] value Pointeur valide recevant la valeur du registre ou de la conversion.
 * @return true si l'octet de registre est reçu.
 */
static bool reg_read(uint8_t reg, uint8_t *value)
{
    uint8_t tx[] = {(uint8_t)(0x80U | reg), 0, 0};
    uint8_t rx[sizeof(tx)];
    if (!transaction(tx, rx, sizeof(tx))) {
        return false;
    }
    *value = rx[2];
    return true;
}

/**
 * @brief Écrire un registre STMPE610.
 *
 * Émet une paire adresse-valeur, les données simultanément reçues étant ignorées.
 * @param[in] reg Adresse du registre du composant.
 * @param[in] value Valeur à utiliser dans ce traitement.
 * @return true si l'écriture SPI réussit.
 */
static bool reg_write(uint8_t reg, uint8_t value)
{
    uint8_t tx[] = {reg, value};
    uint8_t rx[sizeof(tx)];
    return transaction(tx, rx, sizeof(tx));
}

/**
 * @brief Appliquer les réglages ADC et FIFO du tactile.
 *
 * L'interrogation se fait par scrutation. La séquence règle résolution, moyenne, délais et
 * activation, puis vérifie le registre de contrôle.
 * @return true si la séquence et sa relecture de contrôle réussissent.
 */
static bool init_touch(void)
{
    /* Interrogation par scrutation. Laisser l'IRQ de la dalle désactivée. */
    static const uint8_t init[][2] = {
        {STMPE_SYS_CTRL2, 0x00}, {STMPE_CTRL, 0x00}, {STMPE_INT_CTRL, 0x00}, {STMPE_INT_EN, 0x00},
        {STMPE_ADC1, 0x68}, {STMPE_ADC2, 0x02}, /* 12 bits, 96 cycles à 6,5 MHz. */
        {STMPE_CFG, 0xA4},                     /* Moyenne de 4, délai 1 ms, stabilisation 5 ms. */
        {STMPE_FRACTION_Z, 0x06}, {STMPE_DRIVE, 0x01}, {STMPE_INT_STA, 0xFF},
        {STMPE_FIFO_STA, STMPE_FIFO_RESET}, {STMPE_FIFO_STA, 0}, {STMPE_CTRL, 0x01}
    };
    for (unsigned i = 0; i < sizeof(init) / sizeof(init[0]); ++i) {
        if (!reg_write(init[i][0], init[i][1])) {
            return false;
        }
    }
    uint8_t control;
    return reg_read(STMPE_CTRL, &control) && (control & 0x7FU) == 1U;
}

/*
 * Reconnaître 0x0811 et configurer le tactile.
 *
 * Les modes SPI zéro puis un sont essayés à 1 MHz. La phase reconnue est conservée pour
 * les acquisitions suivantes. Les réglages du TFT sont restaurés à la sortie.
 */
bool stmpe610_probe(uint16_t *chip)
{
    *chip = 0;
    if (!lcd_spi_available()) {
        return false;
    }
    /* SPI1 reçoit 64 MHz de l'APB2. Diviser par 64 donne 1 MHz. Les
     * révisions anciennes sont essayées en modes 0 puis 1. */
    uint32_t previous = set_spi(SPI_BAUDRATEPRESCALER_64);
    bool found = false;
    for (unsigned mode = 0; mode < 2U && !found; ++mode) {
        uint8_t hi, lo;
        spi_phase = mode == 0U ? SPI_PHASE_1EDGE : SPI_PHASE_2EDGE;
        set_spi(SPI_BAUDRATEPRESCALER_64 | spi_phase);
        if (reg_read(0, &hi) && reg_read(1, &lo)) {
            *chip = (uint16_t)((uint16_t)hi << 8 | lo);
            found = *chip == STMPE_ID;
        }
    }
    bool ready = found && init_touch();
    set_spi(previous);
    return ready;
}

/**
 * @brief Vider la file de points du STMPE610.
 *
 * Le bit RESET est écrit à un puis à zéro. Cette remise à zéro traite aussi les points
 * accumulés pendant une opération lente de stockage.
 * @return true si les deux écritures réussissent.
 */
static bool clear_fifo(void)
{
    return reg_write(STMPE_FIFO_STA, STMPE_FIFO_RESET) && reg_write(STMPE_FIFO_STA, 0);
}

/**
 * @brief Lire un lot FIFO et retenir son observation récente.
 *
 * Contrôle le registre de fonctionnement et le compteur. Une FIFO vide sous un doigt donne
 * EMPTY, tandis qu'un relâchement donne OK sans contact. Les octets reçus sont assemblés
 * en X/Y 12 bits et Z 8 bits.
 * @param[out] sample Structure RawTouch valide recevant l’observation physique.
 * @return État de lecture OK, EMPTY ou ERROR.
 */
static TouchRead read_points(RawTouch *sample)
{
    uint8_t control, count;
    if (!reg_read(STMPE_CTRL, &control) || !reg_read(STMPE_FIFO_SIZE, &count) ||
        (control & 0x7FU) != 1U || count > 128U) {
        return TOUCH_READ_ERROR;
    }
    if ((control & STMPE_TOUCHED) == 0U) {
        return count == 0U || clear_fifo() ? TOUCH_READ_OK : TOUCH_READ_ERROR;
    }
    if (count == 0U) {
        return TOUCH_READ_EMPTY;
    }
    unsigned take = count > STMPE_MAX_POINTS ? STMPE_MAX_POINTS : count;
    uint8_t data[4];
    for (unsigned i = 0; i < take; ++i) {
        for (unsigned byte = 0; byte < sizeof(data); ++byte) {
            if (!reg_read(STMPE_DATA, &data[byte])) {
                return TOUCH_READ_ERROR;
            }
        }
    }
    /* Limiter le temps SPI même après une longue écriture flash. */
    if (count > take && !clear_fifo()) {
        return TOUCH_READ_ERROR;
    }
    /* Le même registre livre X[11:0], Y[11:0], puis Z[7:0]. */
    *sample = (RawTouch){
        {(uint16_t)((uint16_t)data[0] << 4 | data[1] >> 4),
         (uint16_t)((uint16_t)(data[1] & 0x0FU) << 8 | data[2])},
        data[3], 0, true
    };
    return TOUCH_READ_OK;
}

/*
 * Acquérir une observation entre deux bandes TFT.
 *
 * BUSY donne WAIT pour rendre la main. Sinon le pilote emprunte le SPI, lit ses points,
 * puis remet les paramètres initiaux quelle que soit l'issue.
 */
TouchRead stmpe610_sample(RawTouch *sample)
{
    if (!lcd_spi_available()) {
        return TOUCH_READ_WAIT;
    }
    uint32_t previous = set_spi(SPI_BAUDRATEPRESCALER_64 | spi_phase);
    TouchRead result = read_points(sample);
    set_spi(previous);
    return result;
}

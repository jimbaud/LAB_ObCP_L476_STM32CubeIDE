/**
 * @file tsc2007.c
 * @brief Acquisition résistive I²C par étapes non bloquantes.
 * @ingroup obcp_drivers
 *
 * L’adresse 7 bits du shield est 0x48. Une observation enchaîne Z1, Z2, X et Y. Deux
 * millisecondes de stabilisation sont laissées après chaque commande. Les états de
 * conversion persistent entre les appels.
 *
 * @par Repères de lecture
 * Les coordonnées écran sont en pixels. Les durées suffixées ms sont en
 * millisecondes. Les adresses du service de stockage sont des offsets en octets.
 */
/** @var adc
 * @brief Machine d'acquisition I2C conservée entre commande, attente et lecture de conversion.
 */

#include "touch_drivers.h"
#include "i2c.h"

/** @brief Constantes privées de ce module, avec leurs unités. */
enum {
    TSC_ADDRESS = 0x48U << 1, /**< Adresse I²C 7 bits 0x48, décalée d’un bit pour la HAL. */
    TSC_SLEEP = 0x00, /**< Commande d’arrêt de l’ADC entre deux acquisitions. */
    TSC_X = 0xC4, /**< Commande de mesure de X sur 12 bits, ADC maintenu actif. */
    TSC_Y = 0xD4, /**< Commande de mesure de Y sur 12 bits, ADC maintenu actif. */
    TSC_Z1 = 0xE4, /**< Commande de première mesure liée à la pression. */
    TSC_Z2 = 0xF4, /**< Commande de seconde mesure liée à la pression. */
    TSC_TIMEOUT_MS = 2, /**< Délai maximal d’un échange I²C bloquant court, en ms. */
    TSC_SETTLE_MS = 2, /**< Attente coopérative de stabilisation entre commande et lecture, en ms. */
    TSC_ADC_MAX = 4095, /**< Pleine échelle d’une conversion 12 bits. */
};

static struct {
    uint32_t started; /**< Tick de départ de la conversion, en millisecondes. */
    uint16_t value[4]; /**< Résultats dans l’ordre Z1, Z2, X puis Y. */
    uint8_t index; /**< Conversion courante de zéro à trois. */
    bool converting; /**< true après la commande, avant lecture de sa conversion. */
    bool contact; /**< Dernier état de présence après hystérésis. */
    bool sleeping; /**< true tant que la conversion de mise en sommeil doit se terminer. */
} adc;

/**
 * @brief Envoyer une commande de conversion TSC2007.
 *
 * Vérifie le bit BUSY du bus et borne la transaction HAL à deux millisecondes. L'attente
 * de conversion est gérée séparément.
 * @param[in] command Octet de commande du protocole.
 * @return true si l'octet a été transmis.
 */
static bool send_command(uint8_t command)
{
    return !__HAL_I2C_GET_FLAG(&hi2c1, I2C_FLAG_BUSY) &&
           HAL_I2C_Master_Transmit(&hi2c1, TSC_ADDRESS, &command, 1, TSC_TIMEOUT_MS) == HAL_OK;
}

/**
 * @brief Décoder une conversion 12 bits reçue sur I²C.
 *
 * La valeur occupe les douze bits de poids fort. Les quatre bits bas doivent être nuls
 * pour accepter le format de réponse.
 * @param[out] value Pointeur valide recevant la valeur du registre ou de la conversion.
 * @return true si deux octets de format attendu sont reçus.
 */
static bool read_conversion(uint16_t *value)
{
    uint8_t bytes[2];
    if (__HAL_I2C_GET_FLAG(&hi2c1, I2C_FLAG_BUSY) ||
        HAL_I2C_Master_Receive(&hi2c1, TSC_ADDRESS, bytes, 2, TSC_TIMEOUT_MS) != HAL_OK ||
        (bytes[1] & 0x0FU) != 0U) {
        return false;
    }
    /* Le résultat de 12 bits occupe la partie haute des deux octets. */
    *value = (uint16_t)((uint16_t)bytes[0] << 4 | bytes[1] >> 4);
    return true;
}

/*
 * Tester le protocole de conversion du TSC2007.
 *
 * Remet l'état d'acquisition à zéro, demande TEMP0 avec mise en sommeil et attend une
 * milliseconde au seul démarrage. Le contrôle suppose un périphérique dédié à 0x48.
 */
bool tsc2007_probe(void)
{
    adc.index = 0;
    adc.converting = false;
    adc.contact = false;
    adc.sleeping = false;
    if (!send_command(TSC_SLEEP)) {
        return false;
    }
    /* Ce délai unique concerne la recherche. L'acquisition normale cède
     * la main pendant chaque conversion pour servir l'audio et le TFT. */
    HAL_Delay(1);
    uint16_t temperature;
    return read_conversion(&temperature) && temperature > 0U && temperature < TSC_ADC_MAX;
}

/**
 * @brief Finaliser une observation et demander le sommeil ADC.
 *
 * Prépare l'état de sommeil et la dernière observation, avec X/Y et les deux mesures de
 * pression. Une erreur de commande empêche de déclarer le résultat valide.
 * @param[out] sample Structure RawTouch valide recevant l’observation physique.
 * @param[in] contact État de présence retenu après les mesures de pression.
 * @return TOUCH_READ_OK ou TOUCH_READ_ERROR.
 */
static TouchRead finish(RawTouch *sample, bool contact)
{
    adc.index = 0;
    if (!send_command(TSC_SLEEP)) {
        return TOUCH_READ_ERROR;
    }
    adc.started = HAL_GetTick();
    adc.sleeping = true;
    adc.contact = contact;
    *sample = (RawTouch){{adc.value[2], adc.value[3]}, adc.value[0], adc.value[1], contact};
    return TOUCH_READ_OK;
}

/*
 * Avancer d'une étape dans l'observation résistive.
 *
 * Alterner commande, attente puis lecture permet de servir l'écran et l'audio. Z1 et Z2
 * décident du contact avec hystérésis. X et Y sont lus seulement quand le contact est
 * retenu.
 */
TouchRead tsc2007_sample(RawTouch *sample)
{
    static const uint8_t commands[] = {TSC_Z1, TSC_Z2, TSC_X, TSC_Y};
    if (adc.sleeping) {
        /* TEMP0 termine sa conversion avant l'arrêt de l'ADC. */
        if (HAL_GetTick() - adc.started < TSC_SETTLE_MS) {
            return TOUCH_READ_WAIT;
        }
        adc.sleeping = false;
    }
    if (!adc.converting) {
        if (!send_command(commands[adc.index])) {
            adc.index = 0;
            return TOUCH_READ_ERROR;
        }
        adc.started = HAL_GetTick();
        adc.converting = true;
        return TOUCH_READ_WAIT;
    }
    if (HAL_GetTick() - adc.started < TSC_SETTLE_MS) {
        return TOUCH_READ_WAIT;
    }
    adc.converting = false;
    if (!read_conversion(&adc.value[adc.index])) {
        adc.index = 0;
        return TOUCH_READ_ERROR;
    }
    ++adc.index;
    if (adc.index == 2U) {
        uint16_t z1 = adc.value[0], z2 = adc.value[1];
        /* Dalle ouverte : Z1 proche de zéro, Z2 à pleine échelle.
         * L'hystérésis évite les bascules à la limite d'un appui léger. */
        if (z1 < (adc.contact ? 45U : 80U) || z2 >= 4090U || z2 < z1) {
            return finish(sample, false);
        }
    }
    if (adc.index < sizeof(commands)) {
        return TOUCH_READ_WAIT;
    }
    return finish(sample, adc.value[2] < TSC_ADC_MAX && adc.value[3] < TSC_ADC_MAX);
}

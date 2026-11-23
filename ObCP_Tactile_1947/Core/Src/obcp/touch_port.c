/**
 * @file touch_port.c
 * @brief Reconnaissance des shields et façade tactile commune.
 * @ingroup obcp_drivers
 *
 * L’application lit toujours un RawTouch et un état TouchRead. La façade choisit un
 * protocole capacitif I²C, résistif SPI ou résistif I²C. Un état WAIT conserve la
 * possibilité de servir d’autres tâches entre deux étapes.
 *
 * @par Repères de lecture
 * Les coordonnées écran sont en pixels. Les durées suffixées ms sont en
 * millisecondes. Les adresses du service de stockage sont des offsets en octets.
 */
#include "obcp_panel.h"
#include "touch_drivers.h"

/*
 * Reconnaître le contrôleur tactile présent.
 *
 * Vérifie d'abord que SPI1 peut être utilisé, essaie les signatures FT62xx puis STMPE610,
 * enfin le protocole TSC2007. Une recherche terminée peut produire TOUCH_CTRL_NONE.
 */
bool touch_probe(TouchDevice *device)
{
    if (!lcd_spi_available()) {
        return false;
    }

    /* Essayer les signatures explicites avant la reconnaissance par
     * protocole du TSC2007. Le bus I²C du shield réserve 0x38 et 0x48. */
    uint8_t ft_id;
    uint16_t stmpe_id;
    if (ft6206_probe(&ft_id)) {
        *device = (TouchDevice){TOUCH_CTRL_FT62XX, ft_id};
    } else if (stmpe610_probe(&stmpe_id)) {
        *device = (TouchDevice){TOUCH_CTRL_STMPE610, stmpe_id};
    } else if (tsc2007_probe()) {
        *device = (TouchDevice){TOUCH_CTRL_TSC2007, 0};
    } else {
        *device = (TouchDevice){0};
    }
    return true;
}

/*
 * Acquérir un RawTouch avec le pilote reconnu.
 *
 * Remet d'abord la structure de sortie à zéro. Le contrôleur détermine le pilote, et les
 * états asynchrones sont propagés à board.c.
 */
TouchRead touch_read(const TouchDevice *device, RawTouch *sample)
{
    /* Une structure remise à zéro protège l’appelant d’anciennes coordonnées en cas d’échec. Le code de retour indique si elle doit être exploitée. */
    *sample = (RawTouch){0};
    switch (device->controller) {
    case TOUCH_CTRL_FT62XX:
        return ft6206_sample(&sample->down, &sample->point.x, &sample->point.y)
            ? TOUCH_READ_OK : TOUCH_READ_ERROR;
    case TOUCH_CTRL_STMPE610:
        return stmpe610_sample(sample);
    case TOUCH_CTRL_TSC2007:
        return tsc2007_sample(sample);
    default:
        return TOUCH_READ_ERROR;
    }
}

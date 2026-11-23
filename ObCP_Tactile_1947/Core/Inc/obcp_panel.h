/**
 * @file obcp_panel.h
 * @brief Commandes du TFT ILI9341 et transport SPI1 DMA.
 * @ingroup obcp_drivers
 *
 * Le TFT utilise CS sur D10 et D/C sur D9. L’initialisation et les petites commandes sont
 * synchrones. Les pixels sont transmis par DMA1 canal 3. Le STMPE610 peut emprunter SPI1
 * uniquement entre ces bandes.
 *
 * @par Repères de lecture
 * Les coordonnées écran sont en pixels. Les durées suffixées ms sont en
 * millisecondes. Les adresses du service de stockage sont des offsets en octets.
 */
#ifndef OBCP_PANEL_H
#define OBCP_PANEL_H

#include "obcp_gfx.h"

/**
 * @brief Initialiser le TFT en paysage RGB565.
 *
 * Lit l'identifiant, applique la séquence d'alimentation et de gamma, choisit MADCTL et
 * sort le contrôleur du sommeil. Cette étape unique peut utiliser les délais requis au
 * démarrage.
 * @param[out] id Pointeur valide recevant l’identifiant lu du composant.
 * @return true si les échanges d'initialisation ont réussi. L'identifiant est fourni séparément.
 */
bool lcd_init(uint16_t *id);
/**
 * @brief Exposer les callbacks de transport à Renderer.
 *
 * Le moteur graphique n'a ainsi pas besoin de connaître SPI1 ni la HAL. Le contexte opaque
 * est inutilisé par ce port unique.
 * @return Interface d'envoi et de consultation du TFT.
 */
DisplayPort lcd_port(void);

/* SPI1 peut être utilisé par le STMPE610 entre deux bandes du TFT. */
/**
 * @brief Vérifier si SPI1 peut servir le tactile résistif.
 *
 * Le transport ne doit plus être BUSY, la HAL doit être prête et le bit matériel BSY doit
 * être effacé.
 * @return true si le STMPE610 peut emprunter le bus.
 */
bool lcd_spi_available(void);

#endif

/**
 * @file ili9341.c
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
/* Séquence d'alimentation/gamma d'après Adafruit_ILI9341, licence BSD.
 * Voir docs/PROVENANCE.md. Le transport SPI et le DMA sont propres à l'ObCP. */
/** @var state
 * @brief État volatile du transfert, partagé entre la boucle et les callbacks SPI.
 */
/** @var started_ms
 * @brief Tick du départ DMA pour détecter une bande bloquée plus de 50 ms.
 */

#include "obcp_panel.h"
#include "obcp_config.h"

#include "main.h"
#include "spi.h"

/** @brief Constantes privées de ce module, avec leurs unités. */
enum {
    CMD_SWRESET = 0x01, /**< Commande ILI9341 de réinitialisation logicielle. */
    CMD_SLEEP_OUT = 0x11, /**< Commande de sortie du mode sommeil. */
    CMD_DISPLAY_ON = 0x29, /**< Commande d’activation de l’affichage. */
    CMD_COLUMN = 0x2A, /**< Commande de définition des colonnes de la fenêtre. */
    CMD_PAGE = 0x2B, /**< Commande de définition des lignes de la fenêtre. */
    CMD_RAM_WRITE = 0x2C, /**< Commande précédant les octets RGB565 de la fenêtre. */
    CMD_READ_ID = 0xD3, /**< Commande de lecture de l’identifiant du contrôleur TFT. */
};

static volatile TransferState state = TRANSFER_DONE;
static uint32_t started_ms;

/**
 * @brief Appliquer le niveau logique de sélection du TFT.
 *
 * Le périphérique est sélectionné quand CS vaut zéro. Une sélection basse doit couvrir la
 * commande et tous les octets de sa bande.
 * @param[in] level Indice de niveau à base zéro. Une valeur hors plage choisit le premier
 *   niveau.
 */
static void select_lcd(GPIO_PinState level)
{
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, level);
}

/**
 * @brief Émettre une commande en plaçant D/C à zéro.
 *
 * CS passe bas, l'octet de commande est envoyé puis D/C repasse haut pour les paramètres.
 * La désélection reste à la charge du code appelant.
 * @param[in] command Octet de commande du protocole.
 * @return true si l'octet de commande a été envoyé.
 */
static bool begin_command(uint8_t command)
{
    select_lcd(GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_RESET);
    bool ok = HAL_SPI_Transmit(&hspi1, &command, 1, 3) == HAL_OK;
    HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET);
    return ok;
}

/**
 * @brief Envoyer une commande avec ses paramètres puis libérer CS.
 *
 * Le message complet reste sélectionné et utilise un délai HAL limité. count peut être nul
 * lorsqu'une commande ne demande aucun paramètre.
 * @param[in] code Code de commande ILI9341.
 * @param[in] params Paramètres de la commande, count octets. NULL est permis si count vaut
 *   zéro.
 * @param[in] count Nombre d’octets de l’opération ou de la transaction.
 * @return true si commande et paramètres ont été transmis.
 */
static bool command(uint8_t code, const uint8_t *params, uint16_t count)
{
    bool ok = begin_command(code);
    if (ok && count > 0U) {
        ok = HAL_SPI_Transmit(&hspi1, params, count, 3) == HAL_OK;
    }
    select_lcd(GPIO_PIN_SET);
    return ok;
}

/**
 * @brief Lire le code du contrôleur TFT à une fréquence réduite.
 *
 * Sauvegarde le prescaler SPI1, le réduit pour la lecture de D3 puis rétablit sa valeur.
 * Les octets d'identité sont recomposés dans l'ordre du protocole.
 * @return Identifiant sur 16 bits, ou zéro si l'échange échoue.
 */
static uint16_t read_id(void)
{
    uint32_t speed = READ_BIT(hspi1.Instance->CR1, SPI_CR1_BR);
    /* La lecture série du contrôleur est plus lente que l'écriture. */
    __HAL_SPI_DISABLE(&hspi1);
    MODIFY_REG(hspi1.Instance->CR1, SPI_CR1_BR, SPI_BAUDRATEPRESCALER_16);
    uint8_t tx[4] = {0};
    uint8_t rx[4] = {0};
    bool ok = begin_command(CMD_READ_ID) &&
              HAL_SPI_TransmitReceive(&hspi1, tx, rx, sizeof(rx), 4) == HAL_OK;
    select_lcd(GPIO_PIN_SET);
    __HAL_SPI_DISABLE(&hspi1);
    MODIFY_REG(hspi1.Instance->CR1, SPI_CR1_BR, speed);
    return ok ? (uint16_t)((uint16_t)rx[2] << 8 | rx[3]) : 0U;
}

/*
 * Initialiser le TFT en paysage RGB565.
 *
 * Lit l'identifiant, applique la séquence d'alimentation et de gamma, choisit MADCTL et
 * sort le contrôleur du sommeil. Cette étape unique peut utiliser les délais requis au
 * démarrage.
 */
bool lcd_init(uint16_t *id)
{
    static const struct { uint8_t cmd, n, data[15]; } init[] = {
        {0xEF,3,{0x03,0x80,0x02}}, {0xCF,3,{0x00,0xC1,0x30}},
        {0xED,4,{0x64,0x03,0x12,0x81}}, {0xE8,3,{0x85,0x00,0x78}},
        {0xCB,5,{0x39,0x2C,0x00,0x34,0x02}}, {0xF7,1,{0x20}}, {0xEA,2,{0x00,0x00}},
        {0xC0,1,{0x23}}, {0xC1,1,{0x10}}, {0xC5,2,{0x3E,0x28}}, {0xC7,1,{0x86}},
        {0x36,1,{OBCP_LCD_MADCTL}}, {0x3A,1,{0x55}}, /* paysage et RGB565 */
        {0xB1,2,{0x00,0x18}}, {0xB6,3,{0x08,0x82,0x27}}, {0xF2,1,{0x00}},
        {0x26,1,{0x01}},
        {0xE0,15,{0x0F,0x31,0x2B,0x0C,0x0E,0x08,0x4E,0xF1,0x37,0x07,0x10,0x03,0x0E,0x09,0x00}},
        {0xE1,15,{0x00,0x0E,0x14,0x03,0x11,0x07,0x31,0xC1,0x48,0x08,0x0F,0x0C,0x31,0x36,0x0F}},
    };
    *id = 0;
    HAL_Delay(12);
    if (!command(CMD_SWRESET, NULL, 0)) {
        return false;
    }
    HAL_Delay(150);
    *id = read_id();
    for (size_t i = 0; i < sizeof(init) / sizeof(init[0]); ++i) {
        if (!command(init[i].cmd, init[i].data, init[i].n)) {
            return false;
        }
    }
    if (!command(CMD_SLEEP_OUT, NULL, 0)) {
        return false;
    }
    HAL_Delay(120);
    return command(CMD_DISPLAY_ON, NULL, 0);
}

/**
 * @brief Définir un intervalle de colonnes ou de lignes du TFT.
 *
 * Le protocole du contrôleur reçoit début et fin inclus sur deux octets chacun. L'appelant
 * transforme donc une largeur en dernière coordonnée moins un.
 * @param[in] cmd Commande de fenêtre de colonnes ou de pages.
 * @param[in] start Première coordonnée incluse de la fenêtre.
 * @param[in] end Dernière coordonnée incluse du protocole ILI9341.
 * @return true si la commande de fenêtre est transmise.
 */
static bool window(uint8_t cmd, int start, int end)
{
    uint8_t data[4] = {(uint8_t)(start >> 8), (uint8_t)start,
                       (uint8_t)(end >> 8), (uint8_t)end};
    return command(cmd, data, sizeof(data));
}

/**
 * @brief Commencer le transfert DMA des pixels d'un rectangle.
 *
 * Définit la fenêtre et RAM_WRITE, puis garde CS actif jusqu'au callback de fin. Le tampon
 * fourni doit rester intact tant que le transport est BUSY.
 * @param[in] context Contexte opaque transmis sans allocation. Le port physique peut ne pas
 *   l’utiliser.
 * @param[in] r Rectangle écran, avec bord droit et bas exclus.
 * @param[in] data Tampon valide pour la longueur demandée par size, len ou count.
 * @param[in] count Nombre d’octets de l’opération ou de la transaction.
 * @return true si le DMA prend effectivement la propriété du tampon.
 */
static bool send_band(void *context, Rect r, const uint8_t *data, uint16_t count)
{
    (void)context;
    if (state == TRANSFER_BUSY) {
        return false;
    }
    bool ok = window(CMD_COLUMN, r.x, r.x + r.w - 1) &&
              window(CMD_PAGE, r.y, r.y + r.h - 1) &&
              begin_command(CMD_RAM_WRITE);
    if (ok) {
        /* CS reste à zéro pendant l'envoi de toute la bande. */
        started_ms = HAL_GetTick();
        state = TRANSFER_BUSY;
        if (HAL_SPI_Transmit_DMA(&hspi1, data, count) == HAL_OK) {
            return true;
        }
    }
    select_lcd(GPIO_PIN_SET);
    state = TRANSFER_ERROR;
    return false;
}

/**
 * @brief Consulter le transfert et surveiller son délai maximal.
 *
 * Un transfert qui dépasse 50 ms est interrompu et libère CS. Renderer pourra demander une
 * image complète pour reprendre un écran cohérent.
 * @param[in] context Contexte opaque transmis sans allocation. Le port physique peut ne pas
 *   l’utiliser.
 * @return État BUSY, DONE ou ERROR du transport.
 */
static TransferState poll_band(void *context)
{
    (void)context;
    if (state == TRANSFER_BUSY && HAL_GetTick() - started_ms > 50U) {
        (void)HAL_SPI_Abort(&hspi1);
        select_lcd(GPIO_PIN_SET);
        state = TRANSFER_ERROR;
    }
    return state;
}

/*
 * Exposer les callbacks de transport à Renderer.
 *
 * Le moteur graphique n'a ainsi pas besoin de connaître SPI1 ni la HAL. Le contexte opaque
 * est inutilisé par ce port unique.
 */
DisplayPort lcd_port(void)
{
    return (DisplayPort){NULL, send_band, poll_band};
}

/*
 * Vérifier si SPI1 peut servir le tactile résistif.
 *
 * Le transport ne doit plus être BUSY, la HAL doit être prête et le bit matériel BSY doit
 * être effacé.
 */
bool lcd_spi_available(void)
{
    return state != TRANSFER_BUSY && HAL_SPI_GetState(&hspi1) == HAL_SPI_STATE_READY &&
           !__HAL_SPI_GET_FLAG(&hspi1, SPI_FLAG_BSY);
}

/**
 * @brief Terminer une bande après l'interruption DMA SPI.
 *
 * Pour SPI1, désélectionne le TFT et publie DONE. Aucun dessin n'est effectué dans
 * l'interruption. Les autres handles sont ignorés.
 * @param[in,out] spi Handle HAL du bus SPI déjà initialisé. Pointeur valide.
 * @note Contexte interruption. Garder ce traitement court et sans opération flash.
 */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *spi)
{
    if (spi == &hspi1) {
        select_lcd(GPIO_PIN_SET);
        state = TRANSFER_DONE;
    }
}

/**
 * @brief Publier une erreur du transport SPI1.
 *
 * Libère CS pour sortir proprement de la transaction. Le moteur de rendu traitera l'état
 * ERROR lors d'une itération suivante.
 * @param[in,out] spi Handle HAL du bus SPI déjà initialisé. Pointeur valide.
 * @note Contexte interruption. Garder ce traitement court et sans opération flash.
 */
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *spi)
{
    if (spi == &hspi1) {
        select_lcd(GPIO_PIN_SET);
        state = TRANSFER_ERROR;
    }
}

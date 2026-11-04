/**
 ******************************************************************************
 * @file    config.h
 * @brief   Configuration courante (RAM) et sauvegarde dans la dernière page
 *          de la flash (banque 2, page 255 : 0x080FF800, 2 Ko). Le script
 *          d'édition de liens généré par CubeMX n'est pas modifié : le
 *          programme (environ 140 Ko) n'atteint jamais cette page, et
 *          config_erase() refuse de l'effacer si c'était le cas.
 ******************************************************************************
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "cfg_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_FLASH_ADDR   0x080FF800UL

extern cfg_t g_cfg;

/**
 * @brief  Charge la configuration enregistrée en flash dans g_cfg (valeurs
 *         d'usine si elle est absente ou invalide).
 * @retval CFG_UNPACK_OK, CFG_UNPACK_EMPTY, CFG_UNPACK_VERSION ou CFG_UNPACK_CRC.
 */
int  config_load(void);

/**
 * @brief  Enregistre g_cfg en flash (effacement de la page puis écriture
 *         par mots de 64 bits, relecture de contrôle).
 * @retval 0 si l'enregistrement a réussi, valeur négative sinon.
 */
int  config_save(void);

/** @brief  Efface la page de configuration (commande « factory erase »). @retval 0 si OK. */
int  config_erase(void);

/** @brief  Vrai si une configuration valide est enregistrée en flash. */
bool config_stored(void);

/**
 * @brief  Appelée par fault_nmi() : une double erreur ECC détectée en lisant
 *         la flash (page de configuration abîmée) déclenche une NMI.
 * @retval true si c'était bien une erreur ECC (acquittée), false sinon.
 */
bool config_ecc_nmi(void);

/* ---- Noms dérivés de l'identifiant unique quand le paramètre est vide ---- */
/** @brief  SSID du point d'accès : ap.ssid ou « ObCP-WiFi-<id> ». */
const char *config_ap_ssid(void);
/** @brief  Nom d'hôte DHCP : net.hostname ou « obcp-<id> ». */
const char *config_hostname(void);
/** @brief  Racine des topics MQTT : mqtt.topic ou « obcp/<id> ». */
const char *config_mqtt_root(void);
/** @brief  Identifiant du client MQTT : « obcp-<id> ». */
const char *config_mqtt_client_id(void);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_H */

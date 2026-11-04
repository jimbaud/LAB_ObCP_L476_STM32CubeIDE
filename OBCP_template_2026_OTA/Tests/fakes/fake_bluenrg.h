/**
 ******************************************************************************
 * @file    Tests/fakes/fake_bluenrg.h
 * @brief   Simulation sur PC du module Bluetooth BlueNRG-M0 et du téléphone :
 *          commandes ACI reçues, notifications envoyées, évènements injectés
 *          (connexion, écriture, déconnexion...).
 ******************************************************************************
 */
#ifndef FAKE_BLUENRG_H
#define FAKE_BLUENRG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- Réglages de la simulation ------------------------------------------- */
extern bool    fake_ble_present;            /* false : le module ne répond pas */
extern bool    fake_ble_random_addr_valid;  /* adresse statique aléatoire disponible */
extern uint8_t fake_ble_random_addr[6];
extern int     fake_ble_busy_next;          /* nb de réponses « tampons pleins » à venir */
extern int     fake_ble_adv_fail_next;      /* nb de refus de publicité à venir */
extern bool    fake_ble_clean_buffers;      /* true : tampons de paquets remis à zéro
                                               avant chaque évènement (cas favorable
                                               à la version 2024, voir compare/) */

/* ---- Ce que le programme a demandé au module ------------------------------ */
extern int      fake_ble_init_count;        /* appels à hci_init() */
extern uint8_t  fake_ble_public_addr[6];    /* adresse écrite (aci_hal_write_config_data) */
extern char     fake_ble_device_name[32];   /* caractéristique Device Name */
extern char     fake_ble_adv_name[32];      /* nom dans la publicité */
extern int      fake_ble_adv_count;         /* publicités lancées avec succès */
extern int      fake_ble_adv_attempts;
extern uint32_t fake_ble_fixed_pin;
extern uint8_t  fake_ble_service_uuid[16];
extern uint8_t  fake_ble_tx_uuid[16], fake_ble_rx_uuid[16];
extern uint8_t  fake_ble_tx_props, fake_ble_rx_props;
extern int      fake_ble_isr_kicks;         /* appels à hci_tl_lowlevel_isr() */
extern bool     fake_ble_advertising;

/* aci_gap_terminate() : fermeture de la connexion demandée par la carte */
extern int      fake_ble_terminate_count;   /* appels */
extern uint16_t fake_ble_terminate_handle;  /* connexion désignée */
extern uint8_t  fake_ble_terminate_reason;  /* raison donnée (0x13 attendu) */
extern bool     fake_ble_terminate_silent;  /* true : le module ne confirme jamais
                                               la fin de la connexion */

/* Repères attribués par la simulation */
#define FAKE_TX_CHAR_HANDLE   0x000DU       /* valeur 0x000E, CCCD 0x000F */
#define FAKE_RX_CHAR_HANDLE   0x0010U       /* valeur 0x0011 */

/* ---- Notifications reçues par le téléphone ------------------------------- */
#define FAKE_BLE_MAX_NOTIF 4096
extern int     fake_ble_notif_count;
extern uint8_t fake_ble_notif_len[FAKE_BLE_MAX_NOTIF];
extern char    fake_ble_notif[FAKE_BLE_MAX_NOTIF][21];
void fake_ble_clear_notifications(void);

/* ---- Évènements envoyés au programme (traités par hci_user_evt_proc) ----- */
void fake_ble_connect(const uint8_t peer[6], uint8_t status);
void fake_ble_disconnect(void);
void fake_ble_write(const char *data, size_t len);     /* écriture dans RX */
void fake_ble_write_str(const char *text);
void fake_ble_subscribe(void);                         /* écriture du CCCD de TX */
void fake_ble_tx_pool_available(void);
void fake_ble_hal_initialized(uint8_t reason);
size_t fake_ble_pending_events(void);

#endif /* FAKE_BLUENRG_H */

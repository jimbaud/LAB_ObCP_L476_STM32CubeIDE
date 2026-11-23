/**
 ******************************************************************************
 * @file    Tests/fakes/fake_bluenrg.h
 * @brief   Simulation sur PC du module Bluetooth de l'ObCP (BlueNRG-M0 ou
 *          SPBTLE-RF) utilisé en diffuseur et observateur : commandes ACI
 *          reçues, publicités diffusées, rapports de publicité injectés.
 *          Règles du vrai module reproduites : voir fake_bluenrg.c.
 ******************************************************************************
 */
#ifndef FAKE_BLUENRG_H
#define FAKE_BLUENRG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mesh_pdu.h"

/* ---- Réglages de la simulation ------------------------------------------- */
extern bool    fake_ble_present;           /* false : le module ne répond pas */
extern bool    fake_ble_concurrent_ok;     /* diffusion permise pendant l'écoute
                                              (en mode 4 seulement, de toute façon) */
extern uint16_t fake_ble_fw_version;       /* pile : 0x0723 = 7.2c (défaut), 0x0715 = 7.1e */
extern uint8_t  fake_ble_hw_version;       /* circuit : 0x31 */
extern bool    fake_ble_mode4_refused;     /* refuse le mode 4 même si la pile le permet */
extern bool    fake_ble_pubaddr_fail_after_mode; /* refuse une fois l'adresse écrite après le mode */
extern bool    fake_ble_strict_roles;      /* refuse « diffuseur + observateur » seuls */
extern int     fake_ble_broadcast_fail_next; /* nb de refus de diffusion à venir */
extern uint8_t fake_ble_broadcast_fail_status; /* code de ces refus (0x0C par défaut) */
extern int     fake_ble_scan_fail_next;    /* nb de refus d'écoute à venir */
extern uint8_t fake_ble_random_addr[6];

/* ---- Ce que le programme a demandé au module ------------------------------ */
extern uint8_t  fake_ble_stack_mode;       /* mode de la pile (2 après un redémarrage) */
extern int      fake_ble_mode_writes;      /* écritures du mode (acceptées ou non) */
extern int      fake_ble_late_config_writes; /* configuration écrite trop tard : refusée */
extern int      fake_ble_init_count;       /* appels à hci_init() */
extern uint8_t  fake_ble_public_addr[6];
extern uint8_t  fake_ble_gap_roles;        /* rôles demandés à aci_gap_init */
extern uint8_t  fake_ble_tx_high, fake_ble_tx_pa;
extern bool     fake_ble_observing;        /* procédure d'observation en cours */
extern int      fake_ble_observe_starts;
extern uint16_t fake_ble_scan_interval, fake_ble_scan_window;
extern uint8_t  fake_ble_scan_type, fake_ble_scan_filter_dup;
extern bool     fake_ble_broadcasting;     /* diffusion en cours */
extern int      fake_ble_broadcast_starts;
extern int      fake_ble_broadcast_stops;
extern uint16_t fake_ble_adv_interval;
extern uint8_t  fake_ble_adv_type;
extern int      fake_ble_rand_calls;
extern int      fake_ble_time_base_refusals; /* publicités refusées : base de temps (0x12) */
extern int      fake_ble_reports_while_deaf; /* rapports injectés écoute arrêtée (rendus quand même) */

/* Trames diffusées (décodées), dans l'ordre. */
#define FAKE_BLE_MAX_SENT 256
extern int          fake_ble_sent_count;
extern mesh_frame_t fake_ble_sent[FAKE_BLE_MAX_SENT];
extern bool         fake_ble_sent_during_scan[FAKE_BLE_MAX_SENT]; /* écoute active à l'émission */
void fake_ble_clear_sent(void);

/* Règle de la base de temps du mode 4 (PM0237, § 4.2.3), en pas de 0,625 ms :
 * vrai si une publicité d'intervalle [adv_min, adv_max] peut s'ajouter à
 * une écoute (scan_interval, scan_window) sans arrêter celle-ci. */
bool fake_ble_time_base_fits(uint16_t scan_interval, uint16_t scan_window, uint16_t adv_min, uint16_t adv_max);

/* ---- Évènements envoyés au programme (rendus par hci_user_evt_proc) ----- */
/* Rapport de publicité quelconque (adv : données de la publicité). */
void fake_ble_adv_report(const uint8_t addr[6], const uint8_t *adv, uint8_t len, int8_t rssi);
/* Rapport de publicité contenant une trame du réseau maillé. */
void fake_ble_mesh_frame(const mesh_frame_t *f, int8_t rssi);
/* Rapport tronqué (longueur annoncée plus grande que le paquet). */
void fake_ble_bad_report(void);
void fake_ble_procedure_complete(uint8_t code);
void fake_ble_hal_initialized(uint8_t reason);
size_t fake_ble_pending_events(void);

#endif /* FAKE_BLUENRG_H */

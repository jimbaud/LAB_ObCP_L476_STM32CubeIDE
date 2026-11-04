/**
 ******************************************************************************
 * @file    cdc_port.h
 * @brief   Port COM virtuel de l'ObCP (classe USB CDC) : tampons circulaires
 *          d'émission (4 Ko) et de réception (2 Ko) avec contrôle de flux,
 *          détection de l'ouverture du port par le terminal (signal DTR),
 *          codage de ligne demandé par le PC (utilisé par la passerelle
 *          « module boot »).
 *
 *          Émission : les données sont gardées dans le tampon tant que le
 *          terminal n'est pas ouvert, puis envoyées à son ouverture (le
 *          message de démarrage n'est donc pas perdu).
 *          Réception : le point d'accès OUT n'est réarmé que s'il reste au
 *          moins 64 octets libres ; sinon l'hôte est mis en attente (NAK) :
 *          aucun octet n'est perdu, même pendant un transfert YMODEM.
 *
 *          Fichier non généré. Le code généré par CubeMX l'appelle depuis ses
 *          sections « USER CODE » :
 *            USB_DEVICE/App/usbd_cdc_if.c   rappels de la classe CDC
 *                                           (cdc_port_itf_*) ;
 *            USB_DEVICE/Target/usbd_conf.c  veille / reprise de l'USB
 *                                           (cdc_port_on_suspend).
 ******************************************************************************
 */
#ifndef CDC_PORT_H
#define CDC_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief  Vrai si le PC a reconnu (énuméré) la carte et n'est pas en veille. */
bool     cdc_port_configured(void);

/** @brief  Vrai si un terminal a ouvert le port (signal DTR ou données reçues). */
bool     cdc_port_open(void);

/** @brief  Nombre d'ouvertures du port par un terminal (+1 à chaque ouverture). */
uint32_t cdc_port_open_count(void);

/* ---- Émission -------------------------------------------------------------------- */
/**
 * @brief  Ajoute des octets au tampon d'émission (non bloquant).
 * @retval nombre d'octets acceptés (moins que n si le tampon est plein).
 */
size_t   cdc_port_write(const uint8_t *data, size_t n);

/** @brief  Place libre dans le tampon d'émission (octets). */
size_t   cdc_port_tx_free(void);

/** @brief  Vrai si le tampon d'émission est vide et aucun transfert USB en cours. */
bool     cdc_port_tx_empty(void);

/** @brief  Relance l'émission USB si des octets attendent dans le tampon. */
void     cdc_port_tx_kick(void);

/** @brief  Oublie les octets pas encore envoyés. */
void     cdc_port_tx_discard(void);

/**
 * @brief  Passerelle « module boot » : émission même si DTR est à 0 (l'outil
 *         du PC peut manipuler DTR/RTS pendant la programmation).
 */
void     cdc_port_set_always_send(bool on);

/* ---- Réception ------------------------------------------------------------------- */
/** @brief  Lit un octet reçu. @retval l'octet (0 à 255), ou -1 si rien n'a été reçu. */
int      cdc_port_getc(void);

/** @brief  Lit jusqu'à max octets reçus. @retval nombre d'octets lus. */
size_t   cdc_port_read(uint8_t *buf, size_t max);

/** @brief  Nombre d'octets reçus en attente de lecture. */
size_t   cdc_port_rx_count(void);

/** @brief  Oublie les octets reçus non lus. */
void     cdc_port_rx_flush(void);

/* ---- Codage de ligne (débit choisi par le programme du PC) ---------------------- */
/** @brief  Débit demandé par le programme du PC (sans effet sur l'USB lui-même). */
uint32_t cdc_port_baudrate(void);

/** @brief  Nombre de changements de débit demandés (+1 à chaque SET_LINE_CODING). */
uint32_t cdc_port_line_coding_count(void);

/** @brief  État du signal DTR (le terminal a ouvert le port). */
bool     cdc_port_dtr(void);

/** @brief  Arrêt de l'USB avant un redémarrage : le PC voit le port disparaître. */
void     cdc_port_usb_stop(void);

/* ---- Appelés par le code généré (sous interruption USB) ------------------------ */
/* Ces fonctions s'exécutent dans l'interruption de l'USB : elles se limitent
 * à copier des octets et à mettre à jour des indicateurs, sans attente.     */
/** @brief  Classe CDC démarrée (usbd_cdc_if.c, CDC_Init_FS). */
int8_t   cdc_port_itf_init(void);
/** @brief  Classe CDC arrêtée (CDC_DeInit_FS). */
int8_t   cdc_port_itf_deinit(void);
/** @brief  Requête de contrôle du PC : codage de ligne, DTR/RTS (CDC_Control_FS). */
int8_t   cdc_port_itf_control(uint8_t cmd, uint8_t *pbuf, uint16_t length);
/** @brief  Paquet reçu du PC (CDC_Receive_FS) : copié dans le tampon de réception. */
int8_t   cdc_port_itf_receive(uint8_t *buf, uint32_t *len);
/** @brief  Paquet émis vers le PC (CDC_TransmitCplt_FS) : envoi de la suite. */
int8_t   cdc_port_itf_tx_complete(uint8_t *buf, uint32_t *len, uint8_t epnum);
/** @brief  Hôte en veille (true) ou réveillé (false), câble débranché (usbd_conf.c). */
void     cdc_port_on_suspend(bool suspended);

#ifdef __cplusplus
}
#endif

#endif /* CDC_PORT_H */

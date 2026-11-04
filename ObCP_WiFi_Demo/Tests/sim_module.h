/* Module ST67W611M1 simulé (tests sur PC du pilote ST67W6X_Lite). */
#ifndef SIM_MODULE_H
#define SIM_MODULE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

void sim_reset(void);
/* Ajoute une trame que le module enverra (le module lève RDY). */
void sim_push(const char *text);
void sim_push_bin(const uint8_t *data, uint16_t len);
/* Le module signalera rx_stall lors des n prochaines transactions. */
void sim_stall_next(int n);
/* « Magic » des en-têtes SPI émis (0x5501 : firmware trop ancien) */
void sim_set_magic(uint16_t m);
/* Jeu de données réaliste (captures d'écran du guide) */
void sim_set_realistic(bool on);
/* Envoie les réponses à la commande en fin de séquence (ping par ex.). */
void sim_set_ping_ok_last(bool last);
/* Données que le module rendra à AT+CIPRECVDATA */
void sim_set_socket_data(const uint8_t *data, uint16_t len);
/* Dernières données reçues après une invite '>' */
const uint8_t *sim_last_data(uint16_t *len);
/* Dernière commande reçue */
const char *sim_last_cmd(void);
int  sim_cmd_count(const char *prefix);
/* Rang de la première commande commençant par prefix (-1 si aucune) : ordre des commandes */
int  sim_cmd_first(const char *prefix);
uint32_t sim_tick(void);
/* Instant présent, sans faire avancer le temps simulé (ncp_port_tick() l'avance) */
uint32_t sim_now(void);
int  sim_frames_rejected(void);
/* Données envoyées par l'hôte (AT+CIPSEND / AT+MQTTPUBRAW) */
void        sim_sent_clear(void);
const char *sim_link_sent(int link, uint32_t *len);      /* concaténation par lien */
int         sim_data_find(const char *cmd_part, const char *needle);
const char *sim_data_last(const char *cmd_part);

/* Mise à jour OTA simulée (AT+OTASTART / AT+OTASEND / AT+OTAFIN) */
void           sim_set_version(const char *v);          /* version rendue par AT+GMR */
const char    *sim_version(void);
/* Pannes : refus de AT+OTASTART=1, refus de AT+OTAFIN, « OK » après « Recv n bytes » */
void           sim_ota_set_faults(bool start_error, bool fin_error, bool trailing_ok);
const uint8_t *sim_ota_image(size_t *len);              /* image reçue par le module */
bool           sim_ota_active(void);
int            sim_ota_count(const char *what);         /* start, abort, send, install, reject */
uint16_t       sim_ota_send_size(int i);                /* taille du i-ème AT+OTASEND */
/* Fabrique une image .ota valide (en-tête ST + SHA-256) ; retourne sa taille */
size_t         sim_make_ota_image(uint8_t *out, size_t cap, size_t payload_len, const char *version, bool xz,
                                  uint32_t seed);

#endif

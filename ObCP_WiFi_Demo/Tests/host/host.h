/* Fonctions de pilotage des remplaçants PC (test_sim). */
#ifndef HOST_H
#define HOST_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Entrée clavier simulée : texte disponible à partir de l'instant at_tick */
void        host_type(const char *text, uint32_t at_tick);
/* Entrée binaire (transfert de fichier simulé) */
void        host_feed(const uint8_t *data, size_t n, uint32_t at_tick);
bool        host_input_pending(void);
/* Fonction appelée quand le programme attend un octet et que rien n'est
 * disponible (émetteur YMODEM simulé). */
typedef void (*host_rx_hook_t)(void);
void        host_set_rx_hook(host_rx_hook_t hook);
/* Console reliée à un pseudo-terminal (essai avec le script PC) */
void        host_attach_fd(int fd);
/* Sortie console capturée depuis le dernier host_clear() */
const char *host_output(void);
size_t      host_output_len(void);
void        host_clear(void);
void        host_transcript(const char *path);   /* copie (sans ANSI) dans un fichier */
bool        host_prompt_visible(void);

void        host_press(uint8_t btn);
void        host_hold_user(bool held);
/* Terminal fermé puis rouvert sur le port COM virtuel */
void        host_reconnect(void);

extern int  g_host_resets;
extern int  g_host_flash_writes;
extern int  g_host_dfu;
extern int  g_host_bridge_runs;
extern bool g_host_fault_pending;
extern int16_t g_host_acc[3];

#endif

/**
 ******************************************************************************
 * @file    usb_console.h
 * @brief   Port COM virtuel par le câble USB (classe USB « CDC ») : réception
 *          des lignes de commande, envoi des réponses vers le PC.
 *
 *  Branchée sur un PC, l'ObCP apparaît comme un port série : « Périphérique
 *  série USB (COMx) » sous Windows, /dev/ttyACM0 sous Linux. N'importe quel
 *  terminal (PuTTY, Tera Term, console de STM32CubeIDE) ou programme (scripts
 *  Python du dossier Python/, VI LabVIEW) peut l'ouvrir. La vitesse choisie
 *  dans le terminal n'a pas d'effet : les données voyagent à la vitesse de
 *  l'USB.
 *
 *  Le code généré par CubeMX (USB_DEVICE/App/usbd_cdc_if.c) reçoit les
 *  évènements de la bibliothèque USB de ST et les transmet à ce module par
 *  les fonctions usb_console_on_xxx(), appelées dans ses sections USER CODE.
 *
 *  RÉCEPTION, sans perte ni écrasement :
 *    1. un paquet arrive du PC (64 octets au plus) : la bibliothèque USB
 *       appelle CDC_Receive_FS() en interruption, qui le signale ici ;
 *    2. la boucle principale lit le paquet (usb_console_read_line) et
 *       assemble les lignes (line_builder.h) ;
 *    3. seulement alors, la réception du paquet suivant est autorisée.
 *       En attendant, le PC patiente : c'est le contrôle de flux de l'USB
 *       (le périphérique répond « NAK », pas prêt, et le PC réessaie).
 *  La version 2024 autorisait le paquet suivant tout de suite : il pouvait
 *  écraser une commande en cours de lecture.
 *
 *  Règle de fin de commande, reprise de la version 2024 :
 *    - un paquet d'un seul caractère est une frappe au clavier : la commande
 *      se termine par Entrée (« \r » ou « \n ») ;
 *    - un paquet de plusieurs caractères qui commence une ligne est envoyé
 *      par un programme : sa fin termine la commande même sans fin de ligne
 *      (Terminal_port_com.py, option « Aucun »).
 *
 *  ÉMISSION : usb_console_write() copie les octets dans un tampon circulaire
 *  (fifo.h) et rend la main tout de suite ; usb_console_process(), appelée
 *  dans la boucle principale, confie le contenu du tampon au pilote USB dès
 *  que le transfert précédent est fini. La version 2024 appelait
 *  CDC_Transmit_FS() directement : un message envoyé pendant le transfert
 *  précédent était perdu, et un texte placé dans une variable locale
 *  pouvait être écrasé avant d'être parti.
 *
 *  Pour aller plus loin : UM1734, bibliothèque USB « device » de ST,
 *  https://www.st.com/resource/en/user_manual/um1734-stm32cube-usb-device-library-stmicroelectronics.pdf
 ******************************************************************************
 */
#ifndef USB_CONSOLE_H
#define USB_CONSOLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "line_builder.h"

/* ---- Pour l'application (boucle principale) ------------------------------ */

/**
 * @brief  Rend la prochaine ligne de commande reçue du PC.
 * @param  line  destination (CMD_LINE_MAX + 1 octets conseillés)
 * @param  size  taille de line en octets
 * @retval LINE_READY : une ligne est dans line (terminée par '\0') ;
 *         LINE_TOO_LONG : une ligne trop longue a été reçue et rejetée ;
 *         LINE_NONE : plus rien à lire pour le moment
 */
line_status_t usb_console_read_line(char *line, size_t size);

/**
 * @brief  Confie des octets à envoyer au PC (copiés aussitôt).
 * @param  data  octets à envoyer
 * @param  len   nombre d'octets
 * @retval true si les octets sont dans le tampon ; false si l'USB n'est pas
 *         prêt (câble débranché) ou si le tampon est plein (le PC ne lit
 *         plus) : le message est alors abandonné, entier
 */
bool usb_console_write(const uint8_t *data, uint16_t len);

/**
 * @brief  Envoie une chaîne de caractères (terminée par '\0') au PC.
 * @retval comme usb_console_write()
 */
bool usb_console_print(const char *text);

/**
 * @brief  Envoie un texte mis en forme comme avec printf (127 caractères au
 *         plus), par exemple usb_console_printf("x = %d\n\r", x).
 * @retval comme usb_console_write()
 */
bool usb_console_printf(const char *format, ...) __attribute__((format(printf, 1, 2)));

/**
 * @brief  À appeler dans la boucle principale : envoie au PC le contenu du
 *         tampon d'émission, morceau par morceau.
 */
void usb_console_process(void);

/**
 * @brief  Indique si l'USB est utilisable : la carte est reconnue
 *         (« énumérée ») par le PC et le bus n'est pas en veille. Ne dit pas
 *         si un terminal a ouvert le port.
 */
bool usb_console_is_ready(void);

/**
 * @brief  Rend true une seule fois après chaque connexion (ou réveil) de
 *         l'USB : sert à envoyer « USB Connected » au téléphone.
 */
bool usb_console_take_link_up_event(void);

/* ---- Rappels appelés par le code généré (usbd_cdc_if.c, usbd_conf.c) ----- */

/** Le PC a configuré le port (fin de l'énumération) : CDC_Init_FS(). */
void usb_console_on_init(void);
/** Le port n'est plus configuré (débranchement, reset du bus) : CDC_DeInit_FS(). */
void usb_console_on_deinit(void);
/** Un paquet de len octets est arrivé dans buf : CDC_Receive_FS(). */
void usb_console_on_receive(uint8_t *buf, uint32_t len);
/** Bus USB en veille (câble débranché, PC endormi) : HAL_PCD_SuspendCallback(). */
void usb_console_on_suspend(void);
/** Fin de veille du bus USB : HAL_PCD_ResumeCallback(). */
void usb_console_on_resume(void);

#endif /* USB_CONSOLE_H */

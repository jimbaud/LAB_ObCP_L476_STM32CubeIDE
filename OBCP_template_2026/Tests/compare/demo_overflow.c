/**
 ******************************************************************************
 * @file    Tests/compare/demo_overflow.c
 * @brief   Démonstration d'un débordement de tableau de la version 2024.
 *
 *  On tape « start timer » dans un terminal, une touche à la fois (paquets
 *  USB d'un seul caractère). La version 2024 rangeait les caractères dans
 *  char UsbBuffer2[10] sans vérifier la place restante : le 11e caractère
 *  est écrit après la fin du tableau, sur les variables voisines (sur la
 *  carte, rien ne le signale : le programme se comporte ensuite de façon
 *  imprévisible). Compilé avec AddressSanitizer (-fsanitize=address), le
 *  programme s'arrête net sur l'écriture fautive et affiche
 *  « global-buffer-overflow ... UsbBuffer2 ».
 *
 *  La version 2026 limite la ligne à CMD_LINE_MAX caractères
 *  (line_builder.c) : la commande est exécutée normalement.
 *
 *  Lancement : make demo2024 (dans le dossier Tests).
 ******************************************************************************
 */
#include <stdio.h>
#include "adapter.h"
#include "fake_board.h"

int main(void)
{
  const char *keys = "start timer\r";

  adapter_init();
  adapter_usb_connect();
  printf("Version %s : on tape « start timer » puis Entrée, touche par touche...\n",
         adapter_name);
  fflush(stdout);
  for (; *keys != '\0'; keys++)
  {
    adapter_usb_send(keys, 1U);
    adapter_loop();
    adapter_loop();
  }
  printf("Version %s : minuterie %s\n", adapter_name,
         htim7.running ? "lancée, tout va bien" : "NON lancée");
  return htim7.running ? 0 : 1;
}

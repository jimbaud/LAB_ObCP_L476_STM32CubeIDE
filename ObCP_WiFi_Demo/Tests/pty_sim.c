/*
 * Essai de bout en bout du script Tools/maj_module_ota.py : l'application
 * tourne sur le PC face au module simulé (en temps réel), sa console étant
 * reliée à un pseudo-terminal que le script ouvre comme un port série.
 *   build/pty_sim [journal] [durée en s]   -> affiche le chemin du port
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "app.h"
#include "host.h"
#include "sim_module.h"

int main(int argc, char **argv)
{
  const char    *journal = (argc > 1) ? argv[1] : "build/pty_transcript.txt";
  long           duree = (argc > 2) ? strtol(argv[2], NULL, 10) : 120;
  int            m = posix_openpt(O_RDWR | O_NOCTTY);
  int            esclave;
  struct termios t;
  time_t         t0;

  if ((m < 0) || (grantpt(m) != 0) || (unlockpt(m) != 0))
  {
    perror("posix_openpt");
    return 1;
  }
  /* côté « port série » en mode brut, gardé ouvert pour éviter les fins de ligne */
  esclave = open(ptsname(m), O_RDWR | O_NOCTTY);
  if ((esclave < 0) || (tcgetattr(esclave, &t) != 0))
  {
    perror("pty");
    return 1;
  }
  cfmakeraw(&t);
  (void)tcsetattr(esclave, TCSANOW, &t);
  (void)fcntl(m, F_SETFL, O_NONBLOCK);
  printf("%s\n", ptsname(m));
  fflush(stdout);

  sim_reset();
  sim_set_version("2.0.97");
  host_transcript(journal);
  host_attach_fd(m);
  app_init();
  t0 = time(NULL);
  while ((time(NULL) - t0) < duree)
  {
    app_loop();
    usleep(500);
  }
  close(esclave);
  close(m);
  return 0;
}

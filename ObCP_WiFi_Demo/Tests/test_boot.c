/*
 * Démarrage automatique (version 1.2) : la configuration est enregistrée dans
 * la flash simulée AVANT app_init(), comme sur une carte mise sous tension sur
 * batterie, sans terminal. Chaque scénario est une exécution séparée (les
 * variables des modules repartent de zéro, comme après une vraie mise sous
 * tension) :
 *
 *   ./test_boot box          box enregistrée + web.auto + mqtt.auto
 *   ./test_boot sans-box     aucun réseau enregistré, web.auto seul
 *   ./test_boot box-absente  la connexion à la box échoue (mot de passe refusé)
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "stm32l4xx_hal.h"
#include "app.h"
#include "config.h"
#include "demos.h"
#include "ncp.h"
#include "sim_module.h"
#include "host.h"

static int g_fail, g_pass;

#define CHECK(c) do { if (c) { g_pass++; } else { g_fail++; printf("  ECHEC %s:%d : %s\n", __FILE__, __LINE__, #c); } } while (0)
#define EXPECT(s) do { if (strstr(host_output(), (s)) != NULL) { g_pass++; } else { g_fail++; \
  printf("  ECHEC %s:%d : sortie sans « %s »\n", __FILE__, __LINE__, (s)); } } while (0)
#define REJECT(s) do { if (strstr(host_output(), (s)) == NULL) { g_pass++; } else { g_fail++; \
  printf("  ECHEC %s:%d : sortie avec « %s »\n", __FILE__, __LINE__, (s)); } } while (0)

static void loop_ms(uint32_t ms)
{
  uint32_t t0 = HAL_GetTick();
  while ((HAL_GetTick() - t0) < ms)
  {
    app_loop();
  }
}

static void cmd(const char *line)
{
  char buf[256];
  host_clear();
  (void)snprintf(buf, sizeof(buf), "%s\r", line);
  host_type(buf, 0U);
  while (host_input_pending())
  {
    app_loop();
  }
  app_loop();
}

/* Requête HTTP simulée sur le lien link ; retourne la réponse complète */
static const char *http(int link, const char *req)
{
  char     ipd[96];
  uint32_t n;

  sim_sent_clear();
  sim_set_socket_data((const uint8_t *)req, (uint16_t)strlen(req));
  (void)snprintf(ipd, sizeof(ipd), "+CIP:%d,CONNECTED\r\n+IPD:%d,%u,\"10.19.96.2\",51000\r\n", link, link,
                 (unsigned)strlen(req));
  sim_push(ipd);
  loop_ms(4000);
  return sim_link_sent(link, &n);
}

/* Configuration enregistrée en flash avant la « mise sous tension » */
static void saved_config(const char *ssid, const char *pass, bool web, bool mqtt)
{
  cfg_defaults(&g_cfg);
  if (ssid != NULL)
  {
    CHECK(cfg_set(&g_cfg, "wifi.ssid", ssid) == CFG_OK);
    CHECK(cfg_set(&g_cfg, "wifi.pass", pass) == CFG_OK);
  }
  CHECK(cfg_set(&g_cfg, "web.auto", web ? "1" : "0") == CFG_OK);
  CHECK(cfg_set(&g_cfg, "mqtt.auto", mqtt ? "1" : "0") == CFG_OK);
  CHECK(config_save() == 0);
  memset(&g_cfg, 0, sizeof(g_cfg));             /* RAM vierge, comme après un reset */
}

/* Box enregistrée, serveur web et MQTT automatiques */
static void scenario_box(void)
{
  const char *r;
  int         w0;

  printf("- box enregistrée, serveur web et MQTT au démarrage\n");
  saved_config("Lab", "labpass+123", true, true);
  app_init();
  EXPECT("Config.      : chargée depuis la flash (réseau « Lab »)");
  EXPECT("Démarrage    : connexion à « Lab », serveur web, MQTT");
  EXPECT("Serveur web : démarrage automatique (web.auto = 1) après la connexion à la box.");
  CHECK(sim_cmd_count("AT+CWSAP=") == 0);
  loop_ms(8000);
  EXPECT("Wi-Fi : connecté à « Lab »");
  EXPECT("Point d'accès et serveur web actifs (démarrage automatique)");
  EXPECT("Réseau Wi-Fi  : ");
  EXPECT("ObCP-WiFi-A1B2C3");
  EXPECT("http://10.19.96.1/");
  EXPECT("Réseau local  : http://192.168.1.42/");
  EXPECT("« web start » affiche le QR code");
  REJECT("\033[30;47m");                         /* pas de QR code au démarrage automatique */
  CHECK(web_running());
  CHECK(sim_cmd_count("AT+CIPSERVER=1,80") == 1);
  /* le point d'accès démarre après la connexion à la box (canal de la box) */
  CHECK(sim_cmd_first("AT+CWJAP=\"Lab\"") >= 0);
  CHECK(sim_cmd_first("AT+CWSAP=") > sim_cmd_first("AT+CWJAP=\"Lab\""));
  loop_ms(4000);
  CHECK(mqttapp_connected());
  CHECK(sim_cmd_count("AT+MQTTCONN=0,\"broker.emqx.io\",1883,0") == 1);
  CHECK(sim_data_find("\"obcp/A1B2C3/telemetry\"", "\"id\":\"A1B2C3\"") >= 1);

  /* page web : lecture et modification du démarrage automatique */
  r = http(0, "GET /api/auto HTTP/1.1\r\nHost: 10.19.96.1\r\n\r\n");
  CHECK(strncmp(r, "HTTP/1.1 200 OK", 15) == 0);
  CHECK(strstr(r, "{\"web\":true,\"mqtt\":true,\"saved\":true}") != NULL);
  w0 = g_host_flash_writes;
  host_clear();
  r = http(1, "GET /api/auto?web=0&mqtt=1 HTTP/1.1\r\nHost: 10.19.96.1\r\n\r\n");
  CHECK(strstr(r, "{\"web\":false,\"mqtt\":true,\"saved\":true}") != NULL);
  CHECK(g_cfg.web_auto == 0U);
  CHECK(g_cfg.mqtt_auto == 1U);
  CHECK(g_host_flash_writes == w0 + 1);
  EXPECT("Page web : démarrage automatique sans serveur web, avec MQTT (enregistré en flash)");
  CHECK(config_load() == CFG_UNPACK_OK);
  CHECK((g_cfg.web_auto == 0U) && (g_cfg.mqtt_auto == 1U));
  r = http(2, "GET /api/auto?web=2 HTTP/1.1\r\nHost: 10.19.96.1\r\n\r\n");   /* valeur ignorée */
  CHECK(strstr(r, "{\"web\":false,\"mqtt\":true,\"saved\":true}") != NULL);
  CHECK(g_host_flash_writes == w0 + 1);

  /* commande auto */
  cmd("auto");
  EXPECT("Démarrage automatique");
  EXPECT("Wi-Fi (box)   : connexion à « Lab », reconnexion automatique");
  EXPECT("Serveur web   : non (auto web on)");
  EXPECT("MQTT          : oui, après la connexion à la box");
  REJECT("Modifications non enregistrées");
  cmd("auto web on");
  CHECK(g_cfg.web_auto == 1U);
  EXPECT("Serveur web   : oui, point d'accès « ObCP-WiFi-A1B2C3 » (mot de passe obcpdemo), http://10.19.96.1/");
  EXPECT("Modifications non enregistrées");
  cmd("auto mqtt");
  EXPECT("Usage : auto");
  cmd("auto radio on");
  EXPECT("Usage : auto");
  cmd("save");
  EXPECT("Configuration enregistrée");
  cmd("info");
  EXPECT("Démarrage auto : connexion à « Lab », serveur web, MQTT");
  cmd("help auto");
  EXPECT("auto web on");
  cmd("help demo");
  EXPECT("Sur batterie, sans terminal");
  cmd("set web.auto 0");
  EXPECT("pris en compte à la prochaine mise sous tension");
  cmd("set web.auto 1");
  cmd("config web");
  EXPECT("web.auto");

  /* le module redémarre de lui-même : point d'accès et serveur relancés */
  host_clear();
  sim_push("\r\nready\r\n");
  loop_ms(12000);
  EXPECT("a redémarré de lui-même");
  EXPECT("Wi-Fi : connecté à « Lab »");
  EXPECT("Point d'accès et serveur web actifs (démarrage automatique)");
  CHECK(sim_cmd_count("AT+CIPSERVER=1,80") == 2);
  CHECK(web_running());
  loop_ms(4000);
  CHECK(mqttapp_connected());

  /* « web stop » : plus de relance après un redémarrage du module */
  cmd("web stop");
  CHECK(!web_running());
  cmd("module reset");
  loop_ms(12000);
  CHECK(!web_running());
  CHECK(sim_cmd_count("AT+CIPSERVER=1,80") == 2);
  cmd("web status");
  EXPECT("Serveur web : arrêté (« web start » pour le lancer)");
  cmd("web start");
  EXPECT("\033[30;47m");                         /* QR code avec la commande */
  REJECT("set web.auto 1");                      /* déjà en démarrage automatique */
}

/* Aucun réseau enregistré : le point d'accès démarre tout de suite */
static void scenario_sans_box(void)
{
  printf("- aucun réseau enregistré, serveur web au démarrage\n");
  saved_config(NULL, NULL, true, false);
  app_init();
  EXPECT("Démarrage    : serveur web");
  EXPECT("Serveur web : démarrage automatique (web.auto = 1).");
  loop_ms(3000);
  CHECK(web_running());
  CHECK(sim_cmd_count("AT+CWJAP=") == 0);
  CHECK(sim_cmd_count("AT+CWSAP=\"ObCP-WiFi-A1B2C3\",\"obcpdemo\"") == 1);
  REJECT("Réseau local");
  cmd("web start");
  EXPECT("déjà actif");
  cmd("auto");
  EXPECT("Wi-Fi (box)   : aucun réseau enregistré");
}

/* La box refuse la connexion : le serveur web démarre quand même, sans attendre */
static void scenario_box_absente(void)
{
  uint32_t t0;

  printf("- connexion à la box impossible, serveur web au démarrage\n");
  saved_config("Lab", "wrong", true, true);
  t0 = HAL_GetTick();
  app_init();
  while (!web_running() && ((HAL_GetTick() - t0) < 40000U))
  {
    app_loop();
  }
  CHECK(web_running());
  CHECK((HAL_GetTick() - t0) < 10000U);          /* dès l'échec, pas au bout de 35 s */
  EXPECT("échec de connexion à « Lab »");
  CHECK(!mqttapp_connected());
  loop_ms(20000);                                /* la station réessaie, le serveur reste actif */
  CHECK(sim_cmd_count("AT+CWJAP=\"Lab\"") >= 2);
  CHECK(web_running());
  CHECK(sim_cmd_count("AT+CIPSERVER=1,80") == 1);
  cmd("auto");
  EXPECT("MQTT          : oui, après la connexion à la box");
}

int main(int argc, char **argv)
{
  const char *which = (argc > 1) ? argv[1] : "box";
  char        journal[64];

  sim_reset();
  (void)snprintf(journal, sizeof(journal), "build/boot_%s.txt", which);
  host_transcript(journal);
  if (strcmp(which, "sans-box") == 0)
  {
    scenario_sans_box();
  }
  else if (strcmp(which, "box-absente") == 0)
  {
    scenario_box_absente();
  }
  else
  {
    scenario_box();
  }
  printf("  %d vérifications réussies, %d échec(s) - journal : %s\n", g_pass, g_fail, journal);
  return (g_fail == 0) ? 0 : 1;
}

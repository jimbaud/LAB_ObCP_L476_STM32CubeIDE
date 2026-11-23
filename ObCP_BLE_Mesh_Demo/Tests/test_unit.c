/**
 ******************************************************************************
 * @file    Tests/test_unit.c
 * @brief   Tests unitaires des fonctions « pures » : format des trames,
 *          emballage dans une publicité (et données abîmées), adresses,
 *          réglages, analyse de toutes les commandes, CRC de l'EEPROM.
 ******************************************************************************
 */
#include <stdint.h>
#include <string.h>
#include "check.h"

#include "app.h"
#include "app_config.h"
#include "fake_bluenrg.h"
#include "commands.h"
#include "config_store.h"
#include "led_fx.h"
#include "mesh_pdu.h"
#include "mesh_radio.h"
#include "settings.h"

/* -------------------------------------------------------------------------- */
static void test_pdu(void)
{
  mesh_frame_t f;
  mesh_frame_t g;
  uint8_t      pdu[MESH_PDU_MAX];
  uint8_t      len;
  const uint8_t expected[] = { 0x4D, 0x01, 0x00, 0x05, 0xFF, 0x2A, 0x03, 0x01, 0x09, 0x06, 'O', 'K' };

  printf("- trame : codage, décodage, exemple de mesh_pdu.h, cas refusés\n");
  memset(&f, 0, sizeof(f));
  f.net = 0U; f.src = 5U; f.dst = MESH_ADDR_ALL; f.seq = 42U; f.ttl = 3U; f.hops = 1U; f.last = 9U;
  f.type = MESH_T_TEXT; f.len = 2U; f.payload[0] = 'O'; f.payload[1] = 'K';
  len = mesh_pdu_encode(&f, pdu, sizeof(pdu));
  CHECK(len == 12U);
  CHECK(memcmp(pdu, expected, sizeof(expected)) == 0);
  CHECK(mesh_pdu_decode(pdu, len, &g));
  CHECK(g.src == 5U && g.dst == 255U && g.seq == 42U && g.ttl == 3U && g.hops == 1U && g.last == 9U);
  CHECK(g.type == MESH_T_TEXT && g.len == 2U && memcmp(g.payload, "OK", 2) == 0);

  CHECK(mesh_pdu_encode(&f, pdu, 11U) == 0U);       /* tampon trop petit */
  f.len = MESH_PAYLOAD_MAX + 1U;
  CHECK(mesh_pdu_encode(&f, pdu, sizeof(pdu)) == 0U);   /* données trop longues */
  f.len = MESH_PAYLOAD_MAX;
  CHECK(mesh_pdu_encode(&f, pdu, sizeof(pdu)) == MESH_PDU_MAX);

  CHECK(!mesh_pdu_decode(pdu, MESH_HEADER_LEN - 1U, &g));      /* trop court */
  CHECK(!mesh_pdu_decode(pdu, MESH_PDU_MAX + 1U, &g));         /* trop long */
  pdu[0] = 0x4E;
  CHECK(!mesh_pdu_decode(pdu, len, &g));                       /* autre magic */
  pdu[0] = 0x4D; pdu[1] = 0x02;
  CHECK(!mesh_pdu_decode(pdu, len, &g));                       /* autre version */
  pdu[1] = 0x01; pdu[3] = 0x00;
  CHECK(!mesh_pdu_decode(pdu, len, &g));                       /* origine 0 */
  pdu[3] = 240U;
  CHECK(!mesh_pdu_decode(pdu, len, &g));                       /* origine = groupe */
  pdu[3] = 5U; pdu[4] = 0U;
  CHECK(!mesh_pdu_decode(pdu, len, &g));                       /* destination 0 */
  pdu[4] = 255U; pdu[8] = 255U;
  CHECK(!mesh_pdu_decode(pdu, len, &g));                       /* émetteur = tous */
}

static void test_adv(void)
{
  uint8_t        pdu[MESH_PDU_MAX];
  uint8_t        adv[40];
  uint8_t        n;
  const uint8_t *found;
  uint8_t        found_len;
  mesh_frame_t   f;

  printf("- publicité : emballage, recherche parmi d'autres champs, données abîmées\n");
  memset(&f, 0, sizeof(f));
  f.src = 1U; f.dst = 2U; f.last = 1U; f.type = MESH_T_PING; f.len = 1U; f.payload[0] = 7U;
  n = mesh_pdu_encode(&f, pdu, sizeof(pdu));
  CHECK(mesh_adv_build(pdu, n, adv, sizeof(adv)) == (uint8_t)(n + 4U));
  CHECK(adv[0] == (uint8_t)(n + 3U) && adv[1] == 0xFF && adv[2] == 0xFF && adv[3] == 0xFF);
  CHECK(mesh_adv_find(adv, (size_t)n + 4U, &found, &found_len));
  CHECK(found == &adv[4] && found_len == n);
  CHECK(mesh_adv_build(pdu, n, adv, 10U) == 0U);
  {
    uint8_t big[MESH_PDU_MAX];
    memset(big, 0, sizeof(big));
    CHECK(mesh_adv_build(big, MESH_PDU_MAX, adv, sizeof(adv)) == MESH_ADV_MAX);
    CHECK(MESH_ADV_MAX <= 31U);                    /* tient dans une publicité */
  }
  {
    /* champ « Flags », puis nom, puis notre trame */
    uint8_t mixed[31] = { 2, 0x01, 0x06, 4, 0x09, 'a', 'b', 'c' };
    uint8_t m = 8U;
    m = (uint8_t)(m + mesh_adv_build(pdu, n, &mixed[m], sizeof(mixed) - m));
    CHECK(mesh_adv_find(mixed, m, &found, &found_len));
    CHECK(found == &mixed[12] && found_len == n);
  }
  {
    const uint8_t other_company[] = { 6, 0xFF, 0x30, 0x00, 0x4D, 0x01, 0x00 };   /* ST, pas nous */
    const uint8_t overflow[]      = { 30, 0xFF, 0xFF, 0xFF, 0x4D };              /* déborde */
    const uint8_t zero[]          = { 0, 0xFF, 0xFF, 0xFF, 0x4D };
    const uint8_t short_field[]   = { 5, 0xFF, 0xFF, 0xFF, 0x4D, 0x01 };          /* moins qu'un en-tête */
    CHECK(!mesh_adv_find(other_company, sizeof(other_company), &found, &found_len));
    CHECK(!mesh_adv_find(overflow, sizeof(overflow), &found, &found_len));
    CHECK(!mesh_adv_find(zero, sizeof(zero), &found, &found_len));
    CHECK(!mesh_adv_find(short_field, sizeof(short_field), &found, &found_len));
    CHECK(!mesh_adv_find(adv, 0U, &found, &found_len));
  }
}

static void test_addresses(void)
{
  uint8_t a = 0U;
  char    text[8];

  printf("- adresses : nœuds, groupes, tous\n");
  CHECK(mesh_addr_parse("17", &a) && a == 17U);
  CHECK(mesh_addr_parse("1", &a) && a == 1U);
  CHECK(mesh_addr_parse("239", &a) && a == 239U);
  CHECK(!mesh_addr_parse("240", &a));
  CHECK(!mesh_addr_parse("0", &a));
  CHECK(!mesh_addr_parse("1234", &a));
  CHECK(!mesh_addr_parse("", &a));
  CHECK(!mesh_addr_parse("12a", &a));
  CHECK(mesh_addr_parse("g1", &a) && a == 240U);
  CHECK(mesh_addr_parse("G15", &a) && a == 254U);
  CHECK(!mesh_addr_parse("g0", &a));
  CHECK(!mesh_addr_parse("g16", &a));
  CHECK(!mesh_addr_parse("g", &a));
  CHECK(mesh_addr_parse("tous", &a) && a == 255U);
  CHECK(mesh_addr_parse("*", &a) && a == 255U);
  CHECK(mesh_addr_is_node(1U) && mesh_addr_is_node(239U) && !mesh_addr_is_node(0U) && !mesh_addr_is_node(240U));
  CHECK(mesh_addr_is_group(240U) && mesh_addr_is_group(254U) && !mesh_addr_is_group(255U));
  CHECK(mesh_addr_group(MESH_GROUP_ADDR(7U)) == 7U && mesh_addr_group(17U) == 0U);
  (void)mesh_addr_format(17U, text, sizeof(text));
  CHECK_STR(text, "17");
  (void)mesh_addr_format(MESH_GROUP_ADDR(3U), text, sizeof(text));
  CHECK_STR(text, "g3");
  (void)mesh_addr_format(255U, text, sizeof(text));
  CHECK_STR(text, "tous");
  CHECK_STR(mesh_type_name(MESH_T_HELLO), "HELLO");
  CHECK_STR(mesh_type_name(MESH_T_CONFIG), "REGLAGE");
  CHECK_STR(mesh_type_name(0x7F), "?");
}

static void test_settings(void)
{
  int32_t   v = 0;
  setting_t k = SETTING_END;
  char      text[24];

  printf("- réglages : noms, limites, valeurs on/off, affichage\n");
  CHECK(settings_from_name("ttl", &k) && k == SETTING_TTL);
  CHECK(settings_from_name("flash", &k) && k == SETTING_FLASH);
  CHECK(!settings_from_name("TTL", &k));
  CHECK_STR(settings_name(SETTING_RSSI_MIN), "rssimin");
  CHECK_STR(settings_name((setting_t)0), "?");
  CHECK_STR(settings_name((setting_t)99), "?");
  CHECK(settings_parse_value(SETTING_TTL, "15", &v) && v == 15);
  CHECK(!settings_parse_value(SETTING_TTL, "16", &v));
  CHECK(!settings_parse_value(SETTING_TTL, "-1", &v));
  CHECK(!settings_parse_value(SETTING_TTL, "3x", &v));
  CHECK(!settings_parse_value(SETTING_TTL, "", &v));
  CHECK(settings_parse_value(SETTING_RELAY, "off", &v) && v == 0);
  CHECK(settings_parse_value(SETTING_RELAY, "oui", &v) && v == 1);
  CHECK(!settings_parse_value(SETTING_RELAY, "2", &v));
  CHECK(settings_parse_value(SETTING_RSSI_MIN, "-65", &v) && v == -65);
  CHECK(settings_parse_value(SETTING_RSSI_MIN, "off", &v) && v == SETTING_RSSI_OFF);
  CHECK(!settings_parse_value(SETTING_RSSI_MIN, "-10", &v));
  CHECK(!settings_parse_value(SETTING_RSSI_MIN, "-101", &v));
  CHECK(settings_parse_value(SETTING_TX_POWER, "-18", &v) && v == -18);
  CHECK(!settings_parse_value(SETTING_TX_POWER, "9", &v));
  CHECK(settings_parse_value(SETTING_HELLO, "0", &v) && v == 0);
  CHECK(!settings_parse_value(SETTING_HELLO, "256", &v));
  CHECK(!settings_parse_value(SETTING_SUPPRESS, "11", &v));
  CHECK(settings_value_ok(SETTING_RSSI_MIN, SETTING_RSSI_OFF));
  CHECK(!settings_value_ok((setting_t)0, 0));
  (void)settings_format_value(SETTING_RSSI_MIN, -60, text, sizeof(text));
  CHECK_STR(text, "-60 dBm");
  (void)settings_format_value(SETTING_RSSI_MIN, SETTING_RSSI_OFF, text, sizeof(text));
  CHECK_STR(text, "off");
  (void)settings_format_value(SETTING_HELLO, 0, text, sizeof(text));
  CHECK_STR(text, "0 (aucun)");
  (void)settings_format_value(SETTING_AUTO, 30, text, sizeof(text));
  CHECK_STR(text, "30 s");
  (void)settings_format_value(SETTING_FLASH, 1, text, sizeof(text));
  CHECK_STR(text, "on");
  (void)settings_format_value(SETTING_TTL, 4, text, sizeof(text));
  CHECK_STR(text, "4");
}

static void test_commands(void)
{
  cmd_t c;

  printf("- commandes : toutes les formes reconnues et les erreurs\n");
  commands_parse("", &c);                  CHECK(c.id == CMD_EMPTY);
  commands_parse("   \t ", &c);            CHECK(c.id == CMD_EMPTY);
  commands_parse("aide", &c);              CHECK(c.id == CMD_HELP);
  commands_parse("?", &c);                 CHECK(c.id == CMD_HELP);
  commands_parse("info", &c);              CHECK(c.id == CMD_INFO);
  commands_parse("voisins", &c);           CHECK(c.id == CMD_NEIGHBORS);
  commands_parse("noeuds", &c);            CHECK(c.id == CMD_NODES);
  commands_parse("stats", &c);             CHECK(c.id == CMD_STATS);
  commands_parse("stats raz", &c);         CHECK(c.id == CMD_STATS_CLEAR);
  commands_parse("mesure", &c);            CHECK(c.id == CMD_MEASURE_LOCAL);
  commands_parse("mesure g2", &c);         CHECK(c.id == CMD_MEASURE_REQ && c.dst == MESH_GROUP_ADDR(2U));
  commands_parse("mesure x", &c);          CHECK(c.id == CMD_ERROR);

  commands_parse("msg 17 Bonjour à tous", &c);
  CHECK(c.id == CMD_MSG && c.dst == 17U);
  CHECK_STR(c.text, "Bonjour à tous");
  commands_parse("  msg   tous   a  b ", &c);
  CHECK(c.id == CMD_MSG && c.dst == 255U);
  CHECK_STR(c.text, "a  b ");
  commands_parse("msg 17", &c);            CHECK(c.id == CMD_ERROR);
  commands_parse("msg", &c);               CHECK(c.id == CMD_ERROR);
  commands_parse("msg 0 x", &c);           CHECK(c.id == CMD_ERROR);

  commands_parse("led 5 rouge", &c);
  CHECK(c.id == CMD_LED && c.dst == 5U && c.color.r == 255U && c.color.g == 0U && !c.has_value);
  commands_parse("led tous 10 20 30 5", &c);
  CHECK(c.id == CMD_LED && c.color.r == 10U && c.color.g == 20U && c.color.b == 30U && c.has_value && c.value == 5);
  commands_parse("led g1 eteint", &c);
  CHECK(c.id == CMD_LED && c.color.r == 0U && c.color.g == 0U && c.color.b == 0U);
  commands_parse("led 5 mauve", &c);       CHECK(c.id == CMD_ERROR);
  commands_parse("led 5 300 0 0", &c);     CHECK(c.id == CMD_ERROR);
  commands_parse("led 5 rouge 999", &c);   CHECK(c.id == CMD_ERROR);
  commands_parse("led 5 rouge 3 4", &c);   CHECK(c.id == CMD_ERROR);

  commands_parse("vague", &c);             CHECK(c.id == CMD_WAVE);
  commands_parse("ident 12", &c);          CHECK(c.id == CMD_IDENT && c.dst == 12U && c.value == (int32_t)NODE_IDENT_DEFAULT_S);
  commands_parse("ident tous 9", &c);      CHECK(c.id == CMD_IDENT && c.value == 9);
  commands_parse("ident 12 0", &c);        CHECK(c.id == CMD_ERROR);
  commands_parse("ping 5", &c);            CHECK(c.id == CMD_PING && c.dst == 5U);
  commands_parse("ping", &c);              CHECK(c.id == CMD_ERROR);
  commands_parse("trace 9", &c);           CHECK(c.id == CMD_TRACE && c.dst == 9U);
  commands_parse("trace tous", &c);        CHECK(c.id == CMD_ERROR);

  commands_parse("regle tous rssimin -65", &c);
  CHECK(c.id == CMD_REMOTE_SET && c.dst == 255U && c.key == SETTING_RSSI_MIN && c.value == -65);
  commands_parse("regle 5 relais off", &c);
  CHECK(c.id == CMD_REMOTE_SET && c.key == SETTING_RELAY && c.value == 0);
  commands_parse("regle 5 ttl 99", &c);    CHECK(c.id == CMD_ERROR);
  commands_parse("regle 5 truc 1", &c);    CHECK(c.id == CMD_ERROR);
  commands_parse("regle 5 ttl", &c);       CHECK(c.id == CMD_ERROR);

  commands_parse("ttl", &c);               CHECK(c.id == CMD_SET && c.key == SETTING_TTL && !c.has_value);
  commands_parse("ttl 3", &c);             CHECK(c.id == CMD_SET && c.has_value && c.value == 3);
  commands_parse("ttl 30", &c);            CHECK(c.id == CMD_ERROR);
  commands_parse("puissance -18", &c);     CHECK(c.id == CMD_SET && c.key == SETTING_TX_POWER && c.value == -18);
  commands_parse("hello 0", &c);           CHECK(c.id == CMD_SET && c.key == SETTING_HELLO && c.value == 0);

  commands_parse("id", &c);                CHECK(c.id == CMD_ID && !c.has_value);
  commands_parse("id 42", &c);             CHECK(c.id == CMD_ID && c.has_value && c.value == 42);
  commands_parse("id auto", &c);           CHECK(c.id == CMD_ID && c.has_value && c.value == 0);
  commands_parse("id 240", &c);            CHECK(c.id == CMD_ERROR);
  commands_parse("reseau 255", &c);        CHECK(c.id == CMD_NET && c.value == 255);
  commands_parse("réseau 3", &c);          CHECK(c.id == CMD_NET && c.value == 3);
  commands_parse("reseau 256", &c);        CHECK(c.id == CMD_ERROR);
  commands_parse("groupe", &c);            CHECK(c.id == CMD_GROUP && !c.has_value);
  commands_parse("groupe +2", &c);         CHECK(c.id == CMD_GROUP && c.join && c.value == 2);
  commands_parse("groupe -g15", &c);       CHECK(c.id == CMD_GROUP && !c.join && c.value == 15);
  commands_parse("groupe 7", &c);          CHECK(c.id == CMD_GROUP && c.join && c.value == 7);
  commands_parse("groupe +16", &c);        CHECK(c.id == CMD_ERROR);
  commands_parse("bloque", &c);            CHECK(c.id == CMD_BLOCK && !c.has_value);
  commands_parse("bloque 8", &c);          CHECK(c.id == CMD_BLOCK && c.value == 8);
  commands_parse("debloque tous", &c);     CHECK(c.id == CMD_UNBLOCK && c.value == 0);
  commands_parse("debloque 8", &c);        CHECK(c.id == CMD_UNBLOCK && c.value == 8);
  commands_parse("debloque", &c);          CHECK(c.id == CMD_ERROR);
  commands_parse("radio", &c);             CHECK(c.id == CMD_RADIO && !c.has_value);
  commands_parse("radio alterne", &c);     CHECK(c.id == CMD_RADIO && c.value == CMD_RADIO_ALTERNATE);
  commands_parse("radio simultane", &c);   CHECK(c.id == CMD_RADIO && c.value == CMD_RADIO_SIMULTANEOUS);
  commands_parse("radio auto", &c);        CHECK(c.id == CMD_RADIO && c.value == CMD_RADIO_AUTO);
  commands_parse("radio vite", &c);        CHECK(c.id == CMD_ERROR);
  commands_parse("journal on", &c);        CHECK(c.id == CMD_JOURNAL && c.has_value && c.value == 1);
  commands_parse("journal", &c);           CHECK(c.id == CMD_JOURNAL && !c.has_value);
  commands_parse("journal peut-être", &c); CHECK(c.id == CMD_ERROR);
  commands_parse("pc off", &c);            CHECK(c.id == CMD_PC && c.value == 0);
  commands_parse("sauve", &c);             CHECK(c.id == CMD_SAVE);
  commands_parse("oublie", &c);            CHECK(c.id == CMD_FORGET);
  commands_parse("cls", &c);               CHECK(c.id == CMD_CLS);
  commands_parse("#ota", &c);              CHECK(c.id == CMD_OTA);
  commands_parse("PING 5", &c);            CHECK(c.id == CMD_PING && c.dst == 5U);   /* majuscules permises */
  commands_parse("Msg TOUS Salut", &c);    CHECK(c.id == CMD_MSG && c.dst == 255U);
  CHECK_STR(c.text, "Salut");                                             /* le texte garde sa casse */
  commands_parse("led 5 ROUGE", &c);       CHECK(c.id == CMD_LED && c.color.r == 255U);
  commands_parse("r500", &c);              CHECK(c.id == CMD_ERROR && c.error != NULL);
  {
    /* ligne de la longueur maximale, faite de mots courts : pas de débordement */
    char line[CMD_LINE_MAX + 1U];
    for (unsigned i = 0U; i < CMD_LINE_MAX; i++)
    {
      line[i] = ((i % 2U) == 0U) ? 'a' : ' ';
    }
    line[CMD_LINE_MAX] = '\0';
    commands_parse(line, &c);
    CHECK(c.id == CMD_ERROR);
  }
  CHECK(strstr(commands_help_text(), "regle dest réglage valeur") != NULL);
}

static void test_crc_and_colors(void)
{
  printf("- CRC-16 de l'EEPROM, couleurs de la vague\n");
  CHECK(config_store_crc16((const uint8_t *)"123456789", 9U) == 0x29B1U);   /* valeur de référence CCITT-FALSE */
  CHECK(config_store_crc16((const uint8_t *)"", 0U) == 0xFFFFU);
  CHECK(led_fx_hops_color(0U).r == 255U && led_fx_hops_color(0U).g == 0U);
  CHECK(led_fx_hops_color(3U).g == 255U && led_fx_hops_color(3U).r == 0U);
  CHECK(led_fx_hops_color(200U).r == led_fx_hops_color(7U).r);
  CHECK_STR(led_fx_hops_color_name(2U), "jaune");
  CHECK_STR(led_fx_hops_color_name(9U), "rose");
}

/* Versions de la pile des modules (format 0xJJMN, lettre comptée à partir de
 * a = 1 : note DT0074 de ST) et seuil du mode 4. */
static void test_radio_versions(void)
{
  char t[12];

  printf("Versions de pile : SPBTLE-RF (7.1e), BlueNRG-M0 (7.2c), seuil du mode 4 (7.1b)\n");
  (void)mesh_radio_version_text(0x0715U, t, sizeof(t));
  CHECK_STR(t, "7.1e");
  (void)mesh_radio_version_text(0x0723U, t, sizeof(t));
  CHECK_STR(t, "7.2c");
  (void)mesh_radio_version_text(0x0711U, t, sizeof(t));
  CHECK_STR(t, "7.1a");
  (void)mesh_radio_version_text(RADIO_STACK_MODE_MIN_FW, t, sizeof(t));
  CHECK_STR(t, "7.1b");
  (void)mesh_radio_version_text(0x0730U, t, sizeof(t));
  CHECK_STR(t, "7.3");
  CHECK(RADIO_STACK_MODE == 4U);
  CHECK((0x0715U >= RADIO_STACK_MODE_MIN_FW) && (0x0711U < RADIO_STACK_MODE_MIN_FW));
}

/* Temps de la radio face aux règles du mode 4 (PM0237, § 4.2.3), telles que
 * le module simulé les applique : les temps de la version 1.2.0, refusés
 * par le module sur carte, le sont aussi par le module simulé. */
static void test_time_base(void)
{
  printf("Temps de la radio et base de temps du mode 4 (règles de ST)\n");
  CHECK(!fake_ble_time_base_fits(80U, 64U, 160U, 160U));      /* 1.2.0 : 40 ms sur 50, publicité 105 ms */
  CHECK(fake_ble_time_base_fits(RADIO_SIM_SCAN_INTERVAL, RADIO_SIM_SCAN_WINDOW, RADIO_ADV_INTERVAL,
                                RADIO_ADV_INTERVAL));
  CHECK(!fake_ble_time_base_fits(RADIO_SCAN_INTERVAL, RADIO_SCAN_WINDOW, RADIO_ADV_INTERVAL,
                                 RADIO_ADV_INTERVAL));        /* écoute continue : aucune place */
  CHECK(((RADIO_ADV_INTERVAL + 8U) % RADIO_SIM_SCAN_INTERVAL) == 0U);   /* 100 + 5 ms = 105 ms */
  CHECK(RADIO_ADV_INTERVAL >= 0x00A0U);                         /* 100 ms : minimum de la norme */
  CHECK((RADIO_SCAN_WINDOW <= RADIO_SCAN_INTERVAL) && (RADIO_SIM_SCAN_WINDOW <= RADIO_SIM_SCAN_INTERVAL));
}

static void test_variant(void)
{
  printf("Version USB ou OTA, d'après l'adresse de la table des vecteurs\n");
  CHECK(!app_address_is_ota(0x08000000U));      /* STM32L476RGTX_FLASH.ld */
  CHECK(!app_address_is_ota(0x0807FFFFU));
  CHECK(app_address_is_ota(0x08080000U));       /* STM32L476RGTX_FLASH_OTA.ld */
  CHECK(app_address_is_ota(0x080FFFFFU));
  CHECK(!app_address_is_ota(0x08100000U));
  CHECK(!app_is_ota());                         /* sur PC : version USB */
}

int main(void)
{
  test_pdu();
  test_adv();
  test_addresses();
  test_settings();
  test_commands();
  test_crc_and_colors();
  test_radio_versions();
  test_time_base();
  test_variant();
  return check_summary("tests unitaires");
}

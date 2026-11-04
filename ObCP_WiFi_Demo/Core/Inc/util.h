/**
 ******************************************************************************
 * @file    util.h
 * @brief   Fonctions utilitaires indépendantes du matériel (testées sur PC) :
 *          chaînes, analyse et mise en forme de nombres et d'adresses,
 *          découpage de la ligne de commande, construction de JSON.
 *
 *  Convention : les fonctions qui écrivent du texte reçoivent le tampon
 *  de destination (out) et sa taille (len) ; elles ne débordent jamais et
 *  terminent toujours le texte par '\0' (texte tronqué si nécessaire).
 ******************************************************************************
 */
#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief  CRC32 (polynôme de l'Ethernet et du ZIP) : empreinte de la configuration en flash. */
uint32_t util_crc32(const void *data, size_t len);

/** @brief  Copie bornée d'une chaîne (toujours terminée) ; retourne strlen(src). */
size_t   util_strlcpy(char *dst, const char *src, size_t size);

/** @brief  Comparaison de deux chaînes sans tenir compte des majuscules. */
bool     util_strieq(const char *a, const char *b);

/** @brief  Nombre de caractères (colonnes) d'un texte UTF-8 (« é » compte pour 1). */
size_t   util_utf8_len(const char *s);

/** @brief  Racine carrée entière (pas de calcul en virgule flottante). */
uint32_t util_isqrt(uint64_t v);

/* ---- Analyse stricte : toute la chaîne doit être valide ------------------- */
/** @brief  Lit une adresse IPv4 « a.b.c.d ». @retval false si invalide. */
bool     util_parse_ip(const char *s, uint8_t ip[4]);
/** @brief  Lit un entier décimal (signe admis). @retval false si invalide. */
bool     util_parse_long(const char *s, long *out);
/** @brief  Lit un booléen : 1/0, on/off, oui/non, true/false. @retval false si invalide. */
bool     util_parse_bool(const char *s, bool *out);

/* ---- Mise en forme ---------------------------------------------------------- */
/** @brief  Adresse IPv4 en texte « a.b.c.d ». */
void     util_fmt_ip(char *out, size_t len, const uint8_t ip[4]);
/** @brief  Adresse MAC en texte « AA:BB:CC:DD:EE:FF ». */
void     util_fmt_mac(char *out, size_t len, const uint8_t mac[6]);
/** @brief  Nombre à virgule fixe : (245, 1) -> "24.5" ; (-5, 1) -> "-0.5". */
void     util_fmt_fixed(char *out, size_t len, int32_t value, unsigned decimals);
/** @brief  Durée en secondes en texte « 3 j 04:05:06 ». */
void     util_fmt_uptime(char *out, size_t len, uint32_t seconds);

/** @brief  Échappement JSON (", \, caractères de contrôle) ; retourne la longueur écrite. */
size_t   util_json_escape(char *out, size_t len, const char *in);
/** @brief  Échappement HTML (<, >, &, ", ') ; retourne la longueur écrite. */
size_t   util_html_escape(char *out, size_t len, const char *in);

/**
 * @brief  Découpe une ligne de commande en arguments séparés par des espaces
 *         ("..." pour un argument contenant des espaces). Modifie la ligne.
 * @param  line  ligne tapée (les séparateurs sont remplacés par '\0').
 * @param  argv  reçoit un pointeur vers chaque argument.
 * @param  max   taille du tableau argv.
 * @retval nombre d'arguments (argc), ou -1 s'il y en a plus que max.
 */
int      util_tokenize(char *line, char **argv, int max);

/* ---- Qualité de signal ------------------------------------------------------- */
/** @brief  Qualité d'un signal de rssi dBm, de 0 % (-100 dBm) à 100 % (-50 dBm). */
int      util_rssi_quality(int rssi);
/** @brief  Appréciation d'un signal : « excellent », « bon », « moyen », « faible »... */
const char *util_rssi_label(int rssi);
/** @brief  Barre de niveau en UTF-8 (█ et ░) de width cellules pour value entre vmin et vmax. */
void     util_bar(char *out, size_t len, int value, int vmin, int vmax, int width);
/** @brief  Fréquence centrale d'un canal Wi-Fi de la bande 2,4 GHz (canal 6 -> 2437 MHz). */
uint16_t util_channel_mhz(uint8_t channel);

/**
 * @brief  Texte d'un QR code de connexion Wi-Fi : WIFI:T:WPA;S:<ssid>;P:<mdp>;;
 *         (caractères \ ; , : " échappés).
 * @retval longueur écrite, 0 si le tampon est trop petit.
 */
size_t   util_wifi_qr(char *out, size_t len, const char *ssid, const char *pass);

/* ---- Construction de JSON dans un tampon fixe -----------------------------
 * Exemple : jb_init(&j, buf, sizeof buf); jb_open(&j, '{');
 *           jb_kv_int(&j, "led", 40); jb_kv_str(&j, "rgb", "#FF0000");
 *           jb_close(&j, '}');          ->  {"led":40,"rgb":"#FF0000"}
 * Les virgules sont ajoutées automatiquement ; j.overflow signale un tampon
 * trop petit (texte tronqué).                                             */
/** État d'un document JSON en cours de construction. */
typedef struct
{
  char  *buf;
  size_t cap;
  size_t pos;
  bool   overflow;     /* le texte a été tronqué */
  bool   need_comma;
} jbuf_t;

/** @brief  Commence un document JSON dans le tampon buf de cap octets. */
void   jb_init(jbuf_t *j, char *buf, size_t cap);
/** @brief  Ajoute du texte brut, mis en forme comme avec printf(). */
void   jb_raw(jbuf_t *j, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/** @brief  Ajoute ,"key": (la virgule seulement si nécessaire). */
void   jb_key(jbuf_t *j, const char *key);
/** @brief  Ajoute une chaîne entre guillemets, échappée. */
void   jb_str(jbuf_t *j, const char *s);
/** @brief  Ouvre un objet '{' ou un tableau '['. */
void   jb_open(jbuf_t *j, char c);
/** @brief  Ferme un objet '}' ou un tableau ']'. */
void   jb_close(jbuf_t *j, char c);
/** @brief  Séparateur avant un nouvel élément de tableau. */
void   jb_item(jbuf_t *j);
/** @brief  Ajoute "key":"val" (chaîne échappée). */
void   jb_kv_str(jbuf_t *j, const char *key, const char *val);
/** @brief  Ajoute "key":val (entier). */
void   jb_kv_int(jbuf_t *j, const char *key, long val);
/** @brief  Ajoute "key":true ou "key":false. */
void   jb_kv_bool(jbuf_t *j, const char *key, bool val);
/** @brief  Ajoute "key":raw, raw étant déjà écrit en JSON (nombre formaté, null...). */
void   jb_kv_raw(jbuf_t *j, const char *key, const char *raw);

#ifdef __cplusplus
}
#endif

#endif /* UTIL_H */

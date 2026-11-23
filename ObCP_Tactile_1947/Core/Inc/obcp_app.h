/**
 * @file obcp_app.h
 * @brief Écrans, commandes tactiles et requêtes de sauvegarde.
 * @ingroup obcp_apps
 *
 * App réunit les modèles en RAM. Les événements modifient ces modèles et invalident des
 * rectangles. Le dessin reçoit ensuite une bande RGB565. Les accès physiques à la flash
 * sont différés vers board.c pour laisser afficher leur état.
 *
 * @par Repères de lecture
 * Les coordonnées écran sont en pixels. Les durées suffixées ms sont en
 * millisecondes. Les adresses du service de stockage sont des offsets en octets.
 */
#ifndef OBCP_APP_H
#define OBCP_APP_H

#include "obcp_audio.h"
#include "obcp_calibration.h"
#include "obcp_games.h"
#include "obcp_input.h"
#include "obcp_paint.h"
#include "obcp_storage.h"

/**
 * @brief Pages accessibles dans la machine de navigation.
 */
typedef enum {
    SCREEN_MENU, /**< Lanceur des six applications et paramètres. */
    SCREEN_PAINT, /**< Canevas et outils de dessin. */
    SCREEN_GROOVE, /**< Grille musicale ou clavier. */
    SCREEN_MAZE, /**< Labyrinthe piloté sur deux axes. */
    SCREEN_BREAKOUT, /**< Casse-briques piloté sur X. */
    SCREEN_RACE, /**< Course à trois voies pilotée sur X. */
    SCREEN_SETTINGS, /**< Page d'accès aux quatre rubriques de réglage. */
    SCREEN_DIAG, /**< Identités, mesures, cibles, mires et note audio. */
    SCREEN_TOUCH_CAL, /**< Acquisition brute des quatre cibles résistives. */
    SCREEN_TILT, /**< Assistant d'orientation droite puis bas. */
    SCREEN_RESPONSE, /**< Réglage des réactivités X et Y. */
    SCREEN_SPLASH, /**< Accueil SUPMICROTECH temporisé au démarrage. */
} Screen;

/**
 * @brief Dialogue superposé à une application.
 */
typedef enum {
    MODAL_NONE, /**< Aucun dialogue superposé. */
    MODAL_COLORS, /**< Choix d'une des seize cases de palette. */
    MODAL_DRAW_SAVE, /**< Choix de l'emplacement de sauvegarde du dessin. */
    MODAL_DRAW_OPEN, /**< Choix du dessin à relire. */
    MODAL_DRAW_CLEAR, /**< Confirmation d'effacement du canevas. */
    MODAL_DRAW_LEAVE, /**< Confirmation de sortie avec un dessin modifié. */
    MODAL_MUSIC_SAVE, /**< Choix de l'emplacement du motif à enregistrer. */
    MODAL_MUSIC_OPEN, /**< Choix du motif à relire. */
    MODAL_BRUSH, /**< Sélection du diamètre de crayon ou de gomme. */
    MODAL_RGB, /**< Édition des trois composantes de la case de palette. */
} Modal;

/**
 * @brief Demande de stockage à exécuter après son message d’attente.
 */
typedef enum {
    JOB_NONE, /**< Aucun travail de stockage en attente. */
    JOB_DRAW_SAVE, /**< Écrire le dessin et sa palette. */
    JOB_DRAW_OPEN, /**< Relire un dessin et son format. */
    JOB_MUSIC_SAVE, /**< Écrire le livre avec le motif courant. */
    JOB_MUSIC_OPEN, /**< Rappeler un motif du livre en RAM. */
    JOB_TOUCH_CAL_SAVE, /**< Écrire les deux profils résistifs. */
    JOB_TILT_SAVE, /**< Écrire les bits d'orientation des jeux. */
    JOB_PREFERENCES_SAVE, /**< Écrire les deux réactivités X et Y. */
} StoreJob;

/**
 * @brief Valeurs présentées dans les pages de réglage.
 *
 * Les pilotes et board.c mettent ces valeurs à jour. Le rendu les lit sans accès au
 * matériel.
 */
typedef struct {
    uint16_t raw_x; /**< Dernière abscisse native du contrôleur tactile, avant transformation. */
    uint16_t raw_y; /**< Dernière ordonnée native du contrôleur tactile. */
    uint16_t z1; /**< Première mesure de pression, en unités ADC propres au pilote. */
    uint16_t z2; /**< Deuxième mesure de pression. Zéro lorsque le pilote ne la fournit pas. */
    uint16_t tft_id; /**< Identifiant lu du TFT, normalement 0x9341. */
    uint16_t touch_id; /**< Identifiant du contrôleur s'il en possède un. Zéro pour TSC2007. */
    int16_t x; /**< Dernière abscisse écran valide, en pixels. */
    int16_t y; /**< Dernière ordonnée écran valide, en pixels. */
    uint32_t frames_per_second; /**< Lots de rectangles achevés par seconde, hors compteur d'images complet. */
    uint32_t pixels_per_second; /**< Pixels acheminés vers le TFT par seconde. */
    uint32_t lcd_errors; /**< Nombre d'erreurs signalées par le moteur de transport TFT. */
    uint32_t audio_missed; /**< Nombre cumulé d'échéances ou erreurs audio. */
    uint32_t last_save_ms; /**< Durée de la dernière opération de stockage, en millisecondes. */
    TouchController touch_controller; /**< Type de tactile détecté et choix du protocole à utiliser. */
    uint8_t hits; /**< Masque des cinq cibles touchées. Tous les bits attendus donnent 0x1F. */
    bool touch_ok; /**< true quand le tactile reconnu est disponible. */
    bool flash_ok; /**< true quand le port de stockage physique a été obtenu. */
    bool accel_ok; /**< true quand un LIS3DH ou LIS3DSH a été configuré. */
    const char *accel_name; /**< Nom constant du capteur, conservé par son pilote. */
} Diagnostics;

/**
 * @brief État complet des applications et de leur navigation.
 *
 * Un seul App appartient à board.c. Les écrans partagent le tactile, le filtre
 * d’inclinaison, le moteur audio et le service de stockage. Aucune allocation dynamique
 * n’est requise.
 */
typedef struct {
    Damage dirty; /**< Rectangles modifiés en attente d'un prochain lot de rendu. */
    Paint paint; /**< Pixels indexés, palette, outils et état de modification du dessin. */
    Maze maze; /**< Positions, vitesses, temps et phase du labyrinthe. */
    Breakout breakout; /**< Balle, raquette, masques de briques et phase du casse-briques. */
    Race race; /**< Joueur, trafic, objectif et animation de collision de la course. */
    Audio audio; /**< Motif actif et états des oscillateurs audio. */
    PatternBook book; /**< Quatre motifs musicaux chargés ou sauvegardés en RAM. */
    TouchCalBook touch_cal; /**< Deux profils résistifs, un pour chaque famille prise en charge. */
    TouchCalSession touch_cal_session; /**< Progression du calibrage brut à quatre cibles. */
    Tilt tilt; /**< Neutre, filtre et commandes d'inclinaison communes aux jeux. */
    TiltGuide tilt_guide; /**< Candidat de repère proposé par l'assistant droite puis bas. */
    Diagnostics diag; /**< Mesures et compteurs montrés au diagnostic. */
    Screen screen; /**< Écran actuellement sélectionné. */
    Screen splash_target; /**< Écran à ouvrir après l'accueil, menu ou procédure de démarrage. */
    Modal modal; /**< Dialogue affiché au-dessus de l'écran courant. */
    StoreJob job; /**< Opération de stockage différée, ou JOB_NONE. */
    const char *notice; /**< Message constant affiché dans le bandeau, ou NULL. */
    uint8_t slot; /**< Emplacement choisi. Dessin de 0 à 1, motif de 0 à 3. */
    uint8_t track; /**< Piste musicale sélectionnée de 0 à 3. */
    uint8_t note; /**< Hauteur de clavier sélectionnée de 1 à 8. */
    uint8_t shown_step; /**< Dernier pas de séquenceur dessiné, pour invalider son ancienne case. */
    uint8_t test_color; /**< Mire de diagnostic de 0 à 4. Zéro désigne les cibles. */
    uint8_t rgb[3]; /**< Composantes de travail rouge, vert et bleu, chacune de 0 à 255. */
    uint16_t splash_ms; /**< Temps cumulé de l'accueil, en millisecondes de simulation. */
    bool keys; /**< true lorsque la groovebox présente le clavier au lieu des pas. */
    bool cal_can_cancel; /**< true si un ancien profil permet de viser le bouton Annuler. */
} App;

/**
 * @brief Initialiser les modèles et les valeurs de départ.
 *
 * Met les états à zéro, prépare le crayon et la palette, initialise l'audio et les axes,
 * puis demande une image complète. La lecture des objets persistants est une étape
 * séparée.
 * @param[out] a Contexte des applications en RAM, à initialiser par cet appel. Pointeur
 *   valide.
 */
void app_init(App *a);
/**
 * @brief Effectuer une transition vers un autre écran.
 *
 * Arrête les voix, ferme les dialogues et annule les gestes en cours. Prépare le jeu ou
 * l'assistant demandé, puis invalide l'écran. Les objets de dessin et les réglages restent
 * dans App.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 * @param[in] screen Écran de destination.
 */
void app_open(App *a, Screen screen);
/**
 * @brief Afficher l'accueil en conservant sa destination.
 *
 * Mémorise l'écran qui devait s'ouvrir, remet le compteur à zéro et choisit l'accueil.
 * app_step() termine l'accueil après 3,2 s. Les capteurs continuent d'être servis par
 * board.c.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 */
void app_start_splash(App *a);
/**
 * @brief Ouvrir le calibrage résistif de secours.
 *
 * Le bouton d'annulation fondé sur l'ancien profil est désactivé car ce profil peut
 * justement être erroné. Les cibles seront traitées dans le repère brut.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 */
void app_force_touch_calibration(App *a);
/**
 * @brief Distribuer un événement tactile à l'écran approprié.
 *
 * Les travaux flash et le calibrage brut ont leur propre entrée. Les dialogues reçoivent
 * ensuite les gestes, puis le canevas, le retour et les écrans. Cet ordre évite qu'un
 * appui serve deux commandes.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 * @param[in] touch Événement de contact en coordonnées écran.
 */
void app_touch(App *a, TouchEvent touch);
/**
 * @brief Transmettre une observation brute au calibrage.
 *
 * Le contact ayant ouvert l'assistant doit d'abord être relâché. Une session complète met
 * son candidat en RAM et demande sa sauvegarde. Un ancien profil permet l'annulation lors
 * d'une entrée normale.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 * @param[in] sample Observation brute tactile, à remplir ou à interpréter selon le pilote.
 */
void app_calibration_sample(App *a, RawTouch sample);
/**
 * @brief Traiter le raccourci facultatif de SW1.
 *
 * Cette entrée reçoit déjà un événement débouncé. Elle change la mire, ouvre un diagnostic
 * ou revient au menu selon l'écran. Les commandes tactiles couvrent l'usage avec le shield
 * monté.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 * @param[in] event Événement validé du bouton utilisateur.
 */
void app_button(App *a, ButtonEvent event);
/**
 * @brief Avancer les états temporels de vingt millisecondes.
 *
 * Le moteur du jeu actif reçoit les commandes filtrées. Les variations de score ou de
 * temps invalident le bandeau. L'accueil et le curseur de groovebox utilisent également ce
 * passage coopératif.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 */
void app_step(App *a); /* pas de simulation de 20 ms */
/**
 * @brief Recomposer une bande RGB565 à partir de l'état App.
 *
 * Choisit l'écran, ajoute le dialogue puis le message. Cette fonction est appelée
 * synchroniquement par renderer_task() avant le départ du DMA. Aucun tampon d'écran
 * complet n'est requis.
 * @param[in] context Pointeur App valide présenté sous une forme opaque au moteur graphique.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 */
void app_render(const void *context, Gfx *g);
/**
 * @brief Indiquer si le transport PWM doit fonctionner.
 *
 * Un travail flash suspend la production. L'audio est demandé lorsque la groovebox est
 * ouverte ou lorsque la note de diagnostic est active.
 * @param[in] a Contexte des applications en RAM, consulté sans modification. Pointeur
 *   valide.
 * @return true si board.c doit servir l'audio à cet instant.
 */
bool app_wants_audio(const App *a);
/**
 * @brief Charger le livre des quatre motifs au démarrage.
 *
 * Le CRC est contrôlé par le service de stockage, puis le décodeur contrôle la version et
 * les valeurs musicales avant d'accepter le livre.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 * @param[in,out] storage Port de stockage et ses callbacks synchrones. Pointeur valide.
 */
void app_read_book(App *a, Storage *storage);
/**
 * @brief Charger les profils des deux tactiles résistifs.
 *
 * Le profil est associé à l'orientation MADCTL. Un objet absent ou invalide laisse un
 * livre vide, ce qui déclenchera le calibrage lors de la reconnaissance du shield.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 * @param[in,out] storage Port de stockage et ses callbacks synchrones. Pointeur valide.
 */
void app_read_calibration(App *a, Storage *storage);
/**
 * @brief Charger le repère des jeux depuis la flash.
 *
 * Le petit format contient version, orientation du TFT et bits d'axes. Le masque est
 * vérifié pour garder les valeurs de départ si le contenu ne correspond pas.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 * @param[in,out] storage Port de stockage et ses callbacks synchrones. Pointeur valide.
 */
void app_read_axes(App *a, Storage *storage);
/**
 * @brief Charger les deux valeurs de réactivité.
 *
 * La version et les bornes 1 à 10 sont contrôlées avant de remplacer les valeurs de
 * départ. Cette opération ne modifie pas le neutre du capteur.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 * @param[in,out] storage Port de stockage et ses callbacks synchrones. Pointeur valide.
 */
void app_read_preferences(App *a, Storage *storage);
/**
 * @brief Exécuter la requête de stockage en attente.
 *
 * Les formats propres au dessin, au motif, au tactile et aux axes sont traités séparément.
 * Le résultat prépare le message de fin et sera utilisé par board.c pour l'impulsion de
 * LED.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 * @param[in,out] storage Port de stockage et ses callbacks synchrones. Pointeur valide.
 * @return Résultat STORE_OK, STORE_EMPTY, STORE_IO ou STORE_INVALID. STORE_EMPTY si aucun travail n'est demandé.
 */
StoreResult app_store_task(App *a, Storage *storage);
/**
 * @brief Invalider les valeurs de diagnostic qui évoluent.
 *
 * Les zones des mesures, de l'aperçu d'axes ou de la réponse sont ajoutées aux rectangles
 * en attente. Les éléments fixes restent hors de cette invalidation périodique.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 */
void app_diag_refresh(App *a);

#endif

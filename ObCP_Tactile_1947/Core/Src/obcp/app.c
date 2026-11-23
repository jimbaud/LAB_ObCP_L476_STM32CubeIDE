/**
 * @file app.c
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
/** @def COLOR_BG
 * @brief Couleur RGB565 du fond principal sombre.
 */
/** @def COLOR_PANEL
 * @brief Couleur RGB565 des panneaux et des cartes de menu.
 */
/** @def COLOR_PANEL_HI
 * @brief Couleur RGB565 de cadre et de bouton secondaire.
 */
/** @def COLOR_TEXT
 * @brief Couleur RGB565 du texte principal clair.
 */
/** @def COLOR_DIM
 * @brief Couleur RGB565 des indications secondaires.
 */
/** @def COLOR_MINT
 * @brief Accent RGB565 de validation et de commande active.
 */
/** @def COLOR_BLUE
 * @brief Accent RGB565 des informations et de certaines applications.
 */
/** @def COLOR_GOLD
 * @brief Accent RGB565 des avertissements et du pas musical courant.
 */
/** @def COLOR_PINK
 * @brief Accent RGB565 des commandes de confirmation ou du dessin.
 */
/** @var top
 * @brief Bandeau supérieur commun de 34 pixels de haut.
 */
/** @var home
 * @brief Zone de retour du bandeau, partagée entre dessin et hit-test.
 */
/** @var pause_btn
 * @brief Zone Pause ou Reprise du bandeau des jeux.
 */
/** @var play_btn
 * @brief Zone Jouer, Rejouer ou Niveau suivant du dialogue de jeu.
 */
/** @var game_menu
 * @brief Grande zone de retour au menu dans un bilan ou une pause.
 */
/** @var notice_box
 * @brief Emprise du message de stockage ou d'information temporaire.
 */
/** @var overlay
 * @brief Emprise commune des dialogues de confirmation et d'emplacement.
 */
/** @var cancel
 * @brief Bouton d'annulation des petits dialogues.
 */
/** @var paint_canvas
 * @brief Canevas de 320 × 200 pixels, séparé du bandeau d'outils.
 */
/** @var diag_color
 * @brief Bouton de cycle des aplats de diagnostic.
 */
/** @var diag_neutral
 * @brief Bouton de nouvelle mesure du neutre des jeux.
 */
/** @var diag_tone
 * @brief Bouton de la note d'essai à 440 Hz.
 */
/** @var diag_volume
 * @brief Bouton d'incrément du volume audio.
 */
/** @var diag_axes
 * @brief Raccourci vers l'assistant d'orientation dans le diagnostic.
 */
/** @var diag_cal
 * @brief Raccourci vers le calibrage résistif dans le diagnostic.
 */
/** @var cal_cancel
 * @brief Bouton d'annulation utilisant l'ancien profil tactile.
 */
/** @var tilt_pad
 * @brief Zone de visualisation de la commande X/Y dans l'assistant.
 */
/** @var tilt_action
 * @brief Zone Mesurer ou Enregistrer du guide d'axes.
 */
/** @var tilt_retry
 * @brief Zone Refaire du guide d'axes.
 */
/** @var edit_overlay
 * @brief Grande emprise des dialogues RVB, palette et diamètre.
 */
/** @var edit_apply
 * @brief Action de gauche des éditeurs et du réglage de réactivité.
 */
/** @var edit_cancel
 * @brief Action de droite des éditeurs, variable selon le dialogue.
 */
/** @var titles
 * @brief Titres d'écran dans le même ordre que l'énumération Screen.
 */
/** @var subtitles
 * @brief Indications secondaires des six cartes du menu.
 */
/** @var accents
 * @brief Couleurs des indicateurs des six cartes du menu.
 */
/** @var track_label
 * @brief Noms des quatre timbres proposés aux pistes.
 */
/** @var notes
 * @brief Noms des huit hauteurs du clavier et des pas.
 */
/** @var target_xy
 * @brief Centres des cinq cibles de vérification en pixels paysage.
 */

#include "obcp_app.h"
#include "obcp_config.h"
#include "obcp_logo.h"

#include <stdio.h>
#include <string.h>

#define COLOR_BG RGB565(13,22,37)
#define COLOR_PANEL RGB565(30,48,69)
#define COLOR_PANEL_HI RGB565(47,67,92)
#define COLOR_TEXT RGB565(239,246,251)
#define COLOR_DIM RGB565(156,179,200)
#define COLOR_MINT RGB565(73,225,186)
#define COLOR_BLUE RGB565(91,160,251)
#define COLOR_GOLD RGB565(252,202,82)
#define COLOR_PINK RGB565(248,112,146)

static const Rect top = {0, 0, 320, 34};
static const Rect home = {5, 4, 53, 25};
static const Rect pause_btn = {264, 4, 51, 25};
static const Rect play_btn = {77, 112, 166, 35};
static const Rect game_menu = {77, 153, 166, 35};
static const Rect notice_box = {22, 40, 276, 27};
static const Rect overlay = {29, 35, 262, 172};
static const Rect cancel = {103, 176, 114, 24};
static const Rect paint_canvas = {0, 0, LCD_WIDTH, PAINT_HEIGHT};
static const Rect diag_color = {65, 4, 57, 25};
static const Rect diag_neutral = {130, 4, 74, 25};
static const Rect diag_tone = {211, 4, 51, 25};
static const Rect diag_volume = {269, 4, 46, 25};
static const Rect diag_axes = {41, 192, 113, 34};
static const Rect diag_cal = {162, 192, 117, 34};
static const Rect cal_cancel = {238, 4, 77, 25};
static const Rect tilt_pad = {14, 109, 146, 109};
static const Rect tilt_action = {179, 147, 127, 40};
static const Rect tilt_retry = {179, 194, 127, 32};
static const Rect edit_overlay = {15, 35, 290, 196};
static const Rect edit_apply = {32, 195, 120, 28};
static const Rect edit_cancel = {168, 195, 120, 28};
/** @brief Constantes privées de ce module, avec leurs unités. */
enum {
    AXES_VERSION = 1, /**< Version du petit format persistant des axes. */
    AXES_BYTES = 3, /**< Trois octets, version, orientation TFT et masque des axes. */
};

static const char *const titles[] = {
    "ATELIER TACTILE", "PAINT", "GROOVEBOX", "LABYRINTHE", "CASSE-BRIQUES", "COURSE AUTO",
    "PARAMETRES", "DIAGNOSTIC", "CALIBRAGE", "AXES DES JEUX", "REACTIVITE", "ACCUEIL"
};
static const char *const subtitles[] = {
    "DESSIN ET FLASH", "4 PISTES / 16 PAS", "INCLINAISON X / Y",
    "RAQUETTE", "DIRECTION", "REGLAGES / TESTS"
};
static const uint16_t accents[] = {COLOR_PINK, COLOR_GOLD, COLOR_MINT, COLOR_BLUE, COLOR_PINK, COLOR_MINT};
static const char *const track_label[] = {"KICK", "SNARE", "HAT", "BASS"};
static const char *const notes[] = {"DO", "RE", "MI", "FA", "SOL", "LA", "SI", "DO+"};
static const int16_t target_xy[5][2] = {{16,50},{303,50},{16,221},{303,221},{160,132}};

/**
 * @brief Calculer le rectangle de une des six applications.
 *
 * L’indice i va de 0 à 5. La même fonction est appelée pour dessiner et pour reconnaître
 * un appui. Les coordonnées sont exprimées en pixels paysage depuis le coin supérieur
 * gauche.
 * @param[in] i Indice de l’élément dans la table de géométrie décrite ci-dessus.
 * @return Rectangle avec origine, largeur et hauteur, sans allocation de mémoire.
 */
static Rect menu_card(unsigned i)
{
    return (Rect){(int16_t)(9 + i % 2U * 155U), (int16_t)(9 + i / 2U * 70U), 147, 64};
}

/**
 * @brief Calculer le rectangle de un des sept outils de Paint.
 *
 * L’indice i va de 0 à 6. La même fonction est appelée pour dessiner et pour reconnaître
 * un appui. Les coordonnées sont exprimées en pixels paysage depuis le coin supérieur
 * gauche.
 * @param[in] i Indice de l’élément dans la table de géométrie décrite ci-dessus.
 * @return Rectangle avec origine, largeur et hauteur, sans allocation de mémoire.
 */
static Rect paint_button(unsigned i)
{
    return (Rect){(int16_t)(3 + i * 45U), 204, 42, 33};
}

/**
 * @brief Calculer le rectangle de une piste musicale.
 *
 * L’indice i va de 0 à 3. La même fonction est appelée pour dessiner et pour reconnaître
 * un appui. Les coordonnées sont exprimées en pixels paysage depuis le coin supérieur
 * gauche.
 * @param[in] i Indice de l’élément dans la table de géométrie décrite ci-dessus.
 * @return Rectangle avec origine, largeur et hauteur, sans allocation de mémoire.
 */
static Rect track_button(unsigned i)
{
    return (Rect){(int16_t)(5 + i * 78U), 37, 74, 27};
}

/**
 * @brief Calculer le rectangle de un des seize pas du séquenceur.
 *
 * L’indice i va de 0 à 15. La même fonction est appelée pour dessiner et pour reconnaître
 * un appui. Les coordonnées sont exprimées en pixels paysage depuis le coin supérieur
 * gauche.
 * @param[in] i Indice de l’élément dans la table de géométrie décrite ci-dessus.
 * @return Rectangle avec origine, largeur et hauteur, sans allocation de mémoire.
 */
static Rect step_button(unsigned i)
{
    return (Rect){(int16_t)(6 + i % 4U * 78U), (int16_t)(70 + i / 4U * 31U), 72, 27};
}

/**
 * @brief Calculer le rectangle de une des huit touches du clavier.
 *
 * L’indice i va de 0 à 7. La même fonction est appelée pour dessiner et pour reconnaître
 * un appui. Les coordonnées sont exprimées en pixels paysage depuis le coin supérieur
 * gauche.
 * @param[in] i Indice de l’élément dans la table de géométrie décrite ci-dessus.
 * @return Rectangle avec origine, largeur et hauteur, sans allocation de mémoire.
 */
static Rect key_button(unsigned i)
{
    return (Rect){(int16_t)(6 + i % 4U * 78U), (int16_t)(74 + i / 4U * 57U), 72, 47};
}

/**
 * @brief Calculer le rectangle de une commande de transport musical.
 *
 * L’indice i va de 0 à 6. La même fonction est appelée pour dessiner et pour reconnaître
 * un appui. Les coordonnées sont exprimées en pixels paysage depuis le coin supérieur
 * gauche.
 * @param[in] i Indice de l’élément dans la table de géométrie décrite ci-dessus.
 * @return Rectangle avec origine, largeur et hauteur, sans allocation de mémoire.
 */
static Rect groove_button(unsigned i)
{
    return (Rect){(int16_t)(4 + i * 45U), 201, 42, 34};
}

/**
 * @brief Calculer le rectangle de un emplacement de dessin ou de motif.
 *
 * L’indice i va de 0 à 3. La même fonction est appelée pour dessiner et pour reconnaître
 * un appui. Les coordonnées sont exprimées en pixels paysage depuis le coin supérieur
 * gauche.
 * @param[in] i Indice de l’élément dans la table de géométrie décrite ci-dessus.
 * @return Rectangle avec origine, largeur et hauteur, sans allocation de mémoire.
 */
static Rect slot_button(unsigned i)
{
    return (Rect){(int16_t)(48 + i % 2U * 114U), (int16_t)(87 + i / 2U * 42U), 108, 36};
}

/**
 * @brief Calculer le rectangle de une case de la palette.
 *
 * L’indice i va de 0 à 15. La même fonction est appelée pour dessiner et pour reconnaître
 * un appui. Les coordonnées sont exprimées en pixels paysage depuis le coin supérieur
 * gauche.
 * @param[in] i Indice de l’élément dans la table de géométrie décrite ci-dessus.
 * @return Rectangle avec origine, largeur et hauteur, sans allocation de mémoire.
 */
static Rect color_button(unsigned i)
{
    return (Rect){(int16_t)(34 + i % 4U * 65U), (int16_t)(73 + i / 4U * 27U), 55, 23};
}

/**
 * @brief Calculer le rectangle de un diamètre de crayon.
 *
 * L’indice i va de 0 à 5. La même fonction est appelée pour dessiner et pour reconnaître
 * un appui. Les coordonnées sont exprimées en pixels paysage depuis le coin supérieur
 * gauche.
 * @param[in] i Indice de l’élément dans la table de géométrie décrite ci-dessus.
 * @return Rectangle avec origine, largeur et hauteur, sans allocation de mémoire.
 */
static Rect brush_button(unsigned i)
{
    return (Rect){(int16_t)(33 + i % 3U * 86U), (int16_t)(76 + i / 3U * 53U), 80, 43};
}

/**
 * @brief Calculer le rectangle de une rubrique des paramètres.
 *
 * L’indice i va de 0 à 3. La même fonction est appelée pour dessiner et pour reconnaître
 * un appui. Les coordonnées sont exprimées en pixels paysage depuis le coin supérieur
 * gauche.
 * @param[in] i Indice de l’élément dans la table de géométrie décrite ci-dessus.
 * @return Rectangle avec origine, largeur et hauteur, sans allocation de mémoire.
 */
static Rect settings_button(unsigned i)
{
    return (Rect){(int16_t)(14 + i % 2U * 150U), (int16_t)(65 + i / 2U * 68U), 142, 58};
}

/**
 * @brief Calculer le rectangle de une barre de composante RVB.
 *
 * L’indice i va de 0 à 2. La même fonction est appelée pour dessiner et pour reconnaître
 * un appui. Les coordonnées sont exprimées en pixels paysage depuis le coin supérieur
 * gauche.
 * @param[in] i Indice de l’élément dans la table de géométrie décrite ci-dessus.
 * @return Rectangle avec origine, largeur et hauteur, sans allocation de mémoire.
 */
static Rect rgb_slider(unsigned i) { return (Rect){90, (int16_t)(60 + i * 43U), 130, 28}; }
/**
 * @brief Calculer le rectangle de le bouton de réduction RVB.
 *
 * L’indice i va de 0 à 2. La même fonction est appelée pour dessiner et pour reconnaître
 * un appui. Les coordonnées sont exprimées en pixels paysage depuis le coin supérieur
 * gauche.
 * @param[in] i Indice de l’élément dans la table de géométrie décrite ci-dessus.
 * @return Rectangle avec origine, largeur et hauteur, sans allocation de mémoire.
 */
static Rect rgb_minus(unsigned i) { return (Rect){51, (int16_t)(60 + i * 43U), 28, 28}; }
/**
 * @brief Calculer le rectangle de le bouton d’augmentation RVB.
 *
 * L’indice i va de 0 à 2. La même fonction est appelée pour dessiner et pour reconnaître
 * un appui. Les coordonnées sont exprimées en pixels paysage depuis le coin supérieur
 * gauche.
 * @param[in] i Indice de l’élément dans la table de géométrie décrite ci-dessus.
 * @return Rectangle avec origine, largeur et hauteur, sans allocation de mémoire.
 */
static Rect rgb_plus(unsigned i) { return (Rect){228, (int16_t)(60 + i * 43U), 28, 28}; }
/**
 * @brief Calculer le rectangle de le bouton de réduction de réactivité.
 *
 * L’indice i va de 0 à 1. La même fonction est appelée pour dessiner et pour reconnaître
 * un appui. Les coordonnées sont exprimées en pixels paysage depuis le coin supérieur
 * gauche.
 * @param[in] i Indice de l’élément dans la table de géométrie décrite ci-dessus.
 * @return Rectangle avec origine, largeur et hauteur, sans allocation de mémoire.
 */
static Rect response_minus(unsigned i) { return (Rect){24, (int16_t)(71 + i * 57U), 40, 34}; }
/**
 * @brief Calculer le rectangle de le bouton d’augmentation de réactivité.
 *
 * L’indice i va de 0 à 1. La même fonction est appelée pour dessiner et pour reconnaître
 * un appui. Les coordonnées sont exprimées en pixels paysage depuis le coin supérieur
 * gauche.
 * @param[in] i Indice de l’élément dans la table de géométrie décrite ci-dessus.
 * @return Rectangle avec origine, largeur et hauteur, sans allocation de mémoire.
 */
static Rect response_plus(unsigned i) { return (Rect){256, (int16_t)(71 + i * 57U), 40, 34}; }
/**
 * @brief Calculer le rectangle de une barre de réactivité.
 *
 * L’indice i va de 0 à 1. La même fonction est appelée pour dessiner et pour reconnaître
 * un appui. Les coordonnées sont exprimées en pixels paysage depuis le coin supérieur
 * gauche.
 * @param[in] i Indice de l’élément dans la table de géométrie décrite ci-dessus.
 * @return Rectangle avec origine, largeur et hauteur, sans allocation de mémoire.
 */
static Rect response_slider(unsigned i) { return (Rect){78, (int16_t)(71 + i * 57U), 164, 34}; }

/**
 * @brief Centrer une chaîne dans la largeur du TFT.
 *
 * La largeur est calculée avec une avance de six pixels par caractère et le facteur
 * d'agrandissement. La primitive texte limite ensuite les écritures à la bande active.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 * @param[in] y Ordonnée dans le repère écran, en pixels.
 * @param[in] text Chaîne valide terminée par zéro, conservée pendant l’appel.
 * @param[in] scale Facteur entier positif d’agrandissement des glyphes.
 * @param[in] color Couleur encodée au format RGB565.
 */
static void centered_text(Gfx *g, int y, const char *text, unsigned scale, uint16_t color)
{
    int width = ((int)strlen(text) * 6 - 1) * (int)scale;
    gfx_text(g, (LCD_WIDTH - width) / 2, y, text, scale, color);
}

/**
 * @brief Dessiner un bouton rectangulaire et son libellé.
 *
 * Le rectangle, le fond, le cadre et le texte sont recomposés à chaque rendu. Cette
 * fonction dessine seulement. Les tests tactiles utilisent le même rectangle ailleurs.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 * @param[in] r Rectangle écran, avec bord droit et bas exclus.
 * @param[in] label Libellé valide terminé par zéro.
 * @param[in] fill Couleur RGB565 du fond du bouton.
 * @param[in] ink Couleur RGB565 du libellé.
 */
static void button(Gfx *g, Rect r, const char *label, uint16_t fill, uint16_t ink)
{
    gfx_fill(g, r, fill);
    gfx_outline(g, r, COLOR_PANEL_HI);
    int width = (int)strlen(label) * 6 - 1;
    gfx_text(g, r.x + (r.w - width) / 2, r.y + (r.h - 7) / 2, label, 1, ink);
}

/**
 * @brief Ouvrir ou fermer une boîte de dialogue.
 *
 * Le dialogue est enregistré dans App. Toute l'image est invalidée car le canevas doit
 * aussi réapparaître sous le dialogue précédent.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 * @param[in] modal Dialogue à sélectionner, MODAL_NONE pour fermer.
 */
static void show_modal(App *a, Modal modal)
{
    a->modal = modal;
    damage_full(&a->dirty);
}

/**
 * @brief Préparer un message temporaire pour l'utilisateur.
 *
 * Le pointeur est mémorisé sans copie. Il doit désigner une chaîne encore valide lors des
 * rendus suivants. NULL efface le message. Le bandeau concerné est invalidé.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 * @param[in] notice Chaîne de durée de vie suffisante, ou NULL pour effacer le message.
 */
static void set_notice(App *a, const char *notice)
{
    a->notice = notice;
    damage_add(&a->dirty, notice_box);
}

/*
 * Initialiser les modèles et les valeurs de départ.
 *
 * Met les états à zéro, prépare le crayon et la palette, initialise l'audio et les axes,
 * puis demande une image complète. La lecture des objets persistants est une étape
 * séparée.
 */
void app_init(App *a)
{
    memset(a, 0, sizeof(*a));
    a->screen = SCREEN_MENU;
    paint_init(&a->paint);
    a->note = 1;
    a->diag.accel_name = "ABSENT";
    audio_init(&a->audio);
    tilt_init(&a->tilt);
    damage_full(&a->dirty);
}

/*
 * Afficher l'accueil en conservant sa destination.
 *
 * Mémorise l'écran qui devait s'ouvrir, remet le compteur à zéro et choisit l'accueil.
 * app_step() termine l'accueil après 3,2 s. Les capteurs continuent d'être servis par
 * board.c.
 */
void app_start_splash(App *a)
{
    a->splash_target = a->screen;
    a->splash_ms = 0;
    app_open(a, SCREEN_SPLASH);
}

/**
 * @brief Charger un niveau dans le moteur du jeu actif.
 *
 * Le moteur propre au labyrinthe, au casse-briques ou à la course reçoit l'indice. Son
 * initialisation remet les positions, le score local et la phase prête.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 * @param[in] level Indice de niveau à base zéro. Une valeur hors plage choisit le premier
 *   niveau.
 */
static void game_load_level(App *a, unsigned level)
{
    switch (a->screen) {
    case SCREEN_MAZE: maze_init(&a->maze, level); break;
    case SCREEN_BREAKOUT: breakout_init(&a->breakout, level); break;
    case SCREEN_RACE: race_init(&a->race, level); break;
    default: break;
    }
}

/*
 * Effectuer une transition vers un autre écran.
 *
 * Arrête les voix, ferme les dialogues et annule les gestes en cours. Prépare le jeu ou
 * l'assistant demandé, puis invalide l'écran. Les objets de dessin et les réglages restent
 * dans App.
 */
void app_open(App *a, Screen screen)
{
    audio_stop(&a->audio);
    a->screen = screen;
    a->modal = MODAL_NONE;
    a->notice = NULL;
    a->test_color = 0;
    a->paint.stroke = false;
    a->keys = false;
    game_load_level(a, 0);
    if (screen == SCREEN_TOUCH_CAL) {
        touch_cal_start(&a->touch_cal_session);
        a->cal_can_cancel = touch_cal_get(&a->touch_cal, a->diag.touch_controller) != NULL;
        a->diag.hits = 0;
    } else if (screen == SCREEN_TILT) {
        a->tilt_guide = (TiltGuide){0};
    }
    damage_full(&a->dirty);
}

/*
 * Ouvrir le calibrage résistif de secours.
 *
 * Le bouton d'annulation fondé sur l'ancien profil est désactivé car ce profil peut
 * justement être erroné. Les cibles seront traitées dans le repère brut.
 */
void app_force_touch_calibration(App *a)
{
    app_open(a, SCREEN_TOUCH_CAL);
    /* Le profil précédent peut placer une cible dans la zone d'annulation. */
    a->cal_can_cancel = false;
}

/**
 * @brief Trouver la phase du jeu actuellement ouvert.
 *
 * Le pointeur permet au code de navigation de traiter les phases de trois structures
 * différentes avec les mêmes règles.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 * @return Adresse de la phase du jeu, ou NULL si l'écran actif n'est pas un jeu.
 */
static GamePhase *game_phase(App *a)
{
    switch (a->screen) {
    case SCREEN_MAZE: return &a->maze.phase;
    case SCREEN_BREAKOUT: return &a->breakout.phase;
    case SCREEN_RACE: return &a->race.phase;
    default: return NULL;
    }
}

/**
 * @brief Lire l'indice du niveau du jeu actif.
 *
 * Les modèles stockent un indice de 0 à 9. L'affichage ajoute un pour présenter les
 * niveaux 1 à 10.
 * @param[in] a Contexte des applications en RAM, consulté sans modification. Pointeur
 *   valide.
 * @return Indice courant, ou zéro hors d'un écran de jeu.
 */
static unsigned game_level(const App *a)
{
    switch (a->screen) {
    case SCREEN_MAZE: return a->maze.level;
    case SCREEN_BREAKOUT: return a->breakout.level;
    case SCREEN_RACE: return a->race.level;
    default: return 0;
    }
}

/**
 * @brief Traiter Jouer, Pause, Rejouer et Menu principal.
 *
 * Les sorties sont examinées avant la disponibilité de l'accéléromètre. Une défaite
 * recharge le même niveau, une victoire avance au suivant. L'éclat de course laisse
 * d'abord sa place à l'animation.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 * @param[in] x Abscisse dans le repère écran, en pixels.
 * @param[in] y Ordonnée dans le repère écran, en pixels.
 */
static void game_touch(App *a, int x, int y)
{
    GamePhase *phase = game_phase(a);
    if (phase == NULL) {
        return;
    }
    if (a->screen == SCREEN_RACE && a->race.crash_ms > 0U) {
        return; /* Le bouton Menu du bandeau reste traité par app_touch(). */
    }
    if (*phase != GAME_RUN && rect_contains(game_menu, x, y)) {
        app_open(a, SCREEN_MENU);
        return;
    }
    if ((*phase == GAME_RUN || *phase == GAME_PAUSE) && rect_contains(pause_btn, x, y)) {
        if (*phase == GAME_RUN) {
            *phase = GAME_PAUSE;
        } else if (*phase == GAME_PAUSE) {
            *phase = GAME_RUN;
        }
        damage_full(&a->dirty);
    } else if (rect_contains(play_btn, x, y) && *phase != GAME_RUN) {
        unsigned level = game_level(a);
        if (*phase == GAME_WON) {
            game_load_level(a, level + 1U < GAME_LEVELS ? level + 1U : 0U);
        } else if (*phase == GAME_LOST) {
            game_load_level(a, level);
        }
        if (a->tilt.calibrated && a->tilt.valid) {
            *phase = GAME_RUN;
        }
        damage_full(&a->dirty);
    }
}

/**
 * @brief Différer une lecture ou une écriture d'objet.
 *
 * Mémorise le travail et l'emplacement, coupe les voix et affiche le message d'attente.
 * board.c attend que ce message soit envoyé avant d'exécuter la transaction flash.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 * @param[in] job Travail de stockage à différer.
 * @param[in] slot Emplacement de dessin ou de motif, indexé depuis zéro.
 */
static void request_store(App *a, StoreJob job, unsigned slot)
{
    a->job = job;
    a->slot = (uint8_t)slot;
    audio_stop(&a->audio);
    show_modal(a, MODAL_NONE);
    bool write = job == JOB_DRAW_SAVE || job == JOB_MUSIC_SAVE;
    set_notice(a, write ? "ENREGISTREMENT EN COURS" : "LECTURE EN COURS");
}

/**
 * @brief Traiter un appui dans une boîte de dialogue.
 *
 * Distingue palette, composantes RVB, diamètre, confirmations et emplacements.
 * L'application du RVB modifie une case de palette et invalide tous les pixels qui peuvent
 * l'utiliser.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 * @param[in] x Abscisse dans le repère écran, en pixels.
 * @param[in] y Ordonnée dans le repère écran, en pixels.
 */
static void modal_touch(App *a, int x, int y)
{
    if (a->modal == MODAL_RGB) {
        if (rect_contains(edit_apply, x, y)) {
            paint_set_color(&a->paint, RGB565(a->rgb[0], a->rgb[1], a->rgb[2]));
            show_modal(a, MODAL_NONE);
        } else if (rect_contains(edit_cancel, x, y)) {
            show_modal(a, MODAL_COLORS);
        }
        for (unsigned i = 0; i < 3U; ++i) {
            if (rect_contains(rgb_minus(i), x, y) && a->rgb[i] > 0U) {
                --a->rgb[i];
            } else if (rect_contains(rgb_plus(i), x, y) && a->rgb[i] < 255U) {
                ++a->rgb[i];
            }
        }
        damage_add(&a->dirty, edit_overlay);
        return;
    }
    if (a->modal == MODAL_BRUSH) {
        for (unsigned i = 0; i < PAINT_BRUSHES; ++i) {
            if (rect_contains(brush_button(i), x, y)) {
                a->paint.brush = (uint8_t)i;
                show_modal(a, MODAL_NONE);
                return;
            }
        }
        if (rect_contains(edit_cancel, x, y)) {
            show_modal(a, MODAL_NONE);
        }
        return;
    }
    if (a->modal == MODAL_COLORS && rect_contains(edit_apply, x, y)) {
        uint16_t color = a->paint.palette[a->paint.ink];
        a->rgb[0] = (uint8_t)(((color >> 11) * 255U + 15U) / 31U);
        a->rgb[1] = (uint8_t)((((color >> 5) & 63U) * 255U + 31U) / 63U);
        a->rgb[2] = (uint8_t)(((color & 31U) * 255U + 15U) / 31U);
        show_modal(a, MODAL_RGB);
        return;
    }
    if ((a->modal == MODAL_COLORS || a->modal == MODAL_BRUSH) && rect_contains(edit_cancel, x, y)) {
        show_modal(a, MODAL_NONE);
        return;
    }
    if (a->modal != MODAL_COLORS && rect_contains(cancel, x, y)) {
        show_modal(a, MODAL_NONE);
        return;
    }
    if (a->modal == MODAL_COLORS) {
        for (unsigned i = 0; i < 16; ++i) {
            if (rect_contains(color_button(i), x, y)) {
                a->paint.ink = (uint8_t)i;
                a->paint.erase = false;
                damage_add(&a->dirty, edit_overlay);
                break;
            }
        }
    } else if (a->modal == MODAL_DRAW_CLEAR || a->modal == MODAL_DRAW_LEAVE) {
        if (rect_contains(slot_button(0), x, y)) {
            if (a->modal == MODAL_DRAW_CLEAR) {
                paint_clear(&a->paint);
                show_modal(a, MODAL_NONE);
                damage_add(&a->dirty, paint_canvas);
            } else {
                app_open(a, SCREEN_MENU);
            }
        }
    } else {
        bool music = a->modal == MODAL_MUSIC_SAVE || a->modal == MODAL_MUSIC_OPEN;
        for (unsigned i = 0; i < (music ? SEQ_SLOTS : 2U); ++i) {
            if (rect_contains(slot_button(i), x, y)) {
                StoreJob job = JOB_DRAW_OPEN;
                if (music) {
                    job = a->modal == MODAL_MUSIC_SAVE ? JOB_MUSIC_SAVE : JOB_MUSIC_OPEN;
                } else if (a->modal == MODAL_DRAW_SAVE) {
                    job = JOB_DRAW_SAVE;
                }
                request_store(a, job, i);
                break;
            }
        }
    }
}

/**
 * @brief Convertir une abscisse de barre en composante RVB.
 *
 * Les deux extrémités de la barre représentent zéro et 255. Le déplacement change
 * seulement la couleur de travail tant que l'utilisateur n'a pas choisi Appliquer.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 * @param[in] x Abscisse dans le repère écran, en pixels.
 * @param[in] y Ordonnée dans le repère écran, en pixels.
 */
static void rgb_drag(App *a, int x, int y)
{
    for (unsigned i = 0; i < 3U; ++i) {
        Rect slider = rgb_slider(i);
        if (rect_contains(slider, x, y)) {
            a->rgb[i] = (uint8_t)((x - slider.x) * 255 / (slider.w - 1));
            damage_add(&a->dirty, edit_overlay);
        }
    }
}

/**
 * @brief Transformer un geste en trait ou commande de dessin.
 *
 * Un appui sur le canevas amorce le trait. Les mouvements relient l'ancienne et la
 * nouvelle mesure. Le relâchement ou la sortie du canevas termine le trait pour éviter une
 * liaison au prochain contact.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 * @param[in] e Événement de contact en coordonnées écran.
 */
static void paint_touch(App *a, TouchEvent e)
{
    if (e.kind == TOUCH_UP || !rect_contains(paint_canvas, e.x, e.y)) {
        a->paint.stroke = false;
    }
    if (e.kind == TOUCH_DOWN) {
        if (rect_contains(paint_canvas, e.x, e.y)) {
            a->paint.last_x = e.x;
            a->paint.last_y = e.y;
            a->paint.stroke = true;
        } else if (rect_contains(paint_button(0), e.x, e.y)) {
            if (a->paint.modified) {
                show_modal(a, MODAL_DRAW_LEAVE);
            } else {
                app_open(a, SCREEN_MENU);
            }
        } else if (rect_contains(paint_button(1), e.x, e.y)) {
            show_modal(a, MODAL_COLORS);
        } else if (rect_contains(paint_button(2), e.x, e.y)) {
            show_modal(a, MODAL_BRUSH);
        } else if (rect_contains(paint_button(3), e.x, e.y)) {
            a->paint.erase = !a->paint.erase;
            damage_add(&a->dirty, paint_button(3));
        } else if (rect_contains(paint_button(4), e.x, e.y)) {
            show_modal(a, MODAL_DRAW_SAVE);
        } else if (rect_contains(paint_button(5), e.x, e.y)) {
            show_modal(a, MODAL_DRAW_OPEN);
        } else if (rect_contains(paint_button(6), e.x, e.y)) {
            show_modal(a, MODAL_DRAW_CLEAR);
        }
    }
    if (a->paint.stroke && (e.kind == TOUCH_DOWN || e.kind == TOUCH_MOVE)) {
        Rect changed = paint_segment(&a->paint, a->paint.last_x, a->paint.last_y, e.x, e.y);
        damage_add(&a->dirty, changed);
        a->paint.last_x = e.x;
        a->paint.last_y = e.y;
    }
}

/**
 * @brief Commander les pistes, les pas et le clavier.
 *
 * Seuls les appuis déclenchent les actions. Le relâchement termine la note manuelle. Le
 * clavier choisit aussi la hauteur affectée aux prochains pas.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 * @param[in] e Événement de contact en coordonnées écran.
 */
static void groove_touch(App *a, TouchEvent e)
{
    if (e.kind == TOUCH_UP) {
        audio_key_release(&a->audio);
        return;
    }
    if (e.kind != TOUCH_DOWN) {
        return;
    }
    for (unsigned i = 0; i < SEQ_TRACKS; ++i) {
        if (rect_contains(track_button(i), e.x, e.y)) {
            a->track = (uint8_t)i;
            damage_add(&a->dirty, (Rect){0, 34, 320, 163});
            return;
        }
    }
    if (a->keys) {
        for (unsigned i = 0; i < 8; ++i) {
            if (rect_contains(key_button(i), e.x, e.y)) {
                damage_add(&a->dirty, key_button(a->note - 1U));
                a->note = (uint8_t)(i + 1U);
                audio_key(&a->audio, a->note);
                damage_add(&a->dirty, key_button(i));
                return;
            }
        }
    } else {
        for (unsigned i = 0; i < SEQ_STEPS; ++i) {
            if (rect_contains(step_button(i), e.x, e.y)) {
                uint8_t *note = &a->audio.pattern.notes[a->track][i];
                *note = *note == 0U ? a->note : 0U;
                damage_add(&a->dirty, step_button(i));
                return;
            }
        }
    }
    if (rect_contains(groove_button(0), e.x, e.y)) {
        if (a->audio.playing) {
            audio_stop(&a->audio);
        } else {
            audio_start(&a->audio);
        }
        damage_add(&a->dirty, (Rect){0, 68, 320, 172});
    } else if (rect_contains(groove_button(1), e.x, e.y)) {
        if (a->audio.pattern.bpm >= SEQ_MIN_BPM + 2U) {
            a->audio.pattern.bpm -= 2U;
        }
        damage_add(&a->dirty, top);
    } else if (rect_contains(groove_button(2), e.x, e.y)) {
        if (a->audio.pattern.bpm <= SEQ_MAX_BPM - 2U) {
            a->audio.pattern.bpm += 2U;
        }
        damage_add(&a->dirty, top);
    } else if (rect_contains(groove_button(3), e.x, e.y)) {
        uint8_t *timbre = &a->audio.pattern.timbre[a->track];
        *timbre = (uint8_t)((*timbre + 1U) % SEQ_TRACKS);
        damage_add(&a->dirty, track_button(a->track));
    } else if (rect_contains(groove_button(4), e.x, e.y)) {
        show_modal(a, MODAL_MUSIC_SAVE);
    } else if (rect_contains(groove_button(5), e.x, e.y)) {
        show_modal(a, MODAL_MUSIC_OPEN);
    } else if (rect_contains(groove_button(6), e.x, e.y)) {
        a->keys = !a->keys;
        damage_add(&a->dirty, (Rect){0, 68, 320, 172});
    }
}

/**
 * @brief Passer à la mire suivante.
 *
 * Le compteur parcourt la page des cibles puis les quatre aplats RGB565. Une image
 * complète est demandée pour effacer la mire précédente.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 */
static void diag_next_pattern(App *a)
{
    a->test_color = (uint8_t)((a->test_color + 1U) % 5U);
    damage_full(&a->dirty);
}

/**
 * @brief Traiter les contrôles et les cibles de diagnostic.
 *
 * Les commandes du bandeau sont prioritaires. Les cinq cibles mettent leurs bits à un dans
 * le masque hits pour visualiser les zones touchées.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 * @param[in] x Abscisse dans le repère écran, en pixels.
 * @param[in] y Ordonnée dans le repère écran, en pixels.
 */
static void diag_touch(App *a, int x, int y)
{
    if (rect_contains(diag_color, x, y)) {
        diag_next_pattern(a);
    } else if (rect_contains(diag_neutral, x, y)) {
        tilt_recenter(&a->tilt);
    } else if (rect_contains(diag_tone, x, y)) {
        a->audio.test_tone = !a->audio.test_tone;
        damage_add(&a->dirty, top);
    } else if (rect_contains(diag_volume, x, y)) {
        a->audio.volume = a->audio.volume < 75U ? (uint8_t)(a->audio.volume + 25U) : 25U;
        damage_add(&a->dirty, top);
    } else if (a->test_color == 0U && rect_contains(diag_axes, x, y)) {
        app_open(a, SCREEN_TILT);
    } else if (a->test_color == 0U && a->diag.touch_ok && touch_resistive(a->diag.touch_controller) &&
               rect_contains(diag_cal, x, y)) {
        app_open(a, SCREEN_TOUCH_CAL);
    } else {
        for (unsigned i = 0; i < 5; ++i) {
            Rect target = {target_xy[i][0] - 14, target_xy[i][1] - 14, 29, 29};
            if (rect_contains(target, x, y)) {
                a->diag.hits |= (uint8_t)(1U << i);
                damage_add(&a->dirty, target);
            }
        }
    }
}

/**
 * @brief Commander l'assistant d'orientation des axes.
 *
 * Mesurer remet le neutre en acquisition. Enregistrer demande une écriture des bits
 * proposés. Refaire recommence l'assistant sans écrire en flash.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 * @param[in] x Abscisse dans le repère écran, en pixels.
 * @param[in] y Ordonnée dans le repère écran, en pixels.
 */
static void tilt_touch(App *a, int x, int y)
{
    if (a->tilt_guide.phase != TILT_GUIDE_INTRO && rect_contains(tilt_retry, x, y)) {
        a->tilt_guide = (TiltGuide){0};
        damage_full(&a->dirty);
    } else if (a->diag.accel_ok && rect_contains(tilt_action, x, y)) {
        if (a->tilt_guide.phase == TILT_GUIDE_INTRO) {
            tilt_guide_start(&a->tilt_guide, &a->tilt);
            damage_full(&a->dirty);
        } else if (a->tilt_guide.phase == TILT_GUIDE_PREVIEW) {
            a->job = JOB_TILT_SAVE;
            set_notice(a, "ENREGISTREMENT DES AXES");
        }
    }
}

/**
 * @brief Ouvrir une rubrique des paramètres.
 *
 * Les quatre grandes zones mènent à la réactivité, aux axes, au diagnostic et au calibrage
 * résistif. La disponibilité du tactile est vérifiée avant ce dernier.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 * @param[in] x Abscisse dans le repère écran, en pixels.
 * @param[in] y Ordonnée dans le repère écran, en pixels.
 */
static void settings_touch(App *a, int x, int y)
{
    if (rect_contains(settings_button(0), x, y)) {
        app_open(a, SCREEN_RESPONSE);
    } else if (rect_contains(settings_button(1), x, y)) {
        app_open(a, SCREEN_TILT);
    } else if (rect_contains(settings_button(2), x, y)) {
        app_open(a, SCREEN_DIAG);
    } else if (rect_contains(settings_button(3), x, y)) {
        if (a->diag.touch_ok && touch_resistive(a->diag.touch_controller)) {
            app_open(a, SCREEN_TOUCH_CAL);
        } else {
            set_notice(a, "CALIBRAGE RESERVE AU TACTILE RESISTIF");
        }
    }
}

/**
 * @brief Modifier séparément la réactivité X et Y.
 *
 * Un curseur sélectionne une valeur de 1 à 10. Les boutons avancent d'une unité. La
 * modification agit immédiatement en RAM. Enregistrer crée une requête de persistance.
 * @param[in,out] a Contexte des applications en RAM. Pointeur valide.
 * @param[in] e Événement de contact en coordonnées écran.
 */
static void response_touch(App *a, TouchEvent e)
{
    for (unsigned i = 0; i < 2U; ++i) {
        uint8_t *response = i == 0U ? &a->tilt.response_x : &a->tilt.response_y;
        Rect slider = response_slider(i);
        if (rect_contains(slider, e.x, e.y)) {
            *response = (uint8_t)(TILT_RESPONSE_MIN +
                         (e.x - slider.x) * (TILT_RESPONSE_MAX - TILT_RESPONSE_MIN) / (slider.w - 1));
        } else if (e.kind == TOUCH_DOWN && rect_contains(response_minus(i), e.x, e.y) &&
                   *response > TILT_RESPONSE_MIN) {
            --*response;
        } else if (e.kind == TOUCH_DOWN && rect_contains(response_plus(i), e.x, e.y) &&
                   *response < TILT_RESPONSE_MAX) {
            ++*response;
        }
    }
    if (e.kind == TOUCH_DOWN && rect_contains(edit_apply, e.x, e.y)) {
        a->tilt.response_x = a->tilt.response_y = TILT_RESPONSE_DEFAULT;
    } else if (e.kind == TOUCH_DOWN && rect_contains(edit_cancel, e.x, e.y)) {
        a->job = JOB_PREFERENCES_SAVE;
        set_notice(a, "ENREGISTREMENT DE LA REACTIVITE");
    }
    damage_full(&a->dirty);
}

/*
 * Distribuer un événement tactile à l'écran approprié.
 *
 * Les travaux flash et le calibrage brut ont leur propre entrée. Les dialogues reçoivent
 * ensuite les gestes, puis le canevas, le retour et les écrans. Cet ordre évite qu'un
 * appui serve deux commandes.
 */
void app_touch(App *a, TouchEvent e)
{
    if (a->job != JOB_NONE || e.kind == TOUCH_NONE || a->screen == SCREEN_TOUCH_CAL ||
        a->screen == SCREEN_SPLASH) {
        return;
    }
    if (e.kind == TOUCH_DOWN && a->notice != NULL) {
        set_notice(a, NULL);
    }
    if (a->modal != MODAL_NONE) {
        if (a->modal == MODAL_RGB && (e.kind == TOUCH_DOWN || e.kind == TOUCH_MOVE)) {
            rgb_drag(a, e.x, e.y);
        }
        if (e.kind == TOUCH_UP) {
            audio_key_release(&a->audio);
        } else if (e.kind == TOUCH_DOWN) {
            modal_touch(a, e.x, e.y);
        }
        return;
    }
    if (a->screen == SCREEN_PAINT) {
        paint_touch(a, e);
        return;
    }
    if (e.kind == TOUCH_DOWN && a->screen != SCREEN_MENU && rect_contains(home, e.x, e.y)) {
        bool child = a->screen == SCREEN_DIAG || a->screen == SCREEN_TILT || a->screen == SCREEN_RESPONSE;
        app_open(a, child ? SCREEN_SETTINGS : SCREEN_MENU);
        return;
    }
    if (a->screen == SCREEN_RESPONSE && (e.kind == TOUCH_DOWN || e.kind == TOUCH_MOVE)) {
        response_touch(a, e);
        return;
    }
    if (a->screen == SCREEN_GROOVE) {
        groove_touch(a, e);
    } else if (e.kind == TOUCH_DOWN) {
        if (a->screen == SCREEN_MENU) {
            for (unsigned i = 0; i < 6; ++i) {
                if (rect_contains(menu_card(i), e.x, e.y)) {
                    app_open(a, (Screen)(SCREEN_PAINT + i));
                    break;
                }
            }
        } else if (a->screen == SCREEN_SETTINGS) {
            settings_touch(a, e.x, e.y);
        } else if (a->screen == SCREEN_DIAG) {
            diag_touch(a, e.x, e.y);
        } else if (a->screen == SCREEN_TILT) {
            tilt_touch(a, e.x, e.y);
        } else {
            game_touch(a, e.x, e.y);
        }
    }
}

/*
 * Transmettre une observation brute au calibrage.
 *
 * Le contact ayant ouvert l'assistant doit d'abord être relâché. Une session complète met
 * son candidat en RAM et demande sa sauvegarde. Un ancien profil permet l'annulation lors
 * d'une entrée normale.
 */
void app_calibration_sample(App *a, RawTouch sample)
{
    if (a->screen != SCREEN_TOUCH_CAL || a->job != JOB_NONE) {
        return;
    }
    if (a->cal_can_cancel && sample.down && !a->touch_cal_session.wait_release &&
        a->touch_cal_session.samples == 0U) {
        const TouchCalibration *cal = touch_cal_get(&a->touch_cal, a->diag.touch_controller);
        int16_t x, y;
        if (touch_cal_map(cal, sample.point, &x, &y) && rect_contains(cal_cancel, x, y)) {
            app_open(a, SCREEN_SETTINGS);
            return;
        }
    }
    TouchCalEvent event = touch_cal_sample(&a->touch_cal_session, sample);
    if (event == CAL_COMPLETE) {
        touch_cal_set(&a->touch_cal, a->diag.touch_controller, &a->touch_cal_session.candidate);
        a->job = JOB_TOUCH_CAL_SAVE;
    }
    if (event != CAL_UNCHANGED) {
        damage_full(&a->dirty);
    }
}

/*
 * Traiter le raccourci facultatif de SW1.
 *
 * Cette entrée reçoit déjà un événement débouncé. Elle change la mire, ouvre un diagnostic
 * ou revient au menu selon l'écran. Les commandes tactiles couvrent l'usage avec le shield
 * monté.
 */
void app_button(App *a, ButtonEvent event)
{
    if (a->job != JOB_NONE || event == BUTTON_NONE || a->screen == SCREEN_SPLASH) {
        return;
    }
    if (a->screen == SCREEN_TOUCH_CAL) {
        if (event == BUTTON_LONG) {
            app_open(a, SCREEN_MENU);
        }
    } else if (a->screen == SCREEN_MENU && event == BUTTON_LONG && a->diag.touch_ok &&
               touch_resistive(a->diag.touch_controller)) {
        app_open(a, SCREEN_TOUCH_CAL);
    } else if (a->screen == SCREEN_DIAG && event == BUTTON_SHORT) {
        diag_next_pattern(a);
    } else if (a->screen == SCREEN_PAINT && a->paint.modified) {
        show_modal(a, MODAL_DRAW_LEAVE);
    } else {
        app_open(a, a->screen == SCREEN_MENU ? SCREEN_SETTINGS : SCREEN_MENU);
    }
}

/*
 * Avancer les états temporels de vingt millisecondes.
 *
 * Le moteur du jeu actif reçoit les commandes filtrées. Les variations de score ou de
 * temps invalident le bandeau. L'accueil et le curseur de groovebox utilisent également ce
 * passage coopératif.
 */
void app_step(App *a)
{
    if (a->screen == SCREEN_SPLASH) {
        a->splash_ms += GAME_STEP_MS;
        if (a->splash_ms >= 3200U) {
            app_open(a, a->splash_target);
        }
    } else if (a->screen == SCREEN_MAZE) {
        uint32_t before = maze_seconds_left(&a->maze);
        maze_step(&a->maze, a->tilt.x, a->tilt.y, &a->dirty);
        if (before != maze_seconds_left(&a->maze)) {
            damage_add(&a->dirty, top);
        }
    } else if (a->screen == SCREEN_BREAKOUT) {
        unsigned before = a->breakout.score;
        breakout_step(&a->breakout, a->tilt.x, &a->dirty);
        if (before != a->breakout.score) {
            damage_add(&a->dirty, top);
        }
    } else if (a->screen == SCREEN_RACE) {
        uint32_t before = a->race.score;
        race_step(&a->race, a->tilt.x, &a->dirty);
        if (before != a->race.score) {
            damage_add(&a->dirty, top);
        }
    } else if (a->screen == SCREEN_GROOVE && a->shown_step != a->audio.step) {
        if (!a->keys) {
            damage_add(&a->dirty, step_button(a->shown_step));
            damage_add(&a->dirty, step_button(a->audio.step));
        }
        a->shown_step = a->audio.step;
    }
}

/*
 * Indiquer si le transport PWM doit fonctionner.
 *
 * Un travail flash suspend la production. L'audio est demandé lorsque la groovebox est
 * ouverte ou lorsque la note de diagnostic est active.
 */
bool app_wants_audio(const App *a)
{
    return a->job == JOB_NONE && (a->screen == SCREEN_GROOVE || a->audio.test_tone);
}

/*
 * Charger le livre des quatre motifs au démarrage.
 *
 * Le CRC est contrôlé par le service de stockage, puis le décodeur contrôle la version et
 * les valeurs musicales avant d'accepter le livre.
 */
void app_read_book(App *a, Storage *storage)
{
    uint8_t data[SEQ_BOOK_BYTES];
    if (storage_read(storage, SAVE_MUSIC, data, sizeof(data)) == STORE_OK) {
        (void)book_decode(&a->book, data);
    }
}

/*
 * Charger les profils des deux tactiles résistifs.
 *
 * Le profil est associé à l'orientation MADCTL. Un objet absent ou invalide laisse un
 * livre vide, ce qui déclenchera le calibrage lors de la reconnaissance du shield.
 */
void app_read_calibration(App *a, Storage *storage)
{
    (void)touch_cal_load(&a->touch_cal, storage, OBCP_LCD_MADCTL);
}

/*
 * Charger le repère des jeux depuis la flash.
 *
 * Le petit format contient version, orientation du TFT et bits d'axes. Le masque est
 * vérifié pour garder les valeurs de départ si le contenu ne correspond pas.
 */
void app_read_axes(App *a, Storage *storage)
{
    uint8_t data[AXES_BYTES];
    if (storage_read(storage, SAVE_TILT_AXES, data, sizeof(data)) == STORE_OK &&
        data[0] == AXES_VERSION && data[1] == OBCP_LCD_MADCTL && (data[2] & ~TILT_AXES_MASK) == 0U) {
        a->tilt.axes = data[2];
    }
}

/*
 * Charger les deux valeurs de réactivité.
 *
 * La version et les bornes 1 à 10 sont contrôlées avant de remplacer les valeurs de
 * départ. Cette opération ne modifie pas le neutre du capteur.
 */
void app_read_preferences(App *a, Storage *storage)
{
    uint8_t data[3];
    if (storage_read(storage, SAVE_PREFERENCES, data, sizeof(data)) == STORE_OK && data[0] == 1U &&
        data[1] >= TILT_RESPONSE_MIN && data[1] <= TILT_RESPONSE_MAX &&
        data[2] >= TILT_RESPONSE_MIN && data[2] <= TILT_RESPONSE_MAX) {
        a->tilt.response_x = data[1];
        a->tilt.response_y = data[2];
    }
}

/*
 * Exécuter la requête de stockage en attente.
 *
 * Les formats propres au dessin, au motif, au tactile et aux axes sont traités séparément.
 * Le résultat prépare le message de fin et sera utilisé par board.c pour l'impulsion de
 * LED.
 */
StoreResult app_store_task(App *a, Storage *storage)
{
    if (a->job == JOB_NONE) {
        return STORE_EMPTY;
    }
    if (a->job == JOB_TOUCH_CAL_SAVE) {
        StoreResult saved = touch_cal_save(&a->touch_cal, storage, OBCP_LCD_MADCTL);
        a->job = JOB_NONE;
        app_open(a, SCREEN_SETTINGS);
        set_notice(a, saved == STORE_OK ? "CALIBRAGE ENREGISTRE" : "CAL EN RAM / ERREUR FLASH");
        return saved;
    }
    if (a->job == JOB_TILT_SAVE) {
        a->tilt.axes = a->tilt_guide.axes;
        tilt_project(&a->tilt, a->tilt.axes, &a->tilt.x, &a->tilt.y);
        const uint8_t data[AXES_BYTES] = {AXES_VERSION, OBCP_LCD_MADCTL, a->tilt.axes};
        StoreResult saved = storage_save(storage, SAVE_TILT_AXES, data, sizeof(data));
        a->job = JOB_NONE;
        app_open(a, SCREEN_SETTINGS);
        set_notice(a, saved == STORE_OK ? "AXES ENREGISTRES" : "AXES EN RAM / ERREUR FLASH");
        return saved;
    }
    if (a->job == JOB_PREFERENCES_SAVE) {
        const uint8_t data[3] = {1, a->tilt.response_x, a->tilt.response_y};
        StoreResult saved = storage_save(storage, SAVE_PREFERENCES, data, sizeof(data));
        a->job = JOB_NONE;
        app_open(a, SCREEN_SETTINGS);
        set_notice(a, saved == STORE_OK ? "REACTIVITE ENREGISTREE" : "REACTIVITE EN RAM / ERREUR FLASH");
        return saved;
    }
    StoreResult result = STORE_EMPTY;
    if (a->job == JOB_DRAW_SAVE || a->job == JOB_DRAW_OPEN) {
        SaveObject object = a->slot == 0U ? SAVE_DRAW_1 : SAVE_DRAW_2;
        if (a->job == JOB_DRAW_SAVE) {
            result = paint_save(&a->paint, storage, object);
        } else {
            result = paint_load(&a->paint, storage, object);
            damage_add(&a->dirty, paint_canvas);
        }
        if (result == STORE_OK) {
            a->paint.modified = false;
        }
    } else if (a->job == JOB_MUSIC_SAVE) {
        PatternBook candidate = a->book;
        candidate.pattern[a->slot] = a->audio.pattern;
        candidate.present |= (uint8_t)(1U << a->slot);
        uint8_t data[SEQ_BOOK_BYTES];
        book_encode(&candidate, data);
        result = storage_save(storage, SAVE_MUSIC, data, sizeof(data));
        if (result == STORE_OK) {
            a->book = candidate;
        }
    } else if ((a->book.present & (1U << a->slot)) != 0U) {
        a->audio.pattern = a->book.pattern[a->slot];
        result = STORE_OK;
        damage_full(&a->dirty);
    }
    const char *message = "ERREUR FLASH";
    if (result == STORE_OK) {
        message = a->job == JOB_DRAW_SAVE || a->job == JOB_MUSIC_SAVE ? "ENREGISTRE" : "CHARGE";
    } else if (result == STORE_EMPTY) {
        message = "EMPLACEMENT VIDE OU INVALIDE";
    }
    a->job = JOB_NONE;
    set_notice(a, message);
    return result;
}

/*
 * Invalider les valeurs de diagnostic qui évoluent.
 *
 * Les zones des mesures, de l'aperçu d'axes ou de la réponse sont ajoutées aux rectangles
 * en attente. Les éléments fixes restent hors de cette invalidation périodique.
 */
void app_diag_refresh(App *a)
{
    if (a->screen == SCREEN_DIAG && a->test_color == 0U) {
        damage_add(&a->dirty, (Rect){54, 67, 212, 128});
    } else if (a->screen == SCREEN_TILT) {
        damage_add(&a->dirty, tilt_pad);
        damage_add(&a->dirty, (Rect){179, 108, 127, 26});
    } else if (a->screen == SCREEN_RESPONSE) {
        damage_add(&a->dirty, (Rect){14, 181, 292, 11});
    }
}

/**
 * @brief Dessiner une grande carte de menu uniforme.
 *
 * Le titre utilise un facteur deux et peut avoir deux lignes. Le sous-titre garde un
 * facteur un. Les emprises servent aussi aux tests de contact.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 * @param[in] r Rectangle écran, avec bord droit et bas exclus.
 * @param[in] first Première ligne du titre du bouton.
 * @param[in] second Deuxième ligne de titre, chaîne vide autorisée.
 * @param[in] subtitle Sous-titre de carte terminé par zéro.
 * @param[in] accent Couleur RGB565 de l’indicateur de la carte.
 */
static void card(Gfx *g, Rect r, const char *first, const char *second,
                 const char *subtitle, uint16_t accent)
{
    gfx_fill(g, r, COLOR_PANEL);
    gfx_outline(g, r, COLOR_PANEL_HI);
    gfx_fill(g, (Rect){r.x, r.y, 4, r.h}, accent);
    int first_width = ((int)strlen(first) * 6 - 1) * 2;
    gfx_text(g, r.x + (r.w - first_width) / 2 + 2, r.y + (second[0] ? 8 : 18), first, 2, COLOR_TEXT);
    if (second[0]) {
        int second_width = ((int)strlen(second) * 6 - 1) * 2;
        gfx_text(g, r.x + (r.w - second_width) / 2 + 2, r.y + 27, second, 2, COLOR_TEXT);
    }
    int width = (int)strlen(subtitle) * 6 - 1;
    gfx_text(g, r.x + (r.w - width) / 2 + 2, r.y + r.h - 13, subtitle, 1, accent);
}

/**
 * @brief Composer les six entrées et la signature du lanceur.
 *
 * Chaque carte partage sa géométrie, sa police et ses marges. Les noms et couleurs
 * viennent de tables constantes en flash.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 */
static void draw_menu(Gfx *g)
{
    static const char *const names[6][2] = {
        {"PAINT", ""}, {"GROOVEBOX", ""}, {"LABYRINTHE", ""},
        {"CASSE", "BRIQUES"}, {"COURSE", "AUTO"}, {"PARAMETRES", ""}
    };
    for (unsigned i = 0; i < 6; ++i) {
        card(g, menu_card(i), names[i][0], names[i][1], subtitles[i], accents[i]);
    }
    centered_text(g, 227, "J. Imbaud - SUPMICROTECH - CROC", 1, COLOR_DIM);
}

/**
 * @brief Composer l'accueil SUPMICROTECH.
 *
 * Le fond clair, le logo compressé et les titres sont dessinés dans la bande courante. La
 * durée est gérée ailleurs pour que le dessin ne bloque pas la boucle.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 */
static void draw_splash(Gfx *g)
{
    gfx_fill(g, g->clip, RGB565(255,255,255));
    centered_text(g, 14, "OBCP", 3, RGB565(27,62,99));
    centered_text(g, 43, "STM32L476", 1, RGB565(64,91,118));
    logo_draw(g, 26, 64);
    centered_text(g, 202, "ATELIER TACTILE", 2, RGB565(27,62,99));
    centered_text(g, 227, "J. Imbaud - SUPMICROTECH - CROC", 1, RGB565(64,91,118));
}

/**
 * @brief Composer le bandeau de navigation d'un écran.
 *
 * Un écran enfant des paramètres reçoit un retour vers les paramètres. Les autres écrans
 * proposent le menu principal. Les jeux ajoutent ensuite leurs indicateurs.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 * @param[in] screen Écran de destination.
 */
static void draw_bar(Gfx *g, Screen screen)
{
    gfx_fill(g, top, COLOR_PANEL);
    bool child = screen == SCREEN_DIAG || screen == SCREEN_TILT || screen == SCREEN_RESPONSE;
    button(g, home, child ? "< RET." : "< MENU", COLOR_PANEL_HI, COLOR_TEXT);
    if (screen != SCREEN_DIAG) {
        unsigned scale = screen == SCREEN_SETTINGS || screen == SCREEN_RESPONSE ? 2U : 1U;
        gfx_text(g, 67, scale == 2U ? 9 : 12, titles[screen], scale, COLOR_TEXT);
    }
}

/**
 * @brief Dessiner les quatre rubriques de réglage.
 *
 * Les valeurs X/Y, les mentions du shield et la version sont lues dans l'état courant. Le
 * dessin n'effectue aucune lecture matérielle.
 * @param[in] a Contexte des applications en RAM, consulté sans modification. Pointeur
 *   valide.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 */
static void draw_settings(const App *a, Gfx *g)
{
    draw_bar(g, SCREEN_SETTINGS);
    char text[40];
    (void)snprintf(text, sizeof(text), "REACTIVITE  X %u/10   Y %u/10",
                   a->tilt.response_x, a->tilt.response_y);
    centered_text(g, 46, text, 1, COLOR_DIM);
    card(g, settings_button(0), "REACTIVITE", "", "SENSIBILITE X / Y", COLOR_GOLD);
    card(g, settings_button(1), "AXES JEUX", "", "SENS DE L'ECRAN", COLOR_MINT);
    card(g, settings_button(2), "DIAGNOSTIC", "", "MIRES / CAPTEURS", COLOR_BLUE);
    card(g, settings_button(3), "CALIBRER", "", "TACTILE RESISTIF", COLOR_PINK);
    centered_text(g, 209, "SHIELDS ADAFRUIT 1947 ET 1651", 1, COLOR_DIM);
    centered_text(g, 225, "OBCP TACTILE 0.4.1", 1, COLOR_DIM);
}

/**
 * @brief Dessiner les curseurs de réactivité et leur aperçu.
 *
 * Les largeurs colorées représentent les valeurs 1 à 10. Les commandes X/Y affichées
 * proviennent du filtre déjà calculé par la tâche d'entrée.
 * @param[in] a Contexte des applications en RAM, consulté sans modification. Pointeur
 *   valide.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 */
static void draw_response(const App *a, Gfx *g)
{
    draw_bar(g, SCREEN_RESPONSE);
    char text[32];
    for (unsigned i = 0; i < 2U; ++i) {
        unsigned response = i == 0U ? a->tilt.response_x : a->tilt.response_y;
        gfx_text(g, 24, 51 + (int)i * 57, i == 0U ? "AXE X" : "AXE Y", 1, COLOR_MINT);
        button(g, response_minus(i), "-", COLOR_PANEL_HI, COLOR_TEXT);
        button(g, response_plus(i), "+", COLOR_PANEL_HI, COLOR_TEXT);
        Rect slider = response_slider(i);
        gfx_fill(g, slider, COLOR_PANEL);
        Rect progress = slider;
        progress.w = (int16_t)(slider.w * response / TILT_RESPONSE_MAX);
        gfx_fill(g, progress, COLOR_PANEL_HI);
        (void)snprintf(text, sizeof(text), "%u / 10", response);
        gfx_outline(g, slider, COLOR_PANEL_HI);
        int width = (int)strlen(text) * 6 - 1;
        gfx_text(g, slider.x + (slider.w - width) / 2, slider.y + 13, text, 1, COLOR_TEXT);
    }
    gfx_text(g, 24, 168, "1 DOUX / 10 VIF - ESSAYER PUIS SAUVER", 1, COLOR_DIM);
    (void)snprintf(text, sizeof(text), "TEST X %+d %%   Y %+d %%",
                   (int)(a->tilt.x * 100.0f), (int)(a->tilt.y * 100.0f));
    centered_text(g, 182, text, 1, COLOR_GOLD);
    button(g, edit_apply, "PAR DEFAUT", COLOR_PANEL_HI, COLOR_TEXT);
    button(g, edit_cancel, "ENREGISTRER", COLOR_MINT, COLOR_BG);
}

/**
 * @brief Composer le jeu actif et son écran de bilan.
 *
 * Le moteur dessine la scène, puis le bandeau commun et les boutons de phase sont
 * superposés. Pendant le BOUM de course, le bilan attend la fin du compteur d'animation.
 * @param[in] a Contexte des applications en RAM, consulté sans modification. Pointeur
 *   valide.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 */
static void draw_game(const App *a, Gfx *g)
{
    GamePhase phase;
    char label[26];
    if (a->screen == SCREEN_MAZE) {
        maze_draw(&a->maze, g);
        phase = a->maze.phase;
        (void)snprintf(label, sizeof(label), "%lu S", (unsigned long)maze_seconds_left(&a->maze));
    } else if (a->screen == SCREEN_BREAKOUT) {
        breakout_draw(&a->breakout, g);
        phase = a->breakout.phase;
        (void)snprintf(label, sizeof(label), "%u PTS", a->breakout.score);
    } else {
        race_draw(&a->race, g);
        phase = a->race.phase;
        (void)snprintf(label, sizeof(label), "%lu/%u", (unsigned long)a->race.score, race_goal(&a->race));
    }
    draw_bar(g, a->screen);
    gfx_text(g, 194, 12, label, 1, COLOR_DIM);
    unsigned level = game_level(a);
    (void)snprintf(label, sizeof(label), "N%u/%u", level + 1U, (unsigned)GAME_LEVELS);
    gfx_text(g, 151, 12, label, 1, COLOR_GOLD);
    if (phase == GAME_RUN || phase == GAME_PAUSE) {
        button(g, pause_btn, phase == GAME_RUN ? "PAUSE" : "REPRISE", COLOR_PANEL_HI, COLOR_TEXT);
    }
    if (phase != GAME_RUN && !(a->screen == SCREEN_RACE && a->race.crash_ms > 0U)) {
        Rect popup = {60, 79, 200, 118};
        const char *msg = "PRET A JOUER";
        const char *action = "JOUER";
        if (phase == GAME_PAUSE) {
            msg = "PAUSE";
            action = "REPRENDRE";
        } else if (phase == GAME_WON) {
            bool last = level + 1U == GAME_LEVELS;
            msg = last ? "JEU TERMINE" : "NIVEAU REUSSI";
            action = last ? "RECOMMENCER" : "NIVEAU SUIVANT";
        } else if (phase == GAME_LOST) {
            msg = a->screen == SCREEN_MAZE ? "TEMPS ECOULE" : "PARTIE TERMINEE";
            action = "REJOUER";
        } else if (!a->tilt.valid) {
            msg = "CAPTEUR NON PRET";
        } else if (!a->tilt.calibrated) {
            msg = "POSER AU NEUTRE";
        }
        gfx_fill(g, popup, COLOR_BG);
        gfx_outline(g, popup, COLOR_MINT);
        gfx_text(g, popup.x + (popup.w - (int)strlen(msg) * 6) / 2, 94, msg, 1, COLOR_TEXT);
        button(g, play_btn, action, COLOR_MINT, COLOR_BG);
        button(g, game_menu, "MENU PRINCIPAL", COLOR_PANEL_HI, COLOR_TEXT);
    }
}

/**
 * @brief Convertir le canevas indexé et dessiner ses outils.
 *
 * La bande reçoit les couleurs de la palette du dessin. Les boutons présentent la gomme
 * active, la couleur courante et le diamètre du crayon.
 * @param[in] a Contexte des applications en RAM, consulté sans modification. Pointeur
 *   valide.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 */
static void draw_paint(const App *a, Gfx *g)
{
    static const char *const tools[] = {"MENU", "COUL.", "TAILLE", "GOMME", "SAUVE", "OUVRE", "EFFACE"};
    paint_draw(&a->paint, g);
    gfx_fill(g, (Rect){0, 200, 320, 40}, COLOR_PANEL);
    for (unsigned i = 0; i < sizeof(tools) / sizeof(tools[0]); ++i) {
        uint16_t fill = i == 3U && a->paint.erase ? COLOR_GOLD : COLOR_PANEL_HI;
        button(g, paint_button(i), tools[i], fill, fill == COLOR_GOLD ? COLOR_BG : COLOR_TEXT);
    }
    Rect swatch = {paint_button(1).x + 7, 230, 28, 4};
    gfx_fill(g, swatch, a->paint.palette[a->paint.ink]);
    char diameter[8];
    (void)snprintf(diameter, sizeof(diameter), "%uPX", PAINT_DIAMETERS[a->paint.brush]);
    gfx_text(g, paint_button(2).x + 7, 229, diameter, 1, COLOR_GOLD);
}

/**
 * @brief Composer le séquenceur ou le clavier tactile.
 *
 * Les cases sont colorées selon leur note et le pas en cours. Les pistes et commandes de
 * transport réutilisent les mêmes rectangles que les événements tactiles.
 * @param[in] a Contexte des applications en RAM, consulté sans modification. Pointeur
 *   valide.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 */
static void draw_groove(const App *a, Gfx *g)
{
    char buf[25];
    draw_bar(g, a->screen);
    (void)snprintf(buf, sizeof(buf), "%u BPM", (unsigned)a->audio.pattern.bpm);
    gfx_text(g, 178, 8, buf, 2, COLOR_GOLD);
    for (unsigned i = 0; i < SEQ_TRACKS; ++i) {
        (void)snprintf(buf, sizeof(buf), "%u %s", i + 1U, track_label[a->audio.pattern.timbre[i]]);
        button(g, track_button(i), buf, a->track == i ? COLOR_BLUE : COLOR_PANEL,
               a->track == i ? COLOR_BG : COLOR_TEXT);
    }
    if (a->keys) {
        for (unsigned i = 0; i < 8; ++i) {
            bool selected = a->note == i + 1U;
            button(g, key_button(i), notes[i], selected ? COLOR_MINT : COLOR_PANEL,
                   selected ? COLOR_BG : COLOR_TEXT);
        }
        gfx_text(g, 12, 183, "LA NOTE CHOISIE SERT AUX PROCHAINS PAS", 1, COLOR_DIM);
    } else {
        for (unsigned i = 0; i < SEQ_STEPS; ++i) {
            uint8_t note = a->audio.pattern.notes[a->track][i];
            if (note != 0U) {
                (void)snprintf(buf, sizeof(buf), "%02u %s", i + 1U, notes[note - 1U]);
            } else {
                (void)snprintf(buf, sizeof(buf), "%02u", i + 1U);
            }
            bool current = a->audio.playing && a->audio.step == i;
            uint16_t fill = current ? COLOR_GOLD : (note ? COLOR_MINT : COLOR_PANEL);
            button(g, step_button(i), buf, fill, current || note ? COLOR_BG : COLOR_TEXT);
        }
    }
    static const char *const tools[] = {"PLAY", "-", "+", "TIMB.", "SAUVE", "OUVRE", "CLAV."};
    for (unsigned i = 0; i < sizeof(tools) / sizeof(tools[0]); ++i) {
        const char *label = tools[i];
        if (i == 0U && a->audio.playing) {
            label = "STOP";
        } else if (i == 6U && a->keys) {
            label = "PAS";
        }
        button(g, groove_button(i), label, COLOR_PANEL_HI, COLOR_TEXT);
    }
}

/**
 * @brief Présenter les identités, cibles et compteurs de la carte.
 *
 * Les valeurs viennent de Diagnostics et de Tilt. Les aplats court-circuitent la page des
 * cibles pour remplir la zone de test avec une couleur RGB565 précise.
 * @param[in] a Contexte des applications en RAM, consulté sans modification. Pointeur
 *   valide.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 */
static void draw_diag(const App *a, Gfx *g)
{
    draw_bar(g, a->screen);
    button(g, diag_color, "APLATS", COLOR_PANEL_HI, COLOR_TEXT);
    button(g, diag_neutral, "NEUTRE", COLOR_PANEL_HI, COLOR_TEXT);
    button(g, diag_tone, "440 HZ", a->audio.test_tone ? COLOR_GOLD : COLOR_PANEL_HI,
           a->audio.test_tone ? COLOR_BG : COLOR_TEXT);
    char text[48];
    (void)snprintf(text, sizeof(text), "%u%%", (unsigned)a->audio.volume);
    button(g, diag_volume, text, COLOR_PANEL_HI, COLOR_TEXT);
    if (a->test_color != 0U) {
        static const uint16_t colors[4] = {0xF800,0x07E0,0x001F,0xFFFF};
        gfx_fill(g, (Rect){0, 34, 320, 206}, colors[a->test_color - 1U]);
        return;
    }
    for (unsigned i = 0; i < 5; ++i) {
        uint16_t color = (a->diag.hits & (1U << i)) ? COLOR_MINT : COLOR_BLUE;
        gfx_disc(g, target_xy[i][0], target_xy[i][1], 9, color);
        gfx_disc(g, target_xy[i][0], target_xy[i][1], 4, COLOR_BG);
    }
    if (a->diag.tft_id == 0x9341U) {
        (void)snprintf(text, sizeof(text), "TFT ILI9341 / ID 9341");
    } else {
        (void)snprintf(text, sizeof(text), "TFT ID %04X / A VERIFIER", (unsigned)a->diag.tft_id);
    }
    gfx_text(g, 62, 71, text, 1, COLOR_TEXT);
    const char *kind = touch_name(a->diag.touch_controller);
    if (!a->diag.touch_ok) {
        (void)snprintf(text, sizeof(text), "TACTILE %s%s", kind,
                       a->diag.touch_controller == TOUCH_CTRL_NONE ? "" : " / I/O");
    } else if (a->diag.touch_controller == TOUCH_CTRL_FT62XX) {
        (void)snprintf(text, sizeof(text), "FT62XX I2C 38 / ID %02X", (unsigned)a->diag.touch_id);
    } else {
        const char *cal = touch_cal_get(&a->touch_cal, a->diag.touch_controller) != NULL
            ? "CAL OK" : "CAL A FAIRE";
        const char *bus = a->diag.touch_controller == TOUCH_CTRL_TSC2007 ? "I2C 48" : "SPI 0811";
        (void)snprintf(text, sizeof(text), "%s %s / %s", kind, bus, cal);
    }
    gfx_text(g, 62, 85, text, 1, COLOR_MINT);
    (void)snprintf(text, sizeof(text), "BRUT %u/%u  LCD %d/%d", (unsigned)a->diag.raw_x,
                   (unsigned)a->diag.raw_y, (int)a->diag.x, (int)a->diag.y);
    gfx_text(g, 62, 99, text, 1, COLOR_TEXT);
    (void)snprintf(text, sizeof(text), "%s / FLASH %s", a->diag.accel_name,
                   a->diag.flash_ok ? "OK" : "N/D");
    gfx_text(g, 62, 113, text, 1, COLOR_DIM);
    (void)snprintf(text, sizeof(text), "X %+d Y %+d / %s", (int)(100.0f * a->tilt.x),
                   (int)(100.0f * a->tilt.y), a->tilt.calibrated ? "CAL" : "NEUTRE");
    gfx_text(g, 62, 147, text, 1, COLOR_TEXT);
    (void)snprintf(text, sizeof(text), "%lu LOTS/S %lu PX/S", (unsigned long)a->diag.frames_per_second,
                   (unsigned long)a->diag.pixels_per_second);
    gfx_text(g, 62, 159, text, 1, COLOR_TEXT);
    (void)snprintf(text, sizeof(text), "DMA LCD %lu / AUDIO %lu", (unsigned long)a->diag.lcd_errors,
                   (unsigned long)a->diag.audio_missed);
    gfx_text(g, 62, 171, text, 1, COLOR_DIM);
    button(g, diag_axes, "AXES JEUX", COLOR_PANEL_HI, COLOR_TEXT);
    if (a->diag.touch_ok && touch_resistive(a->diag.touch_controller)) {
        button(g, diag_cal, "CAL. TACTILE", COLOR_PANEL_HI, COLOR_TEXT);
    }
}

/**
 * @brief Présenter la cible et l'instruction de calibrage.
 *
 * La couleur indique cible attendue, appui stabilisé ou cible acquise. Les messages
 * distinguent le maintien, le relâchement et la sauvegarde.
 * @param[in] a Contexte des applications en RAM, consulté sans modification. Pointeur
 *   valide.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 */
static void draw_calibration(const App *a, Gfx *g)
{
    const TouchCalSession *s = &a->touch_cal_session;
    gfx_fill(g, top, COLOR_PANEL);
    gfx_text(g, 10, 7, "CALIBRAGE TACTILE", 2, COLOR_TEXT);
    gfx_text(g, 10, 25, touch_name(a->diag.touch_controller), 1, COLOR_MINT);
    if (a->cal_can_cancel) {
        button(g, cal_cancel, "ANNULER", COLOR_PANEL_HI, COLOR_TEXT);
    }
    bool ready = s->samples == CAL_STABLE_SAMPLES;
    for (unsigned i = 0; i < CAL_TARGET_COUNT; ++i) {
        RawPoint p = TOUCH_CAL_TARGETS[i];
        uint16_t color = i < s->target || (i == s->target && ready) ? COLOR_MINT :
                         i == s->target ? COLOR_GOLD : COLOR_PANEL_HI;
        gfx_disc(g, p.x, p.y, 11, color);
        gfx_disc(g, p.x, p.y, 7, COLOR_BG);
        gfx_disc(g, p.x, p.y, 2, color);
    }
    gfx_fill(g, (Rect){43, 84, 234, 95}, COLOR_PANEL);
    char title[24];
    (void)snprintf(title, sizeof(title), "CIBLE %u / %u",
                   (unsigned)s->target + 1U, (unsigned)CAL_TARGET_COUNT);
    gfx_text(g, 60, 97, title, 2, COLOR_TEXT);
    const char *instruction = "MAINTENIR LA CIBLE";
    if (a->job == JOB_TOUCH_CAL_SAVE) {
        instruction = "SAUVEGARDE EN COURS";
    } else if (!a->diag.touch_ok) {
        instruction = "TACTILE INDISPONIBLE";
    } else if (s->samples == CAL_STABLE_SAMPLES) {
        instruction = "RELACHER";
    } else if (s->wait_release) {
        instruction = "LEVER LE DOIGT";
    }
    gfx_text(g, 60, 124, instruction, 1, COLOR_GOLD);
    gfx_text(g, 60, 141, s->retry ? "CIBLES A RECOMMENCER" : "APPUYER AU CENTRE", 1, COLOR_DIM);
    gfx_text(g, 60, 160, "4 CIBLES A VALIDER", 1, COLOR_DIM);
}

/**
 * @brief Présenter les gestes demandés par l'assistant d'axes.
 *
 * L'aperçu utilise les bits proposés pour montrer le résultat avant validation.
 * L'utilisateur peut refaire la mesure ou demander la persistance.
 * @param[in] a Contexte des applications en RAM, consulté sans modification. Pointeur
 *   valide.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 */
static void draw_tilt(const App *a, Gfx *g)
{
    static const char *const instruction[] = {
        "1 / POSER LA CARTE A PLAT", "MESURE DU NEUTRE", "2 / ABAISSER LE BORD DROIT",
        "REPOSER LA CARTE A PLAT", "3 / ABAISSER LE BORD DU BAS", "VERIFIER LE SENS DU POINT"
    };
    TiltGuidePhase phase = a->tilt_guide.phase;
    gfx_fill(g, top, COLOR_PANEL);
    button(g, home, "< RET.", COLOR_PANEL_HI, COLOR_TEXT);
    gfx_text(g, 68, 9, "AXES DES JEUX", 2, COLOR_TEXT);
    gfx_text(g, 14, 51, a->diag.accel_ok ? instruction[phase] : "ACCELEROMETRE ABSENT", 1, COLOR_MINT);
    if (a->diag.accel_ok) {
        static const char *const hint[] = {
            "TOUCHER MESURER POUR COMMENCER", "GARDER LA CARTE IMMOBILE", "TENIR CETTE POSITION",
            "GARDER LA CARTE IMMOBILE", "TENIR CETTE POSITION", "REPOSER LA CARTE PUIS ENREGISTRER"
        };
        gfx_text(g, 14, 72, hint[phase], 1, COLOR_DIM);
    }

    float dx, dy;
    uint8_t axes = phase == TILT_GUIDE_PREVIEW ? a->tilt_guide.axes : a->tilt.axes;
    tilt_project(&a->tilt, axes, &dx, &dy);
    int cx = tilt_pad.x + tilt_pad.w / 2, cy = tilt_pad.y + tilt_pad.h / 2;
    gfx_fill(g, tilt_pad, COLOR_PANEL);
    gfx_outline(g, tilt_pad, COLOR_PANEL_HI);
    gfx_fill(g, (Rect){(int16_t)(tilt_pad.x + 7), (int16_t)cy, (int16_t)(tilt_pad.w - 14), 1}, COLOR_DIM);
    gfx_fill(g, (Rect){(int16_t)cx, (int16_t)(tilt_pad.y + 7), 1, (int16_t)(tilt_pad.h - 14)}, COLOR_DIM);
    gfx_text(g, tilt_pad.x + tilt_pad.w - 17, cy - 11, "+X", 1, COLOR_DIM);
    gfx_text(g, cx + 7, tilt_pad.y + tilt_pad.h - 12, "+Y", 1, COLOR_DIM);
    gfx_disc(g, cx + (int)(dx * 57.0f), cy + (int)(dy * 42.0f), 5, COLOR_GOLD);
    char values[24];
    (void)snprintf(values, sizeof(values), "X %+d  Y %+d", (int)(dx * 100.0f), (int)(dy * 100.0f));
    gfx_text(g, 179, 115, values, 1, COLOR_TEXT);
    if (a->diag.accel_ok && (phase == TILT_GUIDE_INTRO || phase == TILT_GUIDE_PREVIEW)) {
        button(g, tilt_action, phase == TILT_GUIDE_INTRO ? "MESURER" : "ENREGISTRER", COLOR_MINT, COLOR_BG);
    }
    if (phase != TILT_GUIDE_INTRO) {
        button(g, tilt_retry, "REFAIRE", COLOR_PANEL_HI, COLOR_TEXT);
    }
}

/**
 * @brief Composer les dialogues de palette, RVB et diamètre.
 *
 * Les valeurs RVB restent des entiers 0 à 255 dans l'éditeur. La conversion RGB565
 * intervient à l'application de la nouvelle couleur.
 * @param[in] a Contexte des applications en RAM, consulté sans modification. Pointeur
 *   valide.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 */
static void draw_paint_editor(const App *a, Gfx *g)
{
    gfx_fill(g, edit_overlay, COLOR_BG);
    gfx_outline(g, edit_overlay, COLOR_MINT);
    char text[32];
    if (a->modal == MODAL_COLORS) {
        (void)snprintf(text, sizeof(text), "PALETTE / CASE %u", a->paint.ink + 1U);
        gfx_text(g, 34, 46, text, 1, COLOR_TEXT);
        gfx_text(g, 34, 60, "CHOISIR UNE CASE PUIS RVB OU UTILISER", 1, COLOR_DIM);
        for (unsigned i = 0; i < 16U; ++i) {
            Rect r = color_button(i);
            gfx_fill(g, r, a->paint.palette[i]);
            gfx_outline(g, r, a->paint.ink == i ? COLOR_MINT : COLOR_PANEL_HI);
            if (a->paint.ink == i) {
                gfx_outline(g, (Rect){r.x + 2, r.y + 2, r.w - 4, r.h - 4}, COLOR_BG);
            }
        }
        button(g, edit_apply, "RVB", COLOR_GOLD, COLOR_BG);
        button(g, edit_cancel, "UTILISER", COLOR_MINT, COLOR_BG);
    } else if (a->modal == MODAL_RGB) {
        (void)snprintf(text, sizeof(text), "RVB / CASE %u", a->paint.ink + 1U);
        gfx_text(g, 34, 44, text, 1, COLOR_TEXT);
        gfx_fill(g, (Rect){259, 41, 28, 12}, RGB565(a->rgb[0], a->rgb[1], a->rgb[2]));
        static const uint16_t colors[] = {COLOR_PINK, COLOR_MINT, COLOR_BLUE};
        static const char *const channels[] = {"R", "V", "B"};
        for (unsigned i = 0; i < 3U; ++i) {
            Rect slider = rgb_slider(i);
            gfx_text(g, 31, slider.y + 10, channels[i], 1, colors[i]);
            button(g, rgb_minus(i), "-", COLOR_PANEL_HI, COLOR_TEXT);
            button(g, rgb_plus(i), "+", COLOR_PANEL_HI, COLOR_TEXT);
            gfx_fill(g, slider, COLOR_PANEL);
            Rect progress = slider;
            progress.w = (int16_t)(1U + (slider.w - 1U) * a->rgb[i] / 255U);
            gfx_fill(g, progress, colors[i]);
            (void)snprintf(text, sizeof(text), "%03u", a->rgb[i]);
            gfx_text(g, 264, slider.y + 10, text, 1, COLOR_TEXT);
        }
        gfx_text(g, 32, 182, "LES TRAITS DE CETTE CASE CHANGENT AUSSI", 1, COLOR_DIM);
        button(g, edit_apply, "APPLIQUER", COLOR_MINT, COLOR_BG);
        button(g, edit_cancel, "ANNULER", COLOR_PANEL_HI, COLOR_TEXT);
    } else {
        gfx_text(g, 34, 49, "TAILLE DU TRAIT", 2, COLOR_TEXT);
        for (unsigned i = 0; i < PAINT_BRUSHES; ++i) {
            Rect r = brush_button(i);
            gfx_fill(g, r, a->paint.brush == i ? COLOR_PANEL_HI : COLOR_PANEL);
            gfx_outline(g, r, a->paint.brush == i ? COLOR_MINT : COLOR_PANEL_HI);
            (void)snprintf(text, sizeof(text), "%u PX", PAINT_DIAMETERS[i]);
            gfx_text(g, r.x + 16, r.y + 6, text, 1, COLOR_TEXT);
            gfx_disc(g, r.x + r.w / 2, r.y + 29, PAINT_DIAMETERS[i] / 2, COLOR_GOLD);
        }
        button(g, edit_cancel, "FERMER", COLOR_PANEL_HI, COLOR_TEXT);
    }
}

/**
 * @brief Composer une boîte de dialogue au-dessus de l'application.
 *
 * Les éditeurs de dessin ont une emprise plus grande. Les autres dialogues partagent les
 * boutons de confirmation, d'annulation et d'emplacement.
 * @param[in] a Contexte des applications en RAM, consulté sans modification. Pointeur
 *   valide.
 * @param[in,out] g Vue de la bande RGB565 et de son tampon accessible en écriture. Pointeur valide.
 */
static void draw_modal(const App *a, Gfx *g)
{
    if (a->modal == MODAL_COLORS || a->modal == MODAL_RGB || a->modal == MODAL_BRUSH) {
        draw_paint_editor(a, g);
        return;
    }
    if (!rect_valid(rect_intersection(overlay, g->clip))) {
        return;
    }
    static const char *const names[] = {
        "", "PALETTE", "ENREGISTRER UN DESSIN", "OUVRIR UN DESSIN",
        "EFFACER LE DESSIN ?", "QUITTER LE PAINT ?", "ENREGISTRER UN MOTIF", "OUVRIR UN MOTIF"
    };
    gfx_fill(g, overlay, COLOR_BG);
    gfx_outline(g, overlay, COLOR_MINT);
    gfx_text(g, overlay.x + 15, overlay.y + 13, names[a->modal], 1, COLOR_TEXT);
    button(g, cancel, "ANNULER", COLOR_PANEL, COLOR_TEXT);
    if (a->modal == MODAL_DRAW_CLEAR || a->modal == MODAL_DRAW_LEAVE) {
        button(g, slot_button(0), "CONFIRMER", COLOR_PINK, COLOR_BG);
        if (a->modal == MODAL_DRAW_LEAVE) {
            gfx_text(g, 48, 141, "LE CANEVAS RESTE EN RAM", 1, COLOR_DIM);
        }
    } else {
        bool music = a->modal == MODAL_MUSIC_SAVE || a->modal == MODAL_MUSIC_OPEN;
        for (unsigned i = 0; i < (music ? SEQ_SLOTS : 2U); ++i) {
            char label[20];
            (void)snprintf(label, sizeof(label), "%s %u", music ? "MOTIF" : "DESSIN", i + 1U);
            uint16_t fill = music && (a->book.present & (1U << i)) != 0U ? COLOR_MINT : COLOR_PANEL_HI;
            button(g, slot_button(i), label, fill, fill == COLOR_MINT ? COLOR_BG : COLOR_TEXT);
        }
    }
}

/*
 * Recomposer une bande RGB565 à partir de l'état App.
 *
 * Choisit l'écran, ajoute le dialogue puis le message. Cette fonction est appelée
 * synchroniquement par renderer_task() avant le départ du DMA. Aucun tampon d'écran
 * complet n'est requis.
 */
void app_render(const void *context, Gfx *g)
{
    const App *a = context;
    gfx_fill(g, g->clip, COLOR_BG);
    switch (a->screen) {
    case SCREEN_SPLASH: draw_splash(g); break;
    case SCREEN_MENU: draw_menu(g); break;
    case SCREEN_PAINT: draw_paint(a, g); break;
    case SCREEN_GROOVE: draw_groove(a, g); break;
    case SCREEN_DIAG: draw_diag(a, g); break;
    case SCREEN_SETTINGS: draw_settings(a, g); break;
    case SCREEN_TOUCH_CAL: draw_calibration(a, g); break;
    case SCREEN_TILT: draw_tilt(a, g); break;
    case SCREEN_RESPONSE: draw_response(a, g); break;
    default: draw_game(a, g); break;
    }
    if (a->modal != MODAL_NONE) {
        draw_modal(a, g);
    }
    if (a->notice != NULL) {
        gfx_fill(g, notice_box, COLOR_BG);
        gfx_outline(g, notice_box, COLOR_GOLD);
        gfx_text(g, notice_box.x + 10, notice_box.y + 10, a->notice, 1, COLOR_GOLD);
    }
}

/**
 * @file pwm_audio.c
 * @brief Transport des rapports cycliques par TIM1 et DMA circulaire.
 * @ingroup obcp_audio
 *
 * Deux demi-tampons de 256 comparaisons alternent sur DMA1 canal 6. Les interruptions
 * comptent les moitiés consommées. La synthèse reste dans la boucle principale et passe
 * par staging avant une copie protégée vers la moitié libre.
 *
 * @par Repères de lecture
 * Les coordonnées écran sont en pixels. Les durées suffixées ms sont en
 * millisecondes. Les adresses du service de stockage sont des offsets en octets.
 */
/** @var ring
 * @brief Anneau DMA de deux fois 256 comparaisons uint16_t.
 */
/** @var staging
 * @brief Demi-bloc de synthèse préparé avant sa copie dans ring.
 */
/** @var epoch
 * @brief Compteur volatile de moitiés consommées, incrémenté par les callbacks DMA.
 */
/** @var dma_error
 * @brief Erreur volatile signalée en interruption et traitée par la boucle principale.
 */
/** @var served
 * @brief Dernière valeur de epoch dont la moitié libre a été traitée.
 */
/** @var missed
 * @brief Nombre cumulé d'échéances et incidents audio.
 */
/** @var running
 * @brief true si le DMA et la sortie complémentaire du timer fonctionnent.
 */
/** @var failed
 * @brief true si le démarrage doit rester suspendu jusqu'à une nouvelle demande audio.
 */

#include "obcp_pwm.h"
#include "main.h"
#include "tim.h"

#include <string.h>

/** @brief Constantes privées de ce module, avec leurs unités. */
enum {
    HALF_SAMPLES = 256, /**< 256 échantillons par moitié, soit 16,384 ms à 15 625 Hz. */
    COPY_GUARD = 8, /**< Marge minimale de huit échantillons avant la reprise DMA. */
};
static uint16_t ring[2 * HALF_SAMPLES];
static uint16_t staging[HALF_SAMPLES];
static volatile uint32_t epoch;
static volatile bool dma_error;
static uint32_t served, missed;
static bool running, failed;

/**
 * @brief Retrouver le handle DMA associé aux mises à jour de TIM1.
 *
 * La liaison hdma de TIM1 est préparée par CubeMX. Ce handle porte aussi les callbacks de
 * demi-transfert et de fin de transfert.
 * @return Pointeur HAL du DMA TIM1_UP, éventuellement NULL avant configuration.
 */
static DMA_HandleTypeDef *dma(void)
{
    return htim1.hdma[TIM_DMA_ID_UPDATE];
}

/**
 * @brief Placer PB1 à zéro en sortie GPIO.
 *
 * Précharge le niveau bas avant de choisir le mode GPIO. Ce chemin est utilisé au repos et
 * avant l'arrêt du transport.
 */
static void drive_low(void)
{
    GPIO_InitTypeDef gpio = {0};
    HAL_GPIO_WritePin(PWM_MOTOR_GPIO_Port, PWM_MOTOR_Pin, GPIO_PIN_RESET);
    gpio.Pin = PWM_MOTOR_Pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(PWM_MOTOR_GPIO_Port, &gpio);
}

/**
 * @brief Relier PB1 à la sortie complémentaire TIM1_CH3N.
 *
 * Le niveau PWM vient ensuite du timer et de son registre CCR3. Le changement de fonction
 * alternative est réalisé une fois le transport préparé.
 */
static void connect_timer(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = PWM_MOTOR_Pin;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF1_TIM1;
    HAL_GPIO_Init(PWM_MOTOR_GPIO_Port, &gpio);
}

/*
 * Préparer l'état de repos de la sortie PWM.
 *
 * L'initialisation démarre avec PB1 en GPIO bas. Le timer et le DMA seront lancés
 * seulement quand l'application demandera de l'audio.
 */
void pwm_audio_init(void)
{
    drive_low();
}

/**
 * @brief Signaler une demi-zone consommée par le DMA.
 *
 * Callback d'interruption très court. L'incrément de epoch permet à la boucle principale
 * de savoir quelle moitié elle peut préparer. Aucune synthèse n'est faite ici.
 * @param[in,out] handle Handle HAL du canal DMA à l’origine du callback. Pointeur valide.
 * @note Contexte interruption. Garder ce traitement court et sans opération flash.
 */
static void half_done(DMA_HandleTypeDef *handle)
{
    (void)handle;
    ++epoch;
}

/**
 * @brief Mémoriser une erreur de transfert audio.
 *
 * Le callback place un indicateur volatile. La tâche principale arrêtera ensuite le
 * transport et comptabilisera l'erreur.
 * @param[in,out] handle Handle HAL du canal DMA à l’origine du callback. Pointeur valide.
 * @note Contexte interruption. Garder ce traitement court et sans opération flash.
 */
static void dma_fault(DMA_HandleTypeDef *handle)
{
    (void)handle;
    dma_error = true;
}

/**
 * @brief Préparer et lancer le timer avec son anneau circulaire.
 *
 * Vérifie ARR et RCR, remplit l'anneau au centre de la PWM, branche les callbacks, démarre
 * le DMA puis la sortie complémentaire et sa requête de mise à jour.
 * @return true si les démarrages DMA et timer ont réussi.
 */
static bool start(void)
{
    DMA_HandleTypeDef *handle = dma();
    if (handle == NULL || htim1.Init.Period != 1023U || htim1.Init.RepetitionCounter != 3U) {
        return false;
    }
    epoch = 0;
    served = 0;
    dma_error = false;
    for (unsigned i = 0; i < 2U * HALF_SAMPLES; ++i) {
        ring[i] = AUDIO_CENTER;
    }
    /* Le DMA alterne deux moitiés. Chaque interruption incrémente epoch ; la synthèse reste dans la boucle principale. */
    handle->XferHalfCpltCallback = half_done;
    handle->XferCpltCallback = half_done;
    handle->XferErrorCallback = dma_fault;
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, AUDIO_CENTER);
    __HAL_TIM_SET_COUNTER(&htim1, 0U);
    htim1.Instance->EGR = TIM_EGR_UG;
    __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_UPDATE);
    if (HAL_DMA_Start_IT(handle, (uint32_t)(uintptr_t)ring,
                         (uint32_t)(uintptr_t)&htim1.Instance->CCR3,
                         2U * HALF_SAMPLES) != HAL_OK) {
        return false;
    }
    if (HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_3) != HAL_OK) {
        (void)HAL_DMA_Abort(handle);
        return false;
    }
    connect_timer();
    __HAL_TIM_ENABLE_DMA(&htim1, TIM_DMA_UPDATE);
    return true;
}

/**
 * @brief Arrêter le transport et revenir au niveau de repos.
 *
 * Déconnecte d'abord PB1, désactive la requête DMA, arrête la sortie PWM puis interrompt
 * le DMA. L'état running est remis à false.
 */
static void stop(void)
{
    drive_low();
    __HAL_TIM_DISABLE_DMA(&htim1, TIM_DMA_UPDATE);
    (void)HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_3);
    (void)HAL_DMA_Abort(dma());
    running = false;
}

/*
 * Produire au bon moment la moitié libre de l'anneau.
 *
 * Compare epoch à served, synthétise dans staging puis relit le compteur DMA. La copie se
 * fait seulement si le DMA utilise toujours l'autre moitié avec une marge de huit
 * échantillons. Les échéances perdues sont comptées.
 */
void pwm_audio_task(Audio *audio, bool needed)
{
    if (!needed) {
        if (running) {
            stop();
        }
        failed = false;
        return;
    }
    if (!running && !failed) {
        running = start();
        failed = !running;
        if (failed) {
            ++missed;
        }
    }
    if (!running) {
        return;
    }
    if (dma_error) {
        ++missed;
        stop();
        failed = true;
        return;
    }

    uint32_t observed = epoch;
    if (observed == served) {
        return;
    }
    missed += observed - served - 1U;
    served = observed;
    /* Après une fin de première moitié, le DMA lit la seconde : la première est donc la moitié à refaire. La parité suit cette alternance. */
    unsigned half = (observed & 1U) != 0U ? 0U : 1U;
    audio_fill(audio, staging, HALF_SAMPLES);

    /* On synthétise dans staging. Avant la copie, vérifier que le DMA
     * lit toujours l'autre moitié et qu'il reste assez de temps. */
    uint32_t left = __HAL_DMA_GET_COUNTER(dma());
    bool writable = half == 0U ? left <= HALF_SAMPLES && left > COPY_GUARD
                                : left > HALF_SAMPLES + COPY_GUARD;
    /* Une interruption survenue pendant le calcul invalide notre autorisation. Le contrôle du compteur ajoute une marge avant la copie. */
    if (epoch == observed && writable) {
        memcpy(&ring[half * HALF_SAMPLES], staging, sizeof(staging));
    } else {
        ++missed;
    }
}

/*
 * Consulter le nombre d'échéances ou erreurs audio.
 *
 * Ce compteur sert à qualifier le comportement lorsque l'écran et le tactile sollicitent
 * aussi le processeur.
 */
uint32_t pwm_audio_missed(void) { return missed; }

/** @file oa_calibration.h
 * @brief Étalonnage coopératif, transactionnel et testable sans HAL.
 * @details L'application conserve ses réglages tant que status != OA_CAL_DONE.
 * Un abandon, une absence de capteur ou un délai dépassé ne les remplace pas.
 * SPDX-License-Identifier: MIT
 */
#ifndef OA_CALIBRATION_H
#define OA_CALIBRATION_H
#include "oa_input.h"
enum {
    OA_CAL_POINTS = 5,
    OA_CAL_SAMPLES = 4,
    OA_CAL_TILT_SAMPLES = 100,
    OA_CAL_POINT_TIMEOUT_MS = 30000,
    OA_CAL_TOTAL_TIMEOUT_MS = 180000,
    OA_CAL_RAW_SPREAD = 160,
    OA_CAL_MAX_ERROR_PX = 18
};
typedef enum {
    OA_CAL_RUNNING,
    OA_CAL_DONE,
    OA_CAL_CANCELLED,
    OA_CAL_TIMEOUT,
    OA_CAL_UNAVAILABLE
} OaCalStatus;
/** @brief Information lisible à afficher, sans remettre silencieusement le compteur à zéro. */
typedef enum {
    OA_CAL_NOTICE_RELEASE,
    OA_CAL_NOTICE_PRESS,
    OA_CAL_NOTICE_UNSTABLE,
    OA_CAL_NOTICE_RETRY,
    OA_CAL_NOTICE_RESTART,
    OA_CAL_NOTICE_STILL
} OaCalNotice;
/** @brief État d'une session ; les sommes signées évitent une perte du signe en inclinaison. */
typedef struct {
    OaSettings candidate;
    OaCalPoint raw[3];
    int32_t sums[3];
    int64_t squares[3];
    uint16_t lo[2], hi[2], count;
    uint32_t started_ms, point_ms, last_sequence;
    uint8_t point, retries;
    OaCalStatus status;
    OaCalNotice notice;
    bool tilt, wait_release, point_complete;
} OaCalibration;
extern const OaCalPoint oa_cal_targets[OA_CAL_POINTS];
/** @brief Démarre une session sans modifier settings ; available est une preuve d'acquisition
 * possible. */
void oa_cal_begin(OaCalibration *session, const OaSettings *settings, bool tilt, bool available,
                  uint32_t now_ms);
/** @brief Applique annulation et délais, même en l'absence de toute nouvelle mesure. */
OaCalStatus oa_cal_watchdog(OaCalibration *session, uint32_t now_ms, bool cancel);
/** @brief Consomme au plus une fois chaque numéro de séquence tactile. */
void oa_cal_touch(OaCalibration *session, const OaTouchSample *sample, uint32_t now_ms);
/** @brief Consomme une mesure fraîche en mg ; l'appelant contrôle la fraîcheur du capteur. */
void oa_cal_tilt(OaCalibration *session, const int16_t mg[3], uint32_t now_ms);
#endif

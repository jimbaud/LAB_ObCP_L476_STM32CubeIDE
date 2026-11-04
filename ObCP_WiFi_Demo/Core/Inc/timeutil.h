/**
 ******************************************************************************
 * @file    timeutil.h
 * @brief   Calculs calendaires (UTC <-> heure locale, heure d'été européenne).
 *          Indépendant du matériel (testé sur PC).
 *
 *  La RTC de la carte reste en temps universel (UTC) ; l'heure locale est
 *  calculée à l'affichage : UTC + fuseau (time.tz) + 1 h en été (time.dst).
 *  « Époque » : nombre de secondes écoulées depuis le 1er janvier 1970 à
 *  0 h UTC ; elle rend les calculs (ajout d'un décalage...) très simples.
 ******************************************************************************
 */
#ifndef TIMEUTIL_H
#define TIMEUTIL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Date et heure (UTC ou locale selon l'usage). */
typedef struct
{
  uint16_t year;   /* 2000..2099 pour la RTC */
  uint8_t  mon;    /* 1..12 */
  uint8_t  day;    /* 1..31 */
  uint8_t  hour;
  uint8_t  min;
  uint8_t  sec;
  uint8_t  wday;   /* 1 = lundi ... 7 = dimanche */
} dt_t;

/** @brief  Date -> secondes depuis le 1er janvier 1970 (« époque »). */
int64_t dt_to_epoch(const dt_t *dt);

/** @brief  Secondes depuis 1970 -> date (jour de la semaine compris). */
void    epoch_to_dt(int64_t t, dt_t *dt);

/** @brief  Jour de la semaine d'une date : 1 = lundi ... 7 = dimanche. */
uint8_t dt_weekday(uint16_t y, uint8_t m, uint8_t d);

/**
 * @brief  Heure d'été de l'Union européenne en vigueur ? Du dernier dimanche
 *         de mars 01:00 UTC au dernier dimanche d'octobre 01:00 UTC.
 * @param  utc  instant en secondes depuis 1970.
 */
bool    eu_dst_active(int64_t utc);

/**
 * @brief  Décalage de l'heure locale par rapport à l'UTC.
 * @param  utc     instant en secondes depuis 1970.
 * @param  tz_min  fuseau horaire en minutes (60 pour Paris).
 * @param  dst_eu  true : ajoute 1 h pendant l'heure d'été européenne.
 * @retval décalage en secondes (3600 en hiver à Paris, 7200 en été).
 */
int32_t local_offset_s(int64_t utc, int16_t tz_min, bool dst_eu);

/** @brief  Écrit la date au format « AAAA-MM-JJ hh:mm:ss ». */
void    dt_format(const dt_t *dt, char *out, size_t len);

/** @brief  Nom français du jour de la semaine (1 = « lundi »). */
const char *dt_wday_fr(uint8_t wday);

#ifdef __cplusplus
}
#endif

#endif /* TIMEUTIL_H */

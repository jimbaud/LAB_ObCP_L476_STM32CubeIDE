/**
 ******************************************************************************
 * @file    timeutil.c
 * @brief   Calculs calendaires (voir timeutil.h). Algorithmes de H. Hinnant
 *          (« days_from_civil » / « civil_from_days »), valables pour le
 *          calendrier grégorien proleptique.
 ******************************************************************************
 */
#include <stdio.h>
#include "timeutil.h"

/* Nombre de jours entre le 1er janvier 1970 et la date y-m-d (négatif avant) */
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d)
{
  int64_t  era;
  unsigned yoe;
  unsigned doy;
  unsigned doe;

  y -= (m <= 2U) ? 1 : 0;
  era = ((y >= 0) ? y : (y - 399)) / 400;
  yoe = (unsigned)(y - (era * 400));
  doy = ((153U * ((m > 2U) ? (m - 3U) : (m + 9U))) + 2U) / 5U + d - 1U;
  doe = (yoe * 365U) + (yoe / 4U) - (yoe / 100U) + doy;
  return (era * 146097) + (int64_t)doe - 719468;
}

/* Opération inverse : date du jour numéro z depuis le 1er janvier 1970 */
static void civil_from_days(int64_t z, int64_t *y, unsigned *m, unsigned *d)
{
  int64_t  era;
  unsigned doe;
  unsigned yoe;
  unsigned doy;
  unsigned mp;

  z += 719468;
  era = ((z >= 0) ? z : (z - 146096)) / 146097;
  doe = (unsigned)(z - (era * 146097));
  yoe = (doe - (doe / 1460U) + (doe / 36524U) - (doe / 146096U)) / 365U;
  doy = doe - ((365U * yoe) + (yoe / 4U) - (yoe / 100U));
  mp  = ((5U * doy) + 2U) / 153U;
  *d  = doy - (((153U * mp) + 2U) / 5U) + 1U;
  *m  = (mp < 10U) ? (mp + 3U) : (mp - 9U);
  *y  = (int64_t)yoe + (era * 400) + ((*m <= 2U) ? 1 : 0);
}

/* Voir timeutil.h (le 1er janvier 1970 était un jeudi) */
uint8_t dt_weekday(uint16_t y, uint8_t m, uint8_t d)
{
  int64_t days = days_from_civil(y, m, d);     /* 1970-01-01 = jeudi */
  int64_t w = (days + 3) % 7;                  /* 0 = lundi           */
  if (w < 0)
  {
    w += 7;
  }
  return (uint8_t)(w + 1);
}

/* Voir timeutil.h */
int64_t dt_to_epoch(const dt_t *dt)
{
  return (days_from_civil(dt->year, dt->mon, dt->day) * 86400) +
         ((int64_t)dt->hour * 3600) + ((int64_t)dt->min * 60) + dt->sec;
}

/* Voir timeutil.h */
void epoch_to_dt(int64_t t, dt_t *dt)
{
  int64_t  days = t / 86400;
  int64_t  rem = t % 86400;
  int64_t  y;
  unsigned m;
  unsigned d;

  if (rem < 0)
  {
    rem += 86400;
    days--;
  }
  civil_from_days(days, &y, &m, &d);
  dt->year = (uint16_t)y;
  dt->mon  = (uint8_t)m;
  dt->day  = (uint8_t)d;
  dt->hour = (uint8_t)(rem / 3600);
  dt->min  = (uint8_t)((rem % 3600) / 60);
  dt->sec  = (uint8_t)(rem % 60);
  dt->wday = dt_weekday(dt->year, dt->mon, dt->day);
}

/* Date (jour du mois) du dernier dimanche d'un mois de 31 jours */
static uint8_t last_sunday_31(uint16_t y, uint8_t m)
{
  uint8_t w = dt_weekday(y, m, 31U);            /* 7 = dimanche */
  return (uint8_t)(31U - (w % 7U));
}

/* Voir timeutil.h */
bool eu_dst_active(int64_t utc)
{
  dt_t    d;
  dt_t    start = { 0 };
  dt_t    end = { 0 };

  epoch_to_dt(utc, &d);
  start.year = d.year;
  start.mon  = 3U;
  start.day  = last_sunday_31(d.year, 3U);
  start.hour = 1U;
  end.year   = d.year;
  end.mon    = 10U;
  end.day    = last_sunday_31(d.year, 10U);
  end.hour   = 1U;
  return (utc >= dt_to_epoch(&start)) && (utc < dt_to_epoch(&end));
}

/* Voir timeutil.h */
int32_t local_offset_s(int64_t utc, int16_t tz_min, bool dst_eu)
{
  int32_t off = (int32_t)tz_min * 60;
  if (dst_eu && eu_dst_active(utc))
  {
    off += 3600;
  }
  return off;
}

/* Voir timeutil.h */
void dt_format(const dt_t *dt, char *out, size_t len)
{
  (void)snprintf(out, len, "%04u-%02u-%02u %02u:%02u:%02u",
                 (unsigned)dt->year, (unsigned)dt->mon, (unsigned)dt->day,
                 (unsigned)dt->hour, (unsigned)dt->min, (unsigned)dt->sec);
}

/* Voir timeutil.h */
const char *dt_wday_fr(uint8_t wday)
{
  static const char *const tab[] = { "?", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi", "dimanche" };
  return (wday <= 7U) ? tab[wday] : tab[0];
}

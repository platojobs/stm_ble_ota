/**
 * @file    sensor_filter.h
 * @brief   Small, allocation-free filters used by the sensor service.
 */

#ifndef SENSOR_FILTER_H
#define SENSOR_FILTER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float ema;
    float alpha;        /* 0 < alpha <= 1, larger tracks faster                  */
    float max_delta;    /* maximum change accepted per sample, <=0 disables      */
    bool  initialized;
} ema_filter_t;

void ema_filter_init(ema_filter_t *filter, float alpha, float max_delta);

/** Add a raw sample, returns the filtered value. */
float ema_filter_update(ema_filter_t *filter, float sample);

void ema_filter_reset(ema_filter_t *filter);

#define MOVING_AVERAGE_WINDOW 8u

typedef struct {
    float    samples[MOVING_AVERAGE_WINDOW];
    uint8_t  index;
    uint8_t  count;
} moving_average_t;

void moving_average_init(moving_average_t *avg);
void moving_average_add(moving_average_t *avg, float value);
float moving_average_value(const moving_average_t *avg);

#endif /* SENSOR_FILTER_H */

#include "sensor_filter.h"

#include <string.h>

void ema_filter_init(ema_filter_t *filter, float alpha, float max_delta)
{
    memset(filter, 0, sizeof(*filter));
    if (alpha <= 0.0f) {
        alpha = 0.1f;
    } else if (alpha > 1.0f) {
        alpha = 1.0f;
    }
    filter->alpha = alpha;
    filter->max_delta = max_delta;
}

void ema_filter_reset(ema_filter_t *filter)
{
    filter->ema = 0.0f;
    filter->initialized = false;
}

float ema_filter_update(ema_filter_t *filter, float sample)
{
    if (!filter->initialized) {
        filter->ema = sample;
        filter->initialized = true;
        return sample;
    }

    if (filter->max_delta > 0.0f) {
        float delta = sample - filter->ema;
        if (delta > filter->max_delta) {
            sample = filter->ema + filter->max_delta;
        } else if (delta < -filter->max_delta) {
            sample = filter->ema - filter->max_delta;
        }
    }

    filter->ema += filter->alpha * (sample - filter->ema);
    return filter->ema;
}

void moving_average_init(moving_average_t *avg)
{
    memset(avg, 0, sizeof(*avg));
}

void moving_average_add(moving_average_t *avg, float value)
{
    if (avg->count < MOVING_AVERAGE_WINDOW) {
        avg->samples[avg->index] = value;
        avg->count++;
        avg->index = (uint8_t)((avg->index + 1u) % MOVING_AVERAGE_WINDOW);
        return;
    }
    /* Replace the oldest sample in the circular buffer. */
    avg->samples[avg->index] = value;
    avg->index = (uint8_t)((avg->index + 1u) % MOVING_AVERAGE_WINDOW);
}

float moving_average_value(const moving_average_t *avg)
{
    if (avg->count == 0u) {
        return 0.0f;
    }
    float sum = 0.0f;
    for (uint8_t i = 0; i < avg->count; ++i) {
        sum += avg->samples[i];
    }
    return sum / (float)avg->count;
}

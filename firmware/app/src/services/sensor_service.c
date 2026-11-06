/**
 * @file    sensor_service.c
 * @brief   SHT30 + ADC light sampling with EMA / moving-average filtering.
 *
 * Temperature and humidity use an EMA with a 1.5 degC / 3 %RH per-sample
 * slew limit, which suppresses occasional CRC-valid outliers. The light
 * channel uses an 8-sample moving average. Reporting happens once per second.
 */

#include "sensor_service.h"
#include "sensor_filter.h"
#include "sht30.h"
#include "bsp_adc.h"
#include "bsp_time.h"
#include "bsp_i2c.h"
#include "app_config.h"

#include <string.h>
#include <stdbool.h>

#define TEMP_ALPHA  0.25f
#define TEMP_DELTA  1.5f    /* degC */
#define HUM_ALPHA   0.25f
#define HUM_DELTA   3.0f    /* %RH  */

static ema_filter_t g_temp_filter;
static ema_filter_t g_hum_filter;
static moving_average_t g_light_average;

static sensor_notify_payload_t g_last;
static sensor_report_cb_t g_report_cb;
static void *g_report_context;
static uint32_t g_next_sample_ms;
static bool g_initialized;

void sensor_service_init(void)
{
    memset(&g_last, 0, sizeof(g_last));
    g_last.payload_type = 0x01u;

    ema_filter_init(&g_temp_filter, TEMP_ALPHA, TEMP_DELTA);
    ema_filter_init(&g_hum_filter, HUM_ALPHA, HUM_DELTA);
    moving_average_init(&g_light_average);

    (void)bsp_i2c_init();
    (void)bsp_adc_init();
    (void)sht30_init();

    g_next_sample_ms = bsp_time_ms();
    g_initialized = true;
}

void sensor_service_set_report_callback(sensor_report_cb_t callback,
                                        void *context)
{
    g_report_cb = callback;
    g_report_context = context;
}

static void sample_once(uint32_t now_ms)
{
    int32_t temperature_centi = 0;
    int32_t humidity_centi = 0;
    uint16_t light_mv = 0;
    uint8_t valid = 0u;

    if (sht30_read(&temperature_centi, &humidity_centi) == 0) {
        float filtered_temp = ema_filter_update(&g_temp_filter,
                                               (float)temperature_centi / 100.0f);
        float filtered_hum = ema_filter_update(&g_hum_filter,
                                               (float)humidity_centi / 100.0f);
        g_last.temperature_centi = (int32_t)(filtered_temp * 100.0f);
        g_last.humidity_centi = (int32_t)(filtered_hum * 100.0f);
        valid |= 0x03u; /* temperature + humidity */
    }

    if (bsp_adc_read_mv(&light_mv) == 0) {
        moving_average_add(&g_light_average, (float)light_mv);
        g_last.light_mv = (uint16_t)moving_average_value(&g_light_average);
        valid |= 0x04u;
    }

    g_last.valid_mask = valid;
    g_last.uptime_ms = now_ms;

    if (g_report_cb != NULL) {
        g_report_cb(&g_last, g_report_context);
    }
}

void sensor_service_tick(uint32_t now_ms)
{
    if (!g_initialized) {
        return;
    }
    if ((int32_t)(now_ms - g_next_sample_ms) < 0) {
        return;
    }
    g_next_sample_ms = now_ms + SENSOR_PERIOD_MS;
    sample_once(now_ms);
}

const sensor_notify_payload_t *sensor_service_last(void)
{
    return &g_last;
}

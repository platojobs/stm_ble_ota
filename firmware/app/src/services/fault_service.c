// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

#include "fault_service.h"
#include "bsp_time.h"

static error_log_t g_log;

void fault_service_init(void)
{
    error_log_init(&g_log);
}

void fault_service_raise(err_code_t code, uint32_t detail)
{
    (void)error_log_push(&g_log, code, detail, bsp_time_ms());
}

uint16_t fault_service_count(void)
{
    return error_log_count(&g_log);
}

err_code_t fault_service_last_code(void)
{
    return error_log_last_code(&g_log);
}

uint32_t fault_service_json(char *out, uint32_t capacity)
{
    return error_log_to_json(&g_log, out, capacity);
}

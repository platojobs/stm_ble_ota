// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

/**
 * @file    err_code.c
 * @brief   Lookup table and domain mappings for the shared error codes.
 */

#include "err_code.h"

#include <stddef.h>

typedef struct {
    err_code_t code;
    const char *name;
    const char *domain;
} err_code_entry_t;

static const err_code_entry_t k_codes[] = {
    { ERR_CODE_OK,                       "OK",                       "ok"      },

    { ERR_CODE_INVALID_ARG,              "INVALID_ARG",              "arg"     },
    { ERR_CODE_NULL_POINTER,             "NULL_POINTER",             "arg"     },
    { ERR_CODE_BUFFER_TOO_SMALL,         "BUFFER_TOO_SMALL",         "arg"     },
    { ERR_CODE_MALFORMED_FRAME,          "MALFORMED_FRAME",          "arg"     },
    { ERR_CODE_BAD_CRC,                  "BAD_CRC",                  "arg"     },
    { ERR_CODE_UNSUPPORTED,              "UNSUPPORTED",              "arg"     },
    { ERR_CODE_LINE_TOO_LONG,            "LINE_TOO_LONG",            "arg"     },

    { ERR_CODE_FLASH_ERASE,              "FLASH_ERASE",              "flash"   },
    { ERR_CODE_FLASH_PROGRAM,            "FLASH_PROGRAM",            "flash"   },
    { ERR_CODE_FLASH_READ,               "FLASH_READ",               "flash"   },
    { ERR_CODE_STORE_EMPTY,              "STORE_EMPTY",              "flash"   },
    { ERR_CODE_STORE_CORRUPT,            "STORE_CORRUPT",            "flash"   },
    { ERR_CODE_STORE_FULL,               "STORE_FULL",               "flash"   },

    { ERR_CODE_OTA_BAD_STATE,            "OTA_BAD_STATE",            "ota"     },
    { ERR_CODE_OTA_BAD_SIZE,             "OTA_BAD_SIZE",             "ota"     },
    { ERR_CODE_OTA_BAD_CRC,              "OTA_BAD_CRC",              "ota"     },
    { ERR_CODE_OTA_OUT_OF_ORDER,         "OTA_OUT_OF_ORDER",         "ota"     },
    { ERR_CODE_OTA_TIMEOUT,              "OTA_TIMEOUT",              "ota"     },
    { ERR_CODE_OTA_ABORTED,              "OTA_ABORTED",              "ota"     },
    { ERR_CODE_OTA_BAD_VERSION,          "OTA_BAD_VERSION",          "ota"     },

    { ERR_CODE_IMAGE_SIZE,               "IMAGE_SIZE",               "image"   },
    { ERR_CODE_IMAGE_VECTOR,             "IMAGE_VECTOR",             "image"   },
    { ERR_CODE_IMAGE_CRC,                "IMAGE_CRC",                "image"   },
    { ERR_CODE_IMAGE_SHA256,             "IMAGE_SHA256",             "image"   },
    { ERR_CODE_IMAGE_SIGNATURE,          "IMAGE_SIGNATURE",          "image"   },

    { ERR_CODE_AT_TIMEOUT,               "AT_TIMEOUT",               "network" },
    { ERR_CODE_AT_ERROR,                 "AT_ERROR",                 "network" },
    { ERR_CODE_WIFI_NOT_PROVISIONED,     "WIFI_NOT_PROVISIONED",     "network" },
    { ERR_CODE_WIFI_CONNECT_FAILED,      "WIFI_CONNECT_FAILED",      "network" },
    { ERR_CODE_BLE_UNAVAILABLE,          "BLE_UNAVAILABLE",          "network" },

    { ERR_CODE_I2C_ERROR,                "I2C_ERROR",                "sensor"  },
    { ERR_CODE_SENSOR_BUSY,              "SENSOR_BUSY",              "sensor"  },
    { ERR_CODE_SENSOR_RANGE,             "SENSOR_RANGE",             "sensor"  },

    { ERR_CODE_WATCHDOG_RESET,           "WATCHDOG_RESET",           "system"  },
    { ERR_CODE_BOOT_FAILED,              "BOOT_FAILED",              "system"  },
    { ERR_CODE_ROLLBACK,                 "ROLLBACK",                 "system"  },
    { ERR_CODE_NOT_IMPLEMENTED,          "NOT_IMPLEMENTED",          "system"  },
    { ERR_CODE_SELFTEST_FAILED,          "SELFTEST_FAILED",          "system"  },
};

#define ERR_CODE_ENTRY_COUNT \
    (sizeof(k_codes) / sizeof(k_codes[0]))

static const err_code_entry_t *lookup(err_code_t code)
{
    for (size_t i = 0; i < ERR_CODE_ENTRY_COUNT; ++i) {
        if (k_codes[i].code == code) {
            return &k_codes[i];
        }
    }
    return NULL;
}

const char *err_code_name(err_code_t code)
{
    const err_code_entry_t *entry = lookup(code);
    return (entry != NULL) ? entry->name : "UNKNOWN";
}

const char *err_code_domain_name(err_code_t code)
{
    const err_code_entry_t *entry = lookup(code);
    return (entry != NULL) ? entry->domain : "unknown";
}

bool err_code_is_known(err_code_t code)
{
    return lookup(code) != NULL;
}

err_code_t err_code_from_ota_reason(ota_reason_t reason)
{
    switch (reason) {
    case OTA_REASON_NONE:          return ERR_CODE_OK;
    case OTA_REASON_BAD_STATE:     return ERR_CODE_OTA_BAD_STATE;
    case OTA_REASON_BAD_SIZE:      return ERR_CODE_OTA_BAD_SIZE;
    case OTA_REASON_BAD_CRC:       return ERR_CODE_OTA_BAD_CRC;
    case OTA_REASON_OUT_OF_ORDER:  return ERR_CODE_OTA_OUT_OF_ORDER;
    case OTA_REASON_FLASH_ERROR:   return ERR_CODE_FLASH_PROGRAM;
    case OTA_REASON_TIMEOUT:       return ERR_CODE_OTA_TIMEOUT;
    case OTA_REASON_ABORTED:       return ERR_CODE_OTA_ABORTED;
    case OTA_REASON_BAD_VERSION:   return ERR_CODE_OTA_BAD_VERSION;
    case OTA_REASON_BAD_SIGNATURE: return ERR_CODE_IMAGE_SIGNATURE;
    default:                       return ERR_CODE_INVALID_ARG;
    }
}

err_code_t err_code_from_image_result(image_check_result_t result)
{
    switch (result) {
    case IMAGE_OK:            return ERR_CODE_OK;
    case IMAGE_ERR_SIZE:      return ERR_CODE_IMAGE_SIZE;
    case IMAGE_ERR_VECTOR:    return ERR_CODE_IMAGE_VECTOR;
    case IMAGE_ERR_CRC:       return ERR_CODE_IMAGE_CRC;
    case IMAGE_ERR_FLASH:     return ERR_CODE_FLASH_READ;
    case IMAGE_ERR_SHA256:    return ERR_CODE_IMAGE_SHA256;
    case IMAGE_ERR_SIGNATURE: return ERR_CODE_IMAGE_SIGNATURE;
    default:                  return ERR_CODE_INVALID_ARG;
    }
}

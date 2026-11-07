// SPDX-License-Identifier: MIT
// Copyright (c) 2026 platojobs

#include "ota_metadata.h"
#include "flash_layout.h"

#include <string.h>

void ota_metadata_default(ota_metadata_t *meta)
{
    memset(meta, 0, sizeof(*meta));
    meta->magic = OTA_METADATA_MAGIC;
    meta->layout_version = OTA_VERSION_ID;
    meta->state = OTA_STATE_IDLE;
    meta->boot_slot = FLASH_SLOT_A_START;
    meta->chunk_size = OTA_RESUME_CHUNK_SIZE;
}

bool ota_metadata_valid(const ota_metadata_t *meta)
{
    if (meta == NULL) {
        return false;
    }
    if (meta->magic != OTA_METADATA_MAGIC || meta->layout_version != OTA_VERSION_ID) {
        return false;
    }
    if (meta->state > (uint32_t)OTA_STATE_ERROR) {
        return false;
    }
    if (meta->image_size > FLASH_IMAGE_MAX_SIZE) {
        return false;
    }
    if (meta->received_bytes > meta->image_size) {
        return false;
    }
    if (meta->chunk_size != OTA_RESUME_CHUNK_SIZE) {
        return false;
    }
    if (meta->chunk_count > OTA_RESUME_MAX_CHUNKS) {
        return false;
    }
    if (meta->boot_slot != FLASH_SLOT_A_START) {
        return false;
    }
    return true;
}

int ota_metadata_read(const record_store_t *store, ota_metadata_t *out)
{
    ota_metadata_t meta;
    record_info_t info;

    if (out == NULL) {
        return -1;
    }
    if (record_store_load(store, &meta, sizeof(meta), &info) != 0) {
        return -1;
    }
    if (!ota_metadata_valid(&meta)) {
        return -1;
    }
    *out = meta;
    return 0;
}

int ota_metadata_write(record_store_t *store, const ota_metadata_t *meta)
{
    uint32_t sequence = 0u;

    if (meta == NULL) {
        return -1;
    }
    ota_metadata_t copy = *meta;
    copy.magic = OTA_METADATA_MAGIC;
    copy.layout_version = OTA_VERSION_ID;
    if (!ota_metadata_valid(&copy)) {
        return -1;
    }
    return record_store_save(store, &copy, sizeof(copy), &sequence);
}

void ota_metadata_load_or_default(const record_store_t *store, ota_metadata_t *out,
                                  bool *found)
{
    bool ok = (ota_metadata_read(store, out) == 0);
    if (!ok) {
        ota_metadata_default(out);
    }
    if (found != NULL) {
        *found = ok;
    }
}

bool ota_metadata_image_described(const ota_metadata_t *meta)
{
    return meta != NULL && meta->image_size != 0u;
}

void ota_metadata_apply_backup(ota_metadata_t *meta)
{
    if (meta == NULL) {
        return;
    }
    meta->image_size = meta->backup_size;
    meta->image_crc32 = meta->backup_crc32;
    meta->image_version = meta->backup_version;
    memcpy(meta->image_sha256, meta->backup_sha256, sizeof(meta->image_sha256));
    memcpy(meta->image_signature, meta->backup_signature,
           sizeof(meta->image_signature));

    meta->running_size = meta->backup_size;
    meta->running_crc32 = meta->backup_crc32;
    meta->running_version = meta->backup_version;
    memcpy(meta->running_sha256, meta->backup_sha256,
           sizeof(meta->running_sha256));
    memcpy(meta->running_signature, meta->backup_signature,
           sizeof(meta->running_signature));

    meta->backup_valid = 0u;
}

void ota_metadata_promote_running(ota_metadata_t *meta)
{
    if (meta == NULL) {
        return;
    }
    meta->running_size = meta->image_size;
    meta->running_crc32 = meta->image_crc32;
    meta->running_version = meta->image_version;
    memcpy(meta->running_sha256, meta->image_sha256,
           sizeof(meta->running_sha256));
    memcpy(meta->running_signature, meta->image_signature,
           sizeof(meta->running_signature));
}

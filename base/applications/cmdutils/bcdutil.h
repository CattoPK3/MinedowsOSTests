/*
 * PROJECT:     LiberNT boot configuration utilities
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared BCD store and device helpers
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#pragma once

#include <windows.h>

#define BCD_PARTITION_DEVICE_SIZE 0x48

BOOLEAN
BcdBuildPartitionDevice(
    _In_ PCWSTR Path,
    _Out_writes_bytes_(BCD_PARTITION_DEVICE_SIZE) BYTE *Device);

BOOLEAN
BcdOpenStoreFile(
    _In_ PCWSTR Path,
    _In_ BOOLEAN Writable,
    _Out_ PHKEY Store);

BOOLEAN
BcdGetDefaultObjectId(
    _In_ HKEY Store,
    _Out_writes_z_(Chars) PWSTR ObjectId,
    _In_ ULONG Chars);

BOOLEAN
BcdSetElementDevice(
    _In_ HKEY Store,
    _In_ PCWSTR ObjectId,
    _In_ PCWSTR ElementId,
    _In_reads_bytes_(BCD_PARTITION_DEVICE_SIZE) const BYTE *Device);

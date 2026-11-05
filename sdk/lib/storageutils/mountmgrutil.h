/*
 * PROJECT:     LiberNT Storage Utilities
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared Mount Manager helpers for drive-letter assignment/removal.
 */

#ifndef REACTOS_STORAGEUTILS_MOUNTMGRUTIL_H
#define REACTOS_STORAGEUTILS_MOUNTMGRUTIL_H

#include <windef.h>

#ifdef __cplusplus
extern "C" {
#endif

BOOL
StorageUtilAssignDriveLetter(
    _In_z_ PCWSTR DeviceName,
    _In_ WCHAR DriveLetter);

BOOL
StorageUtilAssignNextDriveLetter(
    _In_z_ PCWSTR DeviceName,
    _Out_ PWCHAR DriveLetter);

BOOL
StorageUtilGetDriveLetter(
    _In_z_ PCWSTR DeviceName,
    _Out_ PWCHAR DriveLetter);

BOOL
StorageUtilDeleteDriveLetter(
    _In_ WCHAR DriveLetter);

BOOL
StorageUtilAssignDriveNumber(
    _In_z_ PCWSTR DeviceName,
    _In_z_ PCWSTR DriveNumber);

BOOL
StorageUtilGetDriveNumber(
    _In_z_ PCWSTR DeviceName,
    _Outptr_result_maybenull_z_ PWSTR *DriveNumber);

BOOL
StorageUtilDeleteDriveNumber(
    _In_z_ PCWSTR DriveNumber);

#ifdef __cplusplus
}
#endif

#endif /* REACTOS_STORAGEUTILS_MOUNTMGRUTIL_H */

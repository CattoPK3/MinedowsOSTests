/*
 * PROJECT:     LiberNT boot configuration utilities
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared BCD store and device helpers
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#include <windows.h>
#include <winioctl.h>
#include <ntddstor.h>
#include <strsafe.h>
#include "bcdutil.h"

#define BCD_ELEMENT_HEADER_SIZE 16
#define BCD_PARTITION_DEVICE_TYPE 6
#define BCD_DEVICE_LOCAL 0
#define BCD_PARTITION_GPT 0
#define BCD_PARTITION_MBR 1
#define BCD_KEY_ROOT L"Objects\\"

BOOLEAN
BcdBuildPartitionDevice(
    _In_ PCWSTR Path,
    _Out_writes_bytes_(BCD_PARTITION_DEVICE_SIZE) BYTE *Device)
{
    WCHAR VolumeRoot[MAX_PATH];
    WCHAR VolumeName[MAX_PATH];
    WCHAR DiskPath[64];
    HANDLE Volume = INVALID_HANDLE_VALUE;
    HANDLE Disk = INVALID_HANDLE_VALUE;
    PDRIVE_LAYOUT_INFORMATION_EX Layout = NULL;
    STORAGE_DEVICE_NUMBER DeviceNumber;
    DWORD BytesReturned, BufferSize = 64 * 1024;
    ULONG i;
    BOOLEAN Success = FALSE;

    if (!GetVolumePathNameW(Path, VolumeRoot, ARRAYSIZE(VolumeRoot)))
    {
        return FALSE;
    }

    if (VolumeRoot[0] && VolumeRoot[1] == L':' && VolumeRoot[2] == L'\\')
    {
        if (FAILED(StringCchPrintfW(VolumeName,
                                    ARRAYSIZE(VolumeName),
                                    L"\\\\.\\%c:",
                                    VolumeRoot[0])))
        {
            return FALSE;
        }
    }
    else
    {
        SIZE_T Chars;

        if (!GetVolumeNameForVolumeMountPointW(VolumeRoot, VolumeName, ARRAYSIZE(VolumeName)))
            return FALSE;

        Chars = wcslen(VolumeName);
        if (Chars && VolumeName[Chars - 1] == L'\\')
            VolumeName[Chars - 1] = UNICODE_NULL;
    }

    Volume = CreateFileW(VolumeName,
                         GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         0,
                         NULL);
    if (Volume == INVALID_HANDLE_VALUE)
        goto Cleanup;

    if (!DeviceIoControl(Volume,
                         IOCTL_STORAGE_GET_DEVICE_NUMBER,
                         NULL,
                         0,
                         &DeviceNumber,
                         sizeof(DeviceNumber),
                         &BytesReturned,
                         NULL))
    {
        goto Cleanup;
    }

    if (FAILED(StringCchPrintfW(DiskPath,
                                ARRAYSIZE(DiskPath),
                                L"\\\\.\\PhysicalDrive%lu",
                                DeviceNumber.DeviceNumber)))
    {
        goto Cleanup;
    }

    Disk = CreateFileW(DiskPath,
                       GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL,
                       OPEN_EXISTING,
                       0,
                       NULL);
    if (Disk == INVALID_HANDLE_VALUE)
        goto Cleanup;

    Layout = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, BufferSize);
    if (!Layout)
        goto Cleanup;

    if (!DeviceIoControl(Disk,
                         IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
                         NULL,
                         0,
                         Layout,
                         BufferSize,
                         &BytesReturned,
                         NULL))
    {
        goto Cleanup;
    }

    for (i = 0; i < Layout->PartitionCount; i++)
    {
        PPARTITION_INFORMATION_EX Partition = &Layout->PartitionEntry[i];
        DWORD DeviceType = BCD_PARTITION_DEVICE_TYPE;
        DWORD DeviceSize = BCD_PARTITION_DEVICE_SIZE;
        DWORD DeviceStyle;
        DWORD DeviceLocal = BCD_DEVICE_LOCAL;
        ULONGLONG PartitionOffset;

        if (Partition->PartitionNumber != DeviceNumber.PartitionNumber)
            continue;

        ZeroMemory(Device, BCD_PARTITION_DEVICE_SIZE);
        CopyMemory(Device + 0x00, &DeviceType, sizeof(DeviceType));
        CopyMemory(Device + 0x08, &DeviceSize, sizeof(DeviceSize));
        CopyMemory(Device + 0x20, &DeviceLocal, sizeof(DeviceLocal));

        if (Partition->PartitionStyle == PARTITION_STYLE_GPT)
        {
            DeviceStyle = BCD_PARTITION_GPT;
            CopyMemory(Device + 0x24, &DeviceStyle, sizeof(DeviceStyle));
            CopyMemory(Device + 0x10, &Partition->Gpt.PartitionId, sizeof(GUID));
            CopyMemory(Device + 0x28, &Layout->Gpt.DiskId, sizeof(GUID));
            Success = TRUE;
        }
        else if (Partition->PartitionStyle == PARTITION_STYLE_MBR)
        {
            DeviceStyle = BCD_PARTITION_MBR;
            CopyMemory(Device + 0x24, &DeviceStyle, sizeof(DeviceStyle));
            PartitionOffset = (ULONGLONG)Partition->StartingOffset.QuadPart;
            CopyMemory(Device + 0x10, &PartitionOffset, sizeof(PartitionOffset));
            CopyMemory(Device + 0x28, &Layout->Mbr.Signature, sizeof(Layout->Mbr.Signature));
            Success = TRUE;
        }

        break;
    }

Cleanup:
    if (Layout)
        HeapFree(GetProcessHeap(), 0, Layout);
    if (Disk != INVALID_HANDLE_VALUE)
        CloseHandle(Disk);
    if (Volume != INVALID_HANDLE_VALUE)
        CloseHandle(Volume);
    return Success;
}

BOOLEAN
BcdOpenStoreFile(
    _In_ PCWSTR Path,
    _In_ BOOLEAN Writable,
    _Out_ PHKEY Store)
{
    REGSAM Access = KEY_READ;

    if (Writable)
        Access |= KEY_WRITE;

    return RegLoadAppKeyW(Path, Store, Access, 0, 0) == ERROR_SUCCESS;
}

BOOLEAN
BcdGetDefaultObjectId(
    _In_ HKEY Store,
    _Out_writes_z_(Chars) PWSTR ObjectId,
    _In_ ULONG Chars)
{
    static const WCHAR KeyPath[] =
        L"Objects\\{9dea862c-5cdd-4e70-acc1-f32b344d4795}\\Elements\\23000003";
    WCHAR Data[64];
    DWORD Type, Size = sizeof(Data);
    HKEY Key;
    LSTATUS Status;

    Status = RegOpenKeyExW(Store, KeyPath, 0, KEY_QUERY_VALUE, &Key);
    if (Status != ERROR_SUCCESS)
        return FALSE;

    Status = RegQueryValueExW(Key, L"Element", NULL, &Type, (BYTE *)Data, &Size);
    RegCloseKey(Key);
    if (Status != ERROR_SUCCESS ||
        Type != REG_SZ ||
        Size < sizeof(WCHAR) ||
        Size > sizeof(Data) ||
        Data[Size / sizeof(WCHAR) - 1] != UNICODE_NULL)
    {
        return FALSE;
    }

    return SUCCEEDED(StringCchCopyNW(ObjectId,
                                     Chars,
                                     Data,
                                     Size / sizeof(WCHAR)));
}

BOOLEAN
BcdSetElementDevice(
    _In_ HKEY Store,
    _In_ PCWSTR ObjectId,
    _In_ PCWSTR ElementId,
    _In_reads_bytes_(BCD_PARTITION_DEVICE_SIZE) const BYTE *Device)
{
    WCHAR KeyPath[128];
    BYTE Data[BCD_ELEMENT_HEADER_SIZE + BCD_PARTITION_DEVICE_SIZE];
    DWORD Type, Size = sizeof(Data);
    HKEY Key;
    LSTATUS Status;

    if (FAILED(StringCchPrintfW(KeyPath,
                                ARRAYSIZE(KeyPath),
                                L"%s%s\\Elements\\%s",
                                BCD_KEY_ROOT,
                                ObjectId,
                                ElementId)))
    {
        return FALSE;
    }

    Status = RegOpenKeyExW(Store, KeyPath, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &Key);
    if (Status != ERROR_SUCCESS)
        return FALSE;

    Status = RegQueryValueExW(Key, L"Element", NULL, &Type, Data, &Size);
    if (Status == ERROR_SUCCESS && Type == REG_BINARY && Size >= BCD_ELEMENT_HEADER_SIZE)
    {
        CopyMemory(Data + BCD_ELEMENT_HEADER_SIZE, Device, BCD_PARTITION_DEVICE_SIZE);
        Size = sizeof(Data);
        Status = RegSetValueExW(Key, L"Element", 0, REG_BINARY, Data, Size);
    }
    else
    {
        Status = ERROR_INVALID_DATA;
    }

    RegCloseKey(Key);
    return Status == ERROR_SUCCESS;
}

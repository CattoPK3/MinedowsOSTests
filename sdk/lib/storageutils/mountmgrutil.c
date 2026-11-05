/*
 * PROJECT:     ReactOS Storage Utilities
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Shared Mount Manager helpers for drive-letter assignment/removal.
 */

#include "mountmgrutil.h"

#include <winbase.h>
#include <winioctl.h>
#include <strsafe.h>
#include <mountmgr.h>
#include <wctype.h>
#include <limits.h>

static HANDLE
StorageUtilOpenMountManager(VOID)
{
    return CreateFileW(MOUNTMGR_DOS_DEVICE_NAME,
                       GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
}

static BOOL
StorageUtilBuildDriveNumberLink(
    _In_z_ PCWSTR DriveNumber,
    _Outptr_result_z_ PWSTR *DosDeviceName,
    _Out_ PULONG DosDeviceNameLength)
{
    static const WCHAR Prefix[] = L"\\DosDevices\\";
    SIZE_T NumberLength;
    SIZE_T NameChars;
    SIZE_T Index;
    PWSTR Name;

    if (DriveNumber == NULL || DriveNumber[0] == UNICODE_NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    NumberLength = wcslen(DriveNumber);
    for (Index = 0; Index < NumberLength; Index++)
    {
        if (DriveNumber[Index] < L'0' || DriveNumber[Index] > L'9')
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
    }

    NameChars = ARRAYSIZE(Prefix) - 1 + NumberLength + 1;
    if (NameChars > USHRT_MAX / sizeof(WCHAR))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Name = HeapAlloc(GetProcessHeap(), 0, (NameChars + 1) * sizeof(WCHAR));
    if (Name == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    CopyMemory(Name, Prefix, (ARRAYSIZE(Prefix) - 1) * sizeof(WCHAR));
    CopyMemory(Name + ARRAYSIZE(Prefix) - 1, DriveNumber, NumberLength * sizeof(WCHAR));
    Name[NameChars - 1] = L':';
    Name[NameChars] = UNICODE_NULL;
    *DosDeviceName = Name;
    *DosDeviceNameLength = (ULONG)(NameChars * sizeof(WCHAR));
    return TRUE;
}

BOOL
StorageUtilAssignDriveLetter(
    _In_z_ PCWSTR DeviceName,
    _In_ WCHAR DriveLetter)
{
    WCHAR DosDeviceName[30];
    ULONG DosDeviceNameLength;
    ULONG DeviceNameLength;
    ULONG InputBufferLength;
    PMOUNTMGR_CREATE_POINT_INPUT InputBuffer;
    HANDLE MountMgrHandle;
    DWORD BytesReturned;
    BOOL Success;

    if (DeviceName == NULL || DriveLetter == UNICODE_NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    StringCchPrintfW(DosDeviceName, ARRAYSIZE(DosDeviceName), L"\\DosDevices\\%c:", towupper(DriveLetter));
    DosDeviceNameLength = (ULONG)(wcslen(DosDeviceName) * sizeof(WCHAR));
    DeviceNameLength = (ULONG)(wcslen(DeviceName) * sizeof(WCHAR));

    InputBufferLength = DosDeviceNameLength + DeviceNameLength + sizeof(MOUNTMGR_CREATE_POINT_INPUT);
    InputBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, InputBufferLength);
    if (InputBuffer == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    InputBuffer->SymbolicLinkNameOffset = sizeof(MOUNTMGR_CREATE_POINT_INPUT);
    InputBuffer->SymbolicLinkNameLength = DosDeviceNameLength;
    InputBuffer->DeviceNameOffset = sizeof(MOUNTMGR_CREATE_POINT_INPUT) + DosDeviceNameLength;
    InputBuffer->DeviceNameLength = DeviceNameLength;
    CopyMemory((PBYTE)InputBuffer + InputBuffer->SymbolicLinkNameOffset,
               DosDeviceName,
               DosDeviceNameLength);
    CopyMemory((PBYTE)InputBuffer + InputBuffer->DeviceNameOffset,
               DeviceName,
               DeviceNameLength);

    MountMgrHandle = StorageUtilOpenMountManager();
    if (MountMgrHandle == INVALID_HANDLE_VALUE)
    {
        HeapFree(GetProcessHeap(), 0, InputBuffer);
        return FALSE;
    }

    BytesReturned = 0;
    Success = DeviceIoControl(MountMgrHandle,
                              IOCTL_MOUNTMGR_CREATE_POINT,
                              InputBuffer,
                              InputBufferLength,
                              NULL,
                              0,
                              &BytesReturned,
                              NULL);
    CloseHandle(MountMgrHandle);
    HeapFree(GetProcessHeap(), 0, InputBuffer);
    return Success;
}

BOOL
StorageUtilAssignDriveNumber(
    _In_z_ PCWSTR DeviceName,
    _In_z_ PCWSTR DriveNumber)
{
    PWSTR DosDeviceName;
    ULONG DosDeviceNameLength;
    ULONG DeviceNameLength;
    ULONG InputBufferLength;
    PMOUNTMGR_CREATE_POINT_INPUT InputBuffer;
    HANDLE MountMgrHandle;
    DWORD BytesReturned;
    BOOL Success;

    if (DeviceName == NULL ||
        !StorageUtilBuildDriveNumberLink(DriveNumber, &DosDeviceName, &DosDeviceNameLength))
        return FALSE;

    DeviceNameLength = (ULONG)(wcslen(DeviceName) * sizeof(WCHAR));
    if (DeviceNameLength > USHRT_MAX ||
        DosDeviceNameLength > ULONG_MAX - DeviceNameLength - sizeof(MOUNTMGR_CREATE_POINT_INPUT))
    {
        HeapFree(GetProcessHeap(), 0, DosDeviceName);
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    InputBufferLength = DosDeviceNameLength + DeviceNameLength + sizeof(MOUNTMGR_CREATE_POINT_INPUT);
    InputBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, InputBufferLength);
    if (InputBuffer == NULL)
    {
        HeapFree(GetProcessHeap(), 0, DosDeviceName);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    InputBuffer->SymbolicLinkNameOffset = sizeof(MOUNTMGR_CREATE_POINT_INPUT);
    InputBuffer->SymbolicLinkNameLength = (USHORT)DosDeviceNameLength;
    InputBuffer->DeviceNameOffset = sizeof(MOUNTMGR_CREATE_POINT_INPUT) + DosDeviceNameLength;
    InputBuffer->DeviceNameLength = (USHORT)DeviceNameLength;
    CopyMemory((PBYTE)InputBuffer + InputBuffer->SymbolicLinkNameOffset,
               DosDeviceName,
               DosDeviceNameLength);
    CopyMemory((PBYTE)InputBuffer + InputBuffer->DeviceNameOffset,
               DeviceName,
               DeviceNameLength);
    HeapFree(GetProcessHeap(), 0, DosDeviceName);

    MountMgrHandle = StorageUtilOpenMountManager();
    if (MountMgrHandle == INVALID_HANDLE_VALUE)
    {
        HeapFree(GetProcessHeap(), 0, InputBuffer);
        return FALSE;
    }

    BytesReturned = 0;
    Success = DeviceIoControl(MountMgrHandle,
                              IOCTL_MOUNTMGR_CREATE_POINT,
                              InputBuffer,
                              InputBufferLength,
                              NULL,
                              0,
                              &BytesReturned,
                              NULL);
    CloseHandle(MountMgrHandle);
    HeapFree(GetProcessHeap(), 0, InputBuffer);
    return Success;
}

BOOL
StorageUtilAssignNextDriveLetter(
    _In_z_ PCWSTR DeviceName,
    _Out_ PWCHAR DriveLetter)
{
    ULONG DeviceNameLength;
    ULONG InputBufferLength;
    PMOUNTMGR_DRIVE_LETTER_TARGET InputBuffer;
    MOUNTMGR_DRIVE_LETTER_INFORMATION LetterInfo;
    HANDLE MountMgrHandle;
    DWORD BytesReturned;
    BOOL Success;

    if (DeviceName == NULL || DriveLetter == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    *DriveLetter = UNICODE_NULL;
    DeviceNameLength = (ULONG)(wcslen(DeviceName) * sizeof(WCHAR));
    InputBufferLength = DeviceNameLength + FIELD_OFFSET(MOUNTMGR_DRIVE_LETTER_TARGET, DeviceName);
    InputBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, InputBufferLength);
    if (InputBuffer == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    InputBuffer->DeviceNameLength = DeviceNameLength;
    CopyMemory(InputBuffer->DeviceName, DeviceName, DeviceNameLength);

    MountMgrHandle = StorageUtilOpenMountManager();
    if (MountMgrHandle == INVALID_HANDLE_VALUE)
    {
        HeapFree(GetProcessHeap(), 0, InputBuffer);
        return FALSE;
    }

    ZeroMemory(&LetterInfo, sizeof(LetterInfo));
    BytesReturned = 0;
    Success = DeviceIoControl(MountMgrHandle,
                              IOCTL_MOUNTMGR_NEXT_DRIVE_LETTER,
                              InputBuffer,
                              InputBufferLength,
                              &LetterInfo,
                              sizeof(LetterInfo),
                              &BytesReturned,
                              NULL);
    CloseHandle(MountMgrHandle);
    HeapFree(GetProcessHeap(), 0, InputBuffer);
    if (!Success)
        return FALSE;

    if (LetterInfo.DriveLetterWasAssigned)
        *DriveLetter = LetterInfo.CurrentDriveLetter;

    return TRUE;
}

BOOL
StorageUtilGetDriveLetter(
    _In_z_ PCWSTR DeviceName,
    _Out_ PWCHAR DriveLetter)
{
    ULONG DeviceNameLength;
    ULONG InputBufferLength;
    ULONG OutputBufferLength;
    PMOUNTMGR_MOUNT_POINT InputBuffer;
    PMOUNTMGR_MOUNT_POINTS OutputBuffer;
    PMOUNTMGR_MOUNT_POINT Point;
    PWCHAR LinkName;
    ULONG LinkChars;
    ULONG Index;
    HANDLE MountMgrHandle;
    DWORD BytesReturned;
    BOOL Success;

    if (DeviceName == NULL || DriveLetter == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    *DriveLetter = UNICODE_NULL;
    DeviceNameLength = (ULONG)(wcslen(DeviceName) * sizeof(WCHAR));
    InputBufferLength = sizeof(MOUNTMGR_MOUNT_POINT) + DeviceNameLength;
    OutputBufferLength = 0x1000;

    InputBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, InputBufferLength);
    OutputBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, OutputBufferLength);
    if (InputBuffer == NULL || OutputBuffer == NULL)
    {
        HeapFree(GetProcessHeap(), 0, InputBuffer);
        HeapFree(GetProcessHeap(), 0, OutputBuffer);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    InputBuffer->DeviceNameOffset = sizeof(MOUNTMGR_MOUNT_POINT);
    InputBuffer->DeviceNameLength = (USHORT)DeviceNameLength;
    CopyMemory((PBYTE)InputBuffer + InputBuffer->DeviceNameOffset, DeviceName, DeviceNameLength);

    MountMgrHandle = StorageUtilOpenMountManager();
    if (MountMgrHandle == INVALID_HANDLE_VALUE)
    {
        HeapFree(GetProcessHeap(), 0, InputBuffer);
        HeapFree(GetProcessHeap(), 0, OutputBuffer);
        return FALSE;
    }

    BytesReturned = 0;
    Success = DeviceIoControl(MountMgrHandle,
                              IOCTL_MOUNTMGR_QUERY_POINTS,
                              InputBuffer,
                              InputBufferLength,
                              OutputBuffer,
                              OutputBufferLength,
                              &BytesReturned,
                              NULL);
    CloseHandle(MountMgrHandle);

    if (Success)
    {
        for (Index = 0; Index < OutputBuffer->NumberOfMountPoints; Index++)
        {
            Point = &OutputBuffer->MountPoints[Index];
            LinkName = (PWCHAR)((PBYTE)OutputBuffer + Point->SymbolicLinkNameOffset);
            LinkChars = Point->SymbolicLinkNameLength / sizeof(WCHAR);

            /* Mount manager writes drive letters exactly as \DosDevices\X: */
            if ((LinkChars == 14) && (wcsncmp(LinkName, L"\\DosDevices\\", 12) == 0) && (LinkName[13] == L':'))
            {
                *DriveLetter = towupper(LinkName[12]);
                break;
            }
        }
    }

    HeapFree(GetProcessHeap(), 0, InputBuffer);
    HeapFree(GetProcessHeap(), 0, OutputBuffer);
    return Success;
}

BOOL
StorageUtilDeleteDriveLetter(
    _In_ WCHAR DriveLetter)
{
    PMOUNTMGR_MOUNT_POINT InputBuffer;
    PMOUNTMGR_MOUNT_POINTS OutputBuffer;
    WCHAR DosDeviceName[30];
    ULONG DosDeviceNameLength;
    ULONG InputBufferLength;
    ULONG OutputBufferLength;
    HANDLE MountMgrHandle;
    DWORD BytesReturned;
    BOOL Success;

    if (DriveLetter == UNICODE_NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    StringCchPrintfW(DosDeviceName, ARRAYSIZE(DosDeviceName), L"\\DosDevices\\%c:", towupper(DriveLetter));
    DosDeviceNameLength = (ULONG)(wcslen(DosDeviceName) * sizeof(WCHAR));
    InputBufferLength = sizeof(MOUNTMGR_MOUNT_POINT) + DosDeviceNameLength;
    OutputBufferLength = 0x1000;

    InputBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, InputBufferLength);
    OutputBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, OutputBufferLength);
    if (InputBuffer == NULL || OutputBuffer == NULL)
    {
        HeapFree(GetProcessHeap(), 0, InputBuffer);
        HeapFree(GetProcessHeap(), 0, OutputBuffer);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    InputBuffer->SymbolicLinkNameOffset = sizeof(MOUNTMGR_MOUNT_POINT);
    InputBuffer->SymbolicLinkNameLength = DosDeviceNameLength;
    CopyMemory((PBYTE)InputBuffer + InputBuffer->SymbolicLinkNameOffset,
               DosDeviceName,
               DosDeviceNameLength);
    OutputBuffer->Size = OutputBufferLength;

    MountMgrHandle = StorageUtilOpenMountManager();
    if (MountMgrHandle == INVALID_HANDLE_VALUE)
    {
        HeapFree(GetProcessHeap(), 0, InputBuffer);
        HeapFree(GetProcessHeap(), 0, OutputBuffer);
        return FALSE;
    }

    BytesReturned = 0;
    Success = DeviceIoControl(MountMgrHandle,
                              IOCTL_MOUNTMGR_DELETE_POINTS,
                              InputBuffer,
                              InputBufferLength,
                              OutputBuffer,
                              OutputBufferLength,
                              &BytesReturned,
                              NULL);
    CloseHandle(MountMgrHandle);
    HeapFree(GetProcessHeap(), 0, InputBuffer);
    HeapFree(GetProcessHeap(), 0, OutputBuffer);
    return Success;
}

BOOL
StorageUtilGetDriveNumber(
    _In_z_ PCWSTR DeviceName,
    _Outptr_result_maybenull_z_ PWSTR *DriveNumber)
{
    static const WCHAR Prefix[] = L"\\DosDevices\\";
    ULONG DeviceNameLength;
    ULONG InputBufferLength;
    ULONG OutputBufferLength = 0x1000;
    PMOUNTMGR_MOUNT_POINT InputBuffer;
    PMOUNTMGR_MOUNT_POINTS OutputBuffer;
    PMOUNTMGR_MOUNT_POINT Point;
    PWSTR LinkName;
    ULONG LinkChars;
    ULONG NumberLength;
    ULONG Index;
    ULONG DigitIndex;
    HANDLE MountMgrHandle;
    DWORD BytesReturned;
    BOOL Success;

    if (DeviceName == NULL || DriveNumber == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    *DriveNumber = NULL;
    DeviceNameLength = (ULONG)(wcslen(DeviceName) * sizeof(WCHAR));
    if (DeviceNameLength > USHRT_MAX ||
        DeviceNameLength > ULONG_MAX - sizeof(MOUNTMGR_MOUNT_POINT))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    InputBufferLength = sizeof(MOUNTMGR_MOUNT_POINT) + DeviceNameLength;
    InputBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, InputBufferLength);
    OutputBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, OutputBufferLength);
    if (InputBuffer == NULL || OutputBuffer == NULL)
    {
        HeapFree(GetProcessHeap(), 0, InputBuffer);
        HeapFree(GetProcessHeap(), 0, OutputBuffer);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    InputBuffer->DeviceNameOffset = sizeof(MOUNTMGR_MOUNT_POINT);
    InputBuffer->DeviceNameLength = (USHORT)DeviceNameLength;
    CopyMemory((PBYTE)InputBuffer + InputBuffer->DeviceNameOffset, DeviceName, DeviceNameLength);

    MountMgrHandle = StorageUtilOpenMountManager();
    if (MountMgrHandle == INVALID_HANDLE_VALUE)
    {
        HeapFree(GetProcessHeap(), 0, InputBuffer);
        HeapFree(GetProcessHeap(), 0, OutputBuffer);
        return FALSE;
    }

    BytesReturned = 0;
    Success = DeviceIoControl(MountMgrHandle,
                              IOCTL_MOUNTMGR_QUERY_POINTS,
                              InputBuffer,
                              InputBufferLength,
                              OutputBuffer,
                              OutputBufferLength,
                              &BytesReturned,
                              NULL);
    CloseHandle(MountMgrHandle);

    if (Success)
    {
        for (Index = 0; Index < OutputBuffer->NumberOfMountPoints; Index++)
        {
            Point = &OutputBuffer->MountPoints[Index];
            LinkName = (PWSTR)((PBYTE)OutputBuffer + Point->SymbolicLinkNameOffset);
            LinkChars = Point->SymbolicLinkNameLength / sizeof(WCHAR);

            if (LinkChars <= ARRAYSIZE(Prefix) ||
                _wcsnicmp(LinkName, Prefix, ARRAYSIZE(Prefix) - 1) != 0 ||
                LinkName[LinkChars - 1] != L':')
                continue;

            NumberLength = LinkChars - (ARRAYSIZE(Prefix) - 1) - 1;
            if (NumberLength == 0)
                continue;

            for (DigitIndex = 0; DigitIndex < NumberLength; DigitIndex++)
            {
                if (LinkName[ARRAYSIZE(Prefix) - 1 + DigitIndex] < L'0' ||
                    LinkName[ARRAYSIZE(Prefix) - 1 + DigitIndex] > L'9')
                    break;
            }

            if (DigitIndex != NumberLength)
                continue;

            *DriveNumber = HeapAlloc(GetProcessHeap(),
                                     0,
                                     (NumberLength + 1) * sizeof(WCHAR));
            if (*DriveNumber == NULL)
            {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                Success = FALSE;
                break;
            }

            CopyMemory(*DriveNumber,
                       LinkName + ARRAYSIZE(Prefix) - 1,
                       NumberLength * sizeof(WCHAR));
            (*DriveNumber)[NumberLength] = UNICODE_NULL;
            break;
        }
    }

    HeapFree(GetProcessHeap(), 0, InputBuffer);
    HeapFree(GetProcessHeap(), 0, OutputBuffer);
    return Success;
}

BOOL
StorageUtilDeleteDriveNumber(
    _In_z_ PCWSTR DriveNumber)
{
    PWSTR DosDeviceName;
    ULONG DosDeviceNameLength;
    ULONG InputBufferLength;
    ULONG OutputBufferLength = 0x1000;
    PMOUNTMGR_MOUNT_POINT InputBuffer;
    PMOUNTMGR_MOUNT_POINTS OutputBuffer;
    HANDLE MountMgrHandle;
    DWORD BytesReturned;
    BOOL Success;

    if (!StorageUtilBuildDriveNumberLink(DriveNumber, &DosDeviceName, &DosDeviceNameLength))
        return FALSE;

    InputBufferLength = sizeof(MOUNTMGR_MOUNT_POINT) + DosDeviceNameLength;
    InputBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, InputBufferLength);
    OutputBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, OutputBufferLength);
    if (InputBuffer == NULL || OutputBuffer == NULL)
    {
        HeapFree(GetProcessHeap(), 0, DosDeviceName);
        HeapFree(GetProcessHeap(), 0, InputBuffer);
        HeapFree(GetProcessHeap(), 0, OutputBuffer);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    InputBuffer->SymbolicLinkNameOffset = sizeof(MOUNTMGR_MOUNT_POINT);
    InputBuffer->SymbolicLinkNameLength = (USHORT)DosDeviceNameLength;
    CopyMemory((PBYTE)InputBuffer + InputBuffer->SymbolicLinkNameOffset,
               DosDeviceName,
               DosDeviceNameLength);
    OutputBuffer->Size = OutputBufferLength;
    HeapFree(GetProcessHeap(), 0, DosDeviceName);

    MountMgrHandle = StorageUtilOpenMountManager();
    if (MountMgrHandle == INVALID_HANDLE_VALUE)
    {
        HeapFree(GetProcessHeap(), 0, InputBuffer);
        HeapFree(GetProcessHeap(), 0, OutputBuffer);
        return FALSE;
    }

    BytesReturned = 0;
    Success = DeviceIoControl(MountMgrHandle,
                              IOCTL_MOUNTMGR_DELETE_POINTS,
                              InputBuffer,
                              InputBufferLength,
                              OutputBuffer,
                              OutputBufferLength,
                              &BytesReturned,
                              NULL);
    CloseHandle(MountMgrHandle);
    HeapFree(GetProcessHeap(), 0, InputBuffer);
    HeapFree(GetProcessHeap(), 0, OutputBuffer);
    return Success;
}

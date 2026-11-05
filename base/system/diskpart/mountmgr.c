/*
 * PROJECT:         ReactOS DiskPart
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            base/system/diskpart/mountmgr.c
 * PURPOSE:         Manages all the partitions of the OS in an interactive way.
 * PROGRAMMERS:     Eric Kohl
 */

#include "diskpart.h"

#include <mountmgr.h>
#include <mountmgrutil.h>
#include <shlobj.h>

#define NDEBUG
#include <debug.h>

/* FUNCTIONS ******************************************************************/

BOOL
IsValidDriveNumber(
    _In_opt_z_ PCWSTR DriveNumber)
{
    SIZE_T Index;

    if (DriveNumber == NULL || DriveNumber[0] == UNICODE_NULL)
        return FALSE;

    for (Index = 0; DriveNumber[Index] != UNICODE_NULL; Index++)
    {
        if (DriveNumber[Index] < L'0' || DriveNumber[Index] > L'9')
            return FALSE;
    }

    return TRUE;
}

VOID
NotifyDriveNumberChange(
    _In_z_ PCWSTR DriveNumber,
    _In_ BOOL Added)
{
    SIZE_T NumberLength;
    PWSTR DrivePath;

    if (!IsValidDriveNumber(DriveNumber))
        return;

    NumberLength = wcslen(DriveNumber);
    DrivePath = HeapAlloc(GetProcessHeap(), 0, (NumberLength + 4) * sizeof(WCHAR));
    if (DrivePath == NULL)
    {
        DPRINT1("Failed to allocate a numbered-drive notification path\n");
        return;
    }

    CopyMemory(DrivePath, DriveNumber, NumberLength * sizeof(WCHAR));
    DrivePath[NumberLength] = L':';
    DrivePath[NumberLength + 1] = L'\\';
    DrivePath[NumberLength + 2] = UNICODE_NULL;
    SHChangeNotify(Added ? SHCNE_DRIVEADD : SHCNE_DRIVEREMOVED,
                   SHCNF_PATHW | SHCNF_FLUSHNOWAIT,
                   DrivePath,
                   NULL);
    HeapFree(GetProcessHeap(), 0, DrivePath);
}

VOID
NotifyDriveLetterChange(
    _In_ WCHAR DriveLetter,
    _In_ BOOL Added)
{
    WCHAR DrivePath[] = { L'A', L':', L'\\', UNICODE_NULL };
    DrivePath[0] = towupper(DriveLetter);
    SHChangeNotify(Added ? SHCNE_DRIVEADD : SHCNE_DRIVEREMOVED,
                   SHCNF_PATHW | SHCNF_FLUSHNOWAIT,
                   DrivePath,
                   NULL);
}

static
NTSTATUS
OpenMountManager(
    _Out_ PHANDLE MountMgrHandle,
    _In_ ACCESS_MASK Access)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING DeviceName;
    IO_STATUS_BLOCK Iosb;

    RtlInitUnicodeString(&DeviceName, MOUNTMGR_DEVICE_NAME);

    InitializeObjectAttributes(&ObjectAttributes,
                               &DeviceName,
                               0,
                               NULL,
                               NULL);

    return NtOpenFile(MountMgrHandle,
                      Access | SYNCHRONIZE,
                      &ObjectAttributes,
                      &Iosb,
                      0,
                      FILE_SYNCHRONOUS_IO_NONALERT);
}


BOOL
GetAutomountState(
    _Out_ PBOOL State)
{
    HANDLE MountMgrHandle;
    MOUNTMGR_QUERY_AUTO_MOUNT AutoMount;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;

    DPRINT("ShowAutomountState()\n");

    Status = OpenMountManager(&MountMgrHandle, GENERIC_READ);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("OpenMountManager() Status 0x%08lx\n", Status);
        return FALSE;
    }

    Status = NtDeviceIoControlFile(MountMgrHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &Iosb,
                                   IOCTL_MOUNTMGR_QUERY_AUTO_MOUNT,
                                   NULL,
                                   0,
                                   &AutoMount,
                                   sizeof(AutoMount));

    NtClose(MountMgrHandle);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtDeviceIoControlFile() Status 0x%08lx\n", Status);
        return FALSE;
    }

    if (State)
        *State = (AutoMount.CurrentState == Enabled);

    return TRUE;
}


BOOL
SetAutomountState(
    _In_ BOOL bEnable)
{
    HANDLE MountMgrHandle;
    MOUNTMGR_SET_AUTO_MOUNT AutoMount;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;

    DPRINT("SetAutomountState()\n");

    Status = OpenMountManager(&MountMgrHandle, GENERIC_READ | GENERIC_WRITE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("OpenMountManager() Status 0x%08lx\n", Status);
        return TRUE;
    }

    if (bEnable)
        AutoMount.NewState = Enabled;
    else
        AutoMount.NewState = Disabled;

    Status = NtDeviceIoControlFile(MountMgrHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &Iosb,
                                   IOCTL_MOUNTMGR_SET_AUTO_MOUNT,
                                   &AutoMount,
                                   sizeof(AutoMount),
                                   NULL,
                                   0);

    NtClose(MountMgrHandle);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtDeviceIoControlFile() Status 0x%08lx\n", Status);
        return TRUE;
    }

    if (AutoMount.NewState == Enabled)
        ConResPuts(StdOut, IDS_AUTOMOUNT_ENABLED);
    else
        ConResPuts(StdOut, IDS_AUTOMOUNT_DISABLED);
    ConPuts(StdOut, L"\n");

    return TRUE;
}

BOOL
ScrubAutomount(VOID)
{
    HANDLE MountMgrHandle;
    IO_STATUS_BLOCK Iosb;
    NTSTATUS Status;

    DPRINT("ScrubAutomount()\n");

    Status = OpenMountManager(&MountMgrHandle, GENERIC_READ | GENERIC_WRITE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("OpenMountManager() Status 0x%08lx\n", Status);
        return FALSE;
    }

    Status = NtDeviceIoControlFile(MountMgrHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &Iosb,
                                   IOCTL_MOUNTMGR_SCRUB_REGISTRY,
                                   NULL,
                                   0,
                                   NULL,
                                   0);

    NtClose(MountMgrHandle);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtDeviceIoControlFile() Status 0x%08lx\n", Status);
        return FALSE;
    }

    return TRUE;
}



BOOL
AssignDriveLetter(
    _In_ PWSTR DeviceName,
    _In_ WCHAR DriveLetter)
{
    DPRINT1("AssignDriveLetter(%S %c)\n", DeviceName, DriveLetter);
    return StorageUtilAssignDriveLetter(DeviceName, DriveLetter);
}


BOOL
AssignNextDriveLetter(
    _In_ PWSTR DeviceName,
    _Out_ PWCHAR DriveLetter)
{
    DPRINT("AssignNextDriveLetter(%S %p)\n", DeviceName, DriveLetter);
    return StorageUtilAssignNextDriveLetter(DeviceName, DriveLetter);
}


BOOL
DeleteDriveLetter(
    _In_ WCHAR DriveLetter)
{
    DPRINT("DeleteDriveLetter(%c)\n", DriveLetter);
    return StorageUtilDeleteDriveLetter(DriveLetter);
}

BOOL
AssignDriveNumber(
    _In_ PWSTR DeviceName,
    _In_ PWSTR DriveNumber)
{
    DPRINT("AssignDriveNumber(%S %S)\n", DeviceName, DriveNumber);
    return StorageUtilAssignDriveNumber(DeviceName, DriveNumber);
}

BOOL
DeleteDriveNumber(
    _In_ PWSTR DriveNumber)
{
    DPRINT("DeleteDriveNumber(%S)\n", DriveNumber);
    return StorageUtilDeleteDriveNumber(DriveNumber);
}

/*
 * PROJECT:     LiberNT Boot Configuration Data editor
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Enumerates and edits common BCD boot options
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#include <stdio.h>
#include <windows.h>
#include <strsafe.h>
#include "../bcdutil.h"

#define BCD_KEY_ROOT L"Objects\\"
#define BCD_ELEMENT_HEADER_SIZE 16
#define BCD_BOOT_DEVICE_TYPE 5

typedef struct _BCD_OPTION
{
    PCWSTR Name;
    PCWSTR Element;
    ULONG Type;
} BCD_OPTION;

static const BCD_OPTION BcdStringOptions[] =
{
    {L"description", L"12000004", REG_SZ},
    {L"path", L"12000002", REG_SZ},
    {L"systemroot", L"22000002", REG_SZ},
    {L"kernel", L"22000011", REG_SZ},
    {L"hal", L"22000012", REG_SZ},
    {L"loadoptions", L"12000030", REG_SZ},
};

static const BCD_OPTION BcdBooleanOptions[] =
{
    {L"debug", L"260000a0", REG_BINARY},
    {L"sos", L"26000091", REG_BINARY},
    {L"bootlog", L"26000090", REG_BINARY},
    {L"testsigning", L"16000049", REG_BINARY},
    {L"nointegritychecks", L"16000048", REG_BINARY},
};

static VOID
PrintUsage(VOID)
{
    fputws(L"BCDEDIT BCD store editor.\n\n", stdout);
    fputws(L"BCDEDIT /store <file> /enum [<identifier>]\n", stdout);
    fputws(L"BCDEDIT /store <file> /set <identifier> <option> <value>\n", stdout);
    fputws(L"BCDEDIT /store <file> /deletevalue <identifier> <option>\n\n", stdout);
    fputws(L"Identifiers may be a loader GUID, {default}, or {current}.\n", stdout);
    fputws(L"Device values are boot or partition=<volume>, for example partition=C:.\n", stdout);
    fputws(L"Supported options include device, osdevice, path, systemroot, kernel,\n", stdout);
    fputws(L"hal, description, loadoptions, debug, sos, bootlog, testsigning,\n", stdout);
    fputws(L"nointegritychecks, numproc, safeboot, and bootmenupolicy.\n", stdout);
}

static BOOLEAN
GetObjectId(
    _In_ HKEY Store,
    _In_ PCWSTR Text,
    _Out_writes_z_(Chars) PWSTR ObjectId,
    _In_ ULONG Chars)
{
    if (_wcsicmp(Text, L"{default}") == 0 || _wcsicmp(Text, L"{current}") == 0)
        return BcdGetDefaultObjectId(Store, ObjectId, Chars);

    return wcslen(Text) < Chars &&
           Text[0] == L'{' &&
           Text[wcslen(Text) - 1] == L'}' &&
           SUCCEEDED(StringCchCopyW(ObjectId, Chars, Text));
}

static BOOLEAN
GetElementKey(
    _In_ HKEY Store,
    _In_ PCWSTR ObjectId,
    _In_ PCWSTR ElementId,
    _In_ REGSAM Access,
    _Out_ PHKEY Key)
{
    WCHAR Path[128];

    if (FAILED(StringCchPrintfW(Path,
                                ARRAYSIZE(Path),
                                L"%s%s\\Elements\\%s",
                                BCD_KEY_ROOT,
                                ObjectId,
                                ElementId)))
    {
        return FALSE;
    }

    return RegOpenKeyExW(Store, Path, 0, Access, Key) == ERROR_SUCCESS;
}

static BOOLEAN
QueryElement(
    _In_ HKEY Store,
    _In_ PCWSTR ObjectId,
    _In_ PCWSTR ElementId,
    _Out_writes_bytes_(BufferSize) BYTE *Buffer,
    _In_ DWORD BufferSize,
    _Out_ PDWORD Type,
    _Out_ PDWORD Size)
{
    HKEY Key;
    LSTATUS Status;

    if (!GetElementKey(Store, ObjectId, ElementId, KEY_QUERY_VALUE, &Key))
        return FALSE;

    *Size = BufferSize;
    Status = RegQueryValueExW(Key, L"Element", NULL, Type, Buffer, Size);
    RegCloseKey(Key);
    return Status == ERROR_SUCCESS;
}

static BOOLEAN
SetElement(
    _In_ HKEY Store,
    _In_ PCWSTR ObjectId,
    _In_ PCWSTR ElementId,
    _In_ ULONG Type,
    _In_reads_bytes_(Size) const BYTE *Data,
    _In_ DWORD Size)
{
    WCHAR Path[128];
    HKEY Key;
    LSTATUS Status;

    if (FAILED(StringCchPrintfW(Path,
                                ARRAYSIZE(Path),
                                L"%s%s\\Elements\\%s",
                                BCD_KEY_ROOT,
                                ObjectId,
                                ElementId)))
    {
        return FALSE;
    }

    Status = RegCreateKeyExW(Store, Path, 0, NULL, 0, KEY_SET_VALUE, NULL, &Key, NULL);
    if (Status != ERROR_SUCCESS)
        return FALSE;

    Status = RegSetValueExW(Key, L"Element", 0, Type, Data, Size);
    RegCloseKey(Key);
    return Status == ERROR_SUCCESS;
}

static const BCD_OPTION *
FindOption(
    _In_ PCWSTR Name,
    _In_reads_(Count) const BCD_OPTION *Options,
    _In_ ULONG Count)
{
    ULONG i;

    for (i = 0; i < Count; i++)
    {
        if (_wcsicmp(Name, Options[i].Name) == 0)
            return &Options[i];
    }

    return NULL;
}

static BOOLEAN
SetStringOption(
    _In_ HKEY Store,
    _In_ PCWSTR ObjectId,
    _In_ const BCD_OPTION *Option,
    _In_ PCWSTR Value)
{
    SIZE_T Chars = wcslen(Value) + 1;

    if (Chars > MAXDWORD / sizeof(WCHAR))
        return FALSE;

    return SetElement(Store,
                      ObjectId,
                      Option->Element,
                      Option->Type,
                      (const BYTE *)Value,
                      (DWORD)(Chars * sizeof(WCHAR)));
}

static BOOLEAN
SetBooleanOption(
    _In_ HKEY Store,
    _In_ PCWSTR ObjectId,
    _In_ const BCD_OPTION *Option,
    _In_ BOOLEAN Value)
{
    BYTE Data[8] = {0};

    Data[0] = Value ? 1 : 0;
    return SetElement(Store, ObjectId, Option->Element, REG_BINARY, Data, sizeof(Data));
}

static BOOLEAN
SetDeviceOption(
    _In_ HKEY Store,
    _In_ PCWSTR ObjectId,
    _In_ PCWSTR ElementId,
    _In_ PCWSTR Value)
{
    WCHAR PartitionPath[MAX_PATH];
    BYTE Device[BCD_PARTITION_DEVICE_SIZE] = {0};
    DWORD Type = BCD_BOOT_DEVICE_TYPE;
    DWORD Size = BCD_PARTITION_DEVICE_SIZE;
    DWORD Local = 0;

    if (_wcsicmp(Value, L"boot") != 0)
    {
        if (_wcsnicmp(Value, L"partition=", 10) != 0 ||
            FAILED(StringCchPrintfW(PartitionPath,
                                    ARRAYSIZE(PartitionPath),
                                    L"%s\\",
                                    Value + 10)) ||
            !BcdBuildPartitionDevice(PartitionPath, Device))
        {
            return FALSE;
        }
    }
    else
    {
        CopyMemory(Device, &Type, sizeof(Type));
        CopyMemory(Device + 8, &Size, sizeof(Size));
        CopyMemory(Device + 0x20, &Local, sizeof(Local));
    }

    return BcdSetElementDevice(Store, ObjectId, ElementId, Device);
}

static BOOLEAN
SetNumericOption(
    _In_ HKEY Store,
    _In_ PCWSTR ObjectId,
    _In_ PCWSTR Option,
    _In_ PCWSTR Value)
{
    WCHAR *End;
    ULONGLONG Number;
    BYTE Data[8] = {0};

    if (_wcsicmp(Option, L"numproc") == 0)
    {
        Number = _wcstoui64(Value, &End, 0);
        if (End == Value || *End != UNICODE_NULL || Number == 0 || Number > MAXDWORD)
            return FALSE;

        CopyMemory(Data, &Number, sizeof(Number));
        return SetElement(Store, ObjectId, L"25000061", REG_BINARY, Data, sizeof(Data));
    }

    if (_wcsicmp(Option, L"safeboot") == 0)
    {
        if (_wcsicmp(Value, L"minimal") == 0)
            Number = 0;
        else if (_wcsicmp(Value, L"network") == 0)
            Number = 1;
        else if (_wcsicmp(Value, L"dsrepair") == 0)
            Number = 2;
        else
            return FALSE;

        CopyMemory(Data, &Number, sizeof(Number));
        return SetElement(Store, ObjectId, L"25000080", REG_BINARY, Data, sizeof(Data));
    }

    if (_wcsicmp(Option, L"bootmenupolicy") == 0)
    {
        if (_wcsicmp(Value, L"standard") == 0)
            Number = 0;
        else if (_wcsicmp(Value, L"legacy") == 0)
            Number = 1;
        else
            return FALSE;

        CopyMemory(Data, &Number, sizeof(Number));
        return SetElement(Store, ObjectId, L"250000c2", REG_BINARY, Data, sizeof(Data));
    }

    return FALSE;
}

static BOOLEAN
SetOption(
    _In_ HKEY Store,
    _In_ PCWSTR ObjectId,
    _In_ PCWSTR OptionName,
    _In_ PCWSTR Value)
{
    const BCD_OPTION *Option;

    if (_wcsicmp(OptionName, L"device") == 0)
        return SetDeviceOption(Store, ObjectId, L"11000001", Value);
    if (_wcsicmp(OptionName, L"osdevice") == 0)
        return SetDeviceOption(Store, ObjectId, L"21000001", Value);

    Option = FindOption(OptionName, BcdStringOptions, RTL_NUMBER_OF(BcdStringOptions));
    if (Option)
        return SetStringOption(Store, ObjectId, Option, Value);

    Option = FindOption(OptionName, BcdBooleanOptions, RTL_NUMBER_OF(BcdBooleanOptions));
    if (Option)
    {
        if (_wcsicmp(Value, L"on") == 0 || _wcsicmp(Value, L"yes") == 0)
            return SetBooleanOption(Store, ObjectId, Option, TRUE);
        if (_wcsicmp(Value, L"off") == 0 || _wcsicmp(Value, L"no") == 0)
            return SetBooleanOption(Store, ObjectId, Option, FALSE);
        return FALSE;
    }

    return SetNumericOption(Store, ObjectId, OptionName, Value);
}

static BOOLEAN
DeleteOption(
    _In_ HKEY Store,
    _In_ PCWSTR ObjectId,
    _In_ PCWSTR OptionName)
{
    PCWSTR ElementId = NULL;
    const BCD_OPTION *Option;
    HKEY Key;
    LSTATUS Status;

    if (_wcsicmp(OptionName, L"device") == 0)
        ElementId = L"11000001";
    else if (_wcsicmp(OptionName, L"osdevice") == 0)
        ElementId = L"21000001";
    else if (_wcsicmp(OptionName, L"numproc") == 0)
        ElementId = L"25000061";
    else if (_wcsicmp(OptionName, L"safeboot") == 0)
        ElementId = L"25000080";
    else if (_wcsicmp(OptionName, L"bootmenupolicy") == 0)
        ElementId = L"250000c2";
    else if ((Option = FindOption(OptionName, BcdStringOptions, RTL_NUMBER_OF(BcdStringOptions))) != NULL)
        ElementId = Option->Element;
    else if ((Option = FindOption(OptionName, BcdBooleanOptions, RTL_NUMBER_OF(BcdBooleanOptions))) != NULL)
        ElementId = Option->Element;

    if (!ElementId || !GetElementKey(Store, ObjectId, ElementId, KEY_SET_VALUE, &Key))
        return FALSE;

    Status = RegDeleteValueW(Key, L"Element");
    RegCloseKey(Key);
    return Status == ERROR_SUCCESS;
}

static BOOLEAN
PrintStringElement(
    _In_ HKEY Store,
    _In_ PCWSTR ObjectId,
    _In_ PCWSTR Name,
    _In_ PCWSTR ElementId)
{
    BYTE Data[1024];
    DWORD Type, Size;

    if (!QueryElement(Store, ObjectId, ElementId, Data, sizeof(Data), &Type, &Size) ||
        Type != REG_SZ ||
        Size < sizeof(WCHAR) ||
        Size % sizeof(WCHAR) != 0)
    {
        return FALSE;
    }

    wprintf(L"  %-20ls %.*ls\n",
            Name,
            (int)(Size / sizeof(WCHAR)),
            (PCWSTR)Data);
    return TRUE;
}

static VOID
PrintDeviceElement(
    _In_ HKEY Store,
    _In_ PCWSTR ObjectId,
    _In_ PCWSTR Name,
    _In_ PCWSTR ElementId)
{
    BYTE Data[BCD_ELEMENT_HEADER_SIZE + BCD_PARTITION_DEVICE_SIZE];
    DWORD Type, Size;
    DWORD DeviceType;

    if (!QueryElement(Store, ObjectId, ElementId, Data, sizeof(Data), &Type, &Size) ||
        Type != REG_BINARY ||
        Size < sizeof(Data))
    {
        return;
    }

    CopyMemory(&DeviceType, Data + BCD_ELEMENT_HEADER_SIZE, sizeof(DeviceType));
    if (DeviceType == BCD_BOOT_DEVICE_TYPE)
    {
        wprintf(L"  %-20ls boot\n", Name);
    }
    else if (DeviceType == 6)
    {
        DWORD Style;
        CopyMemory(&Style, Data + BCD_ELEMENT_HEADER_SIZE + 0x24, sizeof(Style));
        if (Style == 0)
        {
            GUID Partition;
            CopyMemory(&Partition, Data + BCD_ELEMENT_HEADER_SIZE + 0x10, sizeof(Partition));
            wprintf(L"  %-20ls partition={%08lx-%04x-%04x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x}\n",
                    Name,
                    Partition.Data1,
                    Partition.Data2,
                    Partition.Data3,
                    Partition.Data4[0],
                    Partition.Data4[1],
                    Partition.Data4[2],
                    Partition.Data4[3],
                    Partition.Data4[4],
                    Partition.Data4[5],
                    Partition.Data4[6],
                    Partition.Data4[7]);
        }
        else
        {
            ULONGLONG Offset;
            DWORD Signature;
            CopyMemory(&Offset, Data + BCD_ELEMENT_HEADER_SIZE + 0x10, sizeof(Offset));
            CopyMemory(&Signature, Data + BCD_ELEMENT_HEADER_SIZE + 0x28, sizeof(Signature));
            wprintf(L"  %-20ls partition=MBR:%08lx:%I64u\n", Name, Signature, Offset);
        }
    }
}

static VOID
EnumObject(
    _In_ HKEY Store,
    _In_ PCWSTR ObjectId)
{
    wprintf(L"\n%ls\n", ObjectId);
    PrintStringElement(Store, ObjectId, L"description", L"12000004");
    PrintDeviceElement(Store, ObjectId, L"device", L"11000001");
    PrintStringElement(Store, ObjectId, L"path", L"12000002");
    PrintDeviceElement(Store, ObjectId, L"osdevice", L"21000001");
    PrintStringElement(Store, ObjectId, L"systemroot", L"22000002");
    PrintStringElement(Store, ObjectId, L"kernel", L"22000011");
    PrintStringElement(Store, ObjectId, L"hal", L"22000012");
    PrintStringElement(Store, ObjectId, L"loadoptions", L"12000030");
}

static BOOLEAN
EnumEntries(
    _In_ HKEY Store,
    _In_opt_ PCWSTR RequestedId)
{
    WCHAR ObjectId[64];
    WCHAR Path[160];
    WCHAR Ids[4096 / sizeof(WCHAR)];
    DWORD Type, Size = sizeof(Ids);
    HKEY Key;
    LSTATUS Status;
    ULONG i, Count = 0;

    if (RequestedId)
    {
        if (!GetObjectId(Store, RequestedId, ObjectId, ARRAYSIZE(ObjectId)))
            return FALSE;
        EnumObject(Store, ObjectId);
        return TRUE;
    }

    if (FAILED(StringCchPrintfW(Path,
                                ARRAYSIZE(Path),
                                L"%s{9dea862c-5cdd-4e70-acc1-f32b344d4795}\\Elements\\24000001",
                                BCD_KEY_ROOT)) ||
        RegOpenKeyExW(Store, Path, 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
    {
        return FALSE;
    }

    Status = RegQueryValueExW(Key, L"Element", NULL, &Type, (BYTE *)Ids, &Size);
    RegCloseKey(Key);
    if (Status != ERROR_SUCCESS || Type != REG_MULTI_SZ || Size < 2 * sizeof(WCHAR))
        return FALSE;

    for (i = 0; i < Size / sizeof(WCHAR) && Ids[i] != UNICODE_NULL; )
    {
        ULONG j = i;
        while (j < Size / sizeof(WCHAR) && Ids[j] != UNICODE_NULL)
            j++;
        if (j == Size / sizeof(WCHAR) || j == i ||
            !SUCCEEDED(StringCchCopyNW(ObjectId, ARRAYSIZE(ObjectId), &Ids[i], j - i)))
        {
            return FALSE;
        }
        EnumObject(Store, ObjectId);
        Count++;
        i = j + 1;
    }

    return Count != 0;
}

int
wmain(
    _In_ int argc,
    _In_reads_(argc) WCHAR *argv[])
{
    PCWSTR StorePath = NULL;
    PCWSTR Command = NULL;
    PCWSTR ObjectArgument = NULL;
    PCWSTR Option = NULL;
    PCWSTR Value = NULL;
    WCHAR ObjectId[64];
    HKEY Store;
    BOOLEAN Success;
    int i;

    if (argc == 2 && (_wcsicmp(argv[1], L"/?") == 0 || _wcsicmp(argv[1], L"/help") == 0))
    {
        PrintUsage();
        return ERROR_SUCCESS;
    }

    for (i = 1; i < argc; i++)
    {
        if (_wcsicmp(argv[i], L"/store") == 0 && i + 1 < argc)
            StorePath = argv[++i];
        else if (!Command)
            Command = argv[i];
        else if (!ObjectArgument)
            ObjectArgument = argv[i];
        else if (!Option)
            Option = argv[i];
        else if (!Value)
            Value = argv[i];
        else
        {
            PrintUsage();
            return ERROR_INVALID_PARAMETER;
        }
    }

    if (!StorePath || !Command ||
        !BcdOpenStoreFile(StorePath, _wcsicmp(Command, L"/enum") != 0, &Store))
    {
        fputws(L"Failure: specify a readable BCD store with /store.\n", stderr);
        return ERROR_FILE_NOT_FOUND;
    }

    if (_wcsicmp(Command, L"/enum") == 0)
    {
        if (Option || Value)
            Success = FALSE;
        else
            Success = EnumEntries(Store, ObjectArgument);
    }
    else if (_wcsicmp(Command, L"/set") == 0 &&
             ObjectArgument && Option && Value &&
             GetObjectId(Store, ObjectArgument, ObjectId, ARRAYSIZE(ObjectId)))
    {
        Success = SetOption(Store, ObjectId, Option, Value);
    }
    else if (_wcsicmp(Command, L"/deletevalue") == 0 &&
             ObjectArgument && Option && !Value &&
             GetObjectId(Store, ObjectArgument, ObjectId, ARRAYSIZE(ObjectId)))
    {
        Success = DeleteOption(Store, ObjectId, Option);
    }
    else
    {
        Success = FALSE;
    }

    if (Success && RegFlushKey(Store) != ERROR_SUCCESS)
        Success = FALSE;
    RegCloseKey(Store);

    if (!Success)
    {
        fputws(L"Failure: invalid BCD object, option, value, or store.\n", stderr);
        return ERROR_INVALID_PARAMETER;
    }

    return ERROR_SUCCESS;
}

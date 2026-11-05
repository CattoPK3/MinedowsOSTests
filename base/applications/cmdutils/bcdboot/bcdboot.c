/*
 * PROJECT:     LiberNT Boot Configuration Data boot file creation tool
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Provides the BCDBoot command and system image contract
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#include <stdio.h>
#include <windows.h>
#include <strsafe.h>
#include "../bcdutil.h"

static BOOLEAN
UpdateBcdStore(
    _In_ PCWSTR Path,
    _In_reads_bytes_(BCD_PARTITION_DEVICE_SIZE) const BYTE *Device)
{
    WCHAR ObjectId[64];
    HKEY Store;
    BOOLEAN Success = FALSE;

    if (!BcdOpenStoreFile(Path, TRUE, &Store))
        return FALSE;

    Success = BcdGetDefaultObjectId(Store, ObjectId, ARRAYSIZE(ObjectId)) &&
              BcdSetElementDevice(Store, ObjectId, L"11000001", Device) &&
              BcdSetElementDevice(Store, ObjectId, L"21000001", Device);

    if (Success)
        Success = RegFlushKey(Store) == ERROR_SUCCESS;

    RegCloseKey(Store);
    return Success;
}

static VOID
PrintUsage(VOID)
{
    fputws(L"BCDBOOT boot file creation and repair tool.\n\n", stdout);
    fputws(L"BCDBOOT <source> [/l <locale>] [/s <volume-letter> [/f <firmware type>]]\n", stdout);
    fputws(L"        [/v] [/m [{OS Loader GUID}]] [/addlast | /p] [/d] [/c]\n", stdout);
    fputws(L"        [/bootex] [/offline]\n\n", stdout);
    fputws(L"  source       Location of the Windows directory used as the boot-file source.\n", stdout);
    fputws(L"  /s volume    System partition receiving the boot files.\n", stdout);
    fputws(L"  /f type      Firmware type: UEFI, BIOS, or ALL.\n", stdout);
}

static BOOLEAN
FileExists(
    _In_ PCWSTR Path)
{
    DWORD Attributes = GetFileAttributesW(Path);
    return (Attributes != INVALID_FILE_ATTRIBUTES && !(Attributes & FILE_ATTRIBUTE_DIRECTORY));
}

static BOOLEAN
DirectoryExists(
    _In_ PCWSTR Path)
{
    DWORD Attributes = GetFileAttributesW(Path);
    return (Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_DIRECTORY));
}

static VOID
EnsureDirectory(
    _In_ PCWSTR Path)
{
    if (!DirectoryExists(Path))
        CreateDirectoryW(Path, NULL);
}

static BOOLEAN
CopyMatchingFiles(
    _In_ PCWSTR SourcePattern,
    _In_ PCWSTR TargetDirectory)
{
    WIN32_FIND_DATAW FindData;
    HANDLE FindHandle;
    WCHAR SourceDir[MAX_PATH];
    WCHAR SourceFile[MAX_PATH];
    WCHAR TargetFile[MAX_PATH];
    PWSTR LastSlash;

    StringCchCopyW(SourceDir, ARRAYSIZE(SourceDir), SourcePattern);
    LastSlash = wcsrchr(SourceDir, L'\\');
    if (LastSlash)
        *(LastSlash + 1) = L'\0';
    else
        SourceDir[0] = L'\0';

    FindHandle = FindFirstFileW(SourcePattern, &FindData);
    if (FindHandle == INVALID_HANDLE_VALUE)
        return FALSE;

    do
    {
        if (!(FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            StringCchPrintfW(SourceFile, ARRAYSIZE(SourceFile), L"%s%s", SourceDir, FindData.cFileName);
            StringCchPrintfW(TargetFile, ARRAYSIZE(TargetFile), L"%s\\%s", TargetDirectory, FindData.cFileName);
            CopyFileW(SourceFile, TargetFile, FALSE);
        }
    } while (FindNextFileW(FindHandle, &FindData));

    FindClose(FindHandle);
    return TRUE;
}

int
wmain(
    _In_ int argc,
    _In_reads_(argc) WCHAR *argv[])
{
    PCWSTR Source = NULL;
    WCHAR TargetVolume[MAX_PATH] = L"";
    WCHAR FirmwareType[32] = L"UEFI";
    WCHAR WindowsDir[MAX_PATH] = L"";
    WCHAR TestPath[MAX_PATH];
    WCHAR BootmgfwSrc[MAX_PATH] = L"";
    WCHAR BcdTemplateSrc[MAX_PATH] = L"";
    WCHAR TargetDir[MAX_PATH];
    WCHAR TargetFile[MAX_PATH];
    BYTE PartitionDevice[BCD_PARTITION_DEVICE_SIZE];
    BOOLEAN Verbose = FALSE;
    int i;

    if (argc < 2)
    {
        PrintUsage();
        return ERROR_INVALID_PARAMETER;
    }

    for (i = 1; i < argc; i++)
    {
        if ((_wcsicmp(argv[i], L"/?") == 0) ||
            (_wcsicmp(argv[i], L"-?") == 0) ||
            (_wcsicmp(argv[i], L"/help") == 0) ||
            (_wcsicmp(argv[i], L"--help") == 0))
        {
            PrintUsage();
            return ERROR_SUCCESS;
        }
        else if (_wcsicmp(argv[i], L"/v") == 0)
        {
            Verbose = TRUE;
        }
        else if (_wcsicmp(argv[i], L"/s") == 0 && i + 1 < argc)
        {
            StringCchCopyW(TargetVolume, ARRAYSIZE(TargetVolume), argv[++i]);
        }
        else if (_wcsicmp(argv[i], L"/f") == 0 && i + 1 < argc)
        {
            StringCchCopyW(FirmwareType, ARRAYSIZE(FirmwareType), argv[++i]);
        }
        else if (_wcsicmp(argv[i], L"UEFI") == 0)
        {
            StringCchCopyW(FirmwareType, ARRAYSIZE(FirmwareType), argv[i]);
        }
        else if (argv[i][0] != L'/' && !Source)
        {
            Source = argv[i];
        }
    }

    if (!Source)
    {
        PrintUsage();
        return ERROR_INVALID_PARAMETER;
    }

    StringCchCopyW(WindowsDir, ARRAYSIZE(WindowsDir), Source);
    i = (int)wcslen(WindowsDir);
    while (i > 0 && WindowsDir[i - 1] == L'\\')
        WindowsDir[--i] = L'\0';

    StringCchPrintfW(TestPath, ARRAYSIZE(TestPath), L"%s\\system32\\ntoskrnl.exe", WindowsDir);
    if (!FileExists(TestPath))
    {
        StringCchPrintfW(TestPath, ARRAYSIZE(TestPath), L"%s\\Windows\\system32\\ntoskrnl.exe", WindowsDir);
        if (FileExists(TestPath))
            StringCchCatW(WindowsDir, ARRAYSIZE(WindowsDir), L"\\Windows");
        else
        {
            fputws(L"Failure when attempting to locate OS files.\n", stderr);
            return ERROR_FILE_NOT_FOUND;
        }
    }

    if (TargetVolume[0] == L'\0')
    {
        fputws(L"Failure: Target system partition (/s) must be specified.\n", stderr);
        return ERROR_INVALID_PARAMETER;
    }

    if (_wcsicmp(FirmwareType, L"UEFI") != 0)
    {
        fputws(L"Failure: only UEFI boot-file creation is supported.\n", stderr);
        return ERROR_NOT_SUPPORTED;
    }

    i = (int)wcslen(TargetVolume);
    while (i > 0 && TargetVolume[i - 1] == L'\\')
        TargetVolume[--i] = L'\0';

    if (!BcdBuildPartitionDevice(WindowsDir, PartitionDevice))
    {
        fputws(L"Failure when attempting to identify the Windows partition.\n", stderr);
        return ERROR_NOT_SUPPORTED;
    }

    StringCchPrintfW(TestPath, ARRAYSIZE(TestPath), L"%s\\Boot\\EFI\\bootmgfw.efi", WindowsDir);
    if (FileExists(TestPath))
        StringCchCopyW(BootmgfwSrc, ARRAYSIZE(BootmgfwSrc), TestPath);
    else
    {
        StringCchPrintfW(TestPath, ARRAYSIZE(TestPath), L"%s\\system32\\bootmgfw.efi", WindowsDir);
        if (FileExists(TestPath))
            StringCchCopyW(BootmgfwSrc, ARRAYSIZE(BootmgfwSrc), TestPath);
        else
        {
            WCHAR CurrentWinDir[MAX_PATH];
            if (GetWindowsDirectoryW(CurrentWinDir, MAX_PATH))
            {
                StringCchPrintfW(TestPath, ARRAYSIZE(TestPath), L"%s\\Boot\\EFI\\bootmgfw.efi", CurrentWinDir);
                if (FileExists(TestPath))
                    StringCchCopyW(BootmgfwSrc, ARRAYSIZE(BootmgfwSrc), TestPath);
            }
        }
    }

    StringCchPrintfW(TestPath, ARRAYSIZE(TestPath), L"%s\\system32\\config\\BCD-Template", WindowsDir);
    if (FileExists(TestPath))
        StringCchCopyW(BcdTemplateSrc, ARRAYSIZE(BcdTemplateSrc), TestPath);
    else
    {
        StringCchPrintfW(TestPath, ARRAYSIZE(TestPath), L"%s\\system32\\config\\BCD", WindowsDir);
        if (FileExists(TestPath))
            StringCchCopyW(BcdTemplateSrc, ARRAYSIZE(BcdTemplateSrc), TestPath);
        else
        {
            WCHAR CurrentWinDir[MAX_PATH];
            if (GetWindowsDirectoryW(CurrentWinDir, MAX_PATH))
            {
                StringCchPrintfW(TestPath, ARRAYSIZE(TestPath), L"%s\\system32\\config\\BCD-Template", CurrentWinDir);
                if (FileExists(TestPath))
                    StringCchCopyW(BcdTemplateSrc, ARRAYSIZE(BcdTemplateSrc), TestPath);
            }
        }
    }

    if (BootmgfwSrc[0] == L'\0')
    {
        fputws(L"Failure when attempting to locate boot files (bootmgfw.efi).\n", stderr);
        return ERROR_FILE_NOT_FOUND;
    }

    if (BcdTemplateSrc[0] == L'\0')
    {
        fputws(L"Failure when attempting to locate BCD template.\n", stderr);
        return ERROR_FILE_NOT_FOUND;
    }

    StringCchPrintfW(TargetDir, ARRAYSIZE(TargetDir), L"%s\\EFI", TargetVolume);
    EnsureDirectory(TargetDir);

    StringCchPrintfW(TargetDir, ARRAYSIZE(TargetDir), L"%s\\EFI\\Microsoft", TargetVolume);
    EnsureDirectory(TargetDir);

    StringCchPrintfW(TargetDir, ARRAYSIZE(TargetDir), L"%s\\EFI\\Microsoft\\Boot", TargetVolume);
    EnsureDirectory(TargetDir);

    StringCchPrintfW(TargetDir, ARRAYSIZE(TargetDir), L"%s\\EFI\\Microsoft\\Boot\\Fonts", TargetVolume);
    EnsureDirectory(TargetDir);

    StringCchPrintfW(TargetDir, ARRAYSIZE(TargetDir), L"%s\\EFI\\Microsoft\\Boot\\Resources", TargetVolume);
    EnsureDirectory(TargetDir);

    StringCchPrintfW(TargetDir, ARRAYSIZE(TargetDir), L"%s\\EFI\\Boot", TargetVolume);
    EnsureDirectory(TargetDir);

    StringCchPrintfW(TargetDir, ARRAYSIZE(TargetDir), L"%s\\EFI\\LiberNT", TargetVolume);
    EnsureDirectory(TargetDir);

    StringCchPrintfW(TargetDir, ARRAYSIZE(TargetDir), L"%s\\EFI\\LiberNT\\Boot", TargetVolume);
    EnsureDirectory(TargetDir);

    StringCchPrintfW(TargetFile, ARRAYSIZE(TargetFile), L"%s\\EFI\\LiberNT\\Boot\\bootmgfw.efi", TargetVolume);
    if (!CopyFileW(BootmgfwSrc, TargetFile, FALSE))
    {
        fputws(L"Failure when attempting to copy boot files.\n", stderr);
        return GetLastError();
    }

#if defined(_M_ARM64) || defined(__aarch64__)
    StringCchPrintfW(TargetFile, ARRAYSIZE(TargetFile), L"%s\\EFI\\Boot\\bootaa64.efi", TargetVolume);
#elif defined(_M_IX86) || defined(__i386__)
    StringCchPrintfW(TargetFile, ARRAYSIZE(TargetFile), L"%s\\EFI\\Boot\\bootia32.efi", TargetVolume);
#elif defined(_M_RISCV64) || defined(__riscv)
    StringCchPrintfW(TargetFile, ARRAYSIZE(TargetFile), L"%s\\EFI\\Boot\\bootriscv64.efi", TargetVolume);
#else
    StringCchPrintfW(TargetFile, ARRAYSIZE(TargetFile), L"%s\\EFI\\Boot\\bootx64.efi", TargetVolume);
#endif
    CopyFileW(BootmgfwSrc, TargetFile, FALSE);

    StringCchPrintfW(TestPath, ARRAYSIZE(TestPath), L"%s\\system32\\winload.efi", WindowsDir);
    StringCchPrintfW(TargetFile, ARRAYSIZE(TargetFile), L"%s\\EFI\\LiberNT\\Boot\\winload.efi", TargetVolume);
    if (!FileExists(TestPath) || !CopyFileW(TestPath, TargetFile, FALSE))
    {
        fputws(L"Failure when attempting to copy the operating system loader.\n", stderr);
        return GetLastError();
    }

    StringCchPrintfW(TargetFile, ARRAYSIZE(TargetFile), L"%s\\EFI\\Microsoft\\Boot\\BCD", TargetVolume);
    if (!CopyFileW(BcdTemplateSrc, TargetFile, FALSE))
    {
        fputws(L"Failure when attempting to copy the Microsoft boot store.\n", stderr);
        return GetLastError();
    }
    if (!UpdateBcdStore(TargetFile, PartitionDevice))
    {
        fputws(L"Failure when attempting to configure the Microsoft boot store.\n", stderr);
        return ERROR_WRITE_FAULT;
    }

    StringCchPrintfW(TargetFile, ARRAYSIZE(TargetFile), L"%s\\EFI\\LiberNT\\Boot\\BCD", TargetVolume);
    if (!CopyFileW(BcdTemplateSrc, TargetFile, FALSE))
    {
        fputws(L"Failure when attempting to copy the LiberNT boot store.\n", stderr);
        return GetLastError();
    }
    if (!UpdateBcdStore(TargetFile, PartitionDevice))
    {
        fputws(L"Failure when attempting to configure the LiberNT boot store.\n", stderr);
        return ERROR_WRITE_FAULT;
    }

    StringCchPrintfW(TestPath, ARRAYSIZE(TestPath), L"%s\\Boot\\Fonts\\*", WindowsDir);
    StringCchPrintfW(TargetDir, ARRAYSIZE(TargetDir), L"%s\\EFI\\Microsoft\\Boot\\Fonts", TargetVolume);
    CopyMatchingFiles(TestPath, TargetDir);

    StringCchPrintfW(TestPath, ARRAYSIZE(TestPath), L"%s\\Boot\\Resources\\*", WindowsDir);
    StringCchPrintfW(TargetDir, ARRAYSIZE(TargetDir), L"%s\\EFI\\Microsoft\\Boot\\Resources", TargetVolume);
    CopyMatchingFiles(TestPath, TargetDir);

    if (Verbose)
    {
        wprintf(L"Source: %s\n", WindowsDir);
        wprintf(L"Target: %s\n", TargetVolume);
        wprintf(L"Boot loader: %s\n", BootmgfwSrc);
        wprintf(L"BCD source: %s\n", BcdTemplateSrc);
        StringCchPrintfW(TestPath, ARRAYSIZE(TestPath), L"%s\\system32\\winload.efi", WindowsDir);
        wprintf(L"OS loader: %s%s\n", TestPath, FileExists(TestPath) ? L"" : L" (not found)");
    }

    fputws(L"Boot files successfully created.\n", stdout);
    return ERROR_SUCCESS;
}

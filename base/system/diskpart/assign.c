/*
 * PROJECT:         ReactOS DiskPart
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            base/system/diskpart/assign.c
 * PURPOSE:         Manages all the partitions of the OS in an interactive way.
 * PROGRAMMERS:     Lee Schroeder
 */

#include "diskpart.h"

#define NDEBUG
#include <debug.h>

EXIT_CODE
assign_main(
    _In_ INT argc,
    _In_ LPWSTR *argv)
{
    PWSTR pszSuffix = NULL;
    PWSTR DriveNumber = NULL;
    WCHAR DriveLetter = UNICODE_NULL;
    INT i, nExclusive = 0;
    BOOL bResult;

    DPRINT1("assign_main()\n");

    if (CurrentVolume == NULL)
    {
        ConResPuts(StdOut, IDS_SELECT_NO_VOLUME);
        return EXIT_SUCCESS;
    }

    for (i = 1; i < argc; i++)
    {
        if (_wcsicmp(argv[i], L"noerr") == 0)
        {
            /* noerr */
            DPRINT("NoErr\n", pszSuffix);
            ConPuts(StdOut, L"The NOERR option is not supported yet!\n");
#if 0
            bNoErr = TRUE;
#endif
        }
    }

    for (i = 1; i < argc; i++)
    {
        if (HasPrefix(argv[i], L"letter=", &pszSuffix))
        {
            if (wcslen(pszSuffix) == 1)
            {
                DriveLetter = towupper(*pszSuffix);
                nExclusive++;
            }
            else
            {
                ConResPuts(StdErr, IDS_ERROR_INVALID_ARGS);
                return EXIT_SUCCESS; 
            }
        }
        else if (HasPrefix(argv[i], L"mount=", &pszSuffix))
        {
            DPRINT("Mount\n", pszSuffix);
            ConPuts(StdOut, L"The MOUNT option is not supported yet!\n");
            nExclusive++;
        }
        else if (HasPrefix(argv[i], L"number=", &pszSuffix))
        {
            if (IsValidDriveNumber(pszSuffix))
            {
                DriveNumber = pszSuffix;
                nExclusive++;
            }
            else
            {
                ConResPuts(StdErr, IDS_ERROR_INVALID_ARGS);
                return EXIT_SUCCESS;
            }
        }
        else if (_wcsicmp(argv[i], L"noerr") == 0)
        {
            /* noerr - Already handled above */
        }
        else
        {
            ConResPuts(StdErr, IDS_ERROR_INVALID_ARGS);
            return EXIT_SUCCESS;
        }
    }

    if (CurrentVolume->IsBoot || CurrentVolume->IsSystem)
    {
        ConResPuts(StdOut, IDS_ASSIGN_SYSTEM_VOLUME);
        return EXIT_SUCCESS;
    }

    if (nExclusive > 1)
    {
        ConResPuts(StdErr, IDS_ERROR_INVALID_ARGS);
        return EXIT_SUCCESS;
    }

    DPRINT1("VolumeName: %S\n", CurrentVolume->VolumeName);
    DPRINT1("DeviceName: %S\n", CurrentVolume->DeviceName);
    DPRINT1("DriveLetter: %C\n", CurrentVolume->DriveLetter);
    DPRINT1("DriveNumber: %S\n", CurrentVolume->DriveNumber);

    if (DriveLetter != UNICODE_NULL)
    {
        DPRINT1("DriveLetter: %C\n", DriveLetter);

        if ((DriveLetter < L'C') || (DriveLetter > L'Z'))
        {
            ConResPuts(StdOut, IDS_ASSIGN_INVALID_LETTER);
            return EXIT_SUCCESS;
        }

        if (DriveLetter == CurrentVolume->DriveLetter && CurrentVolume->DriveNumber == NULL)
        {
            ConResPuts(StdOut, IDS_ASSIGN_ALREADY_ASSIGNED);
            return EXIT_SUCCESS;
        }
    }
    else if (DriveNumber != NULL &&
             CurrentVolume->DriveNumber != NULL &&
             wcscmp(DriveNumber, CurrentVolume->DriveNumber) == 0 &&
             CurrentVolume->DriveLetter == UNICODE_NULL)
    {
        ConResPuts(StdOut, IDS_ASSIGN_ALREADY_ASSIGNED);
        return EXIT_SUCCESS;
    }

    if (DriveNumber != NULL)
    {
        PWSTR NewDriveNumber;
        BOOL NewNumberCreated = FALSE;
        BOOL OldNumberDeleted = FALSE;
        BOOL OldLetterDeleted = FALSE;

        if (CurrentVolume->DriveNumber != NULL &&
            wcscmp(DriveNumber, CurrentVolume->DriveNumber) == 0)
        {
            WCHAR OldDriveLetter = CurrentVolume->DriveLetter;
            if (DeleteDriveLetter(CurrentVolume->DriveLetter) == FALSE)
            {
                ConResPuts(StdOut, IDS_ASSIGN_FAIL);
                return EXIT_SUCCESS;
            }

            CurrentVolume->DriveLetter = UNICODE_NULL;
            NotifyDriveLetterChange(OldDriveLetter, FALSE);
            ConResPuts(StdOut, IDS_ASSIGN_SUCCESS);
            return EXIT_SUCCESS;
        }

        NewDriveNumber = HeapAlloc(GetProcessHeap(),
                                   0,
                                   (wcslen(DriveNumber) + 1) * sizeof(WCHAR));
        if (NewDriveNumber == NULL)
        {
            ConResPuts(StdOut, IDS_ASSIGN_FAIL);
            return EXIT_SUCCESS;
        }
        wcscpy(NewDriveNumber, DriveNumber);

        if (AssignDriveNumber(CurrentVolume->DeviceName, DriveNumber) == FALSE)
        {
            HeapFree(GetProcessHeap(), 0, NewDriveNumber);
            ConResPuts(StdOut, IDS_ASSIGN_FAIL);
            return EXIT_SUCCESS;
        }
        NewNumberCreated = TRUE;

        if (CurrentVolume->DriveNumber != NULL)
        {
            if (DeleteDriveNumber(CurrentVolume->DriveNumber) == FALSE)
                goto RollbackDriveNumber;
            OldNumberDeleted = TRUE;
        }

        if (CurrentVolume->DriveLetter != UNICODE_NULL)
        {
            if (DeleteDriveLetter(CurrentVolume->DriveLetter) == FALSE)
                goto RollbackDriveNumber;
            OldLetterDeleted = TRUE;
        }

        PWSTR OldDriveNumber = CurrentVolume->DriveNumber;
        WCHAR OldDriveLetter = CurrentVolume->DriveLetter;
        if (OldDriveNumber != NULL)
            NotifyDriveNumberChange(OldDriveNumber, FALSE);
        if (OldDriveLetter != UNICODE_NULL)
            NotifyDriveLetterChange(OldDriveLetter, FALSE);
        if (OldDriveNumber != NULL)
            HeapFree(GetProcessHeap(), 0, CurrentVolume->DriveNumber);
        CurrentVolume->DriveNumber = NewDriveNumber;
        CurrentVolume->DriveLetter = UNICODE_NULL;
        NotifyDriveNumberChange(NewDriveNumber, TRUE);
        ConResPuts(StdOut, IDS_ASSIGN_SUCCESS);
        return EXIT_SUCCESS;

RollbackDriveNumber:
        if (OldLetterDeleted &&
            AssignDriveLetter(CurrentVolume->DeviceName, CurrentVolume->DriveLetter) == FALSE)
        {
            DPRINT1("Failed to restore drive letter %C after numeric assignment failure\n",
                    CurrentVolume->DriveLetter);
        }
        if (OldNumberDeleted &&
            AssignDriveNumber(CurrentVolume->DeviceName, CurrentVolume->DriveNumber) == FALSE)
        {
            DPRINT1("Failed to restore drive number %S after numeric assignment failure\n",
                    CurrentVolume->DriveNumber);
        }
        if (NewNumberCreated && DeleteDriveNumber(DriveNumber) == FALSE)
            DPRINT1("Failed to remove drive number %S after assignment rollback\n", DriveNumber);
        HeapFree(GetProcessHeap(), 0, NewDriveNumber);
        ConResPuts(StdOut, IDS_ASSIGN_FAIL);
        return EXIT_SUCCESS;
    }

    if (CurrentVolume->DriveNumber != NULL)
    {
        bResult = DeleteDriveNumber(CurrentVolume->DriveNumber);
        if (bResult == FALSE)
        {
            ConResPuts(StdOut, IDS_ASSIGN_FAIL);
            return EXIT_SUCCESS;
        }

        HeapFree(GetProcessHeap(), 0, CurrentVolume->DriveNumber);
        CurrentVolume->DriveNumber = NULL;
    }

    if (CurrentVolume->DriveLetter != UNICODE_NULL)
    {
        /* Remove the current drive letter */
        bResult = DeleteDriveLetter(CurrentVolume->DriveLetter);
        if (bResult == FALSE)
        {
            ConResPuts(StdOut, IDS_ASSIGN_FAIL);
            return EXIT_SUCCESS;
        }

        CurrentVolume->DriveLetter = UNICODE_NULL;
    }

    if (DriveLetter != UNICODE_NULL)
    {
        /* Assign the new drive letter */
        bResult = AssignDriveLetter(CurrentVolume->DeviceName,
                                    DriveLetter);
        if (bResult == FALSE)
        {
            ConResPuts(StdOut, IDS_ASSIGN_FAIL);
            return EXIT_SUCCESS;
        }

        CurrentVolume->DriveLetter = DriveLetter;
    }
    else
    {
        bResult = AssignNextDriveLetter(CurrentVolume->DeviceName,
                                        &CurrentVolume->DriveLetter);
        if (bResult == FALSE)
        {
            ConResPuts(StdOut, IDS_ASSIGN_FAIL);
            return EXIT_SUCCESS;
        }

        if (CurrentVolume->DriveLetter == UNICODE_NULL)
        {
            ConResPuts(StdOut, IDS_ASSIGN_NO_MORE_LETTER);
            return EXIT_SUCCESS;
        }
    }

    ConResPuts(StdOut, IDS_ASSIGN_SUCCESS);

    return EXIT_SUCCESS;
}

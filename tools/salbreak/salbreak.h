// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Returns the SID (as a string) for our process.
// The returned SID must be freed by calling LocalFree.
//   LPTSTR sid;
//   if (GetStringSid(&sid))
//     LocalFree(sid);
BOOL GetStringSid(LPTSTR* stringSid);

// Returns an MD5 hash computed from the SID, giving us a 16-byte array from a variable-length SID.
// 'sidMD5' must point to a 16-byte array.
// On success returns TRUE; otherwise returns FALSE and clears the whole 'sidMD5' array.
BOOL GetSidMD5(BYTE* sidMD5);

// Prepares SECURITY_ATTRIBUTES so objects created with them (mutex, mapped memory) are secure.
// This means the Everyone group is denied WRITE_DAC | WRITE_OWNER access; everything else is allowed.
// This is a better security class than "NULL DACL", where the object is fully open to everyone.
// Can be called on any OS; returns a pointer on W2K and later, otherwise returns NULL.
// If 'psidEveryone' or 'paclNewDacl' is returned as non-NULL, it must be destroyed.
SECURITY_ATTRIBUTES* CreateAccessableSecurityAttributes(SECURITY_ATTRIBUTES* sa, SECURITY_DESCRIPTOR* sd,
                                                        DWORD allowedAccessMask, PSID* psidEveryone, PACL* paclNewDacl);

// On success returns TRUE and fills the DWORD referenced by 'integrityLevel'.
// Otherwise (on failure or on OS versions older than Vista) returns FALSE.
BOOL GetProcessIntegrityLevel(DWORD* integrityLevel);

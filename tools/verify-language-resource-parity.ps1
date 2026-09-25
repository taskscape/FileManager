param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,
    # Builds without OPENSAL_BUILD_DIR place each project's output beside its own project file,
    # so callers can search the source tree and keep only one configuration's language directories.
    [string]$IncludePattern = '*'
)

$ErrorActionPreference = 'Stop'

# A translated .slg that lacks a resource present in english.slg fails only at run time, and only for
# users whose locale selects that translation (a Polish build once exited silently at startup because
# its viewer menus were missing). Compare every built translation with its English sibling instead.
if (-not (Test-Path -LiteralPath $BuildDirectory -PathType Container)) {
    throw "The build directory was not found: $BuildDirectory"
}

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class LanguageResourceInventory
{
    const uint LOAD_LIBRARY_AS_DATAFILE = 0x2;
    const uint LOAD_LIBRARY_AS_IMAGE_RESOURCE = 0x20;
    const long RT_STRING = 6;

    delegate bool EnumTypeProc(IntPtr module, IntPtr type, IntPtr param);
    delegate bool EnumNameProc(IntPtr module, IntPtr type, IntPtr name, IntPtr param);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern IntPtr LoadLibraryEx(string fileName, IntPtr file, uint flags);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    static extern bool FreeLibrary(IntPtr module);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    static extern bool EnumResourceTypes(IntPtr module, EnumTypeProc proc, IntPtr param);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    static extern bool EnumResourceNames(IntPtr module, IntPtr type, EnumNameProc proc, IntPtr param);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    static extern IntPtr FindResource(IntPtr module, IntPtr name, IntPtr type);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    static extern IntPtr LockResource(IntPtr data);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    static extern uint SizeofResource(IntPtr module, IntPtr resource);

    static bool IsIntResource(IntPtr value) { return ((ulong)value.ToInt64() >> 16) == 0; }
    static string Describe(IntPtr value) { return IsIntResource(value) ? value.ToInt64().ToString() : Marshal.PtrToStringUni(value); }

    // String tables are stored in blocks of 16; a block can exist while individual strings are missing,
    // so string resources are listed by string ID instead of by block.
    static void AddStrings(IntPtr module, IntPtr type, IntPtr name, SortedSet<string> keys)
    {
        IntPtr resource = FindResource(module, name, type);
        IntPtr data = LockResource(LoadResource(module, resource));
        int size = (int)SizeofResource(module, resource);
        int offset = 0;
        long firstId = (name.ToInt64() - 1) * 16;
        for (int index = 0; index < 16 && offset + 2 <= size; index++)
        {
            int length = Marshal.ReadInt16(data, offset);
            offset += 2 + length * 2;
            if (length > 0)
                keys.Add("STRING/" + (firstId + index));
        }
    }

    public static string[] List(string path)
    {
        IntPtr module = LoadLibraryEx(path, IntPtr.Zero, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        if (module == IntPtr.Zero)
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "Cannot load " + path);
        var keys = new SortedSet<string>(StringComparer.Ordinal);
        try
        {
            EnumResourceTypes(module, (m, type, p) =>
            {
                EnumResourceNames(m, type, (m2, t2, name, p2) =>
                {
                    if (IsIntResource(t2) && t2.ToInt64() == RT_STRING)
                        AddStrings(m2, t2, name, keys);
                    else
                        keys.Add(Describe(t2) + "/" + Describe(name));
                    return true;
                }, IntPtr.Zero);
                return true;
            }, IntPtr.Zero);
        }
        finally
        {
            FreeLibrary(module);
        }
        var result = new string[keys.Count];
        keys.CopyTo(result);
        return result;
    }
}
'@

$errors = [System.Collections.Generic.List[string]]::new()
$englishFiles = @(Get-ChildItem -LiteralPath $BuildDirectory -Recurse -File -Filter 'english.slg' |
    Where-Object { $_.FullName -like $IncludePattern })
if ($englishFiles.Count -eq 0) {
    throw "No english.slg was found below $BuildDirectory; build the language projects first."
}

$comparedCount = 0
foreach ($englishFile in $englishFiles) {
    $englishKeys = [System.Collections.Generic.HashSet[string]]::new([string[]][LanguageResourceInventory]::List($englishFile.FullName))
    $translations = Get-ChildItem -LiteralPath $englishFile.DirectoryName -File -Filter '*.slg' |
        Where-Object { $_.Name -ne 'english.slg' }
    foreach ($translation in $translations) {
        $comparedCount++
        $translatedKeys = [System.Collections.Generic.HashSet[string]]::new([string[]][LanguageResourceInventory]::List($translation.FullName))
        $missing = @($englishKeys | Where-Object { -not $translatedKeys.Contains($_) } | Sort-Object)
        $extra = @($translatedKeys | Where-Object { -not $englishKeys.Contains($_) } | Sort-Object)
        if ($missing.Count -gt 0) {
            $errors.Add("$($translation.FullName) lacks resources present in english.slg: $($missing -join ', ')")
        }
        if ($extra.Count -gt 0) {
            $errors.Add("$($translation.FullName) has resources absent from english.slg: $($extra -join ', ')")
        }
    }
}

if ($errors.Count -gt 0) {
    $errors | ForEach-Object { Write-Host $_ -ForegroundColor Red }
    throw "Language resource parity failed for $($errors.Count) finding(s)."
}

Write-Host "Language resource parity verified for $comparedCount translation(s) across $($englishFiles.Count) language directory(ies)."

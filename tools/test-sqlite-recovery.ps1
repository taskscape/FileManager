[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SqliteDll
)

$ErrorActionPreference = 'Stop'
$sqliteDllPath = (Resolve-Path -LiteralPath $SqliteDll).Path

if ((Split-Path -Leaf $sqliteDllPath) -ne 'sqlite.dll') {
    throw "Expected sqlite.dll, received '$sqliteDllPath'."
}

# An x64 DLL cannot be exercised by a 32-bit PowerShell host; relay to the installed x64 host without changing the test target.
if (-not [Environment]::Is64BitProcess -and $sqliteDllPath -match '(?i)(Debug|Release)_x64') {
    $x64PowerShell = 'C:\Program Files\PowerShell\7\pwsh.exe'
    if (-not (Test-Path -LiteralPath $x64PowerShell)) {
        throw "The x64 SQLite probe requires '$x64PowerShell'."
    }

    & $x64PowerShell -NoProfile -File $PSCommandPath -SqliteDll $sqliteDllPath
    exit $LASTEXITCODE
}

# Embed the resolved build artifact so the probe cannot bind an ambient sqlite.dll from PATH.
$sqliteDllForPInvoke = $sqliteDllPath.Replace('\', '\\')

$nativeSource = @'
using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;

public static class SqliteRecoveryProbe
{
    private const int SQLITE_OK = 0;
    private const int SQLITE_ROW = 100;
    private const int SQLITE_OPEN_READWRITE = 0x00000002;
    private const int SQLITE_OPEN_CREATE = 0x00000004;

    static SqliteRecoveryProbe()
    {
        NativeLibrary.SetDllImportResolver(typeof(SqliteRecoveryProbe).Assembly, ResolveSqlite);
    }

    private static IntPtr ResolveSqlite(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
    {
        // The dynamically compiled probe has no assembly path, so return the explicit build artifact before PowerShell's loader runs.
        return libraryName == "sqlite3.dll" ? NativeLibrary.Load("__SQLITE_DLL_PATH__") : IntPtr.Zero;
    }

    [DllImport("sqlite3.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr sqlite3_libversion();

    [DllImport("sqlite3.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int sqlite3_compileoption_used(string option);

    [DllImport("sqlite3.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int sqlite3_open_v2(string filename, out IntPtr database, int flags, IntPtr vfs);

    [DllImport("sqlite3.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int sqlite3_close(IntPtr database);

    [DllImport("sqlite3.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int sqlite3_exec(IntPtr database, string sql, IntPtr callback, IntPtr callbackArgument, out IntPtr error);

    [DllImport("sqlite3.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int sqlite3_prepare_v2(IntPtr database, string sql, int length, out IntPtr statement, IntPtr tail);

    [DllImport("sqlite3.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int sqlite3_step(IntPtr statement);

    [DllImport("sqlite3.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int sqlite3_finalize(IntPtr statement);

    [DllImport("sqlite3.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int sqlite3_column_int(IntPtr statement, int column);

    [DllImport("sqlite3.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr sqlite3_column_text(IntPtr statement, int column);

    [DllImport("sqlite3.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern void sqlite3_free(IntPtr pointer);

    public static void Run(string databasePath)
    {
        var version = Marshal.PtrToStringUTF8(sqlite3_libversion());
        if (version != "3.53.4") throw new InvalidOperationException("Expected SQLite 3.53.4, loaded " + version + ".");
        RequireCompileOption("DQS=0");
        RequireCompileOption("ENABLE_API_ARMOR");
        RequireCompileOption("DEFAULT_SYNCHRONOUS=2");
        RequireCompileOption("DEFAULT_WAL_SYNCHRONOUS=2");
        RequireCompileOption("DEFAULT_WAL_AUTOCHECKPOINT=1000");

        IntPtr database = IntPtr.Zero;
        try
        {
            Open(databasePath, out database);
            Execute(database, "PRAGMA journal_mode=WAL;");
            Execute(database, "PRAGMA synchronous=FULL;");
            Execute(database, "PRAGMA wal_autocheckpoint=1000;");
            Execute(database, "CREATE TABLE committed_rows (id INTEGER PRIMARY KEY, payload BLOB NOT NULL);");
            Execute(database, "INSERT INTO committed_rows VALUES (1, randomblob(2048));");
            Execute(database, "BEGIN IMMEDIATE;");
            Execute(database, "INSERT INTO committed_rows VALUES (2, randomblob(2048));");
        }
        finally
        {
            if (database != IntPtr.Zero) Require(sqlite3_close(database), "close interrupted transaction");
        }

        try
        {
            Open(databasePath, out database);
            if (QueryInt(database, "SELECT count(*) FROM committed_rows;") != 1)
                throw new InvalidOperationException("Interrupted transaction exposed an uncommitted row.");
            RequireIntegrityCheck(database, true);
            Execute(database, "PRAGMA wal_checkpoint(TRUNCATE);");
        }
        finally
        {
            if (database != IntPtr.Zero) Require(sqlite3_close(database), "close recovered database");
            database = IntPtr.Zero;
        }

        var corruptPath = databasePath + ".corrupt";
        File.Copy(databasePath, corruptPath, true);
        CorruptPage(corruptPath);

        try
        {
            Open(corruptPath, out database);
            RequireIntegrityCheck(database, false);
        }
        finally
        {
            if (database != IntPtr.Zero) Require(sqlite3_close(database), "close corrupted database");
        }
    }

    private static void Open(string databasePath, out IntPtr database)
    {
        database = IntPtr.Zero;
        Require(sqlite3_open_v2(databasePath, out database, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, IntPtr.Zero), "open database");
    }

    private static void Execute(IntPtr database, string sql)
    {
        IntPtr error;
        var result = sqlite3_exec(database, sql, IntPtr.Zero, IntPtr.Zero, out error);
        if (error != IntPtr.Zero) sqlite3_free(error);
        Require(result, sql);
    }

    private static int QueryInt(IntPtr database, string sql)
    {
        IntPtr statement;
        Require(sqlite3_prepare_v2(database, sql, -1, out statement, IntPtr.Zero), sql);
        try
        {
            if (sqlite3_step(statement) != SQLITE_ROW) throw new InvalidOperationException(sql + " did not return a row.");
            return sqlite3_column_int(statement, 0);
        }
        finally
        {
            Require(sqlite3_finalize(statement), "finalize integer query");
        }
    }

    private static void RequireIntegrityCheck(IntPtr database, bool expectedClean)
    {
        IntPtr statement;
        var prepared = sqlite3_prepare_v2(database, "PRAGMA integrity_check;", -1, out statement, IntPtr.Zero);
        var clean = false;
        if (prepared == SQLITE_OK)
        {
            try
            {
                if (sqlite3_step(statement) == SQLITE_ROW)
                    clean = Marshal.PtrToStringUTF8(sqlite3_column_text(statement, 0)) == "ok";
            }
            finally
            {
                sqlite3_finalize(statement);
            }
        }

        if (clean != expectedClean)
            throw new InvalidOperationException(expectedClean ? "Integrity check did not return ok." : "CorruptPage was not detected by integrity_check.");
    }

    private static void CorruptPage(string databasePath)
    {
        // Page two is the first user-table b-tree page for this single-table fixture; an invalid page type must be detected.
        using (var stream = new FileStream(databasePath, FileMode.Open, FileAccess.Write, FileShare.None))
        {
            if (stream.Length < 8192) throw new InvalidOperationException("Database fixture is too small to corrupt page two.");
            stream.Position = 4096;
            stream.WriteByte(0);
            stream.Flush(true);
        }
    }

    private static void RequireCompileOption(string option)
    {
        // Verify the loaded DLL, rather than only the project text, retained the documented recovery profile.
        if (sqlite3_compileoption_used(option) != 1) throw new InvalidOperationException("Missing SQLite compile option " + option + ".");
    }

    private static void Require(int result, string operation)
    {
        if (result != SQLITE_OK) throw new InvalidOperationException(operation + " returned SQLite error " + result + ".");
    }
}
'@

Add-Type -TypeDefinition $nativeSource.Replace('__SQLITE_DLL_PATH__', $sqliteDllForPInvoke)

$testDirectory = Join-Path $env:TEMP ("filemanager-sqlite-recovery-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testDirectory | Out-Null

try {
    # This test owns its GUID-named database, so interruption and page corruption cannot affect user data.
    [SqliteRecoveryProbe]::Run((Join-Path $testDirectory 'owned.db'))
    Write-Output 'SQLite WAL recovery and corrupt-page detection passed.'
}
finally {
    Remove-Item -LiteralPath $testDirectory -Recurse -Force -ErrorAction SilentlyContinue
}

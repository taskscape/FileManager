#pragma once

#ifdef _DEBUG
void ExecLogStartupBegin();
void ExecLogStartupPhase(const char* phase);
void ExecLogStartupComplete();

void ExecLogPluginLoadStart(const char* dllName, const char* pluginName);
void ExecLogPluginLoadResult(const char* dllName, const char* pluginName, BOOL success);

void ExecLogFileListingStart(const char* path, BOOL isPlugin, const char* fsName);
void ExecLogFileListingResult(const char* path, BOOL success, int fileCount, int dirCount,
                              BOOL isPlugin, const char* fsName);

void ExecLogFileOperationStart(const char* operation, const char* source, const char* target);
void ExecLogFileOperationResult(const char* operation, const char* source, const char* target, BOOL success);

void ExecLogFeatureStart(const char* feature, const char* detail);
void ExecLogFeatureResult(const char* feature, const char* detail, BOOL success);
#else
inline void ExecLogStartupBegin() {}
inline void ExecLogStartupPhase(const char* phase) { (void)phase; }
inline void ExecLogStartupComplete() {}

inline void ExecLogPluginLoadStart(const char* dllName, const char* pluginName)
{
    (void)dllName;
    (void)pluginName;
}
inline void ExecLogPluginLoadResult(const char* dllName, const char* pluginName, BOOL success)
{
    (void)dllName;
    (void)pluginName;
    (void)success;
}

inline void ExecLogFileListingStart(const char* path, BOOL isPlugin, const char* fsName)
{
    (void)path;
    (void)isPlugin;
    (void)fsName;
}
inline void ExecLogFileListingResult(const char* path, BOOL success, int fileCount, int dirCount,
                                     BOOL isPlugin, const char* fsName)
{
    (void)path;
    (void)success;
    (void)fileCount;
    (void)dirCount;
    (void)isPlugin;
    (void)fsName;
}

inline void ExecLogFileOperationStart(const char* operation, const char* source, const char* target)
{
    (void)operation;
    (void)source;
    (void)target;
}
inline void ExecLogFileOperationResult(const char* operation, const char* source, const char* target, BOOL success)
{
    (void)operation;
    (void)source;
    (void)target;
    (void)success;
}

inline void ExecLogFeatureStart(const char* feature, const char* detail)
{
    (void)feature;
    (void)detail;
}

inline void ExecLogFeatureResult(const char* feature, const char* detail, BOOL success)
{
    (void)feature;
    (void)detail;
    (void)success;
}
#endif

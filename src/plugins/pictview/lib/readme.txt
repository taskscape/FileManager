08/2026

PVW32Cnv.lib was removed in 11/2023 because its source code is not available,
which violates the GPL license. The plug-in has since been ported to the
Windows Imaging Component, so neither PVW32Cnv.dll nor the SalPVEnv.exe
envelope that hosted the 32-bit build of it is needed any more, and the
project compiles from this repository alone.

PVW32DLL.h stays because it still defines the engine contract the plug-in is
written against: PVImageInfo, PVSaveImageInfo, the PVF_/PVCS_/PVC_ code space
and the rest. The implementation behind that contract now lives in
..\PVWicEngine.cpp and ..\PVWicSave.cpp.

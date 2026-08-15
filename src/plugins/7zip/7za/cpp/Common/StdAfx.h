// StdAfx.h - Precompiled Header for 7za project

#ifndef ZIP7_INC_STDAFX_H
#define ZIP7_INC_STDAFX_H

// Include the base Common header which includes Precomp.h, Common0.h, and MyWindows.h
#include "Common.h"

// Standard C++ headers frequently used (these are expensive to parse repeatedly)
#include <new>
#include <exception>

// Windows headers (on Windows builds) - these are very expensive to compile
#ifdef _WIN32
// Windows.h is already included via Common.h -> MyWindows.h -> 7zWindows.h
// Additional frequently used Windows headers
#include <ole2.h>
#include <objbase.h>
#endif

// Frequently used 7-Zip common headers
#include "NewHandler.h"
#include "MyCom.h"
#include "MyTypes.h"
#include "MyBuffer.h"
#include "MyString.h"
#include "MyVector.h"
#include "IntToString.h"
#include "MyException.h"

#endif

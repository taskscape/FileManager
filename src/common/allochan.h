// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Installs a handler to handle situations when memory runs out during operator new
// or malloc calls (which are used by calloc, realloc and others, see help). Ensures
// that neither operator new nor malloc will ever return NULL without user knowledge.
// Displays a messagebox with "insufficient memory" error and the user can retry the
// memory allocation after closing other applications. The user can also terminate the
// process or let the allocation error pass to the application (operator new or malloc
// will return NULL, large memory block allocations should be prepared for this, otherwise
// a crash will occur - the user is informed about this).

// Setting the localized form of the insufficient memory message and warning messages
// (if the string should not be changed, use NULL); expected content:
// message:
// Insufficient memory to allocate %u bytes. Try to release some memory (e.g.
// close some running application) and click Retry. If it does not help, you can
// click Ignore to pass memory allocation error to this application or click Abort
// to terminate this application.
// title: (used for both: "message" and "warning")
// we recommend using the application name, so the user knows which application is complaining
// warningIgnore:
// Do you really want to pass memory allocation error to this application?\n\n
// WARNING: Application may crash and then all unsaved data will be lost!\n
// HINT: We recommend to risk this action only if the application is trying to
// allocate extra large block of memory (i.e. more than 500 MB).
// warningAbort:
// Do you really want to terminate this application?\n\nWARNING: All unsaved data will be lost!
void SetAllocHandlerMessage(const TCHAR* message, const TCHAR* title,
                            const TCHAR* warningIgnore, const TCHAR* warningAbort);

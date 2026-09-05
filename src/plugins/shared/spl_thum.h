// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

//****************************************************************************
//
// Copyright (c) 2023 Taskscape Ltd
//
// This is a part of the Open Salamander SDK library.
//
//****************************************************************************

#pragma once

#ifdef _MSC_VER
#pragma pack(push, enter_include_spl_thum) // keep structures independent of the current alignment setting
#pragma pack(4)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a4
#endif // __BORLANDC__

//
// ****************************************************************************
// CSalamanderThumbnailMakerAbstract
//

// information about the image used to generate a thumbnail; these flags are used
// in CSalamanderThumbnailMakerAbstract::SetParameters():
#define SSTHUMB_MIRROR_HOR 1                                            // image needs to be mirrored horizontally
#define SSTHUMB_MIRROR_VERT 2                                           // image needs to be mirrored vertically
#define SSTHUMB_ROTATE_90CW 4                                           // image needs to be rotated 90 degrees clockwise
#define SSTHUMB_ROTATE_180 (SSTHUMB_MIRROR_VERT | SSTHUMB_MIRROR_HOR)   // image needs to be rotated 180 degrees
#define SSTHUMB_ROTATE_90CCW (SSTHUMB_ROTATE_90CW | SSTHUMB_ROTATE_180) // image needs to be rotated 90 degrees counterclockwise
// image is in worse quality or smaller than needed, Salamander after completing first round
// of getting "fast" thumbnails tries to get "quality" thumbnail for this image
#define SSTHUMB_ONLY_PREVIEW 8

// Host API a thumbnail loader uses to feed pixel buffers and image orientation.
class CSalamanderThumbnailMakerAbstract
{
public:
    // setting parameters for image processing when creating thumbnail; must be called
    // as the first method of this interface; 'picWidth' and 'picHeight' are dimensions
    // of the processed image (in pixels); 'flags' is a combination of SSTHUMB_XXX flags,
    // which provides information about image passed in parameter 'buffer' in method
    // ProcessBuffer; returns TRUE, if buffers for reduction were successfully allocated
    // and it is possible to subsequently call ProcessBuffer; if returns FALSE, error occurred
    // and thumbnail loading must be stopped
    virtual BOOL WINAPI SetParameters(int picWidth, int picHeight, DWORD flags) = 0;

    // processes a part of the image in 'buffer' (the processed part of the image is stored
    // by rows from top to bottom, pixels in rows are stored from left to right, each pixel
    // is represented by a 32-bit value composed of three bytes with R+G+B colors and a
    // fourth byte that is ignored); there are two processing types: copying the image
    // into the resulting thumbnail (if the size of the processed image does not exceed
    // the thumbnail size) and reducing the image into the thumbnail (image larger than
    // thumbnail); 'buffer' is used only for reading; 'rowsCount' specifies how many rows
    // image are in the buffer;
    // if 'buffer' is NULL, data is taken from the internal buffer (plugin obtains it through GetBuffer);
    // returns TRUE if plugin should continue loading image, if returns FALSE,
    // thumbnail creation is finished (entire image was processed) or should
    // should be interrupted first (for example, the user changed the path in the panel,
    // so the thumbnail is no longer needed)
    //
    // CAUTION: while CPluginInterfaceForThumbLoader::LoadThumbnail is running,
    // path changes in the panel are blocked. For that reason, larger images must be
    // passed and especially loaded in parts, and the return value of ProcessBuffer
    // must be tested to check whether loading should be interrupted.
    // If more time-consuming operations must be performed before calling SetParameters
    // or ProcessBuffer, GetCancelProcessing must be called occasionally during that time.
    virtual BOOL WINAPI ProcessBuffer(void* buffer, int rowsCount) = 0;

    // returns an internal buffer sized to store 'rowsCount' image rows
    // (4 * 'rowsCount' * 'picWidth' bytes); if the object is in an error state
    // (after calling SetError), returns NULL;
    // plugin must not deallocate the obtained buffer (it is deallocated automatically in Salamander)
    virtual void* WINAPI GetBuffer(int rowsCount) = 0;

    // reports an error while obtaining the image (thumbnail is considered invalid
    // and will not be used); after SetError is called, other methods of this interface
    // will only return errors (GetBuffer and SetParameters) or interruption of work
    // (ProcessBuffer)
    virtual void WINAPI SetError() = 0;

    // returns TRUE if plugin should interrupt thumbnail loading
    // returns FALSE if plugin should continue loading the image
    //
    // the method can be called before and after calling SetParameters
    //
    // used to detect an interruption request when the plugin needs to perform
    // time-consuming operations before calling SetParameters, or when the plugin needs
    // to prerender the image, meaning after calling SetParameters but before ProcessBuffer
    virtual BOOL WINAPI GetCancelProcessing() = 0;
};

//
// ****************************************************************************
// CPluginInterfaceForThumbLoaderAbstract
//
// Plugin API for producing thumbnails of files for panel thumbnail view.

class CPluginInterfaceForThumbLoaderAbstract
{
#ifdef INSIDE_SALAMANDER
private: // protection against incorrect direct method calls (see CPluginInterfaceForThumbLoaderEncapsulation)
    friend class CPluginInterfaceForThumbLoaderEncapsulation;
#else  // INSIDE_SALAMANDER
public:
#endif // INSIDE_SALAMANDER

    // loads a thumbnail for file 'filename'; 'thumbWidth' and 'thumbHeight' are
    // dimensions of the requested thumbnail; 'thumbMaker' is the interface of the
    // thumbnail creation algorithm (it can accept a finished thumbnail or create it by
    // reducing the image); returns TRUE if the format of file 'filename' is known; if it
    // returns FALSE, Salamander will try to load the thumbnail using another plugin;
    // plugin reports errors while obtaining the thumbnail (for example, a file read error)
    // through the 'thumbMaker' interface - see SetError; 'fastThumbnail' is TRUE in the
    // first thumbnail loading round - the goal is to return the thumbnail as quickly as
    // possible (even in worse quality or smaller than needed), in the second thumbnail
    // loading round (only if SSTHUMB_ONLY_PREVIEW is set in the first round),
    // 'fastThumbnail' is FALSE - the goal is to return a quality thumbnail
    // limitation: because this is called from the icon loading thread (not the main thread),
    // only CSalamanderGeneralAbstract methods callable from any thread can be used
    //
    // Recommended implementation outline:
    //   - try to open the image
    //   - if it fails, return FALSE
    //   - extract image dimensions
    //   - pass them to Salamander through thumbMaker->SetParameters
    //   - if it returns FALSE, clean up and exit (buffer allocation failed)
    //   - LOOP
    //     - load part of the image data
    //     - send it to Salamander through thumbMaker->ProcessBuffer
    //     - if it returns FALSE, clean up and exit (interrupted due to path change)
    //     - continue LOOP until the whole image is passed
    //   - clean up and exit
    virtual BOOL WINAPI LoadThumbnail(const char* filename, int thumbWidth, int thumbHeight,
                                      CSalamanderThumbnailMakerAbstract* thumbMaker,
                                      BOOL fastThumbnail) = 0;
};

#ifdef _MSC_VER
#pragma pack(pop, enter_include_spl_thum)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a
#endif // __BORLANDC__

// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

//******************************************************************************
//
// CShrinkImage
//

class CShrinkImage
{
protected:
    DWORD NormCoeffX, NormCoeffY;
    DWORD* RowCoeff;
    DWORD* ColCoeff;
    DWORD* YCoeff;
    DWORD NormCoeff;
    DWORD Y, YBndr;
    DWORD* OutLine;
    DWORD* Buff;
    DWORD OrigHeight;
    WORD NewWidth;
    BOOL ProcessTopDown;

public:
    CShrinkImage();
    ~CShrinkImage();

    // Allocates internal data for resizing and returns TRUE on success.
    // If allocation fails, returns FALSE.
    BOOL Alloc(DWORD origWidth, DWORD origHeight,
               WORD newWidth, WORD newHeight,
               DWORD* outBuff, BOOL processTopDown);

    // destruction of the allocated buffers and initialization of variables
    void Destroy();

    void ProcessRows(DWORD* inBuff, DWORD rowCount);

protected:
    DWORD* CreateCoeff(DWORD origLen, WORD newLen, DWORD& norm);
    void Cleanup();
};

//******************************************************************************
//
// CSalamanderThumbnailMaker
//
// Used to shrink the original image into a thumbnail.
//

class CSalamanderThumbnailMaker : public CSalamanderThumbnailMakerAbstract
{
protected:
    CFilesWindow* Window; // panel window whose icon-reader we operate in

    DWORD* Buffer;  // dedicated buffer for row data from the plugin
    int BufferSize; // size of buffer 'Buffer'
    BOOL Error;     // if TRUE, an error occurred while processing the thumbnail (result is unusable)
    int NextLine;   // number of the next row to be processed

    DWORD* ThumbnailBuffer;    // shrunk image
    DWORD* AuxTransformBuffer; // auxiliary buffer of the same size as ThumbnailBuffer (used to transfer data during the transformation; after the transformation the buffers are swapped)
    int ThumbnailMaxWidth;     // maximum theoretical thumbnail dimensions (in points)
    int ThumbnailMaxHeight;
    int ThumbnailRealWidth;  // actual dimensions of the shrunk image (in points)
    int ThumbnailRealHeight; //

    // parameters of the image being processed
    int OriginalWidth;
    int OriginalHeight;
    DWORD PictureFlags;
    BOOL ProcessTopDown;

    CShrinkImage Shrinker; // performs the image shrinking
    BOOL ShrinkImage;

public:
    CSalamanderThumbnailMaker(CFilesWindow* window);
    ~CSalamanderThumbnailMaker();

    // Object cleanup - called before processing the next thumbnail or when
    // a thumbnail (finished or not) from this object is no longer needed.
    // the 'thumbnailMaxSize' parameter gives the maximum possible width and height
    // thumbnail in pixels; if equal to -1, it is ignored
    void Clear(int thumbnailMaxSize = -1);

    // Returns TRUE if the complete thumbnail is ready in this object (it succeeded
    // in obtaining it from the plugin)
    BOOL ThumbnailReady();

    // Performs thumbnail transformation according to PictureFlags (SSTHUMB_MIRROR_VERT is already done,
    // SSTHUMB_MIRROR_HOR and SSTHUMB_ROTATE_90CW remain to be performed)
    void TransformThumbnail();

    // converts the finished thumbnail to a DDB and stores its dimensions and raw data in 'data'
    BOOL RenderToThumbnailData(CThumbnailData* data);

    // If the complete thumbnail was not created and no error occurred (see 'Error'), fills
    // the rest of the thumbnail with white (so that in the undefined part of the thumbnail
    // remnants of the previous thumbnail are not displayed); if not even
    // three rows of the thumbnail, nothing is filled in (the thumbnail would be useless anyway)
    void HandleIncompleteImages();

    BOOL IsOnlyPreview() { return (PictureFlags & SSTHUMB_ONLY_PREVIEW) != 0; }

    // *********************************************************************************
    // methods of the CSalamanderThumbnailMakerAbstract interface
    // *********************************************************************************

    virtual BOOL WINAPI SetParameters(int picWidth, int picHeight, DWORD flags);
    virtual BOOL WINAPI ProcessBuffer(void* buffer, int rowsCount);
    virtual void* WINAPI GetBuffer(int rowsCount);
    virtual void WINAPI SetError() { Error = TRUE; }
    virtual BOOL WINAPI GetCancelProcessing();
};

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

    // destukce alokovanych bufferu a inicializace promennych
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
// Slouzi pro zmensovani puvodniho obrazku do thumbnailu.
//

class CSalamanderThumbnailMaker : public CSalamanderThumbnailMakerAbstract
{
protected:
    CFilesWindow* Window; // panel window whose icon-reader we operate in

    DWORD* Buffer;  // vlastni buffer pro data radek od pluginu
    int BufferSize; // size of buffer 'Buffer'
    BOOL Error;     // if TRUE, an error occurred while processing the thumbnail (result is unusable)
    int NextLine;   // cislo pristi zpracovavane radky

    DWORD* ThumbnailBuffer;    // zmenseny obrazek
    DWORD* AuxTransformBuffer; // pomocny buffer o stejne velikosti jako ThumbnailBuffer (slouzi pro prenos dat pri transformaci + po transformaci se buffery prohodi)
    int ThumbnailMaxWidth;     // maximalni teoreticke rozmery thumbnailu (v bodech)
    int ThumbnailMaxHeight;
    int ThumbnailRealWidth;  // realne rozmery zmenseneho obrazku (v bodech)
    int ThumbnailRealHeight; //

    // parametry zpracovavaneho obrazku
    int OriginalWidth;
    int OriginalHeight;
    DWORD PictureFlags;
    BOOL ProcessTopDown;

    CShrinkImage Shrinker; // zajistuje zmensovani obrazku
    BOOL ShrinkImage;

public:
    CSalamanderThumbnailMaker(CFilesWindow* window);
    ~CSalamanderThumbnailMaker();

    // Object cleanup - called before processing the next thumbnail or when
    // a thumbnail (finished or not) from this object is no longer needed.
    // parametr 'thumbnailMaxSize' udava maximalni moznou sirku a vysku
    // thumbnail in pixels; if equal to -1, it is ignored
    void Clear(int thumbnailMaxSize = -1);

    // Returns TRUE if the complete thumbnail is ready in this object (it succeeded
    // jeho ziskani od pluginu)
    BOOL ThumbnailReady();

    // Performs thumbnail transformation according to PictureFlags (SSTHUMB_MIRROR_VERT is already done,
    // zbyva provest SSTHUMB_MIRROR_HOR a SSTHUMB_ROTATE_90CW)
    void TransformThumbnail();

    // konvertuje hotovy thumbnail na DDB a jeji rozmery a raw data ulozi do 'data'
    BOOL RenderToThumbnailData(CThumbnailData* data);

    // If the complete thumbnail was not created and no error occurred (see 'Error'), fills
    // zbytek thumbnailu bilou barvou (aby se v nedefinovane casti thumbnailu
    // remnants of the previous thumbnail are not displayed); if not even
    // tri radky thumbnailu, nic se nedoplnuje (thumbnail by byl stejne k nicemu)
    void HandleIncompleteImages();

    BOOL IsOnlyPreview() { return (PictureFlags & SSTHUMB_ONLY_PREVIEW) != 0; }

    // *********************************************************************************
    // metody rozhrani CSalamanderThumbnailMakerAbstract
    // *********************************************************************************

    virtual BOOL WINAPI SetParameters(int picWidth, int picHeight, DWORD flags);
    virtual BOOL WINAPI ProcessBuffer(void* buffer, int rowsCount);
    virtual void* WINAPI GetBuffer(int rowsCount);
    virtual void WINAPI SetError() { Error = TRUE; }
    virtual BOOL WINAPI GetCancelProcessing();
};

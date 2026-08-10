// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "registry.h"

#include "ieviewer.h"
#include "dbg.h"
#include "markdown_rendering.h"

#include <vector>

// TODO: MD viewer doesn't display images, one solution is documented in:
// https://blog.kowalczyk.info/article/g9ne/showing-html-from-memory-in-embedded-web-control-on-windows.html
// https://github.com/sumatrapdfreader/sumatrapdf/blob/master/src/utils/HtmlWindow.cpp (BSD license)

FILE* OpenMarkdownCSS()
{
    char path[MAX_PATH];
    if (GetModuleFileName(DLLInstance, path, MAX_PATH) == 0)
    {
        TRACE_E("GetModuleFileName() failed");
        return NULL;
    }
    char* name = strrchr(path, '\\');
    if (name == NULL)
    {
        TRACE_E("Extension not found");
        return NULL;
    }
    strcpy(name + 1, "css\\custom.css");
    FILE* fp = fopen(path, "r");
    if (fp == NULL)
    {
        TRACE_I(path << " not found, we will try githubmd.css instead");
        strcpy(name + 1, "css\\githubmd.css");
        fp = fopen(path, "r");
        if (fp == NULL)
        {
            TRACE_I(path << " not found, we will display unstyled html");
            return NULL;
        }
    }
    return fp;
}

IStream* ConvertMarkdownToHTML(const char* name)
{
    FILE* fp = fopen(name, "rb");
    if (fp == NULL)
    {
        TRACE_E("fopen failed");
        return NULL;
    }

    std::vector<char> markdown;
    char buffer[10000];
    size_t bytes;
    BOOL readFailed = FALSE;
    while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0)
    {
        // Reject before extending the owned input so a growing file cannot
        // bypass the Markdown input budget between the size check and read.
        if (bytes > MarkdownMaximumInputBytes - markdown.size())
        {
            readFailed = TRUE;
            break;
        }
        markdown.insert(markdown.end(), buffer, buffer + bytes);
        if (bytes < sizeof(buffer))
            break;
    }
    if (ferror(fp))
        readFailed = TRUE;
    fclose(fp);

    if (readFailed)
    {
        TRACE_E("Markdown input is unreadable or exceeds the preview limit");
        return NULL;
    }

    char* html = NULL;
    size_t htmlLength = 0;
    const char* markdownBytes = markdown.empty() ? "" : markdown.data();
    EMarkdownRenderResult renderResult = RenderMarkdownToSafeHtml(markdownBytes, markdown.size(),
                                                                    mmeAllViewerExtensions, &html, &htmlLength);
    ReleaseMarkdownRendererPlugins();
    if (renderResult != mrrOk)
    {
        TRACE_E("Markdown preview rejected by renderer policy: " << renderResult);
        return NULL;
    }

    IStream* oStream = NULL;
    DWORD written;
    HRESULT hr = CreateStreamOnHGlobal(NULL, TRUE, &oStream);
    if (FAILED(hr))
    {
        TRACE_E("CreateStreamOnHGlobal() failed");
        FreeRenderedMarkdownHtml(html);
        return NULL;
    }

    char buff[10 * 1024];
    //sprintf_s(buff, "<!DOCTYPE html><html lang=\"cs\" dir=\"ltr\"><head><meta charset=\"utf-8\"><title>zzzz</title><style>\n");
    sprintf_s(buff, "<!DOCTYPE html><html lang=\"cs\" dir=\"ltr\"><head><meta charset=\"utf-8\"><style>\n");
    oStream->Write(buff, (ULONG)strlen(buff), &written);

    // if we find CSS, inline it
    FILE* fpCSS = OpenMarkdownCSS();
    if (fpCSS != NULL)
    {
        size_t bytes;
        while ((bytes = fread(buff, 1, sizeof(buff), fpCSS)) > 0)
            oStream->Write(buff, (ULONG)bytes, &written);
        fclose(fpCSS);
        sprintf_s(buff, "\n");
        oStream->Write(buff, (ULONG)strlen(buff), &written);
    }

    sprintf_s(buff, "</style></head><body><article class=\"markdown-body\">\n");
    oStream->Write(buff, (ULONG)strlen(buff), &written);
    oStream->Write(html, (ULONG)htmlLength, &written);
    sprintf_s(buff, "</article></body></html>\n");
    oStream->Write(buff, (ULONG)strlen(buff), &written);

    // set the pointer to the start of the stream; IE will read from it
    LARGE_INTEGER seek;
    seek.QuadPart = 0;
    oStream->Seek(seek, STREAM_SEEK_SET, NULL);

    FreeRenderedMarkdownHtml(html);

    return oStream;
}

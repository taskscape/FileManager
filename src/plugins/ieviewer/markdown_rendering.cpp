// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "markdown_rendering.h"

#include <stdlib.h>
#include <string.h>

#include "cmark-gfm.h"
#include "cmark-gfm-extension_api.h"
#include "cmark-gfm-core-extensions.h"
#include "registry.h"

namespace
{
bool AttachViewerExtensions(cmark_parser* parser, unsigned int extensionMask)
{
    static const struct
    {
        unsigned int Mask;
        const char* Name;
    } Extensions[] = {
        {mmeAutolink, "autolink"},
        {mmeStrikethrough, "strikethrough"},
        {mmeTable, "table"},
        {mmeTagfilter, "tagfilter"},
        {mmeTasklist, "tasklist"},
    };

    for (size_t i = 0; i < sizeof(Extensions) / sizeof(Extensions[0]); ++i)
    {
        if ((extensionMask & Extensions[i].Mask) == 0)
            continue;

        cmark_syntax_extension* extension = cmark_find_syntax_extension(Extensions[i].Name);
        if (extension == NULL)
            return false;
        cmark_parser_attach_syntax_extension(parser, extension);
    }
    return true;
}

EMarkdownRenderResult CheckTreeBudget(cmark_node* document)
{
    size_t nodeCount = 0;
    cmark_iter* iterator = cmark_iter_new(document);
    if (iterator == NULL)
        return mrrTreeTooLarge;

    cmark_event_type event;
    EMarkdownRenderResult result = mrrOk;
    while ((event = cmark_iter_next(iterator)) != CMARK_EVENT_DONE)
    {
        if (event == CMARK_EVENT_ENTER)
        {
            ++nodeCount;
            if (nodeCount > MarkdownMaximumNodeCount)
            {
                result = mrrTreeTooLarge;
                break;
            }

            // Leaf nodes have no matching EXIT event, so derive depth from
            // parents instead of accumulating iterator events and rejecting a
            // wide but shallow document as if it were deeply nested.
            size_t depth = 0;
            for (cmark_node* parent = cmark_node_parent(cmark_iter_get_node(iterator)); parent != NULL;
                 parent = cmark_node_parent(parent))
                ++depth;
            if (depth > MarkdownMaximumTreeDepth)
            {
                result = mrrTreeTooDeep;
                break;
            }
        }
    }
    cmark_iter_free(iterator);
    return result;
}
}

EMarkdownRenderResult RenderMarkdownToSafeHtml(const char* markdown, size_t markdownLength,
                                               unsigned int extensionMask, char** html, size_t* htmlLength)
{
    if (markdown == NULL || html == NULL || htmlLength == NULL)
        return mrrInvalidInput;
    *html = NULL;
    *htmlLength = 0;
    if (markdownLength > MarkdownMaximumInputBytes)
        return mrrInputTooLarge;

    // SAFE is the default in current cmark-gfm; specifying it documents that
    // this viewer must never opt into CMARK_OPT_UNSAFE at the browser boundary.
    const int options = CMARK_OPT_SAFE | CMARK_OPT_VALIDATE_UTF8;
    cmark_gfm_core_extensions_ensure_registered();

    cmark_node* document = NULL;
    cmark_parser* parser = cmark_parser_new(options);
    if (parser != NULL && AttachViewerExtensions(parser, extensionMask))
    {
        cmark_parser_feed(parser, markdown, markdownLength);
        document = cmark_parser_finish(parser);
    }

    if (parser == NULL || document == NULL)
    {
        if (document != NULL)
            cmark_node_free(document);
        if (parser != NULL)
            cmark_parser_free(parser);
        return mrrRenderFailed;
    }

    EMarkdownRenderResult treeResult = CheckTreeBudget(document);
    if (treeResult != mrrOk)
    {
        cmark_node_free(document);
        cmark_parser_free(parser);
        return treeResult;
    }

    char* rendered = cmark_render_html(document, options, cmark_parser_get_syntax_extensions(parser));

    cmark_node_free(document);
    cmark_parser_free(parser);
    if (rendered == NULL)
    {
        return mrrRenderFailed;
    }

    *htmlLength = strlen(rendered);
    if (*htmlLength > MarkdownMaximumOutputBytes)
    {
        free(rendered);
        return mrrOutputTooLarge;
    }
    *html = rendered;
    return mrrOk;
}

void FreeRenderedMarkdownHtml(char* html)
{
    free(html);
}

void ReleaseMarkdownRendererPlugins()
{
    cmark_release_plugins();
}

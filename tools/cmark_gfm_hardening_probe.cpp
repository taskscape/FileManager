// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include <stdio.h>
#include <string.h>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "markdown_rendering.h"

namespace
{
bool ReadFile(const char* path, std::string* contents)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    contents->assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

void NormalizeSnapshotLineEndings(std::string* contents)
{
    // Git may materialize golden text with CRLF on Windows while cmark emits
    // canonical LF, so compare content independently of checkout convention.
    size_t output = 0;
    for (size_t input = 0; input != contents->size(); ++input)
    {
        char value = (*contents)[input];
        if (value == '\r')
        {
            if (input + 1 != contents->size() && (*contents)[input + 1] == '\n')
                ++input;
            value = '\n';
        }
        (*contents)[output++] = value;
    }
    contents->resize(output);
}

bool RunSnapshot(const char* markdownPath, const char* htmlPath)
{
    std::string markdown;
    std::string expectedHtml;
    if (!ReadFile(markdownPath, &markdown) || !ReadFile(htmlPath, &expectedHtml))
        return false;
    NormalizeSnapshotLineEndings(&expectedHtml);

    char* html = NULL;
    size_t htmlLength = 0;
    EMarkdownRenderResult result = RenderMarkdownToSafeHtml(markdown.data(), markdown.size(), mmeAllViewerExtensions, &html, &htmlLength);
    bool matches = result == mrrOk && expectedHtml.size() == htmlLength && memcmp(expectedHtml.data(), html, htmlLength) == 0;
    FreeRenderedMarkdownHtml(html);
    return matches;
}

unsigned int NextRandom(unsigned int* state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

bool RunExtensionCombinationFuzz()
{
    static const char Alphabet[] = "#>*_[]()|~`<&:/. -\nabcdefghijklmnopqrstuvwxyz0123456789";
    unsigned int state = 0xC0FFEEu;
    for (unsigned int mask = 0; mask <= mmeAllViewerExtensions; ++mask)
    {
        for (unsigned int iteration = 0; iteration != 128; ++iteration)
        {
            std::string markdown;
            size_t length = 1 + (NextRandom(&state) % 2048);
            markdown.reserve(length);
            for (size_t i = 0; i != length; ++i)
                markdown.push_back(Alphabet[NextRandom(&state) % (sizeof(Alphabet) - 1)]);

            char* html = NULL;
            size_t htmlLength = 0;
            EMarkdownRenderResult result = RenderMarkdownToSafeHtml(markdown.data(), markdown.size(), mask, &html, &htmlLength);
            // Every subset is exercised because cmark extensions share parser and renderer hooks.
            bool safe = result != mrrOk || (strstr(html, "href=\"javascript:") == NULL && strstr(html, "<script") == NULL);
            FreeRenderedMarkdownHtml(html);
            if (!safe)
                return false;
        }
    }
    return true;
}

bool VerifyLimitsAndSafeDefaults()
{
    std::string tooLarge(MarkdownMaximumInputBytes + 1, 'a');
    char* html = NULL;
    size_t htmlLength = 0;
    if (RenderMarkdownToSafeHtml(tooLarge.data(), tooLarge.size(), mmeAllViewerExtensions, &html, &htmlLength) != mrrInputTooLarge)
        return false;

    std::string nesting;
    for (size_t i = 0; i <= MarkdownMaximumTreeDepth; ++i)
        nesting += "> ";
    nesting += "nested\n";
    if (RenderMarkdownToSafeHtml(nesting.data(), nesting.size(), mmeAllViewerExtensions, &html, &htmlLength) != mrrTreeTooDeep)
        return false;

    std::string shallowList;
    for (size_t i = 0; i != MarkdownMaximumTreeDepth + 1; ++i)
        shallowList += "- item\n";
    if (RenderMarkdownToSafeHtml(shallowList.data(), shallowList.size(), mmeAllViewerExtensions, &html, &htmlLength) != mrrOk)
        return false;
    FreeRenderedMarkdownHtml(html);

    std::string outputExpansion(MarkdownMaximumInputBytes, '&');
    if (RenderMarkdownToSafeHtml(outputExpansion.data(), outputExpansion.size(), mmeAllViewerExtensions, &html, &htmlLength) != mrrOutputTooLarge)
        return false;

    const char unsafeMarkdown[] = "[unsafe](javascript:alert(1))\n\n<script>alert(1)</script>\n";
    if (RenderMarkdownToSafeHtml(unsafeMarkdown, sizeof(unsafeMarkdown) - 1, mmeAllViewerExtensions, &html, &htmlLength) != mrrOk)
        return false;
    bool safe = strstr(html, "href=\"javascript:") == NULL && strstr(html, "<script") == NULL;
    FreeRenderedMarkdownHtml(html);
    return safe;
}
}

int main(int argc, char* argv[])
{
    if (argc != 2)
        return 2;

    std::string root(argv[1]);
    bool passed = RunSnapshot((root + "\\tests\\cmark-gfm\\snapshots\\basic.md").c_str(),
                              (root + "\\tests\\cmark-gfm\\snapshots\\basic.html").c_str()) &&
                  RunSnapshot((root + "\\tests\\cmark-gfm\\snapshots\\strikethrough.md").c_str(),
                              (root + "\\tests\\cmark-gfm\\snapshots\\strikethrough.html").c_str()) &&
                  VerifyLimitsAndSafeDefaults();
    passed = passed && RunExtensionCombinationFuzz();
    ReleaseMarkdownRendererPlugins();
    if (!passed)
    {
        fputs("cmark-gfm hardening probe failed\n", stderr);
        return 1;
    }
    puts("cmark-gfm hardening probe passed");
    return 0;
}

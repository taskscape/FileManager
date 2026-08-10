// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stddef.h>

// The viewer keeps Markdown within these budgets so an untrusted preview cannot
// consume unbounded memory before it reaches the embedded browser control.
static const size_t MarkdownMaximumInputBytes = 1024 * 1024;
static const size_t MarkdownMaximumNodeCount = 100000;
static const size_t MarkdownMaximumTreeDepth = 128;
static const size_t MarkdownMaximumOutputBytes = 4 * 1024 * 1024;

enum EMarkdownExtensionMask
{
    mmeAutolink = 1 << 0,
    mmeStrikethrough = 1 << 1,
    mmeTable = 1 << 2,
    mmeTagfilter = 1 << 3,
    mmeTasklist = 1 << 4,
    mmeAllViewerExtensions = mmeAutolink | mmeStrikethrough | mmeTable | mmeTagfilter | mmeTasklist
};

enum EMarkdownRenderResult
{
    mrrOk,
    mrrInvalidInput,
    mrrInputTooLarge,
    mrrTreeTooLarge,
    mrrTreeTooDeep,
    mrrOutputTooLarge,
    mrrRenderFailed
};

EMarkdownRenderResult RenderMarkdownToSafeHtml(const char* markdown, size_t markdownLength,
                                               unsigned int extensionMask, char** html, size_t* htmlLength);
void FreeRenderedMarkdownHtml(char* html);
void ReleaseMarkdownRendererPlugins();

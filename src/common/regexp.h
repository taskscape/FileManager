// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

//*****************************************************************************
//*****************************************************************************
//
// original regexp.h
//
//*****************************************************************************
//*****************************************************************************

/*
 * Definitions etc. for regexp(3) routines.
 *
 * Caveat:  this is V8 regexp(3) [actually, a reimplementation thereof],
 * not the System V one.
 */
#define NSUBEXP 10
typedef struct regexp
{
    char* startp[NSUBEXP];
    char* endp[NSUBEXP];
    char regstart;   /* Internal use only. */
    char reganch;    /* Internal use only. */
    char* regmust;   /* Internal use only. */
    int regmlen;     /* Internal use only. */
    char program[1]; /* Unwarranted chumminess with compiler. */
} regexp;

regexp* regcomp(char* exp, const char*& lastErrorText);
int regexec(regexp* prog, char* string, int offset);
void regerror(const char* error);

//*****************************************************************************
//*****************************************************************************
//
// my part of regexp.h
//
//*****************************************************************************
//*****************************************************************************

// errors that can occur during compilation and searching of reg. expr.
enum CRegExpErrors
{
    reeNoError,
    reeLowMemory,
    reeEmpty,
    reeTooBig,
    reeTooManyParenthesises,
    reeUnmatchedParenthesis,
    reeOperandCouldBeEmpty,
    reeNested,
    reeInvalidRange,
    reeUnmatchedBracket,
    reeFollowsNothing,
    reeTrailingBackslash,
    reeInternalDisaster,
};

// function that returns text of occurred error
const char* RegExpErrorText(CRegExpErrors err);

// search flags
#define sfCaseSensitive 0x01 // 0. bit = 1
#define sfForward 0x02       // 1. bit = 1

//*****************************************************************************
//
// CRegularExpression
//

class CRegularExpression
{
public:
    static const char* LastError; // text of last error

protected:
    const char* LastErrorText;
    char* OriginalPattern;
    regexp* Expression; // compiled regular expression
    WORD Flags;

    char* Line;                // buffer for line
    const char* OrigLineStart; // pointer to the beginning of original text (passed to SetLine() as 'start')
    int Allocated;             // how many bytes are allocated
    int LineLength;            // current line length

public:
    CRegularExpression()
    {
        Expression = NULL;
        OriginalPattern = NULL;
        Flags = sfCaseSensitive | sfForward;
        Line = NULL;
        OrigLineStart = NULL;
        Allocated = 0;
        LineLength = 0;
        LastErrorText = NULL;
    }

    ~CRegularExpression()
    {
        if (Expression != NULL)
            free(Expression);
        if (OriginalPattern != NULL)
            free(OriginalPattern);
        if (Line != NULL)
            free(Line);
    }

    BOOL IsGood() const { return OriginalPattern != NULL && Expression != NULL; }
    const char* GetPattern() const { return OriginalPattern; }

    const char* GetLastErrorText() const { return LastErrorText; }
    BOOL Set(const char* pattern, WORD flags); // returns FALSE on error (call GetLastErrorText method)
    BOOL SetFlags(WORD flags);                 // returns FALSE on error (call GetLastErrorText method)

    BOOL SetLine(const char* start, const char* end); // line of text to search in, returns FALSE on error (call GetLastErrorText method)

    int SearchForward(int start, int& foundLen);
    int SearchBackward(int length, int& foundLen);

    // replaces variables \1 ... \9 with text captured by corresponding parentheses
    // 'pattern' is the pattern used to replace the found match, 'buffer' is the buffer
    // for output, 'bufSize' maximum text size including terminating NULL
    // character; returns the number of characters copied to the buffer in 'count'
    // returns TRUE if the whole expression fit into the buffer
    BOOL ExpandVariables(char* pattern, char* buffer,
                         int bufSize, int* count);

    // Return values
    //
    // 0 searched text was not found, nothing was copied to 'buffer'
    // 1 text was replaced successfully
    // 2 'buffer' is too small
    int ReplaceForward(int start, char* pattern, BOOL global,
                       char* buffer, int bufSize);

protected:
    // Reverses the regular expression for backward searching.
    // THE EXPRESSION MUST BE SYNTACTICALLY CORRECT! OTHERWISE IT DOES NOT WORK CORRECTLY!
    // e.g. "a)b(d)(" -> "((d)b)a", which is wrong
    void ReverseRegExp(char*& dstExpEnd, char* srcExp, char* srcExpEnd);
};

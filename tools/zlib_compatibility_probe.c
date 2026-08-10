// This probe pins zlib 1.2.11 output and hostile-stream rejection so a vendor
// refresh must preserve the RFC-compatible data boundary used by plug-ins.
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zlib.h"

static const unsigned char kExpectedPlaintext[] =
    "Open Salamander zlib compatibility vector\0untrusted archive payload\0";

static int ReadHexFixture(const char* path, Bytef** data, uLong* length)
{
    FILE* file = fopen(path, "rb");
    long fileLength;
    char* text;
    size_t index;
    size_t outputLength = 0;
    int highNibble = -1;

    if (file == NULL || fseek(file, 0, SEEK_END) != 0 ||
        (fileLength = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0)
    {
        if (file != NULL)
            fclose(file);
        return 0;
    }

    text = (char*)malloc((size_t)fileLength + 1);
    *data = (Bytef*)malloc((size_t)fileLength / 2 + 1);
    if (text == NULL || *data == NULL ||
        fread(text, 1, (size_t)fileLength, file) != (size_t)fileLength)
    {
        free(text);
        free(*data);
        *data = NULL;
        fclose(file);
        return 0;
    }
    fclose(file);

    for (index = 0; index < (size_t)fileLength; ++index)
    {
        int value;
        if (isspace((unsigned char)text[index]))
            continue;
        if (!isxdigit((unsigned char)text[index]))
        {
            free(text);
            free(*data);
            *data = NULL;
            return 0;
        }
        value = isdigit((unsigned char)text[index]) ? text[index] - '0' :
            (tolower((unsigned char)text[index]) - 'a' + 10);
        if (highNibble < 0)
            highNibble = value;
        else
        {
            (*data)[outputLength++] = (Bytef)((highNibble << 4) | value);
            highNibble = -1;
        }
    }
    free(text);
    if (highNibble >= 0)
    {
        free(*data);
        *data = NULL;
        return 0;
    }
    *length = (uLong)outputLength;
    return 1;
}

static int VerifyLegacyVector(const char* path)
{
    Bytef* compressed = NULL;
    uLong compressedLength = 0;
    uLong outputLength = sizeof(kExpectedPlaintext) - 1;
    unsigned char output[sizeof(kExpectedPlaintext)];
    uLong generatedLength = compressBound(sizeof(kExpectedPlaintext) - 1);
    Bytef* generated = (Bytef*)malloc(generatedLength);
    int success = 0;

    if (!ReadHexFixture(path, &compressed, &compressedLength) || generated == NULL)
        goto cleanup;
    if (uncompress(output, &outputLength, compressed, compressedLength) != Z_OK ||
        outputLength != sizeof(kExpectedPlaintext) - 1 ||
        memcmp(output, kExpectedPlaintext, sizeof(kExpectedPlaintext) - 1) != 0)
        goto cleanup;
    if (compress2(generated, &generatedLength, kExpectedPlaintext,
                  sizeof(kExpectedPlaintext) - 1, Z_DEFAULT_COMPRESSION) != Z_OK ||
        generatedLength != compressedLength ||
        memcmp(generated, compressed, compressedLength) != 0)
        goto cleanup;
    success = 1;

cleanup:
    free(generated);
    free(compressed);
    return success;
}

static int VerifyRejectedStream(const char* path)
{
    Bytef* compressed = NULL;
    uLong compressedLength = 0;
    uLong outputLength = sizeof(kExpectedPlaintext);
    unsigned char output[sizeof(kExpectedPlaintext)];
    int result;

    if (!ReadHexFixture(path, &compressed, &compressedLength))
        return 0;
    result = uncompress(output, &outputLength, compressed, compressedLength);
    free(compressed);
    return result != Z_OK;
}

int main(int argc, char* argv[])
{
    if (argc != 5)
    {
        fprintf(stderr, "Expected a vector directory and three corrupt fixtures.\n");
        return 2;
    }
    if (strcmp(zlibVersion(), "1.3.2") != 0)
    {
        fprintf(stderr, "Expected zlib 1.3.2, loaded %s.\n", zlibVersion());
        return 1;
    }
    if (!VerifyLegacyVector(argv[1]))
    {
        fprintf(stderr, "The zlib 1.2.11 compatibility vector failed.\n");
        return 1;
    }
    if (!VerifyRejectedStream(argv[2]) || !VerifyRejectedStream(argv[3]) ||
        !VerifyRejectedStream(argv[4]))
    {
        fprintf(stderr, "A retained corrupt zlib stream was accepted.\n");
        return 1;
    }
    puts("zlib 1.3.2 compatibility and corrupt-input vectors passed.");
    return 0;
}

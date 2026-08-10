// This probe exercises the host-facing streaming API so bzip2 vendor updates
// cannot silently accept corrupt archive data outside the adapter's call shape.
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bzlib.h"

static const unsigned char kGoldenPlaintext[] =
    "Open Salamander bzip2 golden archive\n"
    "archive adapter streaming compatibility\n";
static const unsigned char kLegacyPlaintext[] =
    "legacy bzip2 archive\0with binary\n";

void bz_internal_error(int errorCode)
{
    fprintf(stderr, "Unexpected bzip2 internal error %d.\n", errorCode);
    abort();
}

static int ReadHexFixture(const char* path, unsigned char** data, size_t* length)
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
        if (file != NULL) fclose(file);
        return 0;
    }

    text = (char*)malloc((size_t)fileLength + 1);
    *data = (unsigned char*)malloc((size_t)fileLength / 2 + 1);
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
        if (isspace((unsigned char)text[index])) continue;
        if (!isxdigit((unsigned char)text[index])) goto fail;
        value = isdigit((unsigned char)text[index]) ? text[index] - '0' :
            tolower((unsigned char)text[index]) - 'a' + 10;
        if (highNibble < 0)
            highNibble = value;
        else
        {
            (*data)[outputLength++] = (unsigned char)((highNibble << 4) | value);
            highNibble = -1;
        }
    }
    free(text);
    if (highNibble >= 0) goto fail_data;
    *length = outputLength;
    return 1;

fail:
    free(text);
fail_data:
    free(*data);
    *data = NULL;
    return 0;
}

static int DecodeFixture(const char* path, const unsigned char* expected, size_t expectedLength, int mustFinish)
{
    unsigned char* compressed = NULL;
    size_t compressedLength = 0;
    size_t inputOffset = 0;
    size_t outputLength = 0;
    unsigned char output[256];
    bz_stream stream;
    int result;
    int iterations = 0;
    int success = 0;

    if (!ReadHexFixture(path, &compressed, &compressedLength)) return 0;
    memset(&stream, 0, sizeof(stream));
    if (BZ2_bzDecompressInit(&stream, 0, 0) != BZ_OK) goto cleanup;

    while (++iterations < 4096)
    {
        unsigned char chunk[13];
        unsigned int outputBefore;
        if (stream.avail_in == 0 && inputOffset < compressedLength)
        {
            size_t inputLength = compressedLength - inputOffset;
            if (inputLength > 7) inputLength = 7;
            stream.next_in = (char*)compressed + inputOffset;
            stream.avail_in = (unsigned int)inputLength;
            inputOffset += inputLength;
        }
        stream.next_out = (char*)chunk;
        stream.avail_out = sizeof(chunk);
        outputBefore = stream.avail_out;
        result = BZ2_bzDecompress(&stream);
        if (sizeof(chunk) - stream.avail_out > sizeof(output) - outputLength) goto cleanup;
        memcpy(output + outputLength, chunk, sizeof(chunk) - stream.avail_out);
        outputLength += sizeof(chunk) - stream.avail_out;
        if (result == BZ_STREAM_END)
        {
            success = mustFinish && outputLength == expectedLength &&
                memcmp(output, expected, expectedLength) == 0;
            goto cleanup;
        }
        if (result != BZ_OK)
        {
            success = !mustFinish;
            goto cleanup;
        }
        if (inputOffset == compressedLength && stream.avail_in == 0 &&
            outputBefore == stream.avail_out)
        {
            success = !mustFinish;
            goto cleanup;
        }
    }

cleanup:
    BZ2_bzDecompressEnd(&stream);
    free(compressed);
    return success;
}

int main(int argc, char* argv[])
{
    int index;
    if (argc < 9)
    {
        fprintf(stderr, "Expected two golden, one truncation, and five fuzz fixtures.\n");
        return 2;
    }
    if (strcmp(BZ2_bzlibVersion(), "1.0.8, 13-Jul-2019") != 0)
    {
        fprintf(stderr, "Expected bzip2 1.0.8, loaded %s.\n", BZ2_bzlibVersion());
        return 1;
    }
    if (!DecodeFixture(argv[1], kGoldenPlaintext, sizeof(kGoldenPlaintext) - 1, 1) ||
        !DecodeFixture(argv[2], kLegacyPlaintext, sizeof(kLegacyPlaintext) - 1, 1))
    {
        fprintf(stderr, "A golden bzip2 archive did not decode through the streaming API.\n");
        return 1;
    }
    for (index = 3; index < argc; ++index)
    {
        if (!DecodeFixture(argv[index], NULL, 0, 0))
        {
            fprintf(stderr, "A truncation or fuzz fixture unexpectedly reached BZ_STREAM_END: %s\n", argv[index]);
            return 1;
        }
    }
    puts("bzip2 1.0.8 golden archives, truncation, and fuzz corpus passed.");
    return 0;
}

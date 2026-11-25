/*
 * MIT License
 *
 * Copyright (c) 2025 Malaka Gunawardana
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "Archive.h"

#include "HttpClient.h"
#include "StoreUtil.h"

#include <Hx86/Hx86.h>
#include <Hx86/Hsyscalls/syscalls.h>
#include <mbedtls/sha256.h>

#define MINIZ_NO_STDIO
#define MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_TIME
#define NDEBUG
#include <miniz.h>

namespace {

// Official idgames mirror. doom-1.8.wad.gz is a plain gzip-compressed WAD,
// unlike doom19s.zip, which is a split DEICE self-extracting installer.
constexpr const char* kGzipUrl =
    "https://ftp.fu-berlin.de/pc/games/idgames/idstuff/doom/doom-1.8.wad.gz";
constexpr uint32_t kGzipSize = 1756050;
constexpr const char* kGzipHash =
    "58cf6a563b631a47561630afa6276d853c27079342f7969ec3006965da8557a1";

constexpr uint32_t kWadSize = 4196020;
constexpr const char* kWadHash =
    "bb449c7480e9a02a62012d041406e8e43daa51caa0650646d1307d8650b8f837";

constexpr uint32_t kPathCapacity = 320;

struct ArchivePaths {
    char sharewareDir[kPathCapacity];
    char gzipPath[kPathCapacity];
    char gzipPartPath[kPathCapacity];
    char wadPath[kPathCapacity];
    char wadPartPath[kPathCapacity];
};

void JoinPath(char* output, uint32_t capacity, const char* base, const char* leaf) {
    uint32_t used = 0;
    if (base != nullptr) {
        while (*base != '\0' && used + 1 < capacity) output[used++] = *base++;
    }
    while (used > 0 && output[used - 1] == '/') used--;
    if (used + 1 < capacity) output[used++] = '/';
    if (leaf != nullptr) {
        while (*leaf != '\0' && used + 1 < capacity) output[used++] = *leaf++;
    }
    output[used] = '\0';
}

void BuildPaths(const char* installDir, ArchivePaths* paths) {
    JoinPath(paths->sharewareDir, kPathCapacity, installDir, "shareware");
    JoinPath(paths->gzipPath, kPathCapacity, installDir, "shareware/doom-1.8.wad.gz");
    JoinPath(paths->gzipPartPath, kPathCapacity, installDir, "shareware/doom-1.8.wad.gz.part");
    JoinPath(paths->wadPath, kPathCapacity, installDir, "doom1.wad");
    JoinPath(paths->wadPartPath, kPathCapacity, installDir, "doom1.wad.part");
}

struct ShaSink {
    int32_t fd;
    mbedtls_sha256_context sha;
    uint32_t received;
    uint32_t expected;
    uint32_t nextProgress;
    ArchiveProgress progress;
    const char* label;
};

void SetError(char* error, uint32_t capacity, const char* text) {
    StoreStrCopy(error, capacity, text);
}

void Report(ArchiveProgress progress, const char* text) {
    if (progress != nullptr) progress(text);
}

void DigestToHex(const uint8_t digest[32], char output[65]) {
    static const char kHex[] = "0123456789abcdef";
    for (uint32_t i = 0; i < 32; i++) {
        output[i * 2] = kHex[digest[i] >> 4];
        output[i * 2 + 1] = kHex[digest[i] & 0x0F];
    }
    output[64] = '\0';
}

bool FinishSha(ShaSink* sink, char output[65]) {
    uint8_t digest[32] = {};
    int result = mbedtls_sha256_finish_ret(&sink->sha, digest);
    mbedtls_sha256_free(&sink->sha);
    if (result != 0) return false;
    DigestToHex(digest, output);
    return true;
}

void UpdateProgress(ShaSink* sink) {
    if (sink->progress == nullptr) return;
    if (sink->expected == 0 || sink->received < sink->nextProgress) return;
    uint32_t percent = (uint32_t)(((uint64_t)sink->received * 100u) / sink->expected);
    char text[160];
    StoreFormat(text, sizeof(text), "%s: %u / %u KB (%u%%)", sink->label,
                sink->received / 1024u, sink->expected / 1024u, percent);
    sink->progress(text);
    sink->nextProgress += 64u * 1024u;
}

bool DownloadSink(void* context, const uint8_t* data, uint32_t length) {
    ShaSink* sink = static_cast<ShaSink*>(context);
    if (!StoreWriteAll(sink->fd, data, length)) return false;
    if (mbedtls_sha256_update_ret(&sink->sha, data, length) != 0) return false;
    sink->received += length;
    UpdateProgress(sink);
    return true;
}

bool ReplaceFile(const char* partPath, const char* finalPath) {
    syscall_unlink(finalPath);
    return syscall_rename(partPath, finalPath) == 0;
}

bool DownloadFile(const char* url, const char* partPath, const char* finalPath,
                  uint32_t expectedSize, const char* expectedHash, const char* label,
                  ArchiveProgress progress, char* error, uint32_t errorCapacity) {
    int32_t fd = syscall_open(partPath, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        StoreFormat(error, errorCapacity, "could not create %s", partPath);
        return false;
    }

    ShaSink sink = {};
    sink.fd = fd;
    sink.expected = expectedSize;
    sink.nextProgress = 64u * 1024u;
    sink.progress = progress;
    sink.label = label;
    mbedtls_sha256_init(&sink.sha);
    if (mbedtls_sha256_starts_ret(&sink.sha, 0) != 0) {
        syscall_close((uint32_t)fd);
        syscall_unlink(partPath);
        mbedtls_sha256_free(&sink.sha);
        SetError(error, errorCapacity, "SHA-256 initialization failed");
        return false;
    }

    StoreHttpResponse response = {};
    bool ok = StoreHttpGet(url, "*/*", DownloadSink, &sink, &response, error, errorCapacity);
    char digest[65];
    bool hashOk = FinishSha(&sink, digest);
    syscall_close((uint32_t)fd);

    if (!ok) {
        syscall_unlink(partPath);
        return false;
    }
    if (!hashOk || sink.received != expectedSize || !StoreHexEqual(digest, expectedHash)) {
        syscall_unlink(partPath);
        SetError(error, errorCapacity, "downloaded file failed its size or SHA-256 check");
        return false;
    }
    if (!ReplaceFile(partPath, finalPath)) {
        syscall_unlink(partPath);
        StoreFormat(error, errorCapacity, "could not move %s into place", finalPath);
        return false;
    }
    return true;
}

bool ReadWholeFile(const char* path, uint32_t expectedSize, uint8_t** data) {
    *data = nullptr;
    int32_t fd = syscall_open(path, O_RDONLY);
    if (fd < 0) return false;

    uint8_t* buffer = (uint8_t*)kmalloc((int)expectedSize);
    if (!buffer) {
        syscall_close((uint32_t)fd);
        return false;
    }

    uint32_t total = 0;
    while (total < expectedSize) {
        int32_t got = syscall_read((uint32_t)fd, (char*)buffer + total, expectedSize - total);
        if (got <= 0) break;
        total += (uint32_t)got;
    }
    syscall_close((uint32_t)fd);

    if (total != expectedSize) {
        kfree(buffer);
        return false;
    }
    *data = buffer;
    return true;
}

// Returns the offset of the raw DEFLATE payload inside a gzip member.
bool SkipGzipHeader(const uint8_t* data, uint32_t size, uint32_t* offset,
                    char* error, uint32_t errorCapacity) {
    if (size < 18 || data[0] != 0x1F || data[1] != 0x8B) {
        SetError(error, errorCapacity, "downloaded file is not a gzip archive");
        return false;
    }
    if (data[2] != 8) {
        SetError(error, errorCapacity, "unsupported gzip compression method");
        return false;
    }

    uint8_t flags = data[3];
    uint32_t pos = 10;

    if (flags & 0x04) {  // FEXTRA
        if (pos + 2 > size) return false;
        uint32_t extra = (uint32_t)data[pos] | ((uint32_t)data[pos + 1] << 8);
        pos += 2 + extra;
    }
    if (flags & 0x08) {  // FNAME
        while (pos < size && data[pos] != 0) pos++;
        pos++;
    }
    if (flags & 0x10) {  // FCOMMENT
        while (pos < size && data[pos] != 0) pos++;
        pos++;
    }
    if (flags & 0x02) {  // FHCRC
        pos += 2;
    }

    if (pos + 8 >= size) {
        SetError(error, errorCapacity, "gzip archive is truncated");
        return false;
    }
    *offset = pos;
    return true;
}

}  // namespace

bool ArchiveSharewareDataInstalled(const char* installDir) {
    if (installDir == nullptr || installDir[0] == '\0') return false;
    ArchivePaths paths = {};
    BuildPaths(installDir, &paths);
    uint32_t size = 0;
    return StoreFileExists(paths.wadPath, &size) && size == kWadSize;
}

bool ArchiveInstallSharewareData(const char* installDir, ArchiveProgress progress, char* error,
                                 uint32_t errorCapacity) {
    if (error != nullptr && errorCapacity != 0) error[0] = '\0';
    if (installDir == nullptr || installDir[0] == '\0') {
        SetError(error, errorCapacity, "no install directory was provided");
        return false;
    }

    ArchivePaths paths = {};
    BuildPaths(installDir, &paths);

    if (ArchiveSharewareDataInstalled(installDir)) {
        Report(progress, "Game data is already installed.");
        return true;
    }

    if (!StoreEnsureDir(paths.sharewareDir)) {
        SetError(error, errorCapacity, "could not create the DOOM data directories");
        return false;
    }

    uint32_t gzipSize = 0;
    if (!StoreFileExists(paths.gzipPath, &gzipSize) || gzipSize != kGzipSize) {
        Report(progress, "Downloading the DOOM shareware data...");
        if (!DownloadFile(kGzipUrl, paths.gzipPartPath, paths.gzipPath, kGzipSize, kGzipHash,
                          "Downloading shareware WAD", progress, error, errorCapacity)) {
            return false;
        }
    } else {
        Report(progress, "Using the downloaded shareware archive...");
    }

    uint8_t* gzipData = nullptr;
    if (!ReadWholeFile(paths.gzipPath, kGzipSize, &gzipData)) {
        SetError(error, errorCapacity, "could not read the downloaded gzip archive");
        return false;
    }

    uint32_t payloadOffset = 0;
    bool ok = SkipGzipHeader(gzipData, kGzipSize, &payloadOffset, error, errorCapacity);
    if (!ok) {
        kfree(gzipData);
        return false;
    }

    Report(progress, "Inflating doom1.wad...");
    uint8_t* wadData = (uint8_t*)kmalloc((int)kWadSize);
    if (!wadData) {
        kfree(gzipData);
        SetError(error, errorCapacity, "not enough memory to inflate the WAD");
        return false;
    }

    uint32_t payloadLength = kGzipSize - payloadOffset - 8;
    size_t inflated = tinfl_decompress_mem_to_mem(wadData, kWadSize, gzipData + payloadOffset,
                                                  payloadLength, 0);
    kfree(gzipData);

    if (inflated == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED || inflated != kWadSize) {
        kfree(wadData);
        SetError(error, errorCapacity, "the gzip archive did not inflate to a full WAD");
        return false;
    }

    uint8_t digest[32] = {};
    char digestText[65];
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    bool hashed = mbedtls_sha256_starts_ret(&sha, 0) == 0 &&
                  mbedtls_sha256_update_ret(&sha, wadData, kWadSize) == 0 &&
                  mbedtls_sha256_finish_ret(&sha, digest) == 0;
    mbedtls_sha256_free(&sha);

    if (!hashed) {
        kfree(wadData);
        SetError(error, errorCapacity, "could not checksum the inflated WAD");
        return false;
    }
    DigestToHex(digest, digestText);
    if (!StoreHexEqual(digestText, kWadHash)) {
        kfree(wadData);
        SetError(error, errorCapacity, "inflated WAD failed its SHA-256 check");
        return false;
    }

    int32_t outFd = syscall_open(paths.wadPartPath, O_WRONLY | O_CREAT | O_TRUNC);
    if (outFd < 0) {
        kfree(wadData);
        SetError(error, errorCapacity, "could not create doom1.wad");
        return false;
    }
    bool written = StoreWriteAll(outFd, wadData, kWadSize);
    syscall_close((uint32_t)outFd);
    kfree(wadData);

    if (!written) {
        syscall_unlink(paths.wadPartPath);
        SetError(error, errorCapacity, "could not write doom1.wad");
        return false;
    }
    if (!ReplaceFile(paths.wadPartPath, paths.wadPath)) {
        syscall_unlink(paths.wadPartPath);
        SetError(error, errorCapacity, "could not move doom1.wad into place");
        return false;
    }

    Report(progress, "DOOM shareware data installed.");
    return true;
}

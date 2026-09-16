#pragma once
#include <stdint.h>

constexpr unsigned DFILE_PATH_SIZE = 65;

// File-management mailbox status codes. MORE means the client must send STEP;
// each step transfers only one sector so the Z80 poll cannot expire mid-image.
enum DiskFileStatus : uint8_t {
    DFILE_OK = 0, DFILE_IO = 4, DFILE_NOFILE = 6, DFILE_PROTECTED = 10,
    DFILE_MORE = 0x10, DFILE_EXISTS = 0x12, DFILE_INUSE = 0x14,
    DFILE_INVALID = 0x16, DFILE_FULL = 0x18
};

// Shared path policy for firmware and emulator. Bounded, explicit DSK paths;
// no FAT volume selector, wildcards, traversal, or names FatFs would trim.
inline bool DskFilePathValid(const char* path)
{
    unsigned n = 0, component = 0;
    for (; n < DFILE_PATH_SIZE && path[n]; ++n) {
        const unsigned char ch = path[n];
        if (ch < 32 || ch == ':' || ch == '*' || ch == '?' || ch == '"' ||
            ch == '<' || ch == '>' || ch == '|') return false;
        if (ch == '/' || ch == '\\') {
            if (!component && n != 0) return false;
            if (component && (path[n-1] == '.' || path[n-1] == ' ')) return false;
            component = 0;
        } else {
            if (!component && (ch == '.' || ch == ' ')) return false;
            ++component;
        }
    }
    return n >= 5 && n < DFILE_PATH_SIZE && component >= 5 &&
           path[n-4] == '.' && (path[n-3] | 32) == 'd' &&
           (path[n-2] | 32) == 's' && (path[n-1] | 32) == 'k';
}


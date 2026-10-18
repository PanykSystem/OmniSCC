/** Numbered DSK set naming, shared by the firmware and openMSX adapter. */
#pragma once

#include <stdint.h>
#include <string.h>
#include <array>

// Keep a template (or a directory and nine 8.3 aliases), not nine full paths:
// the cartridge has very little spare SRAM.
#define DISK_SET_PATH_MAX 65
#define DISK_SET_ALIAS_MAX 13

class CDiskSet {
public:
    void Reset() { _pattern[0] = '\0'; _available = 0; _current = 0; _aliasMode = false; }
    CDiskSet() { Reset(); }

    // Exists must accept only regular, nonempty, sector-aligned images. The
    // mounted member is already validated. Discover once, not on every swap.
    template<typename Exists>
    void Discover(const char* path, Exists exists) {
        Reset();
        const size_t len = strlen(path);
        Member member;
        if (len >= sizeof(_pattern) || !Parse(path, member)) return;
        strcpy(_pattern, path);
        _digit = uint8_t(member.digit);
        _current = uint8_t(member.number);
        for (unsigned n = 1; n <= member.total; ++n) {
            char candidate[DISK_SET_PATH_MAX];
            strcpy(candidate, _pattern);
            candidate[_digit] = char('0' + n);
            if (n == _current || exists(candidate))
                _available |= uint16_t(1u << (n - 1));
        }
    }

    // Canonical names longer than a mailbox path remain launchable through
    // FAT aliases. Retain each actual alias; alias digits are not disk numbers.
    void BeginAliases(const char* directory, unsigned selected) {
        Reset();
        if (strlen(directory) >= sizeof(_pattern) || selected < 1 || selected > 9) return;
        strcpy(_pattern, directory);
        _current = uint8_t(selected);
        _aliasMode = true;
    }
    void AddAlias(unsigned number, const char* alias) {
        if (!_aliasMode || number < 1 || number > 9 || !alias[0] ||
            strlen(alias) >= DISK_SET_ALIAS_MAX ||
            strlen(_pattern) + strlen(alias) >= DISK_SET_PATH_MAX) return;
        strcpy(_aliases[number-1], alias);
        _available |= uint16_t(1u << (number-1));
    }

    static unsigned Number(const char* path) {
        Member member;
        return Parse(path, member) ? member.number : 0;
    }
    static unsigned SiblingNumber(const char* reference, const char* candidate) {
        Member a, b;
        const size_t len = strlen(reference);
        return Parse(reference, a) && Parse(candidate, b) &&
               strlen(candidate) == len && a.digit == b.digit &&
               Equal(reference, candidate, unsigned(a.digit)) &&
               Equal(reference+a.digit+1, candidate+b.digit+1,
                     unsigned(len-a.digit-1)) ? b.number : 0;
    }

    bool Path(unsigned number, char* out) const {
        if (number < 1 || number > 9 ||
            !(_available & (1u << (number - 1)))) return false;
        strcpy(out, _pattern);
        if (_aliasMode) strcat(out, _aliases[number-1]);
        else out[_digit] = char('0' + number);
        return true;
    }
    unsigned Current() const { return _current; }
    void Selected(unsigned number) { _current = uint8_t(number); }

    template<class Archive> void serialize(Archive& ar, unsigned) {
        std::array<uint8_t,DISK_SET_PATH_MAX> pattern{};
        std::array<uint8_t,9*DISK_SET_ALIAS_MAX> aliases{};
        memcpy(pattern.data(),_pattern,sizeof _pattern);
        memcpy(aliases.data(),_aliases,sizeof _aliases);
        ar.serialize("pattern",pattern,"aliases",aliases);
        if constexpr(Archive::IS_LOADER) {
            memcpy(_pattern,pattern.data(),sizeof _pattern);
            memcpy(_aliases,aliases.data(),sizeof _aliases);
            _pattern[sizeof _pattern-1]=0;
        }
        ar.serialize("available",_available,"digit",_digit,"current",_current,"aliasMode",_aliasMode);
    }
private:
    struct Member { size_t digit; unsigned number; unsigned total; };

    // Match only the basename of a .dsk. Downloaded dump names may have tags
    // after the (Disk x of y) token; all text except x identifies the set,
    // including y and those tags. Multiple disk markers are ambiguous.
    static bool Parse(const char* path, Member& member) {
        const size_t len = strlen(path);
        if (len <= 4 || !Equal(path+len-4, ".dsk", 4)) return false;
        const char* base = strrchr(path, '/');
        base = base ? base+1 : path;
        bool found = false;
        if (len > 10) {
            const char* suffix = path+len-10;
            if (suffix > base && Equal(suffix, ".disk", 5) &&
                suffix[5] >= '1' && suffix[5] <= '9') {
                member = {len-5, unsigned(suffix[5]-'0'), 9};
                found = true;
            }
        }
        const char* end = path+len-4;
        for (const char* p = base; end-p >= 13; ++p) {
            if ((p == base && p+13 == end) || p[0] != '(' || !Equal(p+1, "disk ", 5) ||
                p[6] < '1' || p[6] > '9' || !Equal(p+7, " of ", 4) ||
                p[11] < p[6] || p[11] > '9' || p[12] != ')') continue;
            if (found) return false;
            member = {size_t(p+6-path), unsigned(p[6]-'0'), unsigned(p[11]-'0')};
            found = true;
        }
        return found;
    }

    static bool Equal(const char* a, const char* b, unsigned length) {
        for (unsigned i = 0; i < length; ++i) {
            char c = a[i];
            char d = b[i];
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            if (d >= 'A' && d <= 'Z') d += 'a' - 'A';
            if (c != d) return false;
        }
        return true;
    }
    char _pattern[DISK_SET_PATH_MAX]{};
    char _aliases[9][DISK_SET_ALIAS_MAX]{};
    uint16_t _available;
    uint8_t _digit = 0;
    uint8_t _current;
    bool _aliasMode;
};

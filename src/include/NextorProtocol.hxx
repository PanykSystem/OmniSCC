#pragma once
#include <stdint.h>
// Offsets within the 16 KiB driver bank (MSX addresses minus 4000h).
constexpr uint16_t NP_BUFFER=0x2000, NP_LBA=0x3F80, NP_COUNT=0x3F84,
 NP_DONE=0x3F85, NP_CAPACITY=0x3F86, NP_SECTOR_SIZE=0x3F8A,
 NP_VERSION=0x3F8C, NP_STATUS=0x3F8D, NP_READY=0x3F8E, NP_COMMAND=0x3F8F;
constexpr uint8_t NC_INFO=1, NC_READ=2, NC_WRITE=3, NEXTOR_MAX_SECTORS=11,
 NEXTOR_READY=0xA5;
enum NextorStatus : uint8_t { NS_OK=0, NS_BAD_PARAM=1, NS_RANGE=2,
 NS_NOT_READY=3, NS_WRITE_ERROR=4, NS_VERIFY_ERROR=5, NS_BAD_COMMAND=6 };

// Initialization scratch occupies unused driver-bank padding after the transfer buffer.
constexpr uint16_t NP_SCRATCH=NP_BUFFER+NEXTOR_MAX_SECTORS*512;
static_assert(NP_SCRATCH+512<=NP_LBA,"scratch must not overlap mailbox");

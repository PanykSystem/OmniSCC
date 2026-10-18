#pragma once
#include <cstddef>
#include <cstdint>

// One SPI session spans WREN, page program and all WIP polls. Restoring XIP
// between those commands is unsafe while the same flash chip is busy.
namespace KUCFlashSPI {
void Begin();
void Transfer(const uint8_t* tx, uint8_t* rx, size_t count);
void End();
}

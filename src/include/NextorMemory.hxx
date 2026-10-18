#pragma once
#include "NextorMailbox.hxx"
#include <stddef.h>
bool ValidateNextorROM(const uint8_t* rom,size_t size,uint8_t& banks,uint8_t& driverBank);
class CNextorMemory {
public:
 CNextorMemory(uint8_t* rom,uint8_t banks,uint8_t driverBank)
  : _rom(rom),_banks(banks),_driver(driverBank),_mailbox(rom+unsigned(driverBank)*16384) { _mailbox.Reset(); }
 uint8_t Read(uint16_t address) const;
 void Write(uint16_t address,uint8_t value);
private:
 uint8_t* _rom;
 uint8_t _banks,_driver,_bank=0;
 bool _busy=false;
 CNextorMailbox _mailbox;
};

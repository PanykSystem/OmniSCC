#include "NextorMemory.hxx"
#include "NextorProtocol.hxx"
#include <string.h>
#include "pico/stdlib.h"
bool ValidateNextorROM(const uint8_t* rom,size_t size,uint8_t& banks,uint8_t& driverBank) {
 if(!rom || size<32768 || size>131072 || size%16384) return false;
 uint8_t n=size/16384, k=rom[0xFE];
 if(!k || k+1!=n) return false;
 const uint8_t* driver=rom+unsigned(k)*16384;
 if(memcmp(driver+0x100,"NEXTOR_DRIVER",14) || driver[0x10E]!=1 || memcmp(driver+0x110,"OmniSCC shared flash",19)) return false;
 for(uint8_t i=0;i<n;++i) {
  const uint8_t* b=rom+unsigned(i)*16384;
  if(b[0]!='A' || b[1]!='B' || b[0xFE]!=k || b[0xFF]!=i || memcmp(b+0x3FD0,"\x32\xff\x7f\xc9",4)) return false;
 }
 banks=n;driverBank=k;return true;
}
uint8_t __not_in_flash_func(CNextorMemory::Read)(uint16_t address) const {
 if(address<0x4000 || address>=0x8000) return 0xFF;
 if(_busy && _bank==_driver && address==0x7F8E) return 0;
 return _rom[unsigned(_bank)*16384+address-0x4000];
}
void __not_in_flash_func(CNextorMemory::Write)(uint16_t address,uint8_t value) {
 if(_busy) return;
 if(address==0x7FFF) {if(value<_banks)_bank=value;return;}
 if(_bank!=_driver) return;
 uint8_t* b=_rom+unsigned(_driver)*16384;
 if((address>=0x6000 && address<0x7600) || (address>=0x7F80 && address<=0x7F84)) b[address-0x4000]=value;
 else if(address==0x7F8F) {
  _busy=true;
  _mailbox.Execute(value);
  _busy=false;
 }
}

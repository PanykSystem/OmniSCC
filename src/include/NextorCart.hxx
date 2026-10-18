#pragma once
#include "Cartridge.hxx"
#include "NextorMemory.hxx"
class CSCCSound;
class CNextorCartridge : public CCartridge {
public:
 CNextorCartridge(COmniSCC& owner,CSCCSound& scc,uint8_t* rom,uint8_t banks,uint8_t driver)
  : CCartridge(owner,rom,unsigned(banks)*16384-1),_memory(rom,banks,driver),_scc(scc) {}
 bool Execute() override;
private:
 CNextorMemory _memory;
 CSCCSound& _scc;
};

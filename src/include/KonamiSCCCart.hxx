/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 *
 * Needs to be a release NOT debug build for the cartridge emulation to work
 * 
 */

#pragma once

#include "Cartridge.hxx"
#include "SCCSound.hxx"

class CKonamiSCCCartridge: public CCartridge
{
public:
    CKonamiSCCCartridge(COmniSCC& multiCart, CSCCSound& scc, uint8_t* data, uint32_t sizeMask);
    ~CKonamiSCCCartridge();

private:
    virtual bool Execute();
    CSCCSound& _scc;
};
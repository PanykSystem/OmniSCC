#include "NextorCart.hxx"
#include "NextorProtocol.hxx"
#include "SCCSound.hxx"
#include "hardware/sync.h"
bool __not_in_flash_func(CNextorCartridge::Execute)() {
 uint32_t ints=save_and_disable_interrupts();
 uint32_t pins;
 bool sccEnabled=false;
 while (1) {
  while (((pins = gpio_get_all()) & SLTSL_GPIO_MASK) == SLTSL_GPIO_MASK);
  forceWait();
  while ((pins & (SLTSL_GPIO_MASK | RD_GPIO_MASK | WR_GPIO_MASK))
         == (RD_GPIO_MASK | WR_GPIO_MASK)) {
   pins = gpio_get_all();
  }
  if (!(pins & (SLTSL_GPIO_MASK | RD_GPIO_MASK))) {
   const uint16_t addr=(pins&ADDR_GPIO_MASK)>>ADDR_GPIO_SHIFT;
   const uint8_t data=sccEnabled && addr>=0x9800 && addr<0xA000 ? _scc.readRegister(uint8_t(addr)) : _memory.Read(addr);
   gpio_put_masked(DATA_GPIO_MASK,data);
   SET_DATA_MODE_OUT;
#ifdef WITH_SHIFTERS
   gpio_put(DATADIR_PIN,1);
#endif
   releaseWait();
   while (!(gpio_get_all() & RD_GPIO_MASK));
#ifdef WITH_SHIFTERS
   gpio_put(DATADIR_PIN,0);
#endif
   SET_DATA_MODE_IN;
  } else if (!(pins & (SLTSL_GPIO_MASK | WR_GPIO_MASK))) {
   const uint16_t addr=(pins&ADDR_GPIO_MASK)>>ADDR_GPIO_SHIFT;
   const uint8_t data=pins&DATA_GPIO_MASK;
   bool slow=addr==0x7F8F && data==NC_WRITE;
   if(addr>=0x9000 && addr<0x9800) sccEnabled=(data&0x3F)==0x3F;
   else if(sccEnabled && addr>=0x9800 && addr<0xA000) _scc.enqueueWrite(uint8_t(addr),data);
   else {
    if(slow) releaseWait();
    _memory.Write(addr,data);
   }
   releaseWait();
   if(slow) {while (!(gpio_get_all() & SLTSL_GPIO_MASK));}
   else {while (!(gpio_get_all() & WR_GPIO_MASK));}
  } else releaseWait();
 }
 restore_interrupts(ints);
 return true;
}

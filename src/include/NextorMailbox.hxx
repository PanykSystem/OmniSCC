#pragma once
#include <stdint.h>
#include "NextorVolumeView.hxx"
class CNextorMailbox {
public:
 explicit CNextorMailbox(uint8_t* driverBank) : _bank(driverBank) {}
 void Reset();
 void Execute(uint8_t command);
private:
 uint8_t* _bank;
 NextorVolumeView _view;
};

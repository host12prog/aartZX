#include <stdio.h>
#include <iostream>
#include <fstream>
#include "ymfm.h"
#include "ymfm_opn.h"

#define fmVol 128
#define ssgVol 128

ymfm::ym2203* fm;
ymfm::ym2203* fm2;
ymfm::ym2203::output_data fmout;
ymfm::ym2203::output_data fmout2;

// from https://github.com/tildearrow/furnace/blob/ec69c30ca0f3dde7a1248ef7dd49e105c8e24d72/src/engine/platform/fmshared_OPN.h#L90C1-L102C1
class opn_interface: public ymfm::ymfm_interface {
  int setA, setB;
  int countA, countB;

  public:
    void clock(int cycles=144);
    void ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks);
    opn_interface():
      ymfm::ymfm_interface(),
      countA(0),
      countB(0) {}
};

opn_interface iface;
opn_interface iface2;

void opn_interface::ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks) {
  if (tnum==1) {
    setB=duration_in_clocks;
  } else if (tnum==0) {
    setA=duration_in_clocks;
  }
}

void opn_interface::clock(int cycles) {
  if (setA>=0) {
    countA-=cycles;
    if (countA<0) {
      m_engine->engine_timer_expired(0);
      countA+=setA;
    }
  }
  if (setB>=0) {
    countB-=cycles;
    if (countB<0) {
      m_engine->engine_timer_expired(1);
      countB+=setB;
    }
  }
}

extern "C" {

// for zx.c to interface with ymfm...
void init_opn() {
    fm=new ymfm::ym2203(iface);
    fm->set_fidelity(ymfm::OPN_FIDELITY_MIN);
    fm2=new ymfm::ym2203(iface2);
    fm2->set_fidelity(ymfm::OPN_FIDELITY_MIN);
    fm->reset();
    fm2->reset();
}

void write_opn(uint8_t addr, uint8_t val, uint8_t cs) {
    if (cs&1) fm2->write(0,addr);
    else fm->write(0,addr);
    if (cs&1) fm2->write(1,val);
    else fm->write(1,val);
}

uint8_t read_opn(uint8_t addr, uint8_t cs) {
    if (cs&1) return fm2->read(addr);
    else return fm->read(addr);
}

void free_opn() {
    if (fm!=NULL) delete fm;
    if (fm2!=NULL) delete fm2;
}

void OPN_advance_clock() {
    fm->generate(&fmout);
    fm2->generate(&fmout2);
    iface.clock(24);
    iface2.clock(24);
}

int OPN_get_sample() {
    return (int)fmout.data[0]+(int)fmout2.data[0];
}

uint8_t OPN_get_status(uint8_t cs) {
    if (cs&1) return fm2->read(0);
    else return fm->read(0);
}

};
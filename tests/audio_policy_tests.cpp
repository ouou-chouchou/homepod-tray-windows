#include "policy.h"
#include <iostream>
#include <stdexcept>
void check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
int main() {
    using namespace homepod;
    ClockPolicy clock;
    for (int i=0;i<60000;++i) clock.update(0., true, .01);
    check(clock.applied() >= .998 && clock.applied() <= 1., "bounded shortage compensation");
    clock.update(0., false, 36000.);
    check(clock.applied() == 1., "pause must clear prior compensation");
    for (int i=0;i<60000;++i) clock.update(1., true, .01);
    check(clock.applied() <= 1.002 && clock.applied() > 1., "bounded excess compensation");
    PlaybackGate gate;
    check(!gate.ready(0,false), "idle never consumes");
    check(!gate.ready(PlaybackGate::prebufferFrames-1,true), "preserve short priming buffer");
    check(gate.ready(PlaybackGate::prebufferFrames,true), "start once threshold reached");
    check(gate.ready(352,true), "continue below threshold until real underflow");
    gate.underflow();
    check(!gate.ready(352,true), "recover with prebuffer instead of recurrent tiny gaps");
    check(!gate.ready(8820,false), "pause returns idle");
    std::cout << "audio pause, bounds and prebuffer policies passed\n";
}

//
// Created by ezra on 11/15/18.
//
// static class for producing curves in fade period
//
// FIXME: this should be an object owned by SoftcutHead, passed to child SubHeads


#ifndef Softcut_FADECURVES_H
#define Softcut_FADECURVES_H

namespace softcut {

    class FadeCurves {
    public:
        typedef enum { Linear=0, Sine=1, Raised=2 } Shape;

        // initialize with defaults
        void init();
         void setRecDelayRatio(float x);
         void setPreWindowRatio(float x);
         void setMinRecDelayFrames(unsigned int x);
         void setMinPreWindowFrames(unsigned int x);
        // set curve shape
         void setPreShape(Shape x);
         void setRecShape(Shape x);
        // x is assumed to be in [0,1]
         float getRecFadeValue(float x);

         float getPreFadeValue(float x);

    private:
         void calcPreFade();
         void calcRecFade();

    private:

        // xfade curve buffers
        static constexpr unsigned int fadeBufSize = 1001;

        // record delay and pre window in fade, as proportion of fade time.
        // sk-engines PORT NOTE: default member initializers added. Upstream init() calls
        // setPreShape/setRecShape FIRST, which immediately run calc{Pre,Rec}Fade() that read these
        // ratio/min members - but init() does not assign them until later, so the first calc reads
        // UNINITIALIZED members. A garbage ratio makes the fill count huge and overruns the 1001-float
        // stack buffer (stack-buffer-overflow, caught by ASan on host where a Voice lives on the stack;
        // benign on-device only because the engine is a zero-initialized static). Initializing the
        // members to init()'s own values makes the first calc bounded regardless of call order.
         float recDelayRatio       = 1.f / (8 * 16);
         float preWindowRatio      = 1.f / 8;
        // minimum record delay/pre window, in frames
         unsigned int recDelayMinFrames  = 0;
         unsigned int preWindowMinFrames = 0;
         float recFadeBuf[fadeBufSize];
         float preFadeBuf[fadeBufSize];
         Shape recShape = Raised;
         Shape preShape = Linear;
    };
}

#endif //Softcut_FADECURVES_H

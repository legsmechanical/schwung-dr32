/* space_extra.h — Echidna FX Suite's original space-category types (stereo).
 * NOT Typhon-calibrated — ear-tuned suite DSP.
 *
 * Types (slot stype 5..10 map here as type 0..5):
 *   Plate      — Dattorro figure-eight plate tank
 *   Shimmer    — plate tank + pitch-shifted (+12 st) feedback return;
 *                Shim param = shifted-return blend
 *   SpaceRev   — same tank pushed long/dark/slow-modulated (ambient bloom)
 *   GatedRev   — plate tank hard-gated by the INPUT envelope; Decay = hold
 *   DuckDelay  — stereo delay whose wet return ducks under live input
 *   ReverseDelay— chunked backwards playback into the feedback loop
 *
 * setParams(p1,p2,p3,feed,mix) mirrors Fx3's generic slots:
 *   reverbs     : p1=size  p2=damp  p3=pre-delay  feed=decay (+ shim01)
 *   duck/reverse: p1=time L  p2=time R  p3=damp(bipolar)  feed=feedback
 * setBpm/setSync apply to the two delays (division table in beats).
 * Linear dry/wet mix (our own FX). The two delays share one buffer (only one
 * type is armed at a time — Fx3's union-buffer pattern); setType resets.
 * RT-safe, fixed buffers, deterministic PRNG. */
#pragma once
#include <cmath>
#include <cstring>
#include <cstdint>
#include "audio_utils.h"
#include "galactic.h"
#include "airwin_verb.h"

namespace efx {

struct SpaceExtra {
    enum Type { Plate = 0, Shimmer = 1, SpaceRev = 2, GatedRev = 3,
                DuckDelay = 4, ReverseDelay = 5, Digital = 6, LoFi = 7,
                Hall = 8, Freeze = 9 };

    void setSampleRate(float fs) {
        fs_ = (fs > 1.0f) ? fs : 44100.0f;
        galactic_.setSampleRate(fs_);
        chamber_.setSampleRate(fs_);
        infinity_.setSampleRate(fs_);
        recompute();
    }
    void setType(int t) {
        if (t != type_) { type_ = t; reset(); recompute(); }
    }
    void setBpm(float bpm) { if (bpm >= 20.0f && bpm <= 999.0f) bpm_ = bpm; }
    void setSync(int s) { sync_ = s ? 1 : 0; }
    void setShim(float s01) { shim_ = clamp01(s01); }
    /** DR32: gate release as a TIME. The one-pole reaches ~63% in tau, so the
     *  audible close is a few tau; the coefficient is 1/(tau*fs). */
    /** DR32: the gated tank's decay, 0..1 mapped by the caller. */
    void setGateDecay(float d) {
        d = clamp01(d);
        if (d != gateDecay_) { gateDecay_ = d; recompute(); }
    }
    void setGateRelease(float seconds) {
        if (seconds < 0.0002f) seconds = 0.0002f;
        gateRel_ = 1.0f / (seconds * fs_);
        if (gateRel_ > 1.0f) gateRel_ = 1.0f;
    }
    void setParams(float p1, float p2, float p3, float feed, float mix) {
        p1_ = clamp01(p1); p2_ = clamp01(p2); p3_ = clamp01(p3);
        feed_ = clamp01(feed); mix_ = clamp01(mix);
        recompute();
    }

    void tick(float &l, float &r) {
        if (mix_ <= 0.0f) return;
        float wl = l, wr = r;
        switch (type_) {
            case SpaceRev:                            /* Airwindows Galactic */
                galactic_.tick(wl, wr); break;
            case Hall:   chamber_.tick(wl, wr); break;   /* Airwindows Chamber */
            case Freeze: infinity_.tick(wl, wr); break;  /* Airwindows Infinity2 */
            case Plate: case Shimmer: case GatedRev:
                plateTick(wl, wr); break;
            case Digital: case LoFi:
                fdnTick(wl, wr); break;
            case DuckDelay:    duckTick(wl, wr); break;
            case ReverseDelay: reverseTick(wl, wr); break;
            default: break;
        }
        l = l * (1.0f - mix_) + wl * mix_;
        r = r * (1.0f - mix_) + wr * mix_;
    }

    void reset() {
        std::memset(pre_, 0, sizeof(pre_));
        std::memset(dif_, 0, sizeof(dif_));
        std::memset(ap1_, 0, sizeof(ap1_)); std::memset(ap2_, 0, sizeof(ap2_));
        std::memset(ap3_, 0, sizeof(ap3_)); std::memset(ap4_, 0, sizeof(ap4_));
        std::memset(d1_, 0, sizeof(d1_)); std::memset(d2_, 0, sizeof(d2_));
        std::memset(d3_, 0, sizeof(d3_)); std::memset(d4_, 0, sizeof(d4_));
        std::memset(dly_, 0, sizeof(dly_));
        std::memset(shiftBuf_, 0, sizeof(shiftBuf_));
        std::memset(fdn_, 0, sizeof(fdn_));
        for (int i = 0; i < 4; i++) fdnDampZ_[i] = 0.0f;
        fdnW_ = 0; fdnHold_ = 0; fdnModPh_ = 0.0f;
        fdnOutL_ = fdnOutR_ = inZ_ = fdnHpZ_ = 0.0f;
        fdnNzEnv_ = 0.0f;
        galactic_.reset(); chamber_.reset(); infinity_.reset();
        preW_ = difW_[0] = difW_[1] = difW_[2] = difW_[3] = 0;
        ap1W_ = ap2W_ = ap3W_ = ap4W_ = 0;
        d1W_ = d2W_ = d3W_ = d4W_ = 0;
        dlyW_ = 0; shiftW_ = 0; shiftPh_ = 0.0f;
        bwZ_ = damp1Z_ = damp2Z_ = 0.0f;
        dampZ_[0] = dampZ_[1] = 0.0f;
        env_ = 0.0f; gateGain_ = 0.0f; gateHold_ = 0;
        modPh_ = 0.0f;
        revPhase_ = 0; revBase_ = 0;
        rnd_ = 0x2545f491u;
    }

private:
    static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    static constexpr float kTwoPi = 6.28318530718f;

    /* Dattorro lengths (29761 Hz paper rate) scaled to fs and by size01. */
    void recompute() {
        if (type_ == SpaceRev) {                      /* Airwindows Galactic */
            /* Size, Damp(→brightness, inverted), PreDelay(→detune width); run
             * full-wet — the slot's tick() applies the dry/wet blend. */
            galactic_.setParams(p1_, 1.0f - p2_, p3_, 1.0f);
            return;
        }
        if (type_ == Hall) {                          /* Airwindows Chamber */
            /* Size, Damp, Decay(→bigness/regen); full-wet, slot blends */
            chamber_.setParams(p1_, p2_, feed_, 1.0f);
            return;
        }
        if (type_ == Freeze) {                        /* Airwindows Infinity2 */
            /* Size, Damp, Decay(→feedback; high = freeze); full-wet, slot blends */
            infinity_.setParams(p1_, p2_, feed_, 1.0f);
            return;
        }
        float sc = (fs_ / 29761.0f);
        bool rev = type_ <= GatedRev;
        bool fdn = (type_ == Digital || type_ == LoFi);
        if (fdn) {
            /* 4-line FDN. base lengths (44.1k) mutually near-prime; scaled by
             * size + a per-type internal downsample factor that lowers the
             * modal frequencies (the metallic 80s character). */
            static const int kBase[4] = { 1531, 1949, 2251, 2803 };
            float size = 0.35f + 0.9f * p1_;
            fdnDown_ = (type_ == Digital) ? 2 : 3;   /* internal rate = fs/N */
            for (int i = 0; i < 4; i++)
                fdnLen_[i] = lenClampF((int)(kBase[i] * size / fdnDown_), kFdnMax);
            fdnDecay_ = 0.55f + 0.44f * feed_;
            fdnPre_ = 1 + (int)(p3_ * 0.2f * fs_);   /* 0..200 ms */
            if (type_ == Digital) {
                fdnDamp_ = 0.15f + 0.7f * p2_;       /* fairly bright */
                fdnBits_ = 12.0f;                    /* 12-bit loop grain */
                fdnModHz_ = 0.9f; fdnModDepth_ = 6.0f;   /* LX-style chorused tail */
                fdnInLp_ = 0.85f;                    /* minimal input filtering */
                fdnHp_ = 0.010f;                     /* gentle ~80 Hz low cut */
                fdnNoise_ = 0.0f;
            } else {                                 /* LoFi */
                fdnDamp_ = 0.22f + 0.45f * p2_;      /* keep the tail gritty, not dark */
                fdnBits_ = 7.0f;                     /* extra crunch */
                fdnModHz_ = 1.6f; fdnModDepth_ = 14.0f;  /* audible wobble */
                fdnInLp_ = 0.7f;                     /* let the highs bite */
                fdnHp_ = 0.055f;                     /* ~380 Hz low cut — kills the boom */
                fdnNoise_ = 0.0022f;                 /* grittier hiss floor */
            }
            return;
        }
        if (rev) {
            float size = 0.55f + 1.0f * p1_;                 /* 0.55..1.55 */
            if (type_ == SpaceRev) size = 0.8f + 0.8f * p1_; /* stays big */
            sc *= size;
            len_ap1_ = lenClamp((int)(672.0f  * sc), kAp1Max);
            len_ap2_ = lenClamp((int)(1800.0f * sc), kAp2Max);
            len_ap3_ = lenClamp((int)(908.0f  * sc), kAp3Max);
            len_ap4_ = lenClamp((int)(2656.0f * sc), kAp4Max);
            len_d1_  = lenClamp((int)(4453.0f * sc), kD1Max);
            len_d2_  = lenClamp((int)(3720.0f * sc), kD2Max);
            len_d3_  = lenClamp((int)(4217.0f * sc), kD3Max);
            len_d4_  = lenClamp((int)(3163.0f * sc), kD4Max);
            float ds = (fs_ / 29761.0f);
            len_dif_[0] = lenClamp((int)(142.0f * ds), kDifMax);
            len_dif_[1] = lenClamp((int)(107.0f * ds), kDifMax);
            len_dif_[2] = lenClamp((int)(379.0f * ds), kDifMax);
            len_dif_[3] = lenClamp((int)(277.0f * ds), kDifMax);
            preLen_ = 1 + (int)(p3_ * 0.25f * fs_);          /* 0..250 ms */
            damp_ = 0.05f + 0.9f * p2_;
            switch (type_) {
                case SpaceRev: decay_ = 0.85f + 0.147f * feed_; modHz_ = 0.35f; modDepth_ = 22.0f; break;
                /* DR32: the gated tank's decay is a CONTROL, not a constant.
                 * Fixed at 0.72 it did not fall inside a 250 ms hold — measured
                 * +6 dB across the window, i.e. still building density — so the
                 * "gate" was chopping something that behaved like a flat window
                 * and was barely distinguishable from the NonLin type next to
                 * it in the picker. A gate needs something to gate. */
                case GatedRev: decay_ = gateDecay_;            modHz_ = 0.9f;  modDepth_ = 8.0f;  break;
                /* DR32: widened from 0.30+0.65*feed. The old floor gave a
                 * 1.3 s minimum RT60 — far too long for a drum plate, and it
                 * left most of the knob above 3 s. */
                default:       decay_ = 0.05f + 0.90f * feed_; modHz_ = 0.7f;  modDepth_ = 9.0f;  break;
            }
            gateHoldSamps_ = (int)((0.05f + 0.45f * feed_) * fs_);   /* GatedRev: 50..500 ms */
        } else {
            /* delays: time01 -> 30 ms..1.2 s log, or BPM division */
            dlyL_ = delaySamps(p1_);
            dlyR_ = delaySamps((type_ == ReverseDelay) ? p1_ : p2_);
            fb_ = feed_ * 0.9f;
            /* bipolar damp: 0.5 = neutral; <0.5 dark (LP), >0.5 thin (HP-ish) */
            dampBip_ = p3_;
        }
    }
    static int lenClamp(int v, int mx) { return v < 4 ? 4 : (v > mx - 4 ? mx - 4 : v); }
    static int lenClampF(int v, int mx) { return v < 8 ? 8 : (v > mx - 8 ? mx - 8 : v); }

    float delaySamps(float t01) {
        if (sync_) {
            static const float kDivBeats[9] = { 0.125f, 0.1667f, 0.25f, 0.3333f, 0.5f,
                                                0.75f, 1.0f, 1.5f, 2.0f };
            int idx = (int)(t01 * 8.999f);
            float beat = 60.0f / bpm_;
            float s = kDivBeats[idx] * beat * fs_;
            return s > (float)(kDlyMax - 4) ? (float)(kDlyMax - 4) : s;
        }
        float ms = 30.0f * std::pow(40.0f, t01);             /* 30..1200 ms */
        float s = ms * 0.001f * fs_;
        return s > (float)(kDlyMax - 4) ? (float)(kDlyMax - 4) : s;
    }

    float rndf() {
        rnd_ ^= rnd_ << 13; rnd_ ^= rnd_ >> 17; rnd_ ^= rnd_ << 5;
        return (float)(int32_t)rnd_ * (1.0f / 2147483648.0f);
    }

    /* ---- ring helpers ---- */
    static float ringRead(const float *buf, int mask, int w, float delay) {
        float rp = (float)w - delay;
        int i0 = (int)std::floor(rp);
        float fr = rp - (float)i0;
        return buf[i0 & mask] * (1.0f - fr) + buf[(i0 + 1) & mask] * fr;
    }

    /* Schroeder allpass step on a ring buffer (g = diffusion) */
    static float apStep(float *buf, int mask, int &w, int len, float x, float g) {
        float d = buf[(w - len) & mask];
        float y = -g * x + d;
        buf[w & mask] = x + g * y;
        w++;
        return y;
    }
    /* modulated allpass (fractional read) */
    static float apStepMod(float *buf, int mask, int &w, float len, float x, float g) {
        float d = ringRead(buf, mask, w, len);
        float y = -g * x + d;
        buf[w & mask] = x + g * y;
        w++;
        return y;
    }

    /* ---- Dattorro plate tick (Plate / Shimmer / SpaceRev / GatedRev) ---- */
    void plateTick(float &l, float &r) {
        float inMono = 0.5f * (l + r);

        /* shimmer: read the shifted return before writing this sample */
        float shimIn = 0.0f;
        if (type_ == Shimmer && shim_ > 0.005f)
            shimIn = shiftRead() * shim_ * 0.8f;

        /* pre-delay */
        pre_[preW_ & (kPreMax - 1)] = inMono + shimIn;
        float x = pre_[(preW_ - preLen_) & (kPreMax - 1)];
        preW_++;

        /* input bandwidth + diffusion */
        bwZ_ += 0.7f * (x - bwZ_);
        float v = bwZ_;
        v = apStep(dif_[0], kDifMax - 1, difW_[0], len_dif_[0], v, 0.75f);
        v = apStep(dif_[1], kDifMax - 1, difW_[1], len_dif_[1], v, 0.75f);
        v = apStep(dif_[2], kDifMax - 1, difW_[2], len_dif_[2], v, 0.625f);
        v = apStep(dif_[3], kDifMax - 1, difW_[3], len_dif_[3], v, 0.625f);

        /* tank LFO */
        modPh_ += kTwoPi * modHz_ / fs_;
        if (modPh_ > kTwoPi) modPh_ -= kTwoPi;
        float m1 = modDepth_ * (0.5f + 0.5f * std::sin(modPh_));
        float m2 = modDepth_ * (0.5f + 0.5f * std::sin(modPh_ + 2.1f));

        /* figure-eight tank: branch A feeds from d4 tail, branch B from d2 tail */
        float tailB = d4_[(d4W_ - len_d4_) & (kD4Max - 1)];
        float tailA = d2_[(d2W_ - len_d2_) & (kD2Max - 1)];

        /* branch A */
        float a = v + decay_ * tailB;
        a = apStepMod(ap1_, kAp1Max - 1, ap1W_, (float)len_ap1_ + m1, a, 0.7f);
        d1_[d1W_ & (kD1Max - 1)] = a; d1W_++;
        float a2 = d1_[(d1W_ - len_d1_) & (kD1Max - 1)];
        damp1Z_ += (1.0f - damp_) * (a2 - damp1Z_);
        a2 = damp1Z_ * decay_;
        a2 = apStep(ap2_, kAp2Max - 1, ap2W_, len_ap2_, a2, 0.5f);
        d2_[d2W_ & (kD2Max - 1)] = ech::flushDenorm(a2); d2W_++;

        /* branch B */
        float b = v + decay_ * tailA;
        b = apStepMod(ap3_, kAp3Max - 1, ap3W_, (float)len_ap3_ + m2, b, 0.7f);
        d3_[d3W_ & (kD3Max - 1)] = b; d3W_++;
        float b2 = d3_[(d3W_ - len_d3_) & (kD3Max - 1)];
        damp2Z_ += (1.0f - damp_) * (b2 - damp2Z_);
        b2 = damp2Z_ * decay_;
        b2 = apStep(ap4_, kAp4Max - 1, ap4W_, len_ap4_, b2, 0.5f);
        d4_[d4W_ & (kD4Max - 1)] = ech::flushDenorm(b2); d4W_++;

        /* output taps (Dattorro-style spread reads, scaled to current lengths) */
        float yl = 0.6f * (ringRead(d3_, kD3Max - 1, d3W_, len_d3_ * 0.24f)
                         + ringRead(d3_, kD3Max - 1, d3W_, len_d3_ * 0.67f)
                         - ringRead(ap4_, kAp4Max - 1, ap4W_, len_ap4_ * 0.44f)
                         + ringRead(d4_, kD4Max - 1, d4W_, len_d4_ * 0.31f)
                         - ringRead(d1_, kD1Max - 1, d1W_, len_d1_ * 0.42f)
                         - ringRead(ap2_, kAp2Max - 1, ap2W_, len_ap2_ * 0.25f));
        float yr = 0.6f * (ringRead(d1_, kD1Max - 1, d1W_, len_d1_ * 0.19f)
                         + ringRead(d1_, kD1Max - 1, d1W_, len_d1_ * 0.71f)
                         - ringRead(ap2_, kAp2Max - 1, ap2W_, len_ap2_ * 0.58f)
                         + ringRead(d2_, kD2Max - 1, d2W_, len_d2_ * 0.28f)
                         - ringRead(d3_, kD3Max - 1, d3W_, len_d3_ * 0.51f)
                         - ringRead(ap4_, kAp4Max - 1, ap4W_, len_ap4_ * 0.73f));
        /* DR32: EARLY REFLECTIONS.
         *
         * Every output tap above reads a TANK line, and the shortest of them
         * sits at 0.19 of len_d1 — about 28 ms at default size, and 44 ms as
         * measured end to end. So the plate was silent for its first 40 ms:
         * you hit a drum and the reverb arrived a beat later at 90 BPM. It
         * read as a pre-delay nobody had asked for, and it was there in the
         * shipped module (Josh: "that was actually bugging me").
         *
         * The energy to fix it is already in the box and simply never tapped:
         * the four INPUT diffusers are 3.6 / 5 / 9 / 12.7 ms at 44.1 kHz. Those
         * are exactly the times a real plate's first reflections arrive, so
         * they are read here as early taps rather than inventing a delay line.
         *
         * ⚠ These taps are on the OUTPUT ONLY — they do not feed the tank, so
         * there is no feedback path and the tank's own state is untouched.
         * Measured against the previous build: the first 50 ms changes (that is
         * the point), 200-500 ms differs by -23.4 dB and past 500 ms by
         * -83.0 dB. So the tail is NOT bit-identical — the input allpasses ring
         * on for a few hundred ms and those rings are now audible — but the
         * late field is the same tank it always was.
         *
         * Different lines per channel, so the two sides stay decorrelated. */
        yl += 0.42f * (ringRead(dif_[0], kDifMax - 1, difW_[0], len_dif_[0] * 0.90f)
                     - 0.7f * ringRead(dif_[2], kDifMax - 1, difW_[2], len_dif_[2] * 0.80f));
        yr += 0.42f * (ringRead(dif_[1], kDifMax - 1, difW_[1], len_dif_[1] * 0.90f)
                     - 0.7f * ringRead(dif_[3], kDifMax - 1, difW_[3], len_dif_[3] * 0.80f));

        yl = ech::flushDenorm(yl);
        yr = ech::flushDenorm(yr);

        /* shimmer: feed the wet sum into the shifter ring */
        if (type_ == Shimmer) shiftWrite(0.5f * (yl + yr));

        /* gated reverb: input-envelope gate on the wet */
        if (type_ == GatedRev) {
            float aIn = std::fabs(inMono);
            env_ += (aIn > env_ ? 0.02f : 0.0005f) * (aIn - env_);
            if (env_ > 0.02f) gateHold_ = gateHoldSamps_;
            if (gateHold_ > 0) { gateHold_--; gateGain_ += 0.02f * (1.0f - gateGain_); }
            /* DR32: the release is a control now. gateRel_ defaults to 0.004,
             * the fixed value this always used (~5.7 ms), so an untouched gate
             * closes exactly as it did. */
            else               gateGain_ += gateRel_ * (0.0f - gateGain_);
            yl *= gateGain_; yr *= gateGain_;
        }
        l = yl; r = yr;
    }

    /* ---- shimmer grain shifter (+12 st, dual-head, triangular xfade) ---- */
    void shiftWrite(float x) {
        shiftBuf_[shiftW_ & (kShiftMax - 1)] = x;
        shiftW_++;
    }
    float shiftRead() {
        /* ratio 2.0 (one octave up): heads advance at +1 relative to write,
         * window = kShiftWin samples, 180° apart */
        const float ratio = 2.0f;
        shiftPh_ += (ratio - 1.0f);                /* head phase, wraps each window */
        if (shiftPh_ >= (float)kShiftWin) shiftPh_ -= (float)kShiftWin;
        float p0 = shiftPh_;
        float p1 = shiftPh_ + (float)kShiftWin * 0.5f;
        if (p1 >= (float)kShiftWin) p1 -= (float)kShiftWin;
        float w0 = 1.0f - std::fabs(p0 * 2.0f / (float)kShiftWin - 1.0f);
        float w1 = 1.0f - w0;
        /* delay SHRINKS as the phase grows (d' = -1) so the read head sweeps
         * the buffer at 2x write speed = +12 st */
        float g0 = ringRead(shiftBuf_, kShiftMax - 1, shiftW_, 2.0f + (float)kShiftWin - p0);
        float g1 = ringRead(shiftBuf_, kShiftMax - 1, shiftW_, 2.0f + (float)kShiftWin - p1);
        return g0 * w0 + g1 * w1;
    }

    /* ---- ducking delay ---- */
    void duckTick(float &l, float &r) {
        float outL = ringRead(dly_[0], kDlyMax - 1, dlyW_, dlyL_);
        float outR = ringRead(dly_[1], kDlyMax - 1, dlyW_, dlyR_);
        outL = dampStep(0, outL);
        outR = dampStep(1, outR);
        dly_[0][dlyW_ & (kDlyMax - 1)] = ech::fastTanh(l + outL * fb_);
        dly_[1][dlyW_ & (kDlyMax - 1)] = ech::fastTanh(r + outR * fb_);
        dlyW_++;
        /* duck the wet under the live input (5 ms attack / 250 ms release) */
        float aIn = 0.5f * (std::fabs(l) + std::fabs(r));
        env_ += (aIn > env_ ? 0.005f : 0.00009f) * (aIn - env_);
        float duck = 1.0f - 0.85f * (env_ * 6.0f > 1.0f ? 1.0f : env_ * 6.0f);
        l = outL * duck;
        r = outR * duck;
    }

    /* ---- reverse delay ---- */
    void reverseTick(float &l, float &r) {
        int L = (int)dlyL_;
        if (L < 256) L = 256;
        /* backwards read across the previous chunk */
        int rp = revBase_ + (L - 1 - revPhase_);
        float outL = dly_[0][rp & (kDlyMax - 1)];
        float outR = dly_[1][rp & (kDlyMax - 1)];
        /* short edge fade to kill chunk-boundary clicks */
        const int kFade = 220;                     /* ~5 ms */
        int e = revPhase_ < kFade ? revPhase_ : (revPhase_ > L - kFade ? L - revPhase_ : kFade);
        float fade = (float)e / (float)kFade;
        if (fade < 0.0f) fade = 0.0f;
        outL *= fade; outR *= fade;
        outL = dampStep(0, outL);
        outR = dampStep(1, outR);
        dly_[0][dlyW_ & (kDlyMax - 1)] = ech::fastTanh(l + outL * fb_);
        dly_[1][dlyW_ & (kDlyMax - 1)] = ech::fastTanh(r + outR * fb_);
        dlyW_++;
        if (++revPhase_ >= L) {
            revPhase_ = 0;
            revBase_ = dlyW_ - L;                  /* previous chunk start */
        }
        l = outL; r = outR;
    }

    /* ---- 80s-digital / lo-fi FDN reverb ----
     * 4-line feedback delay network with a Hadamard mix, run at an internal
     * fs/N rate (sample&hold in + out) so the modal frequencies drop into the
     * metallic "cheap digital" zone, with bit-reduced feedback for grain and
     * modulated line lengths for the LX-style chorused tail. Digital = brighter
     * 12-bit; LoFi = darker 8-bit with heavier wobble + hiss. ---- */
    float fdnQuant(float x) {
        float step = 2.0f / std::pow(2.0f, fdnBits_);
        return std::round(x / step) * step;
    }
    void fdnTick(float &l, float &r) {
        float inMono = 0.5f * (l + r);
        inZ_ += fdnInLp_ * (inMono - inZ_);
        /* high-pass the tank input so lows don't accumulate into boom */
        fdnHpZ_ += fdnHp_ * (inZ_ - fdnHpZ_);
        float hp = inZ_ - fdnHpZ_;
        /* pre-delay */
        pre_[preW_ & (kPreMax - 1)] = hp;
        float x = pre_[(preW_ - fdnPre_) & (kPreMax - 1)];
        preW_++;

        /* run the tank once per fdnDown_ input samples (internal downsample) */
        if (--fdnHold_ <= 0) {
            fdnHold_ = fdnDown_;
            fdnModPh_ += kTwoPi * fdnModHz_ * fdnDown_ / fs_;
            if (fdnModPh_ > kTwoPi) fdnModPh_ -= kTwoPi;
            float s[4];
            for (int i = 0; i < 4; i++) {
                float mod = fdnModDepth_ * std::sin(fdnModPh_ + i * 1.7f);
                float rd = ringRead(fdn_[i], kFdnMax - 1, fdnW_, (float)fdnLen_[i] + mod);
                fdnDampZ_[i] += (1.0f - fdnDamp_) * (rd - fdnDampZ_[i]);
                s[i] = fdnDampZ_[i];
            }
            /* normalized Hadamard 4x4 (lossless rotation) */
            float m0 = 0.5f * ( s[0] + s[1] + s[2] + s[3]);
            float m1 = 0.5f * ( s[0] - s[1] + s[2] - s[3]);
            float m2 = 0.5f * ( s[0] + s[1] - s[2] - s[3]);
            float m3 = 0.5f * ( s[0] - s[1] - s[2] + s[3]);
            float g = fdnDecay_;
            fdn_[0][fdnW_ & (kFdnMax - 1)] = ech::flushDenorm(fdnQuant(x + g * m0));
            fdn_[1][fdnW_ & (kFdnMax - 1)] = ech::flushDenorm(fdnQuant(x + g * m1));
            fdn_[2][fdnW_ & (kFdnMax - 1)] = ech::flushDenorm(fdnQuant(x + g * m2));
            fdn_[3][fdnW_ & (kFdnMax - 1)] = ech::flushDenorm(fdnQuant(x + g * m3));
            fdnW_++;
            fdnOutL_ = s[0] - s[2];        /* held across the downsample window */
            fdnOutR_ = s[1] - s[3];
        }
        /* DR32: the LoFi hiss floor is GATED by the tail's own envelope and
         * drawn PER CHANNEL. Upstream it was one unconditional mono draw added
         * to both outputs, which is a fine lo-fi character on an insert with a
         * dry/wet blend and wrong on a send return, where it measured:
         *   - a permanent -53 dBFS hiss whenever the type was merely ARMED,
         *     since a send return is 100% wet and has no dry to hide behind;
         *   - L/R correlation 0.97 and a mono-fold of -0.03 dB, i.e. the
         *     "stereo" reverb was dominated by a mono noise source;
         *   - an RT60 that never ended (8 s at every decay setting), which also
         *     defeats the bus's idle-skip: the slot never falls silent, so it
         *     never stops costing CPU.
         * The envelope keeps the grit for as long as there is a tail. */
        float nz[2] = { 0.0f, 0.0f };
        if (fdnNoise_ > 0.0f) {
            float amp = fabsf(fdnOutL_) + fabsf(fdnOutR_);
            fdnNzEnv_ += 0.001f * (amp - fdnNzEnv_);
            const float g = fdnNzEnv_ / (fdnNzEnv_ + 0.02f);
            for (int c = 0; c < 2; c++) {
                rnd_ = rnd_ * 1664525u + 1013904223u;
                nz[c] = (float)(int32_t)rnd_ * (1.0f / 2147483648.0f) * fdnNoise_ * g;
            }
        }
        l = fdnOutL_ + nz[0];
        r = fdnOutR_ + nz[1];
    }

    /* bipolar damp one-pole (0.5 = neutral, low = dark, high = thin) */
    float dampStep(int ch, float x) {
        if (dampBip_ < 0.48f) {                    /* darken: LP */
            float k = 0.08f + 1.6f * dampBip_;     /* deeper = darker */
            if (k > 1.0f) k = 1.0f;
            dampZ_[ch] += k * (x - dampZ_[ch]);
            return dampZ_[ch];
        }
        if (dampBip_ > 0.52f) {                    /* thin: HP via LP subtract */
            float k = 0.02f + 0.25f * (dampBip_ - 0.52f);
            dampZ_[ch] += k * (x - dampZ_[ch]);
            return x - dampZ_[ch] * (dampBip_ - 0.52f) * 2.0f;
        }
        dampZ_[ch] += 0.5f * (x - dampZ_[ch]);
        return x;
    }

    /* ---- state ---- */
    float fs_ = 44100.0f, bpm_ = 120.0f;
    int   type_ = Plate, sync_ = 0;
    float p1_ = 0.5f, p2_ = 0.5f, p3_ = 0.0f, feed_ = 0.5f, mix_ = 0.0f, shim_ = 0.5f;

    /* plate config */
    int   preLen_ = 1;
    int   len_dif_[4] = { 100, 80, 260, 190 };
    int   len_ap1_ = 900, len_ap2_ = 2500, len_ap3_ = 1300, len_ap4_ = 3600;
    int   len_d1_ = 6000, len_d2_ = 5000, len_d3_ = 5800, len_d4_ = 4300;
    float damp_ = 0.5f, decay_ = 0.6f, modHz_ = 0.7f, modDepth_ = 9.0f;
    int   gateHoldSamps_ = 8820;
    float gateRel_ = 0.004f;      /* DR32: one-pole release coefficient */
    float gateDecay_ = 0.72f;     /* DR32: the gated tank's own decay */

    /* delay config */
    float dlyL_ = 22050.0f, dlyR_ = 22050.0f, fb_ = 0.4f, dampBip_ = 0.5f;

    /* FDN (Digital / LoFi) config + state */
    int   fdnLen_[4] = { 700, 900, 1050, 1300 };
    int   fdnDown_ = 2, fdnPre_ = 1;
    float fdnDecay_ = 0.7f, fdnDamp_ = 0.3f, fdnBits_ = 12.0f;
    float fdnModHz_ = 0.9f, fdnModDepth_ = 6.0f, fdnInLp_ = 0.8f, fdnNoise_ = 0.0f, fdnHp_ = 0.0f;
    float fdnNzEnv_ = 0.0f;   /* DR32: gates the LoFi hiss — see fdnTick */
    float fdnDampZ_[4] = {}, fdnModPh_ = 0.0f, fdnOutL_ = 0.0f, fdnOutR_ = 0.0f, inZ_ = 0.0f, fdnHpZ_ = 0.0f;
    int   fdnW_ = 0, fdnHold_ = 0;

    /* buffers (power-of-two rings) */
    static constexpr int kPreMax  = 16384;
    static constexpr int kDifMax  = 1024;
    static constexpr int kAp1Max  = 2048,  kAp2Max = 8192;
    static constexpr int kAp3Max  = 4096,  kAp4Max = 8192;
    static constexpr int kD1Max   = 16384, kD2Max  = 16384;
    static constexpr int kD3Max   = 16384, kD4Max  = 16384;
    static constexpr int kDlyMax  = 65536;
    static constexpr int kShiftMax = 8192;
    static constexpr int kShiftWin = 3600;         /* ~82 ms grain window */
    static constexpr int kFdnMax   = 4096;         /* per FDN line (internal rate) */

    Galactic galactic_;                            /* Airwindows Galactic (SpaceRev) */
    Chamber chamber_;                              /* Airwindows Chamber (Hall) */
    InfinityVerb infinity_;                        /* Airwindows Infinity2 (Freeze) */

    float pre_[kPreMax] = {};
    float dif_[4][kDifMax] = {};
    float ap1_[kAp1Max] = {}, ap2_[kAp2Max] = {};
    float ap3_[kAp3Max] = {}, ap4_[kAp4Max] = {};
    float d1_[kD1Max] = {}, d2_[kD2Max] = {}, d3_[kD3Max] = {}, d4_[kD4Max] = {};
    float dly_[2][kDlyMax] = {};
    float shiftBuf_[kShiftMax] = {};
    float fdn_[4][kFdnMax] = {};

    int   preW_ = 0, difW_[4] = {}, ap1W_ = 0, ap2W_ = 0, ap3W_ = 0, ap4W_ = 0;
    int   d1W_ = 0, d2W_ = 0, d3W_ = 0, d4W_ = 0;
    int   dlyW_ = 0, shiftW_ = 0;
    float shiftPh_ = 0.0f;
    float bwZ_ = 0.0f, damp1Z_ = 0.0f, damp2Z_ = 0.0f;
    float dampZ_[2] = {};
    float env_ = 0.0f, gateGain_ = 0.0f;
    int   gateHold_ = 0;
    float modPh_ = 0.0f;
    int   revPhase_ = 0, revBase_ = 0;
    uint32_t rnd_ = 0x2545f491u;
};

} // namespace efx

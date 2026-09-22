/*
 * faust_shim.h — the three base classes Faust's C++ output expects.
 *
 * Faust's generated code (src/dsp/generated/*.hpp) derives from `dsp` and calls
 * into `UI` and `Meta`. Upstream ships those as architecture headers under
 * /usr/share/faust; we declare the minimum here instead, so the build needs a
 * cross g++ and nothing else — the module is built in the same Debian container
 * as the rest of the fleet, where no Faust install exists (the .hpp files are
 * generated on a workstation by scripts/gen_dsp.sh and committed).
 *
 * Only the members the generated code actually uses are declared. If a future
 * .dsp adds a widget kind (a vertical box, a bargraph, a soundfile), the build
 * fails loudly at the missing virtual rather than misbehaving at runtime.
 *
 * ZoneMap is the piece with a job: Faust hands out a FAUSTFLOAT* per control
 * during buildUserInterface(), keyed by the label from the .dsp. That pointer
 * IS the parameter — writing it is how the wrapper drives the synth. Labels
 * come from params.lib / onetrick.lib and are matched by exact string.
 */
#ifndef DR32_FAUST_SHIM_H
#define DR32_FAUST_SHIM_H

#include <cstring>

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif

struct Meta {
    virtual ~Meta() {}
    virtual void declare(const char *key, const char *value) = 0;
};

struct UI {
    virtual ~UI() {}
    virtual void openVerticalBox(const char *label) = 0;
    virtual void openTabBox(const char *label) = 0;
    virtual void closeBox() = 0;
    virtual void addButton(const char *label, FAUSTFLOAT *zone) = 0;
    virtual void addNumEntry(const char *label, FAUSTFLOAT *zone, FAUSTFLOAT init,
                             FAUSTFLOAT min, FAUSTFLOAT max, FAUSTFLOAT step) = 0;
    virtual void addHorizontalSlider(const char *label, FAUSTFLOAT *zone, FAUSTFLOAT init,
                                     FAUSTFLOAT min, FAUSTFLOAT max, FAUSTFLOAT step) = 0;
    virtual void addVerticalBargraph(const char *label, FAUSTFLOAT *zone,
                                     FAUSTFLOAT min, FAUSTFLOAT max) = 0;
    virtual void declare(FAUSTFLOAT *zone, const char *key, const char *value) = 0;
};

class dsp {
  public:
    virtual ~dsp() {}
    virtual int getNumInputs() = 0;
    virtual int getNumOutputs() = 0;
    virtual void buildUserInterface(UI *ui_interface) = 0;
    virtual int getSampleRate() = 0;
    virtual void init(int sample_rate) = 0;
    virtual void instanceInit(int sample_rate) = 0;
    virtual void instanceConstants(int sample_rate) = 0;
    virtual void instanceResetUserInterface() = 0;
    virtual void instanceClear() = 0;
    virtual dsp *clone() = 0;
    virtual void metadata(Meta *m) = 0;
    virtual void compute(int count, FAUSTFLOAT **inputs, FAUSTFLOAT **outputs) = 0;
};

/* Collects label -> control pointer while the DSP builds its UI. */
class ZoneMap : public UI {
  public:
    enum { kMaxZones = 64 };

    void openVerticalBox(const char *) override {}
    void openTabBox(const char *) override {}
    void closeBox() override {}
    void declare(FAUSTFLOAT *, const char *, const char *) override {}

    void addButton(const char *label, FAUSTFLOAT *zone) override { add(label, zone); }
    void addNumEntry(const char *label, FAUSTFLOAT *zone, FAUSTFLOAT, FAUSTFLOAT,
                     FAUSTFLOAT, FAUSTFLOAT) override { add(label, zone); }
    void addHorizontalSlider(const char *label, FAUSTFLOAT *zone, FAUSTFLOAT, FAUSTFLOAT,
                             FAUSTFLOAT, FAUSTFLOAT) override { add(label, zone); }
    void addVerticalBargraph(const char *label, FAUSTFLOAT *zone,
                             FAUSTFLOAT, FAUSTFLOAT) override { add(label, zone); }

    /* Returns NULL for a label this DSP does not have — every caller in the
     * wrapper checks, because drum.dsp and output.dsp share onetrick.lib and
     * each only instantiates the controls its own graph reads — Faust prunes
     * a declared control nothing downstream reads, which is why upstream's
     * Sensitivity_Global and Transpose have no zone at all. */
    FAUSTFLOAT *find(const char *label) const {
        for (int i = 0; i < count_; i++)
            if (strcmp(labels_[i], label) == 0) return zones_[i];
        return nullptr;
    }

    int count() const { return count_; }
    const char *label_at(int i) const { return (i >= 0 && i < count_) ? labels_[i] : nullptr; }

  private:
    void add(const char *label, FAUSTFLOAT *zone) {
        if (count_ >= kMaxZones) return;   /* silently dropping one would be worse */
        labels_[count_] = label;           /* string literals in generated code */
        zones_[count_] = zone;
        count_++;
    }

    const char *labels_[kMaxZones] = {nullptr};
    FAUSTFLOAT *zones_[kMaxZones] = {nullptr};
    int count_ = 0;
};

#endif /* DR32_FAUST_SHIM_H */

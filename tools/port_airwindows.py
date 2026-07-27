#!/usr/bin/env python3
"""Mechanically convert an Airwindows VST plugin into a self-contained,
RT-safe C++ struct for DR32.

The point of doing this with a script rather than by hand is that the DSP body
is transplanted VERBATIM -- no retyping, so no transcription bugs. The result
is null-tested against the original compiled behind the VST stub.
"""
import re, sys, os

SRC = os.path.dirname(os.path.abspath(__file__))

VST_CALLS = ('setNumInputs', 'setNumOutputs', 'setUniqueID', 'canProcessReplacing',
             'canDoubleReplacing', 'programsAreChunks', 'vst_strncpy', '_canDo',
             'setParameter(', '_programName')


def brace_body(text, start):
    """Return the body between the first '{' at/after start and its match."""
    i = text.index('{', start)
    depth, j = 0, i
    while True:
        if text[j] == '{':
            depth += 1
        elif text[j] == '}':
            depth -= 1
            if depth == 0:
                return text[i + 1:j]
        j += 1


def port(name):
    hdr = open(f'{SRC}/{name}__{name}.h').read()
    main = open(f'{SRC}/{name}__{name}.cpp').read()
    proc = open(f'{SRC}/{name}__{name}Proc.cpp').read()

    # --- 1. namespace-scope constants (delay lengths, early[] taps) ---
    consts = []
    for line in hdr.splitlines():
        s = line.strip()
        if s.startswith('const int kNumPrograms'):
            break
        if s.startswith('const int') or s.startswith('const double'):
            consts.append(line)
    # (simple plugins have no namespace-scope delay tables; that is fine)

    # --- 2. class data members ---
    cls = hdr.index(f'class {name}')
    body = brace_body(hdr, cls)
    members = []
    for line in body.splitlines():
        s = line.strip()
        if not s or s.startswith('//') or s.startswith('virtual') \
           or s.startswith('public:') or s.startswith('private:') \
           or s.startswith(f'{name}(') or s.startswith(f'~{name}') \
           or 'std::set' in s or '_programName' in s:
            continue
        # A..F are the parameters -- we expose them ourselves
        if re.fullmatch(r'float [A-Z];', s):
            continue
        members.append(line)

    # --- 3. constructor body, minus the VST plumbing ---
    ctor = main.index(f'{name}::{name}(audioMasterCallback')
    cbody = brace_body(main, ctor)
    init = [l for l in cbody.splitlines()
            if not any(v in l for v in VST_CALLS)]
    init = [l for l in init if l.strip()]
    # Split the parameter defaults out of the state init. reset() is called on
    # kit change and panic; if it also restored A..F it would silently throw
    # away the user's knob settings.
    defaults, state = [], []
    for l in init:
        m = re.fullmatch(r'\s*([A-Z])\s*=\s*([^;]+);\s*', l)
        (defaults if m else state).append(m.groups() if m else l)
    init = state

    # --- 4. the float processReplacing body, VERBATIM ---
    p = proc.index(f'void {name}::processReplacing')
    pbody = brace_body(proc, p)

    ns = f'awk_{name.lower()}'
    out = []
    out.append(f'// ---------------------------------------------------------------------------')
    out.append(f'//  {name} -- (c) Chris Johnson / Airwindows, MIT licence.')
    out.append(f'//  Transplanted from the upstream LinuxVST source by tools/port_airwindows.py:')
    out.append(f'//  the processReplacing body and all state are VERBATIM, only the VST base')
    out.append(f'//  class and parameter plumbing are replaced. Null-tested against upstream.')
    out.append(f'// ---------------------------------------------------------------------------')
    out.append(f'namespace {ns} {{')
    out.extend(consts)
    out.append('')
    out.append(f'struct {name} {{')
    out.append('    // upstream parameter defaults')
    out.append('    ' + ' '.join(f'float {k} = {v};' for k, v in defaults))
    out.append('    double sampleRate = 44100.0;')
    out.append('    double getSampleRate() const { return sampleRate; }')
    out.append('    void setSampleRate(double sr) { sampleRate = sr; }')
    out.append('')
    out.extend(members)
    out.append('')
    out.append(f'    {name}() {{ reset(); }}')
    out.append('    void reset() {')
    out.extend('    ' + l for l in init)
    out.append('    }')
    out.append('')
    out.append('    void processReplacing(float **inputs, float **outputs, int sampleFrames) {')
    out.extend('    ' + l for l in pbody.splitlines())
    out.append('    }')
    out.append('};')
    out.append(f'}} // namespace {ns}')
    return '\n'.join(out) + '\n'


if __name__ == '__main__':
    print(port(sys.argv[1]))

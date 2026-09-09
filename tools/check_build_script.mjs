#!/usr/bin/env node
/*
 * check_build_script.mjs — invariants of scripts/build.sh that nothing else can see.
 *
 * Greps two shell scripts. No Docker, no cross-compiler, milliseconds.
 *
 * ── WHY THIS EXISTS ─────────────────────────────────────────────────────────
 *
 * build.sh RE-RUNS ITSELF INSIDE DOCKER on the same mounted volume: the outer
 * pass writes build/ui.js on the host (the image has no Node), then
 * `docker run -v "$HERE":/work ... bash scripts/build.sh` re-enters the same
 * file with CROSS_PREFIX set. Everything below that guard therefore runs in the
 * CONTAINER, once — but only because the outer pass exits straight after the
 * docker run instead of falling through.
 *
 * That early exit is load-bearing and completely invisible in the source unless
 * you already know to look for it. Anyone who later moves a packaging step
 * below the docker run, or replaces the exit with a fallthrough so the host can
 * do post-processing, silently converts EVERY line beneath it into a two-pass
 * line — and a `rm -rf` among them then runs twice, the second pass deleting
 * what the first produced. A sibling repo shipped exactly that bug for one
 * build: an unguarded wipe deleted the ui.js the earlier pass had just written,
 * and the build still exited 0 and printed its success banner.
 *
 * DR32 is safe from that today for two independent reasons, and only one of
 * them is durable:
 *
 *   1. the outer pass does not fall through            <- pinned here
 *   2. the wipe is scoped to build/obj, while ui.js
 *      lives at build/ui.js                            <- luck, not design
 *
 * (2) is luck because `rm -rf build` is the obvious thing to write and would
 * have reproduced the bug exactly. So (1) is what gets pinned: it protects the
 * wipe AND every future line added below it, which a wipe-specific test would
 * not.
 *
 * The second invariant is the wipe itself, which exists because a stale TRACKED
 * object outlived its deleted source and was linked straight into dsp.so — the
 * removed FX bus went back into the shipped artifact, and the build printed
 * "==> done:" because a shared-library link does not have to resolve undefined
 * symbols. Caught only by grepping the artifact for symbols that should not
 * exist.
 */
import fs from "node:fs";

const path = process.argv[2] || "scripts/build.sh";
const src = fs.readFileSync(path, "utf8");
const lines = src.split("\n");
const errors = [];

/* Strip comments and blanks; keep original line numbers so a failure can point
 * at something. */
const code = lines
    .map((text, i) => ({ n: i + 1, text: text.replace(/#.*$/, "").trim() }))
    .filter((l) => l.text.length > 0);

/* ---- 1. the outer pass must not fall through past `docker run` ------------ */

const guard = code.find((l) => /\[ ! -f \/\.dockerenv \]/.test(l.text) && /\bthen\b/.test(l.text));
if (!guard) {
    errors.push(
        `no outer-pass guard found — expected an \`if ... [ ! -f /.dockerenv ]; then\` block. ` +
        `If the two-pass (host, then re-exec in Docker) shape is gone, DELETE this check ` +
        `rather than loosening it: a guard that no longer has a subject is worse than none.`);
} else {
    /* The block ends at the first `fi` in column 0 after the guard. */
    const endIdx = lines.findIndex((t, i) => i > guard.n - 1 && /^fi\s*$/.test(t));
    if (endIdx < 0) {
        errors.push(`outer-pass guard at line ${guard.n} has no closing \`fi\` in column 0`);
    } else {
        const inner = code.filter((l) => l.n > guard.n && l.n < endIdx + 1);
        const dockerRun = inner.find((l) => /\bdocker run\b/.test(l.text));
        if (!dockerRun) {
            errors.push(
                `the outer pass at line ${guard.n} no longer runs \`docker run\` — if the build ` +
                `stopped re-entering itself in a container, delete this check rather than ` +
                `weakening it.`);
        }
        /* Statements after the docker run, ignoring its own continuation lines
         * and the status plumbing, must end in an exit. */
        const after = inner.filter((l) => l.n > (dockerRun ? dockerRun.n : 0));
        const last = after[after.length - 1];
        if (!last || !/^exit\b/.test(last.text)) {
            errors.push(
                `THE OUTER PASS FALLS THROUGH past \`docker run\` (last statement in the block ` +
                `is line ${last ? last.n : "?"}: \`${last ? last.text : "<none>"}\`).\n` +
                `        Everything below the guard then runs TWICE — once on the host and once ` +
                `in the container, on the SAME mounted volume. Any \`rm -rf\` down there deletes ` +
                `what the earlier pass produced, and the build still exits 0.\n` +
                `        If you need host-side work after the container, put it in its own block ` +
                `BEFORE this one, or guard it on \`[ -f /.dockerenv ]\` — do not remove the exit.`);
        }
    }
}

/* ---- 2. the object dir is wiped before it is globbed into the link -------- */

const wipe = code.find((l) => /^rm -rf\s+build\/obj\b/.test(l.text));
const link = code.find((l) => /-shared\b/.test(l.text) && /build\/obj\/\*\.o/.test(l.text));
if (!link) {
    errors.push(
        `no \`-shared ... build/obj/*.o\` link line found — if the link stopped globbing the ` +
        `object directory, this check has no subject and should be deleted, not loosened.`);
} else if (!wipe) {
    errors.push(
        `\`build/obj\` is globbed into the link at line ${link.n} but never wiped.\n` +
        `        build/ is TRACKED here, so an object whose SOURCE was deleted survives and is ` +
        `still linked. That is not hypothetical: dr32_fxbus.o outlived the FX bus and put the ` +
        `removed reverbs back into dsp.so, with a clean "==> done:" — a shared-library link does ` +
        `not have to resolve undefined symbols, so nothing failed.`);
} else if (wipe.n > link.n) {
    errors.push(`build/obj is wiped at line ${wipe.n}, AFTER the link at line ${link.n}`);
}

/* ---- 3. the wipe must not be widened to build/ --------------------------- */

const wideWipe = code.find((l) => /^rm -rf\s+build\/?\s*$/.test(l.text));
if (wideWipe) {
    errors.push(
        `line ${wideWipe.n} wipes \`build\` wholesale. \`build/ui.js\` is written by the OUTER ` +
        `(host) pass before the container starts, so a wipe at this level destroys it — the exact ` +
        `bug a sibling repo shipped. Scope the wipe to the object directory.`);
}

if (errors.length) {
    console.error(`${path}: FAILED`);
    for (const e of errors) console.error(`  - ${e}`);
    process.exit(1);
}
console.log(`${path}: build invariants OK (outer pass exits; build/obj wiped before the glob link)`);

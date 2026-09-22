/*
 * browser.js — DR32's sample browser, drawn by the module.
 *
 * ⭐ WHY THE MODULE DRAWS THIS. The browser was first built as an items page:
 * the DSP served the listing as a JSON parameter and the host drew it. Nothing
 * that went wrong was about browsing.
 *
 *   - the value channel is ~4 KB, so a folder of 1339 samples could not be
 *     listed at all and had to be truncated with a row saying so
 *   - `get_param` runs on the SPI callback with a ~900 us budget, so the
 *     directory scan sat in the audio thread
 *   - a click says only "row 3", and one click arrives as SEVERAL writes
 *     (measured on the device: four in ~32 ms) while the host is still drawing
 *     the previous listing — so clicks landed in the wrong listing and walked
 *     the user somewhere they had not asked for
 *
 * A canvas removes all of it. This file runs in the shadow UI's QuickJS, reads
 * the filesystem directly, owns its cursor, and speaks to the DSP exactly once
 * per load: `pad<N>_sample = <path>`. No listing crosses the wire, nothing
 * touches the audio thread, and a click is a click because we handle it.
 *
 * ⭐⭐ IT NEEDS `enterable: true`, AND THAT IS NOT A DETAIL. A canvas normally
 * gets the jog wheel and nothing else: the host spends the CLICK on closing it.
 * A browser is a tree, so the one gesture that means "enter" was the one that
 * meant "leave" — clicking `..` left the browser, clicking a folder left the
 * browser. `enterable` hands the click over and makes Back a contract this file
 * answers with `handleBack`. Without that flag, or on a host that predates it,
 * this screen can be scrolled and cannot be navigated.
 */

import * as os from 'os';

/*
 * ⭐ THIS IS THE ENGINE PICKER NOW, AND THE SAMPLE BROWSER IS ONE SECTION OF IT
 * (Josh, 2026-09-21: "the sample knob becomes an engine knob. it takes you to a
 * picker that has multiple sections, e.g., Sample (items are Move Library, User
 * Library - each pointing to the browser), Simian (items are each of the
 * distinct simian drum models), urchin ...").
 *
 *   ENGINES        Sample ▸ | Simian ▸ | Urchin ▸        <- the top menu
 *     Sample       Move Library | User Library            <- as before
 *       ...        the file browser, unchanged
 *     Simian       .. | Kick | Snare | ...                <- models
 *
 * A model row behaves exactly as a sample row does: scrolling onto it puts it
 * on the pad (`pad<N>_model`), clicking it takes it and leaves. The DSP makes
 * the pad a synth pad and names it; nothing here knows what a model sounds
 * like. The model list is GENERATED from the engines' own tables — see the
 * block below and tools/gen_engine_ui.mjs.
 */

/* BEGIN GENERATED MODELS — tools/gen_engine_ui.mjs writes this; do not edit. */
const FAMILIES = [
    { id: "simian", label: "Simian", models: [
        { slug: "simian/kick", name: "Kick" },
        { slug: "simian/snare", name: "Snare" },
        { slug: "simian/rimshot", name: "Rimshot" },
        { slug: "simian/clap", name: "Clap" },
        { slug: "simian/low_tom", name: "Low Tom" },
        { slug: "simian/mid_tom", name: "Mid Tom" },
        { slug: "simian/high_tom", name: "High Tom" },
        { slug: "simian/hat_closed", name: "Closed Hat" },
        { slug: "simian/hat_open", name: "Open Hat" },
        { slug: "simian/cymbal", name: "Cymbal" },
    ] },
    { id: "urchin", label: "Urchin", models: [
        { slug: "urchin/kick", name: "Kick" },
        { slug: "urchin/snare", name: "Snare" },
        { slug: "urchin/rimshot", name: "Rimshot" },
        { slug: "urchin/low_tom", name: "Low Tom" },
        { slug: "urchin/mid_tom", name: "Mid Tom" },
        { slug: "urchin/high_tom", name: "High Tom" },
        { slug: "urchin/hat_closed", name: "Closed Hat" },
        { slug: "urchin/hat_open", name: "Open Hat" },
        { slug: "urchin/cymbal", name: "Cymbal" },
    ] },
    { id: "9w9", label: "9W9", models: [
        { slug: "9w9/kick", name: "Bass Drum" },
        { slug: "9w9/snare", name: "Snare" },
        { slug: "9w9/low_tom", name: "Low Tom" },
        { slug: "9w9/mid_tom", name: "Mid Tom" },
        { slug: "9w9/hi_tom", name: "Hi Tom" },
        { slug: "9w9/rimshot", name: "Rim Shot" },
        { slug: "9w9/clap", name: "Hand Clap" },
        { slug: "9w9/hat_closed", name: "Closed Hat" },
        { slug: "9w9/hat_open", name: "Open Hat" },
        { slug: "9w9/ride", name: "Ride" },
        { slug: "9w9/crash", name: "Crash" },
    ] },
    { id: "6w6", label: "6W6", models: [
        { slug: "6w6/kick", name: "Bass Drum" },
        { slug: "6w6/snare", name: "Snare" },
        { slug: "6w6/low_tom", name: "Low Tom" },
        { slug: "6w6/hi_tom", name: "Hi Tom" },
        { slug: "6w6/hat_closed", name: "Closed Hat" },
        { slug: "6w6/hat_open", name: "Open Hat" },
        { slug: "6w6/cymbal", name: "Cymbal" },
        { slug: "6w6/clap", name: "Clap" },
    ] },
    { id: "8w8", label: "8W8", models: [
        { slug: "8w8/kick", name: "Bass Drum" },
        { slug: "8w8/snare", name: "Snare" },
        { slug: "8w8/low_tom", name: "Low Tom" },
        { slug: "8w8/mid_tom", name: "Mid Tom" },
        { slug: "8w8/hi_tom", name: "Hi Tom" },
        { slug: "8w8/low_conga", name: "Low Conga" },
        { slug: "8w8/mid_conga", name: "Mid Conga" },
        { slug: "8w8/hi_conga", name: "Hi Conga" },
        { slug: "8w8/rimshot", name: "Rim Shot" },
        { slug: "8w8/claves", name: "Claves" },
        { slug: "8w8/maracas", name: "Maracas" },
        { slug: "8w8/clap", name: "Hand Clap" },
        { slug: "8w8/cowbell", name: "Cowbell" },
        { slug: "8w8/hat_closed", name: "Closed Hat" },
        { slug: "8w8/hat_open", name: "Open Hat" },
        { slug: "8w8/cymbal", name: "Cymbal" },
    ] },
    { id: "cw78", label: "CW-78", models: [
        { slug: "cw78/kick", name: "Bass Drum" },
        { slug: "cw78/snare", name: "Snare" },
        { slug: "cw78/rimshot", name: "Rim Shot" },
        { slug: "cw78/hihat", name: "Hi-Hat" },
        { slug: "cw78/cymbal", name: "Cymbal" },
        { slug: "cw78/maracas", name: "Maracas" },
        { slug: "cw78/claves", name: "Claves" },
        { slug: "cw78/hi_bongo", name: "Hi Bongo" },
        { slug: "cw78/low_bongo", name: "Low Bongo" },
        { slug: "cw78/low_conga", name: "Low Conga" },
        { slug: "cw78/cowbell", name: "Cowbell" },
        { slug: "cw78/tambourine", name: "Tambourine" },
        { slug: "cw78/guiro", name: "Guiro" },
        { slug: "cw78/metal_beat", name: "Metal Beat" },
    ] },
    { id: "chowkick", label: "ChowKick", models: [
        { slug: "chowkick/default", name: "Default" },
        { slug: "chowkick/tight", name: "Tight" },
        { slug: "chowkick/tonal", name: "Tonal" },
        { slug: "chowkick/bouncy", name: "Bouncy" },
        { slug: "chowkick/wonky", name: "Wonky Synth" },
    ] },
    { id: "fm", label: "FM", models: [
        { slug: "fm/kick", name: "Kick" },
        { slug: "fm/tom", name: "Tom" },
        { slug: "fm/snare", name: "Snare" },
        { slug: "fm/clap", name: "Clap" },
        { slug: "fm/rim", name: "Rim" },
        { slug: "fm/chat", name: "Closed Hat" },
        { slug: "fm/ohat", name: "Open Hat" },
        { slug: "fm/cymbal", name: "Cymbal" },
        { slug: "fm/cowbell", name: "Cowbell" },
        { slug: "fm/perc", name: "Percussion" },
        { slug: "fm/zap", name: "Zap" },
        { slug: "fm/drip", name: "Drip" },
        { slug: "fm/glitch", name: "Glitch" },
        { slug: "fm/clank", name: "Clank" },
    ] },
];
/* END GENERATED MODELS */

/* The top menu: samples, then one section per engine family. */
const SAMPLE_SECTION = 'sample';
function sections() {
    const out = [{ label: 'Sample', section: SAMPLE_SECTION }];
    for (let i = 0; i < FAMILIES.length; i++)
        out.push({ label: FAMILIES[i].label, section: FAMILIES[i].id });
    return out;
}
function familyOf(id) {
    for (let i = 0; i < FAMILIES.length; i++) if (FAMILIES[i].id === id) return FAMILIES[i];
    return null;
}
/* "simian/kick" -> the Simian family. */
function familyOfSlug(slug) {
    const s = String(slug || '');
    const cut = s.indexOf('/');
    return cut > 0 ? familyOf(s.slice(0, cut)) : null;
}

/* The two libraries, in display order. */
const ROOTS = [
    { label: 'Move Library', path: '/data/CoreLibrary/Samples' },
    { label: 'User Library', path: '/data/UserData/UserLibrary' },
];

const AUDIO = ['.wav', '.aif', '.aiff'];

/* Move's controls, as CC numbers rather than an import: this script is loaded
 * into the host's runtime BY PATH, and a bare relative import would resolve
 * against the wrong tree — the loader only rewrites /data/UserData/schwung/. */
const CC_JOG   = 14;
const CC_CLICK = 3;

/* Hardware pads arrive as notes 68..99 -- the PHYSICAL grid position, which is
 * not the kit's note map (36..67) and not the pad number either. We use it only
 * to know that a finger landed; see the note at the press handler. */
const PAD_NOTE_LO = 68;
const PAD_NOTE_HI = 99;

/*
 * 128x64, 1-bit, laid out to the device's own chrome geometry
 * (shared/list_geometry.mjs, which every list page on this machine uses):
 *
 *   rows 0..6    the header band -- HEADER_H is 7: a 5-row glyph at y=1 with
 *                one clear row above and below it
 *   row  7       the rule, at TITLE_RULE_Y
 *   rows 8..9    the gap
 *   rows 10..    the list, at MENU_LIST_Y
 *   last 7 rows  our hint row
 */
const HDR_H = 7;        /* HEADER_H */
const HDR_Y = 1;        /* TITLE_Y -- the glyph row inside the band */
const HDR_PAD = 2;      /* the device's own side margin */
const HDR_GAP = 4;      /* HEADER_GAP, between header elements */
/*
 * The rule under the band, at the first row below it -- which is what
 * list_geometry's TITLE_RULE_Y names (it equals HEADER_H).
 *
 * ⓘ The host's own list pages no longer draw one: the band's clear row is
 * their separator. This screen does, at Josh's call -- the body here is a file
 * listing in a larger font, not a knob grid, and the header needs a harder edge
 * against it than a blank row gives.
 */
const HDR_RULE_Y = HDR_H;
/*
 * THE LIST IS THE HOST'S LIST (Josh, 2026-09-22: "scroll bar and 5th line --
 * should be able to fit if everything is arranged scaled like 'module' 'my
 * presets' pages"). Those are drawMenuList (shared/menu_layout.mjs) in the
 * list_geometry.mjs rect, so these are its numbers, not ours:
 *   rows at y = 10, 19, 28, 37, 46 (LIST_TOP_Y, LIST_LINE_HEIGHT 9)
 *   the highlight one row above its text and 9 tall (LIST_HIGHLIGHT_OFFSET)
 *   labels at x = 9 (LIST_LABEL_X)
 *   the selection kept OFF the last row (keepOffLastRow), so you see what
 *   comes next before you reach it
 *   a dotted track with a solid thumb in the last-but-one column, 3 columns
 *   kept clear for it (drawScrollbar, BAR_GUTTER) -- only when the list scrolls
 * Our rule under the header (row 7) and the hint pills (57..63) sit outside
 * that rect, exactly where the host's own chrome sits.
 */
const LIST_Y = 10;      /* LIST_TOP_Y */
const ROW_H = 9;        /* LIST_LINE_HEIGHT */
const LABEL_X = 9;      /* LIST_LABEL_X */
const VISIBLE = 5;
const BAR_X = 126;      /* SCREEN_WIDTH - 2 */
const BAR_GUTTER = 3;
const ROW_INK = 7;      /* LIST_HIGHLIGHT_HEIGHT - 2 * LIST_HIGHLIGHT_OFFSET */
/* The device font's widest advance is 6 (measured through the host's own
 * font table), and there is no measureText on a canvas ctx: a label gets as
 * many characters as fit at 6 each between LABEL_X and the bar's gutter. */
const LABEL_CHARS = Math.floor((128 - BAR_GUTTER - 1 - LABEL_X) / 6);

/*
 * The hint row we draw ourselves.
 *
 * ⭐ OURS, NOT THE HOST'S. `show_footer: false` turns the host's footer off,
 * because a canvas is a screen the MODULE owns and the host has no business
 * painting a vocabulary on it -- "up a level" means something here and nothing
 * on a scope or a meter. So the words are ours and they are about this browser.
 *
 * ⚠ NO measureText. The host's canvas ctx does not offer one (davebox's does,
 * which is exactly the sort of difference not to lean on), so text is laid out
 * from the device font's fixed 6px advance. Keep the hints short enough that a
 * pixel of drift cannot matter.
 */

function isAudio(name) {
    const dot = name.lastIndexOf('.');
    return dot >= 0 && AUDIO.indexOf(name.slice(dot).toLowerCase()) >= 0;
}

function baseName(p) {
    const s = p.lastIndexOf('/');
    return s < 0 ? p : p.slice(s + 1);
}

function dirName(p) {
    const s = p.lastIndexOf('/');
    return s <= 0 ? '' : p.slice(0, s);
}

/** Which library `p` is under, or null. The browser never walks outside them. */
function rootOf(p) {
    for (let i = 0; i < ROOTS.length; i++) {
        const r = ROOTS[i].path;
        if (p === r || p.indexOf(r + '/') === 0) return r;
    }
    return null;
}

function isRoot(p) {
    for (let i = 0; i < ROOTS.length; i++) if (ROOTS[i].path === p) return true;
    return false;
}

/*
 * One directory as rows: '..' first, then folders, then samples.
 *
 * ⚠ os.readdir answers [entries, errno] on some builds and a bare array on
 * others. The host's own FILEPATH_BROWSER_FS carries the same two-shape guard,
 * so this is the contract, not defensive coding.
 */
function listDir(dir) {
    const rows = [];
    if (!dir) {
        for (let i = 0; i < ROOTS.length; i++)
            rows.push({ label: ROOTS[i].label, path: ROOTS[i].path, dir: true });
        return rows;
    }
    rows.push({ label: '..', path: '', dir: true });

    let names = [];
    try {
        const r = os.readdir(dir);
        names = Array.isArray(r) && Array.isArray(r[0]) ? r[0] : (Array.isArray(r) ? r : []);
    } catch (e) {
        return rows;                       /* unreadable: '..' is still a way out */
    }

    const dirs = [], files = [];
    for (let i = 0; i < names.length; i++) {
        const n = names[i];
        if (!n || n.charAt(0) === '.') continue;
        const full = dir + '/' + n;
        let isDir = false;
        try {
            const st = os.stat(full);
            const m = Array.isArray(st) ? st[0] : st;      /* [obj, errno] or obj */
            isDir = !!m && (m.mode & 0o170000) === 0o040000;   /* S_IFDIR */
        } catch (e) { continue; }
        if (isDir) dirs.push({ label: n + '/', path: full, dir: true });
        else if (isAudio(n)) files.push({ label: n, path: full, dir: false });
    }
    const byName = (a, b) => {
        const x = a.label.toLowerCase(), y = b.label.toLowerCase();
        return x < y ? -1 : (x > y ? 1 : 0);
    };
    dirs.sort(byName);
    files.sort(byName);
    return rows.concat(dirs, files);
}

/*
 * The pad the browser is filling.
 *
 * ⚠ 1-BASED ON THE WIRE: split_pad_key does `idx -= 1` on the way in (verified
 * in dr32_params.c, not taken from a comment about the wire — reading the
 * comment instead of the conversion is exactly how an earlier cut filled the
 * pad BELOW the focused one).
 */
function focusedPad(ctx) {
    const n = parseInt(ctx.getParam('ui_current_pad'), 10);
    return isFinite(n) && n >= 1 ? n : 1;
}

/** The top menu — the sections — with the cursor on `cursorOn`. */
function seatTop(st, cursorOn) {
    st.section = '';
    st.dir = '';
    st.rows = sections();
    st.cursor = 0;
    st.top = 0;
    for (let i = 0; i < st.rows.length; i++) if (st.rows[i].label === cursorOn) st.cursor = i;
    clampScroll(st);
}

/** One engine family's models, with the cursor on the model `slug`. */
function seatFamily(st, fam, slug) {
    st.section = fam.id;
    st.dir = '';
    st.rows = [{ label: '..', path: '', dir: true }];
    for (let i = 0; i < fam.models.length; i++)
        st.rows.push({ label: fam.models[i].name, model: fam.models[i].slug });
    st.cursor = 0;
    st.top = 0;
    for (let i = 0; i < st.rows.length; i++) if (slug && st.rows[i].model === slug) st.cursor = i;
    clampScroll(st);
}

/** Show `dir`, with the cursor on `cursorOn` if that row is there. */
function seat(st, dir, cursorOn) {
    st.section = SAMPLE_SECTION;
    st.dir = rootOf(dir || '') ? dir : '';
    st.rows = listDir(st.dir);
    st.cursor = 0;
    st.top = 0;
    if (cursorOn) {
        for (let i = 0; i < st.rows.length; i++) {
            if (st.rows[i].label === cursorOn) { st.cursor = i; break; }
        }
    }
    clampScroll(st);
}

/* drawMenuList's window: the selection may sit on rows 0..VISIBLE-2, never
 * the last one, and scrolling keeps it there. */
function clampScroll(st) {
    const maxRow = VISIBLE - 2;
    st.top = st.cursor > maxRow ? st.cursor - maxRow : 0;
}

/*
 * Put the highlighted sample on the pad.
 *
 * ⚠ THIS NEVER SOUNDS BY ITSELF, deliberately. You hit the pad, so it plays at
 * the velocity you hit it with — which an auto-audition cannot reproduce, and
 * which is why the pads are left unblocked.
 * 🔴 There is no undo, as on the kit browser: scroll past a sample and the pad's
 * previous one is gone.
 */
function audition(ctx, st) {
    const row = st.rows[st.cursor];
    if (!row || row.dir || row.section) return;   /* a folder must not clear the pad */
    if (row.model) {
        /* A model: the same one-write rule, on the model key. */
        if (row.model === st.loaded) return;
        ctx.setParam('pad' + st.pad + '_model', row.model);
        st.loaded = row.model;
        return;
    }
    if (row.path === st.loaded) return;    /* already there; skip the write */
    ctx.setParam('pad' + st.pad + '_sample', row.path);
    st.loaded = row.path;
}

/*
 * Open the highlighted row: a folder, or take the sample and go.
 *
 * ⭑ CLICKING A SAMPLE LEAVES. The scroll already put it on the pad, so the
 * click is not a commit — it is "this one, I'm done", and that is the commonest
 * path through this screen. Making you press Back afterwards would be one
 * gesture too many every single time. Josh, from the device: "i'd like clicking
 * on a sample to exit the browser as well."
 *
 * ⚠ `ctx.close` only exists where the host offers it. Without it the click
 * still confirms and the browser stays up, which is what it did before.
 */
function enterRow(ctx, st) {
    const row = st.rows[st.cursor];
    if (!row) return;
    if (row.section) {
        if (row.section === SAMPLE_SECTION) seat(st, '', null);
        else seatFamily(st, familyOf(row.section), st.loaded);
        return;
    }
    if (!row.dir) {
        audition(ctx, st);
        if (typeof ctx.close === 'function') ctx.close();
        return;
    }
    if (row.path) { seat(st, row.path, null); return; }
    goUp(st);                                      /* the '..' row */
}

/*
 * Up one level, or back to the two-library menu from a library root.
 *
 * The folder you came OUT of becomes the cursor, so stepping out lands you ON
 * it rather than at the top of a list — the whole of "minimize jumping in and
 * out of menus and folders".
 */
function goUp(st) {
    if (!st.section) return false;                 /* already at the top menu */
    if (st.section !== SAMPLE_SECTION) {           /* a family: back to the sections */
        const fam = familyOf(st.section);
        seatTop(st, fam ? fam.label : null);
        return true;
    }
    if (!st.dir) { seatTop(st, 'Sample'); return true; }   /* libraries -> sections */
    const child = baseName(st.dir);
    if (isRoot(st.dir)) { seat(st, '', child); return true; }
    const parent = dirName(st.dir);
    seat(st, rootOf(parent) ? parent : '', child + '/');
    return true;
}

/*
 * A hint, drawn the way the host's own hint rows are drawn.
 *
 * ⭐ THE SHAPE IS THE DEVICE'S, THE WORDS ARE OURS. `show_footer: false` means
 * the host paints nothing here, so this row is entirely the module's -- but a
 * row that looked homemade would read as a different device. So the metrics are
 * lifted from the real thing (ui_movy's drawKitHintRow and the host's own
 * drawFooter): the KEY sits inverted in a filled pill, 2px of padding each
 * side, all four corners knocked out, and the ACTION follows 4px later in plain
 * ink.
 *
 * ⚠ The pill is what makes a row parseable. Without it "BACK UP JOG PAGES" is
 * an unbroken run of words -- which is the reason the host's own rows have one.
 *
 * ⓘ The FONT differs from the host's rows (this is the device font; those are
 * movy's 4x5, which a canvas cannot reach). That is the right trade: the rest
 * of this browser is drawn in the device font, so the row matches the SCREEN it
 * belongs to rather than a row on some other screen.
 */
const HINT_PAD = 2;     /* inside the pill, each side   -- MV_HINT_PAD */
const HINT_GAP = 4;     /* pill to action text          -- MV_HINT_GAP */

/*
 * ⭐⭐ THE DEVICE'S SMALL FONT, CARRIED HERE.
 *
 * The screen is ours -- `show_footer: false` means the host paints nothing on
 * it -- so the type is ours to supply too. The host draws every hint row,
 * header and knob label in this 4x5; drawing our footer in the device 5x7
 * instead was legible and visibly FOREIGN, the right shape in the wrong type.
 *
 * ⚠ A MODULE THAT WANTS A TYPEFACE SHIPS THE TYPEFACE. The first fix for this
 * published the font from the host instead, which made a footer a module could
 * draw by itself depend on a host release. Josh: "the module has the screen.
 * why can't it draw it?" It can: the table is data and the blitter needs only
 * `fillRect`, which every canvas ctx has.
 *
 * Transcribed from schwung's src/shared/param_pages/font4x5.mjs (itself from
 * schwung-movy, MIT), and cut to the 41 glyphs this row can print rather than
 * all 59 -- A-Z, 0-9, space and the punctuation a path uses. Format is that
 * file's: [advance, yOff, w, h, ...rowBits] with bit0 the leftmost pixel.
 */
const FONT_H = 5;
const FONT_FALLBACK_ADV = 5;
const FONT = {
    " ": [3,0,0,5],
    "A": [5,0,4,5,6,9,15,9,9],
    "B": [5,0,4,5,7,9,7,9,7],
    "C": [5,0,4,5,14,1,1,1,14],
    "D": [5,0,4,5,7,9,9,9,7],
    "E": [5,0,4,5,15,1,7,1,15],
    "F": [5,0,4,5,15,1,7,1,1],
    "G": [5,0,4,5,14,1,13,9,14],
    "H": [5,0,4,5,9,9,15,9,9],
    "I": [2,0,1,5,1,1,1,1,1],
    "J": [5,0,4,5,12,8,8,9,6],
    "K": [5,0,4,5,9,5,3,5,9],
    "L": [5,0,4,5,1,1,1,1,15],
    "M": [6,0,5,5,17,27,21,17,17],
    "N": [5,0,4,5,9,11,13,9,9],
    "O": [5,0,4,5,6,9,9,9,6],
    "P": [5,0,4,5,7,9,7,1,1],
    "Q": [5,0,4,5,6,9,9,5,10],
    "R": [5,0,4,5,7,9,7,5,9],
    "S": [5,0,4,5,14,1,6,8,7],
    "T": [4,0,3,5,7,2,2,2,2],
    "U": [5,0,4,5,9,9,9,9,6],
    "V": [6,0,5,5,17,17,17,10,4],
    "W": [6,0,5,5,17,17,21,21,10],
    "X": [5,0,4,5,9,9,6,9,9],
    "Y": [4,0,3,5,5,5,2,2,2],
    "Z": [5,0,4,5,15,8,6,1,15],
    "0": [5,0,4,5,6,9,9,9,6],
    "1": [4,0,3,5,2,3,2,2,7],
    "2": [5,0,4,5,7,8,6,1,15],
    "3": [5,0,4,5,7,8,6,8,7],
    "4": [5,0,4,5,9,9,15,8,8],
    "5": [5,0,4,5,15,1,7,8,7],
    "6": [5,0,4,5,14,1,7,9,6],
    "7": [5,0,4,5,15,8,4,2,1],
    "8": [5,0,4,5,6,9,6,9,6],
    "9": [5,0,4,5,6,9,14,8,7],
    ".": [2,0,1,5,0,0,0,0,1],
    "/": [5,0,4,5,8,8,6,1,1],
    "-": [4,0,3,5,0,0,7,0,0],
    "_": [5,0,4,5,0,0,0,0,15],
};

function glyphFor(ch) { return FONT[ch] || FONT[ch.toUpperCase()] || null; }

/* The advance already carries the 1px inter-glyph gap, so a measured string is
 * one pixel narrower than the sum -- the same correction the source makes. */
function textW(ctx, t) {
    const str = String(t);
    let w = 0;
    for (let i = 0; i < str.length; i++) {
        const g = glyphFor(str[i]);
        w += g ? g[0] : FONT_FALLBACK_ADV;
    }
    return w > 0 ? w - 1 : 0;
}

/* Blit one string. Runs of set bits go out as a single fillRect, which is what
 * the source does and is why this needs no primitive a canvas lacks. */
function printSmall(ctx, x, y, str, color) {
    let cx = x;
    const t = String(str);
    for (let i = 0; i < t.length; i++) {
        const g = glyphFor(t[i]);
        if (!g) { cx += FONT_FALLBACK_ADV; continue; }
        const yOff = g[1], w = g[2], h = g[3];
        for (let row = 0; row < h; row++) {
            const bits = g[4 + row];
            if (!bits) continue;
            let col = 0;
            while (col < w) {
                if (bits & (1 << col)) {
                    const start = col;
                    while (col < w && (bits & (1 << col))) col++;
                    ctx.fillRect(cx + start, y + yOff + row, col - start, 1, color);
                } else col++;
            }
        }
        cx += g[0];
    }
}

function hintWidth(ctx, key, action) {
    return textW(ctx, key) + HINT_PAD * 2 + HINT_GAP + textW(ctx, action);
}

/*
 * A hint, drawn the way the host's own hint rows are drawn.
 *
 * The metrics come from the real thing (ui_movy's drawKitHintRow and the host's
 * own drawFooter): the KEY inverted in a filled pill, 2px padding each side,
 * all four corners knocked out, the ACTION 4px later in plain ink.
 *
 * ⚠ The pill is not decoration. Without it "BACK UP JOG PAGES" is an unbroken
 * run of words, which is exactly why the host's own rows have one.
 */
function drawHint(ctx, x, y, h, key, action) {
    const kw = textW(ctx, key) + HINT_PAD * 2;
    ctx.fillRect(x, y, kw, h, 1);
    /* The notch every filled shape on this device wears: one pixel off each
     * corner, so the pill reads as a shape rather than a block. */
    ctx.setPixel(x, y, 0);
    ctx.setPixel(x + kw - 1, y, 0);
    ctx.setPixel(x, y + h - 1, 0);
    ctx.setPixel(x + kw - 1, y + h - 1, 0);
    printSmall(ctx, x + HINT_PAD, y + 1, key, 0);
    printSmall(ctx, x + kw + HINT_GAP, y + 1, action, 1);
}

/*
 * The host's scroll bar (menu_layout.mjs drawScrollbar), transcribed: a dotted
 * track over the ROWS (not the whole rect), a solid thumb with a 2px floor,
 * sized and placed on the WINDOW so it does not shrink as the list ends.
 */
function drawScrollbar(ctx, total, top, shown) {
    const trackBottom = LIST_Y + (VISIBLE - 1) * ROW_H + ROW_INK;
    const trackH = trackBottom - LIST_Y;
    for (let y = LIST_Y; y < trackBottom; y += 2) ctx.setPixel(BAR_X, y, 1);
    const thumbH = Math.max(2, Math.round((shown / total) * trackH));
    const maxStart = total - shown;
    const ty = LIST_Y + (maxStart > 0 ? Math.round((top / maxStart) * (trackH - thumbH)) : 0);
    ctx.fillRect(BAR_X, ty, 1, thumbH, 1);
}

/* Trim a string to fit `maxW`, in our own font. */
function fitSmall(ctx, t, maxW) {
    let str = String(t);
    while (str.length && textW(ctx, str) > maxW) str = str.slice(0, -1);
    return str;
}

/*
 * The header, in the device's own three-part shape.
 *
 * ⭐ IT MATCHES THE MACHINE, and the numbers are the machine's: a 7-row band,
 * a 5-row glyph at y=1 with a clear row above and below, 2px side margins, all
 * in caps in the 4x5 this device draws every header in. Josh asked for it to
 * look like Modules and My Presets, and those are drawn by the host from
 * list_geometry.mjs -- so the constants above are lifted from there rather than
 * chosen.
 *
 *   PAD 7        which pad this browser is filling -- the thing you most need
 *                to be sure of before a click overwrites a sample
 *   BROWSER      what this screen is, centred
 *   Kicks        the folder you are in, right-aligned
 *
 * ⚠ MEASURED, NOT APPORTIONED. The centre is placed first because its width is
 * fixed, then the sides get what is left on their side of it. A folder name is
 * arbitrarily long, so it is the one that gets trimmed -- never the pad number,
 * which is the one fact on this screen that must not be ambiguous.
 */
function drawHeader(ctx, st) {
    const left = 'PAD ' + st.pad;
    const mid = 'BROWSER';
    const midW = textW(ctx, mid);
    const midX = Math.floor((ctx.width - midW) / 2);

    printSmall(ctx, HDR_PAD, HDR_Y, left, 1);

    /* The centre only if the sides leave room for it; on a narrow squeeze the
     * pad and the folder matter more than the word BROWSER. */
    const leftEnd = HDR_PAD + textW(ctx, left);
    if (midX >= leftEnd + HDR_GAP) printSmall(ctx, midX, HDR_Y, mid, 1);

    const fam = st.section && st.section !== SAMPLE_SECTION ? familyOf(st.section) : null;
    const where = st.dir ? baseName(st.dir)
                : fam ? fam.label
                : st.section === SAMPLE_SECTION ? 'LIBRARIES' : 'ENGINES';
    const rightRoom = ctx.width - HDR_PAD - (midX + midW + HDR_GAP);
    const r = fitSmall(ctx, where.toUpperCase(), Math.max(0, rightRoom));
    if (r) printSmall(ctx, ctx.width - HDR_PAD - textW(ctx, r), HDR_Y, r, 1);

    ctx.fillRect(0, HDR_RULE_Y, ctx.width, 1, 1);
}

/*
 * What Back will do, and -- while Shift is held -- that there is a way out.
 *
 * BACK is pinned to the RIGHT EDGE, which is where it sits on every other
 * screen on this device, and the escape hatch goes left of it. Shown only while
 * Shift is DOWN: a permanent hint for "when navigation goes wrong" would be
 * clutter on a screen that works.
 */
function drawHints(ctx, st) {
    /* The pill is the line height plus one pixel above and below -- the host's
     * own footer rule (FONT4_HEIGHT + 2), and now our font, so it cannot drift
     * from the type actually drawn. */
    const h = FONT_H + 2;
    const y = ctx.height - h;
    const back = st.section ? 'UP' : 'EXIT';
    drawHint(ctx, ctx.width - hintWidth(ctx, 'BACK', back) - 1, y, h, 'BACK', back);
    /* ⚠ ASK THE HOST, do not watch CC 49. The host reads Shift from the shim's
     * shared memory; the CC does not reliably reach a canvas. Watching the byte
     * worked under dAVEBOx, which forwards it, and did nothing at all on stock
     * -- reported from the device as the footer never changing. */
    if (typeof ctx.shiftHeld === 'function' && ctx.shiftHeld()) {
        drawHint(ctx, 1, y, h, 'JOG', 'PAGES');
    }
}

/*
 * Point the browser at what the pad already holds: a synth pad opens on its
 * engine's models with the cursor on its own; a sample pad on its sample's
 * folder, as it always has. An empty pad has nowhere of its own — on OPEN it
 * lands on the top menu, and on a pad PRESS the listing stays exactly where it
 * is, so one folder (or one model list) can fill empty pad after empty pad.
 */
function seatForPad(ctx, st, opening) {
    const model = ctx.getParam('pad' + st.pad + '_model') || '';
    const fam = familyOfSlug(model);
    if (fam) {
        st.loaded = model;
        seatFamily(st, fam, model);
        return;
    }
    const cur = ctx.getParam('pad' + st.pad + '_sample') || '';
    st.loaded = cur;
    if (cur && rootOf(dirName(cur))) seat(st, dirName(cur), baseName(cur));
    else if (opening) seatTop(st, null);
}

globalThis.canvas_overlay = {
    /*
     * ⭐ ASK FOR THE PADS. Opening a canvas leaves the knob grid, and the grid
     * is what normally reconciles `pad_observe` — so without this a browser
     * cannot tell which pad you pressed, on the one screen where filling a pad
     * is the entire job. Measured on the device: knob touches reached this
     * script and pad presses did not.
     *
     * PASSIVE. The pad still plays the kit; the screen is merely told as well,
     * which is the whole point — you hit the pad to hear what you just put on
     * it, at the velocity you hit it with.
     *
     * Ignored by a host that does not know the flag, which is exactly the
     * behaviour we had: the pads play and the browser keeps filling the pad
     * that was focused when it opened.
     */
    wantsPads: true,

    onOpen(ctx) {
        const st = ctx.state;
        st.pad = focusedPad(ctx);
        st.loaded = '';

        /*
         * A pad that already HAS a sample opens on that sample's folder, with
         * the cursor on it. An empty pad has nowhere of its own, so it opens on
         * the two libraries.
         */
        seatForPad(ctx, st, true);
    },

    /*
     * Back climbs the tree, and leaves only when there is nothing left to climb.
     *
     * ⭐ THE HOST'S CONTRACT: true means "I went up a level, keep me here";
     * anything else means "I am at my top". So this implements a way UP and
     * never a way OUT — the host already owns leaving, and one press past the
     * library picker is exactly when it should happen.
     */
    handleBack(ctx) {
        const st = ctx.state;
        return !!(st && goUp(st));
    },

    onMidi(ctx, msg) {
        const d = msg && msg.data;
        if (!d || d.length < 3) return;
        const st = ctx.state;
        if (!st.rows) return;
        const status = d[0] & 0xF0;

        /*
         * A PAD PRESS moves the browser to that pad — and BOTH halves matter:
         * it follows a pad that already HAS a sample, and stays exactly where it
         * is when the pad is empty, so you can walk one folder and fill empty
         * pad after empty pad without the listing moving under you.
         *
         * ⚠ WHETHER PAD NOTES REACH A CANVAS AT ALL IS A HOST QUESTION and the
         * answer differs by host. Nothing here depends on it: unforwarded, the
         * pads simply play and the browser keeps filling the pad that was
         * focused when it opened.
         */
        if (status === 0x90 && d[2] > 0 && d[1] >= PAD_NOTE_LO && d[1] <= PAD_NOTE_HI) {
            /*
             * ⚠⚠ THE NOTE IS A DOORBELL, NOT AN ADDRESS. 68..99 is the PHYSICAL
             * grid position, and it is NOT the pad number: the kit is laid out
             * in drum-rack order, so deriving `note - 68 + 1` names a different
             * pad from the one under your finger. That is what the first cut
             * did, and it showed the grid position as the pad.
             *
             * ⭐ WE DO NOT NEED THE MAP, AND MUST NOT OWN A COPY OF IT. The DSP
             * already moved its own focus: dr32_kit.c sets `ui_current_pad`
             * from the note it RECEIVED the instant a hand hits a pad with
             * nothing sequencing (`ui_auto_select_pad`, no vouch required). So
             * the note only tells us that a finger landed; the module tells us
             * where. The host says the same thing in as many words -- "the
             * pad-to-note map is Move's, not ours" (page_controller) -- and a
             * second copy here would be a copy that can disagree, and would go
             * wrong under a pad layout or a transpose besides.
             *
             * ⚠⚠⚠ AND WE MUST VOUCH, OR THIS STOPS WORKING AFTER A WHILE.
             *
             * `host_vouches` is a ONE-WAY LATCH (dr32_kit.h): the moment any
             * host vouches once, "a bare note-on never moves focus again,
             * whatever the transport says". Until then the DSP follows live
             * hits on its own, which is why a fresh session looks perfect --
             * and why it then quietly stops. Josh, from the device: "was
             * working great. and then after loading a bunch of samples, the
             * browser stopped following the pads." Every trip out to the knob
             * grid vouches, and after the first one our reads went stale.
             *
             * `ui_live_press` is the vouch, and dr32_params.c names US as its
             * writer in as many words: "the canvas is the obvious one". It
             * carries NO pad -- "a grid position is not a pad, so the note
             * decides, and this only vouches that a finger was involved". The
             * DSP looks back at the note it just played (within 20 blocks,
             * 58 ms) and moves focus, or arms forward if we beat the note here.
             *
             * ⓘ Order matters and is reliable: setParam and getParam are both
             * synchronous round trips, so the vouch has landed and focus has
             * moved by the time we ask. Both are legal here -- onMidi is not a
             * draw-path hook, so the accessors are present.
             */
            /*
             * ⭐ ASK BEFORE VOUCHING, because on some hosts focus has ALREADY
             * moved and a second claim is actively harmful.
             *
             * dAVEBOx emits the pad notes itself, so it knows exactly which
             * note it sent and NAMES it (`ui_live_note`) -- deterministic,
             * where a vouch is a race it measured itself losing 2 presses in
             * 16. By the time we see the press, focus is already correct. A
             * vouch on top of that finds the note consumed, ARMS FORWARD, and
             * is then claimed by the next note to arrive -- a sequenced one,
             * moving focus to a pad nobody touched.
             *
             * So: read first. If focus moved, follow it and say nothing.
             */
            let pad = focusedPad(ctx);
            if (pad === st.pad) {
                /* Focus did NOT move, so this host is leaving it to us. Vouch,
                 * and read back -- setParam and getParam are both synchronous
                 * round trips, so the vouch has landed and the DSP has matched
                 * it against the note it just played before we ask again. */
                ctx.setParam('ui_live_press', '1');
                pad = focusedPad(ctx);
            }
            if (pad === st.pad) return;
            st.pad = pad;
            seatForPad(ctx, st, false);
            return;
        }

        if (status !== 0xB0) return;

        if (d[1] === CC_JOG) {
            const v = d[2];
            const delta = v === 0 ? 0 : (v <= 63 ? v : -(128 - v));
            if (!delta) return;
            st.cursor += delta;
            if (st.cursor < 0) st.cursor = 0;
            if (st.cursor >= st.rows.length) st.cursor = st.rows.length - 1;
            clampScroll(st);
            audition(ctx, st);
            return;
        }

        if (d[1] === CC_CLICK && d[2] > 0) enterRow(ctx, st);
    },

    draw(ctx) {
        const st = ctx.state;
        ctx.clear();
        if (!st.rows) return;

        drawHeader(ctx, st);

        const total = st.rows.length;
        const shown = Math.min(VISIBLE, total - st.top);
        const scrolls = st.top > 0 || st.top + shown < total;
        for (let i = 0; i < shown; i++) {
            const idx = st.top + i;
            const y = LIST_Y + i * ROW_H;
            const on = idx === st.cursor;
            if (on) ctx.fillRect(0, y - 1, ctx.width - (scrolls ? BAR_GUTTER : 0), ROW_H, 1);
            ctx.print(LABEL_X, y, st.rows[idx].label.slice(0, LABEL_CHARS), on ? 0 : 1);
        }
        if (scrolls) drawScrollbar(ctx, total, st.top, shown);

        drawHints(ctx, st);
    },
};

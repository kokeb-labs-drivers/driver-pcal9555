"""What one pin call does: compute from the driver's copy, write the pair, read it back, and what each outcome leaves.

Reads: Top to bottom on the left, the steps of SetOutputs(); the other pin setters take the same path to their own
register pair. The call first checks that the driver is started and the mask is not 0, with no I/O. It computes the
new output pair from the driver's copy (GetConfig()) without reading the chip, and writes it in one transfer. With
Verify::ReadBack passed (the default is Verify::None) it reads the pair back. The cards on the right are the
outcomes: Bus may have delivered the pair, so the copy takes the written level, marked unknown, and the next call reads
it before building on it; WriteMismatch means the chip acknowledged the write but holds something
else, and the copy keeps what was asked, so retrying the call sends it again; Ok updates the copy. The band at the bottom contrasts the copy
with ReadOutputLatch(), which reads the output pair from the chip, and says how a handler retries.
"""

import re

import _source as S
from figkit import Fig


def data():
    fns = S.functions()
    regs = S.registers()
    checked, mask = fns["writePairChecked"], fns["storeMask"]
    S.need(re.search(r"!r \|\| \(?verify == Verify::None\)?\) \{\s*return r;", checked), "writePairChecked skips the read")
    S.need("readPair(reg)" in checked and "Error::WriteMismatch" in checked, "writePairChecked reads back")
    S.need(re.search(r"Error::WriteMismatch\) \{\s*config_\.\*field = value;", mask), "storeMask keeps the request")
    S.need("markFailed(asked, reg, r.error)" in mask, "storeMask marks a failed write unknown")
    S.need("detail::Apply(config_.output_high_mask, mask, high)" in fns["SetOutputs"], "SetOutputs uses the copy")
    toggle_from_copy = bool(re.search(r"config_\.output_high_mask\)? \^", fns["ToggleOutputs"]))
    default = S.need(re.search(r"Verify verify = Verify::(None|ReadBack)\) noexcept;", S.read(S.HPP)),
                     "Verify default").group(1)
    setters = [c for c in S.public_calls()
               if c in fns and ("writeMask(" in fns[c] or "storeMask(" in fns[c]) and re.search(r"checkCall\([^)]*, false\)", fns[c])]
    checks = [e for e in S.errors_in(fns["checkCall"]) if e != "Unsupported"]
    out = regs["kOutputPort0"]["addr"]
    return {
        "output_pair": [S.hexb(out), S.hexb(out + 1)],
        "verify_default": default,
        "checks": checks,
        "setters": setters,
        "toggle_from_copy": toggle_from_copy,
        "readback": [c for c, used in S.call_registers().items() if c.startswith("Read") and "kOutputPort0" in used],
        "sources": [f"{S.IPP}:SetOutputs, writeMask, storeMask, writePairChecked, ReadOutputLatch",
                    f"{S.TYPES}:Verify", f"{S.REGS}:kOutputPort0"],
    }


def build(d):
    f = Fig(
        "pin-write",
        "One pin call, and what each outcome leaves",
        alt=("Flow chart of SetOutputs(): compute the new pair from the driver's copy, write it, read it back when "
             "Verify::ReadBack is passed, then Ok, Bus or WriteMismatch; ReadOutputLatch() reads what the chip holds."),
        width=1200,
        kicker="API guide",
        subtitle=(f"Every pin setter works the same way: {', '.join(c + '()' for c in d['setters'])}. "
                  "Shown here for SetOutputs(), whose pair is the output port."),
        legend=[("teal", "your call"), ("blue", "driver steps"), ("green", "Ok"), ("rose", "Bus"),
                ("amber", "WriteMismatch"), ("~solid", "next step")],
    )
    W = f.body_w
    M = 560          # main column
    ex = M + 110     # exits column
    EW = W - ex
    row = 46
    p0, p1 = d["output_pair"]

    def exit_card(main, title, lines, tone, label):
        h = S.card_h(f, EW, title, lines=lines)
        box = f.card(ex, main.cy - h / 2, EW, title, lines, tone=tone, accent=True)
        f.edge([(main.r + 4, main.cy), (box.x - 4, main.cy)], tone=tone, label=label)
        return box

    call = f.card(0, 0, M, "SetOutputs(mask, high)", ["checks that the driver is started and the mask is not 0"],
                  tone="teal", accent=True, mono=False)
    exit_card(call, " or ".join(d["checks"]), ["nothing sent, the copy unchanged"], "slate", "no")
    comp = f.card(0, call.b + row, M, "1  Compute from the driver's copy",
                  ["new pair = GetConfig().output_high_mask with the mask bits set or cleared",
                   "the chip is not read first"], tone="blue", accent=True)
    f.connect(call, comp, label="yes")
    write = f.card(0, comp.b + row, M, "2  Write the pair",
                   [f"one transfer: command {p0}, then port 0, then port 1"], tone="blue", accent=True)
    f.connect(comp, write)
    exit_card(write, "Bus", ["the pair may have arrived: the copy takes it, marked",
                             "unknown; the next call reads it first"], "rose", "fails")
    verify = f.card(0, write.b + row, M, "verify argument?",
                    [f"the call's last argument, Verify::{d['verify_default']} by default"], tone="blue")
    f.connect(write, verify, label="ACK")
    exit_card(verify, "Ok", ["the copy now holds the new pair; nothing read back"], "green", "None")
    read = f.card(0, verify.b + row, M, "3  Read the pair back", [f"one 2-byte read of {p0}, {p1}"],
                  tone="blue", accent=True)
    f.connect(verify, read, label="ReadBack")
    exit_card(read, "Bus", ["the write was acknowledged: the copy takes it, marked",
                            "unknown until a read or a successful write"], "rose", "fails")
    cmp_ = f.card(0, read.b + row, M, "4  Compare with what was written", ["the bytes the chip returned"],
                  tone="blue")
    f.connect(read, cmp_)
    exit_card(cmp_, "WriteMismatch",
              ["the chip acknowledged the write but kept something else",
               "the copy keeps what was asked, so retrying the call sends it again"], "amber", "differs")
    ok = f.card(0, cmp_.b + row, M, "Ok", ["the chip holds the new pair, and so does the copy"], tone="green",
                accent=True)
    f.connect(cmp_, ok, label="same")

    # Bottom band: the two views of the output pair, and the handler's part.
    top = ok.b + 56
    inner = f.region_inner_top(top)
    cw = 290
    hw = W - 32 - 2 * cw - 2 * 16
    texts = [
        ("GetConfig()", ["the driver's copy: what was asked for", "no I/O"], "blue", cw),
        (f"{d['readback'][0]}()", [f"reads {p0}, {p1} from the chip: what it holds",
                                   "differs from the copy after a lost write"], "slate", cw),
        ("A handler retries",
         ["On WriteMismatch or Bus, call again a bounded number of times: the driver stays started and sends the pair again."]
         + (["A retried ToggleOutputs() toggles again from the copy (after Bus, from a fresh read)."] if d["toggle_from_copy"] else [])
         + [f"If it repeats, compare {d['readback'][0]}() with GetConfig() and check the bus; after a bus reset, "
            "Deinitialize() then EnsureInitialized() writes every register again."], "teal", hw),
    ]
    h = max(S.card_h(f, w, t, lines=ln) for t, ln, _, w in texts)
    f.region(0, top, W, inner - top + h + 18, "Copy and chip", tone="slate",
             sub="GetConfig() is never read from the chip; the read-back calls always are")
    x = 16
    for t, ln, tone, w in texts:
        f.card(x, inner, w, t, ln, tone=tone, h=h, accent=tone == "teal")
        x += w + 16
    return f

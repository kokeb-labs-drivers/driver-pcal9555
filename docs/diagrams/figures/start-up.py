"""What EnsureInitialized() does, step by step, and the error each failing step returns.

Reads: Top to bottom on the left, the steps of the first EnsureInitialized() call; once it has succeeded, the call
returns Ok at once with no I/O. The configuration and the address are checked before any I/O. Then the port brings
the bus up, and the presence check reads the input ports; on a PCAL9555A it also reads 0x4F, whose reserved bits must
be 0. Every register the driver owns is then written, in the order the code uses: on a PCAL9555A the Agile I/O bank
first and 0x4F after it, then the output values before the directions, so a new output starts at its configured
level. Every register is read back, unconditionally; a register that does not hold what was written means another
part answers. Each card on the right is the error a step returns. After any failure the driver stays uninitialised
and the next call starts again at the top.
"""

import re

import _source as S
from figkit import Fig


def _order(body: str, marks: list[str], what: str) -> None:
    pos = [S.need(body.find(m) + 1, f"{what}: {m}") for m in marks]
    S.need(pos == sorted(pos), f"{what}: order {marks}")


def data():
    fns = S.functions()
    regs = S.registers()
    types = S.read(S.TYPES)
    init = fns["initialize"]
    _order(init, ["Validate(config_)", "bus_.GetAddress()", "GetPort().EnsureInitialized()", "checkPresence()",
                  "writeAll(config_, failed)", "verifyAll(config_)", "initialized_ = true"], "initialize()")
    S.need(re.search(r"if \(initialized_\) \{\s*return Result<void>::Success\(\);", fns["EnsureInitialized"]),
           "EnsureInitialized() returns at once when started")
    _order(fns["writeAll"], ["AgilePairs", "kOutputPortConfig", "StandardPairs"], "writeAll()")
    _order(fns["verifyAll"], ["StandardPairs", "AgilePairs", "kOutputPortConfig"], "verifyAll()")
    S.need("Verify" not in init + fns["verifyAll"], "start-up read-back does not depend on a Verify argument")
    pres = fns["checkPresence"]
    _order(pres, ["readPair(reg::kInputPort0)", "isAgile()", "reg::kOutputPortConfig", "kOutputConfigReservedMask"],
           "checkPresence()")
    validate = S.need(re.search(r"constexpr Error Validate\(.*?\n\}", types, re.S), "Validate()").group(0)
    addr = {n: int(v, 16) for n, v in re.findall(r"inline constexpr uint8_t (k(?:Min|Max)I2cAddress) = (0x\w+);",
                                                 types)}
    addr_err = S.need(re.search(r"GetAddress\(\) > kMaxI2cAddress\)?\) \{\s*return fail\(Error::(\w+)", init),
                      "address error").group(1)
    port_err = S.need(re.search(r"GetPort\(\)\.EnsureInitialized\(\)\) \{\s*return fail\(Error::(\w+)", init),
                      "port error").group(1)
    reserved = int(S.need(re.search(r"kOutputConfigReservedMask = (0x\w+);", S.read(S.REGS)), "mask").group(1), 16)

    # A step that passes a transfer's error on returns what the bus helper returns: Bus, or NoDevice when the
    # port reports an address NACK (RegisterBus in the interface header).
    bus_errors = S.errors_in(S.need(re.search(r"Error TransferError\(.*?\n\}", S.read(S.IFACE), re.S),
                                    "detail::TransferError()").group(0))

    def step_errors(body):
        found = S.errors_in(body)
        if re.search(r"\b(?:r|value|back)\.error\b|return r;", body):
            found = [e for e in bus_errors[::-1] if e not in found] + found
        return found

    def names(pairs):
        return [(S.hexb(regs[r]["addr"]), re.sub(r" 0$", "", regs[r]["label"].split(",")[0])) for r, _ in pairs]

    oconf = regs["kOutputPortConfig"]["addr"]
    inp = regs["kInputPort0"]["addr"]
    return {
        "validate_errors": S.errors_in(validate),
        "address": [S.hexb(addr["kMinI2cAddress"]), S.hexb(addr["kMaxI2cAddress"])],
        "address_error": addr_err,
        "port_error": port_err,
        "presence": {"input": [S.hexb(inp), S.hexb(inp + 1)], "oconf": S.hexb(oconf),
                     "reserved_bits": [reserved.bit_length() - 1, (reserved & -reserved).bit_length() - 1],
                     "errors": step_errors(pres)},
        "write": {"agile": names(S.pairs("AgilePairs")), "oconf": S.hexb(oconf),
                  "standard": names(S.pairs("StandardPairs")), "errors": step_errors(fns["writeAll"])},
        "verify_errors": step_errors(fns["verifyAll"]),
        "sources": [f"{S.IPP}:EnsureInitialized, initialize, checkPresence, writeAll, verifyAll",
                    f"{S.HPP}:StandardPairs, AgilePairs", f"{S.TYPES}:Validate, kMinI2cAddress, kMaxI2cAddress",
                    f"{S.REGS}:register constants"],
    }


def build(d):
    f = Fig(
        "start-up",
        "What EnsureInitialized() does",
        alt=("Flow chart of EnsureInitialized(): check the configuration and the address, bring the bus up, check "
             "that the chip answers, write every register with the outputs before the directions, read them all "
             "back, then started; each failing step returns its error."),
        width=1200,
        kicker="Contract",
        subtitle=("The first successful call starts the chip. A failed call leaves the driver uninitialised, and "
                  "the next call starts again at the top."),
        legend=[("teal", "your call"), ("blue", "driver steps"), ("slate", "fails, nothing sent"),
                ("rose", "fails on the bus"), ("green", "Ok"), ("~solid", "next step")],
    )
    W = f.body_w
    M = 640
    ex = M + 100
    EW = W - ex
    row = 46

    def exit_card(main, title, lines, tone, label):
        h = S.card_h(f, EW, title, lines=lines)
        box = f.card(ex, main.cy - h / 2, EW, title, lines, tone=tone, accent=True)
        f.edge([(main.r + 4, main.cy), (box.x - 4, main.cy)], tone=tone, label=label)
        return box

    def step(prev, title, lines, label=""):
        box = f.card(0, prev.b + row, M, title, lines, tone="blue", accent=True)
        f.connect(prev, box, label=label)
        return box

    p, w = d["presence"], d["write"]
    call = f.card(0, 0, M, "EnsureInitialized()", ["started already?"], tone="teal", accent=True)
    exit_card(call, "Ok", ["at once, no I/O"], "green", "yes")
    s1 = step(call, "1  Check the configuration", ["Validate(config): enum values, and no Agile I/O field "
                                                   "changed on Variant::Pca9555"], "no")
    exit_card(s1, " or ".join(d["validate_errors"]), ["nothing sent"], "slate", "bad")
    s2 = step(s1, "2  Check the address", [f"{d['address'][0]} to {d['address'][1]}"])
    exit_card(s2, d["address_error"], ["nothing sent"], "slate", "bad")
    s3 = step(s2, "3  Bring the bus up", ["your port's EnsureInitialized()"])
    exit_card(s3, d["port_error"], ["the port returned false"], "rose", "fails")
    hi, lo = p["reserved_bits"]
    s4 = step(s3, "4  Is a chip there?", [f"read the input ports {p['input'][0]}, {p['input'][1]}",
                                         f"PCAL9555A: read {p['oconf']}; reserved bits {hi}..{lo} must be 0"])
    exit_card(s4, " or ".join(p["errors"]), ["Bus: no answer (NACK) or a bus error",
                                             f"DeviceIdMismatch: a reserved bit of {p['oconf']} reads 1"],
              "rose", "fails")
    # Step 5 lists the write order as numbered chips, one row group per bank.
    k = f.k
    pad = k["pad"]
    cx = pad + 6
    cw = M - 2 * pad - 6
    agile = [f"{a} {n.lower()}" for a, n in w["agile"]] + [f"{w['oconf']} output port configuration"]
    std = [f"{a} {n.lower()}" for a, n in w["standard"]]
    agile = [f"{i} · {s}" for i, s in enumerate(agile, 1)]
    std = [f"{i} · {s}" for i, s in enumerate(std, len(agile) + 1)]
    y5 = s4.b + row
    t_y = y5 + pad + k["card_title"] * 1.3 + 6
    h_a = f.chips(cx, t_y + k["small"] * 1.5, cw, agile, tone="violet", on="blue.tint")
    s_y = t_y + k["small"] * 1.5 + h_a + 12
    h_s = f.chips(cx, s_y + k["small"] * 1.5, cw, std, tone="blue", on="blue.tint")
    h5 = s_y + k["small"] * 1.5 + h_s + pad - y5
    s5 = f.card(0, y5, M, "5  Write every register, in this order", tone="blue", accent=True, h=h5)
    f.connect(s4, s5)
    f.text(cx, t_y + k["small"], "PCAL9555A only, first: the Agile I/O bank", size=k["small"], color="blue.text",
           bg="blue.tint")
    f.text(cx, s_y + k["small"], "Both parts: the outputs before the directions, so a new output starts at its level",
           size=k["small"], color="blue.text", bg="blue.tint")
    exit_card(s5, " or ".join(w["errors"]), ["a write failed"], "rose", "fails")
    s6 = step(s5, "6  Read every register back", ["the same registers, always: no Verify argument"])
    exit_card(s6, " or ".join(d["verify_errors"]),
              ["Bus: a read failed", "DeviceIdMismatch: a register holds something else, so another part answers"],
              "rose", "fails")
    ok = f.card(0, s6.b + row, M, "Started: Ok", ["pin calls work from now on"], tone="green", accent=True)
    f.connect(s6, ok, label="all match")
    return f

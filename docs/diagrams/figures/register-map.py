"""The register map: every register pair by command byte, who owns it in the driver, and its power-up value.

Reads: One card per register pair, in command-byte order; port 0 is the even byte, port 1 the odd one. The top band
is the standard bank, 0x00 to 0x07, on both the PCA9555 and the PCAL9555A; the lower band is the Agile I/O bank,
0x40 to 0x4F, which only the PCAL9555A has. Each card gives the access (R or R/W), the power-up value of each
register, the Config field the driver keeps for it, and the calls that read or write it. EnsureInitialized() and
ApplyConfig() write every Config field shown. The input ports and the interrupt status are read only, so they have no
Config field. Addresses come from the constants in inc/pcal9555_registers.hpp, fields from the pair lists in
inc/pcal9555.hpp and calls from src/pcal9555.ipp.
"""

import re

import _source as S
from figkit import Fig


def _bytes(value: int, n: int) -> str:
    return ", ".join(S.hexb((value >> (8 * i)) & 0xFF) for i in range(n))


def data():
    regs = S.registers()
    resets = S.resets()
    fields = dict(S.pairs("StandardPairs"))
    agile_src = S.detail_fn("AgilePairs")
    for reg, field in S.pairs("AgilePairs"):
        fields.setdefault(reg, field)
    calls = S.call_registers()
    regs_text = S.read(S.REGS)
    bits_per_pin = int(S.need(re.search(r"kDriveBitsPerPin = (\d+)U?;", regs_text), "kDriveBitsPerPin").group(1))
    oden = dict(re.findall(r"inline constexpr uint8_t (kOutputConfigOden\d) = 1U << (\d);", regs_text))
    reserved = int(S.need(re.search(r"kOutputConfigReservedMask = (0x[0-9A-Fa-f]+);", regs_text), "reserved").group(1), 16)
    oconf = S.detail_fn("OutputPortConfig")

    rows = []
    covered = set()
    for name, r in regs.items():
        addr = r["addr"]
        single = addr % 2 == 1
        covered |= {addr} if single else {addr, addr + 1}
        label = re.sub(r" 0$", "", r["label"])
        reset = S.reset_for(name, resets)
        field = fields.get(name)
        field_text = f"Config::{field}" if field else ""
        power_up = None
        if name.startswith("kDriveStrength"):
            high = bool(re.search(rf"reg::{name},\s*\.value = [^}}]*>> 16U", agile_src))
            lo = 16 if high else 0
            pins = (lo // bits_per_pin, (lo + 16) // bits_per_pin - 1)
            label = f"Output drive strength, pins {pins[0]} to {pins[1]}"
            field_text += f" bits {lo + 15}..{lo}"
            power_up = _bytes(resets[reset] >> lo, 2)
        elif reset:
            power_up = _bytes(resets[reset], 1 if single else 2)
        if name == "kOutputPortConfig":
            parts = []
            for f, const in re.findall(r"config\.(\w+) == OutputMode::OpenDrain\) \{\s*value (?:= [^;]*|\|= (?:static_cast<uint32_t>\()?)reg::(\w+)\)*;", oconf):
                parts.append(f"Config::{f} ({const[len('kOutputConfig'):].upper()}, bit {oden[const]})")
            S.need(len(parts) == 2, "OutputPortConfig() fields")
            hi = reserved.bit_length() - 1
            lo = (reserved & -reserved).bit_length() - 1
            field_text = "\n".join(parts + [f"bits {hi}..{lo} reserved"])
        users = [c for c, used in calls.items() if name in used]
        rows.append({
            "reg": name,
            "cmd": S.hexb(addr) if single else f"{S.hexb(addr)}, {S.hexb(addr + 1)}",
            "label": label,
            "access": r["access"],
            "agile": addr >= 0x40,
            "power_up": power_up,
            "field": field_text,
            "calls": users,
        })
    agile_range = range(min(a["addr"] for a in regs.values() if a["addr"] >= 0x40),
                        max(a["addr"] for a in regs.values()) + 1)
    unused = [S.hexb(a) for a in agile_range if a not in covered]
    return {
        "rows": rows,
        "unused": unused,
        "sources": [f"{S.REGS}:register constants", f"{S.HPP}:StandardPairs, AgilePairs, OutputPortConfig",
                    f"{S.IPP}:calls"],
    }


def _content(row):
    title = f"{row['cmd']}  {row['label']}"
    if row["power_up"]:
        sub = f"Power-up {row['power_up']}"
    elif row["reg"] == "kInputPort0":
        sub = "Pin levels; read only, the driver never writes it"
    else:
        sub = "Read only, the driver never writes it"
    lines = []
    if row["field"]:
        lines += row["field"].split("\n")
    if row["calls"]:
        lines.append("  ".join(c + "()" for c in row["calls"]))
    return title, sub, lines


def build(d):
    f = Fig(
        "register-map",
        "The register map",
        alt=("Register map: the four standard register pairs at 0x00 to 0x07 on both parts and the Agile I/O "
             "registers at 0x40 to 0x4F on the PCAL9555A only, each with its power-up value, its Config field "
             "and the calls that use it."),
        width=1200,
        kicker="Hardware",
        subtitle=("Registers come in pairs: port 0 at the even command byte, port 1 at the odd one. "
                  "EnsureInitialized() and ApplyConfig() write every Config field shown."),
        legend=[("blue", "PCA9555 and PCAL9555A"), ("violet", "PCAL9555A only")],
    )
    W = f.body_w
    gap = 16
    col = (W - 32 - gap) / 2
    y = 0.0
    for agile, tone, label, sub in (
        (False, "blue", "Standard bank", "0x00 to 0x07, both parts"),
        (True, "violet", "Agile I/O bank", "0x40 to 0x4F, PCAL9555A only; on Variant::Pca9555 these calls return Unsupported"),
    ):
        rows = [r for r in d["rows"] if r["agile"] == agile]
        top = f.region_inner_top(y)
        cy = top
        cards = []
        for i in range(0, len(rows), 2):
            pair = rows[i : i + 2]
            content = [_content(r) for r in pair]
            h = max(S.card_h(f, col, t, s, ln, True, r["access"]) for r, (t, s, ln) in zip(pair, content))
            for j, (r, (t, s, ln)) in enumerate(zip(pair, content)):
                cards.append((16 + j * (col + gap), cy, t, s, ln, r, h))
            cy += h + gap
        note_h = 0
        if agile and d["unused"]:
            note_h = 40
        f.region(0, y, W, cy - y + note_h + (2 if note_h else 0), label, tone=tone, sub=sub)
        for x, yy, t, s, ln, r, h in cards:
            f.card(x, yy, col, t, ln, tone=tone, h=h, sub=s, mono=True, tag=r["access"],
                   accent=False, dashed=r["access"] == "R")
        if note_h:
            f.text(16, cy + 16, f"{', '.join(d['unused'])} is not used.", size=f.k["small"], color=f"{tone}.text",
                   bg=f"{tone}.tint")
        y = cy + note_h + 24
    return f

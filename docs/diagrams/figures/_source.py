"""Facts the chip figures read from the driver source, so that a figure fails to build instead of drifting.

Not a figure (the leading underscore keeps it out of the build); register-map.py, pin-write.py and start-up.py import
it. Every helper raises when the source no longer has the shape it expects.
"""

import re
from pathlib import Path

from figkit.core import wrap

ROOT = Path(__file__).resolve().parents[3]
REGS = "inc/pcal9555_registers.hpp"
TYPES = "inc/pcal9555_types.hpp"
HPP = "inc/pcal9555.hpp"
IPP = "src/pcal9555.ipp"
IFACE = "inc/pcal9555_i2c_interface.hpp"


def read(rel: str) -> str:
    return (ROOT / rel).read_text()


def need(cond, what: str):
    if not cond:
        raise SystemExit(f"[figures] the source no longer matches the figure: {what}")
    return cond


def hexb(v: int) -> str:
    return f"0x{v:02X}"


def registers() -> dict[str, dict]:
    """Register command bytes: constant name -> {addr, access, label}, in source order."""
    text = read(REGS).split("kOutputConfigOden0", 1)[0]
    out = {}
    for name, addr, comment in re.findall(
        r"inline constexpr uint8_t (k\w+) = (0x[0-9A-Fa-f]+);\s*///<\s*(.*)", text
    ):
        access = need(re.search(r"\((R(?:/W)?)\)", comment), f"access of {name}").group(1)
        label = comment.split("(")[0].strip().rstrip(",")
        out[name] = {"addr": int(addr, 16), "access": access, "label": label}
    need(len(out) >= 12, "register constants in " + REGS)
    return out


def resets() -> dict[str, int]:
    """Power-up constants: name -> value."""
    found = re.findall(r"inline constexpr uint(?:8|16|32)_t (k\w+Reset) = (0x[0-9A-Fa-f']+)U?;", read(REGS))
    return {n: int(v.replace("'", ""), 16) for n, v in found}


def reset_for(reg_name: str, table: dict[str, int]) -> str | None:
    """The power-up constant of a register: kOutputPort0 -> kOutputReset, kPullEnable0 -> kPullEnableReset."""
    stem = re.sub(r"(Port)?[02]$", "", reg_name)
    return f"{stem}Reset" if f"{stem}Reset" in table else None


def detail_fn(name: str) -> str:
    """Body of a constexpr function in the detail namespace of the umbrella header."""
    m = need(re.search(rf"constexpr [^\n]*\b{name}\(const Config& \w+\) noexcept \{{(.*?)\n\}}", read(HPP), re.S),
             f"detail::{name}() in {HPP}")
    return m.group(1)


def pairs(name: str) -> list[tuple[str, str]]:
    """(register constant, Config field) in the order detail::StandardPairs() / AgilePairs() lists them."""
    out = re.findall(r"\.reg = reg::(k\w+),\s*\.value = [^}]*?\bc\.(\w+)", detail_fn(name))
    return need(out, f"pairs of detail::{name}()")


def functions() -> dict[str, str]:
    """Every PCAL9555<Port>:: member defined in the implementation: name -> body."""
    text = read(IPP)
    out = {}
    for m in re.finditer(r"PCAL9555<Port>::(\w+)\(", text):
        start = text.index("{", m.end())
        depth, i = 0, start
        while True:
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        out[m.group(1)] = text[start + 1 : i]
    return need(out, "member functions in " + IPP)


def public_calls() -> list[str]:
    public = read(HPP).split("public:", 1)[1].split("private:", 1)[0]
    return re.findall(r"^\s+(?:\[\[nodiscard\]\] )?Result<[^>]*> (\w+)\(", public, re.M)


def regs_used(body: str) -> list[str]:
    seen = []
    for r in re.findall(r"reg::(k\w+)", body):
        if r not in seen:
            seen.append(r)
    return seen


def call_registers() -> dict[str, list[str]]:
    """Public call -> the registers it reads or writes, following one level of private helpers.

    EnsureInitialized() and ApplyConfig() write every register and are left out.
    """
    fns = functions()
    private = {n for n in fns if n[0].islower()}
    out = {}
    for name in public_calls():
        if name in ("EnsureInitialized", "ApplyConfig") or name not in fns:
            continue
        body = fns[name]
        known = registers()
        regs = [r for r in regs_used(body) if r in known]
        for helper in private:
            if re.search(rf"\b{helper}\(", body) and helper not in ("initialize", "checkPresence", "writeAll",
                                                                     "verifyAll", "verifyPairs"):
                regs += [r for r in regs_used(fns[helper]) if r in known and r not in regs]
        out[name] = regs
    return out


def errors_in(body: str) -> list[str]:
    seen = []
    for e in re.findall(r"Error::(\w+)", body):
        if e != "Ok" and e not in seen:
            seen.append(e)
    return seen


def card_h(f, w, title, sub="", lines=(), mono=False, tag=""):
    """Height Fig.card() gives this content, to size a row of cards or centre a card on another."""
    k = f.k
    inner = w - 2 * k["pad"] - 6
    tag_w = f.pill_width(tag) + 8 if tag else 0
    t = wrap(title, inner - tag_w, k["card_title"], 700)
    s = wrap(sub, inner, k["small"]) if sub else []
    body = []
    for ln in lines:
        body += wrap(ln, inner, k["mono"] if mono else k["card_body"], 400, mono)
    th, sh, bh = k["card_title"] * 1.3, k["small"] * 1.35, (k["mono"] if mono else k["card_body"]) * 1.45
    return int(k["pad"] + len(t) * th + (len(s) * sh + 2 if s else 0) + (6 + len(body) * bh if body else 0)
               + k["pad"] - 4 + 1)

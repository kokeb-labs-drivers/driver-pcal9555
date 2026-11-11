---
name: datasheet-conformance
description: Check driver code against the chip's datasheet, citing sections, test first. Use when writing or reviewing register maps, timing, reset values or error handling.
---

# Datasheet conformance

The data sheets (links only; never commit the vendor PDF):
[PCAL9555A, NXP Rev. 2](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf) and
[PCA9555, NXP Rev. 10](https://www.nxp.com/docs/en/data-sheet/PCA9555.pdf).

Every number in the driver has a source. Work one subsystem at a time (presence and identity, the register pairs,
the interrupt and input latch, one Agile I/O block). Cite the PCAL9555A sections: 6.1 (address), 6.2 and table 4
(registers and power-up values), 6.2.6 to 6.2.12 (Agile I/O), 6.5 (interrupt), 7 (bus transactions).

1. **Collect the facts** from the datasheet into a table in the PR description: register, address, bits,
   reset value, access (R, W, R/W, self-clearing, clear-on-read), timing (minimum, typical, maximum), and the
   section, table or figure number. Use the maximum for waits and the minimum for pulses.
2. **Write the model first.** Make `tests/mock_<chip>.hpp` follow the table: reset values, read-only and
   self-clearing bits, clear-on-read flags, timing, invalid writes.
3. **Write the tests** that pin each fact: power-up values read back after `EnsureInitialized()`, the exact bytes
   of each transfer (at most one register pair), the write order, every documented error condition.
4. **Then change the driver** until the tests pass. Each register access and wait carries a comment with the
   section (`// section 6.2.12: 0x4F before 0x06/0x07`).
5. **Cross-check** `Config{}` against the reset values, `Validate()` against the documented ranges, and every
   `Error` against a datasheet condition or a port failure.
6. **Mark what you could not verify**: values the datasheet leaves open, errata, anything inferred. Say so in the
   code comment and the PR, and list it under "not verified on hardware" with a bench step.

Never invent a register, bit or timing. If the datasheet is silent, ask, or leave the feature out.

# Initial exception milestone — 2026-11-10

Historical snapshot before the complete ASCII font and source reorganization.
The current API, register semantics and emulator limits are described in the
[panic reference](../reference/panic.md). Resolve addresses from the current
ELF when reproducing a test.

On 2026-11-10, all ten separate diagnostic images passed the firmware-booted
record/native-pixel/scanout checks. All R0-R12 test patterns and the known
N/Z/C/V flags survived the ARM UDF entry. The final SDK checkout was rebuilt,
its ten BOOT payloads compared byte-for-byte with those tested images, and its
UDF case rerun successfully. The four CTest groups passed, including 38 host
test methods. Hello World and double-buffered animation also retained their
full RAM/scanout verification. A further animation run completed 794 frames
and 796 buffer flips with the write watch enabled: only the expected initial
UND patch appeared after takeover, with no SWI or DMA write to either slot.

For this UDF build (addresses are ELF-specific):

| Captured field | Value |
| --- | --- |
| Reason | `3` — undefined instruction |
| Fault PC | `0x10C01060`, exactly `panic_fault_instruction` |
| Exception LR | `0x10C01064` |
| Entry CPSR | `0x600000DB` — ARM UND |
| Saved SPSR | `0x600000D3` — interrupted ARM SVC |
| Interrupted SP | `0x10FFFFE0` |
| Reserved panic region | `[0x10C26D40, 0x10C4D680)` — 158016 bytes |
| Vector mask | `1` — UND only; firmware SWI retained |

The SDK checkout's UDF evidence is in `build/panic-acceptance/undefined/`.
These are emulator observations, not measurements from physical hardware.

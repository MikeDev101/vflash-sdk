# BOOT.BIN format for the inspected firmware

The local ROM identified in [firmware.md](../research/firmware.md) and the successful
homebrew disc test establish a simple fixed-address loader contract. This is
specific to the tested ROM/emulator combination; it is not a complete
specification of commercial executables or every firmware revision.

| Offset | Meaning in the inspected loader | Generated value |
| --- | --- | --- |
| `0x00` | `BOOT` signature by convention; no signature comparison was found in the inspected loading routine | `BOOT` |
| `0x04` | Number of bytes requested for the full file load | Sector-rounded total length, minimum 512 bytes |
| `0x08` | Destination address for the whole file | `0x10C00000` |
| `0x0C` | Entry offset added to the destination address | `0x10` |
| `0x10` | First executed instruction, selected by the offset above | `LDR pc,[pc,#-4]` |
| `0x14` | Entry literal used by that instruction | ELF `e_entry = 0x10C00018` |
| `0x18` | Start of our linked payload | ARM startup, C code, initialized data |

The routine first requests 512 bytes, extracts words at `+4`, `+8`, and `+12`,
rewinds by 512, and requests `size` bytes into `load`. Immediately before the
jump, it writes zero to CP15 control and selects ARM SVC with IRQ/FIQ masked.
The final branch is to `load + entry_offset`. Static addresses and runtime
trace evidence are recorded in [firmware.md](../research/firmware.md).

The packer uses a 512-byte minimum/multiple so the initial read is fully backed.
This is a conservative tested choice, not proof that every size must be a
multiple of 512. It zero-fills gaps/padding and builds the header from scratch.
The ELF must be fixed-address ARM EABI5 with matching physical/virtual load
addresses, executable entry, nonoverlapping segments, and the first file-backed
byte at `0x10C00018`. It rejects the direct-loader ELF layout and payloads that
would overlap the reserved stack. The generated program is self-contained and
never calls into the old firmware or returns to its scheduler.

## Entry decoding and safe inspection

`inspect_boot.py` uses the header's actual entry offset, checks its alignment
and bounds, and reports the selected instruction. For a supported AL,
pre-indexed, non-writeback `LDR pc,[pc,+/-imm12]`, the literal file offset is
`entry_offset + 8 +/- imm12`. That read is checked before decoding. Other
instructions are reported without inventing their target. The inspector warns
about size/file-length differences and whole-file loads outside modeled SDRAM.
It does not execute payloads or modify inputs.

## Local commercial sample

The local sample SHA-256 is
`c3491e435d60e995deb2da866b3fc2bc3635cd3f276e75e4db7c54ff52b43dc6`.
Its size word equals its 1,724,076-byte length, load base is `0x10C00000`, entry
offset is `0x10`, and its entry literal points to `0x10C0011C`.
`REL\0` occurs at file offsets `0x18` and `0x10C`, and `DBG\0` at `0x110`.
The sample remains private and Git-ignored; it is not a packer template.

[Multimedia Mike's original disc inspection](https://multimedia.cx/eggs/vdisc-analysis/)
reported the path and these identifiers. The inspector caps signature hits at
32 per identifier. A match can be data or code, not necessarily a chunk header.
Their complete structure is still unknown. The minimal generated executable
contains no REL/DBG structures, and the tested firmware accepts it.

## Limits

Commercial relocation/debug records, compressed applications, firmware
variants, callback conventions, and warm µMORE application loading remain
unimplemented. No universal claims about signatures, checksums, or security
checks follow from one loading routine and one successful test. The packer
supports only this minimal takeover experiment. A standalone ISO filesystem is
insufficient for the tested emulator CD path; `raw_cd.py` adds Mode 1 sector
headers, EDC, ECC P/Q, and the CUE track description.

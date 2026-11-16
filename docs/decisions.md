# Decision log

Behaviour the datasheets leave open. Each entry is keyed by id and referenced at the
point of use with `// DECISION <id>: <one line>`.

Entry template:

```
## <Id>: <title>
- Options:
- Choice:
- Evidence:
- Confidence:
- Status:
```

> Entries E*, M*, D*, P1, A1 and A2 below are copied verbatim from KS-32 Reborn
> (`docs/decisions.md` at commit c17c35c) because the copied cores cite them. New entries for this
> project use the areas OT (OTTO) and T (TS machine), and A for the app from Plan 4 on, numbered
> from A100 so they cannot collide with the copied A1/A2.

## ESP

## E1: Carry polarity
- Options: (a) C = unsigned carry out of bit 23 for ADD/ADDU, unsigned borrow (1 when A < B) for SUB/SUBU/CMP/DIFF; (b) C = carry for every op, i.e. "no borrow" after a subtract (the literal §3.3.2 definition "carry (or no-borrow)").
- Choice: (a).
- Evidence: ESP notes §3.3.2 defines C as "carry (or no-borrow)", but the §4.2.2 tables use C alone for "Lower than (unsigned)" and NOT(C) for "Higher or same", which requires C = borrow after A-B. (a) makes the LO/HS masks mean what their names say.
- Confidence: Medium.
- Status: Implemented in ens::esp::alu (Task 5). Tests: alu_sub_borrow, alu_addu_wraps.

## E2: DIFF
- Options: (a) unsaturated, `A = (0x7FFFFF - B) & 0xFFFFFF`, V = signed overflow; (b) saturated, as the function table says "with Overflow Protection".
- Choice: (a). Flags are those of SUBU(0x7FFFFF, B).
- Evidence: §3.3.1 DIFF paragraph says "The subtraction is unsaturated"; the §3.3.3 flag table groups DIFF with CMP (plain V).
- Confidence: Medium.
- Status: Implemented in ens::esp::alu (Task 5). Test: alu_diff_unsaturated.

## E3: ABS
- Options: (a) negative B gives `~B` (one's complement), N forced 0, C = sign of B, V/LT/Z unchanged; (b) true two's-complement negation.
- Choice: (a).
- Evidence: §3.3.1 "Negative arguments are negated with One's complement results"; §3.3.3 flag table row ABS (0, (2), -, -, -).
- Confidence: Medium-high.
- Status: Implemented in ens::esp::alu (Task 5). Test: alu_abs_ones_complement.

## E13: ASL2/ASL8
- Options: (a) saturate when the sign changes (result out of 24-bit range) to 0x7FFFFF/0x800000 by the old sign, V = saturation occurred, N/Z from the result, C and LT unchanged; (b) V as a plain overflow flag with wrapping.
- Choice: (a).
- Evidence: §3.3.1 ("Saturation will occur if the sign bit changes"), flag table row ASL2,ASL8 (X, -, (1), -, X) where (1) means V indicates saturation.
- Confidence: Medium-high.
- Status: Implemented in ens::esp::alu (Task 5). Tests: alu_asl2, alu_asl8.

## E14: LS15
- Options: (a) `A = (B << 15) & 0x7FFFFF`, N = 0, C = bit 8 of B (the bit that would land in the sign position), Z/V/LT unchanged; (b) recompute Z.
- Choice: (a).
- Evidence: §3.3.1 LS15 paragraph; flag table row LS15 (0, (3), -, -, -) with note (3).
- Confidence: Medium.
- Status: Implemented in ens::esp::alu (Task 5). Test: alu_ls15.

## E15: ASR and logic-op flags
- Options: (a) ASR: `A = B >> 1` arithmetic, N from result, C = bit 0 of B, Z/V/LT unchanged; AND/OR/XOR: N and Z from result, C/V/LT unchanged; (b) also update Z for ASR.
- Choice: (a).
- Evidence: §3.3.3 flag table rows AND,OR,XOR (X,-,-,-,X) and ASR (X,(4),-,-,-).
- Confidence: Medium (the Z column cells of ABS/LS15/ASR are the least legible in the scan).
- Status: Implemented in ens::esp::alu (Task 5). Tests: alu_asr, alu_logic_keeps_cvlt.

## E16: ADD/SUB and unsaturated flags
- Options: (a) ADD/SUB: N and Z from the stored (saturated) result, V = saturation occurred, LT = sign of the infinite-precision result (= N_wrapped xor V_overflow); ADDU/SUBU/CMP/DIFF: N and Z from the wrapped result, V = signed overflow, LT = N xor V; (b) LT = N xor V with N from the saturated result.
- Choice: (a).
- Evidence: §3.3.2 (LT = N xor V), §3.3.3 flag table footnote (1) "V indicates whether saturation as opposed to overflow occurred".
- Confidence: Medium.
- Status: Implemented in ens::esp::alu (Task 5). Tests: alu_add_saturates, alu_addu_wraps, alu_sub_borrow.

## E21: Skip formula vs. TRUE/FALSE labels
- Options: (a) the literal equation `SKIP = NOTm xor ((N&Nm)|(Z&Zm)|(V&Vm)|(C&Cm)|(LT&LTm))`; (b) reinterpret the masks so that "TRUE" (0x04) executes always and "FALSE" (0x00) never.
- Choice: (a). Under it 0x04 always skips and 0x00 never skips; the other 14 masks behave as "execute when the condition holds" (e.g. EQ 0x0C skips when Z=0).
- Evidence: ESP notes §4.2.1 equation vs the §4.2.2 mask table labels (part 2, esp_29). The equation is explicit; the labels conflict with it.
- Confidence: Medium.
- Status: Implemented in ens::esp::skipCondition (Task 5). Test: alu_skip_masks. CCR/CMR layout N 23, C 22, V 21, LT 20, Z 19, NOT 18 (CMR); the datasheet's 8-bit masks are `cmr >> 16`.

## E6: MAC model
- Options: (a) `acc` is a 50-bit signed value (48 + 2 guard) that wraps at 50 bits; `product = sext24(X) * sext24(Y) << (SIGREG bit 22 ? 1 : 2)`; readout `sat48(acc)` clamps to [-2^47, 2^47-1], MACH = sat48 >> 24, MACL = sat48 & 0xFFFFFF; ALU write MACH sets `acc = (sext24(v) << 24) | (acc & 0xFFFFFF)`, write MACL sets `acc = (acc & ~0xFFFFFF) | v`; a plain multiply discards the preload; (b) saturate the accumulator on every accumulate (MAME-style, loses the guard bits).
- Choice: (a). The wrap is `((v << 14) >> 14)`, so an accumulator pushed past 2^49 wraps negative (MAME bug #2 not reproduced). The "MACL as destination duplicates the upper half" sentence is read as concerning the multiplier C-destination and is not implemented.
- Evidence: ESP notes part 1 §3.4 (2 guard bits, saturate on Z-bus/SPR readout, SIGREG shift, skipped step does not accumulate); part 2 §4.3.3 (preload at step N-1, plain multiply discards the preload, MACL is the only path to the low 24 bits).
- Confidence: Low-medium.
- Status: Implemented in ens::esp::Mac (Task 6). Tests: mac_fractional_half_times_half, mac_shift_two, mac_neg_full_scale_squared_saturates, mac_guard_bits_recover, mac_wraps_at_50_bits, mac_preload, mac_plain_multiply_discards_preload.

## E7: AGEN units
- Options: (a) the 20-bit AGEN registers (DLENGTH, ABASE, BBASE, DBASE) hold 24-bit-aligned values, writes keep bits 23..4 and reads return the low 4 bits as 0, MEMSIZ keeps its written 24-bit value; `unit = MEMSIZ + 1`, `shift = trailing 1 bits of MEMSIZ`; delay `s = (DBASE + off) & 0xFFFFFF`, if `s > DLENGTH` then `s -= DLENGTH + unit`, `phys = (s & ~MEMSIZ & 0xFFFFFF) >> shift`; table `phys = ((BASE + off) & 0xFFFFFF & ~MEMSIZ) >> shift`; I/O `phys = (off & 0xFFFFFF) >> 4`; (b) subtract DLENGTH only (the text), which breaks the circular period.
- Choice: (a), with two refinements to the literal formula. (1) The delay comparison and wrap are done on address bits only: `s = s & ~MEMSIZ`, `len = DLENGTH & ~MEMSIZ`, wrap when `s > len` by `len + unit`. Reason: the datasheet says MEMSIZ is "a mask for delay address range comparisons", and the literal compare wraps a read at word `len` plus a fraction (e.g. DLENGTH = 0xFFFFF0 with a read at 0xFFFFF8 would wrap to word 0xEFFF). With the masked compare, DLENGTH = 0xFFFFFF (the common firmware value, used as 0xFFFFF0) gives the full 64K ring and never leaves the 16-bit word range, and DLENGTH = 0x1F7DFF (used as 0x1F7DF0) keeps word 0x1F7D valid. The two forms agree whenever DLENGTH and the sum have the same fraction bits (all the datasheet-style examples). (2) The DBASE reload takes `DLENGTH & ~MEMSIZ` (not raw DLENGTH), keeping DBASE's fraction bits 0 as E20 implies; identical for aligned DLENGTH. DBASE decrements by `unit` and reloads when `DBASE < unit` (i.e. it would go below 0); the period is `DLENGTH/unit + 1` samples.
- Evidence: ESP notes part 1 §3.5.2-3.5.4 (MEMSIZ examples $0000FF for 64K, "mask for delay address range comparisons", DBASE "decrements from DLENGTH to 0", table base values not restricted by DLENGTH, I/O "always addressed absolutely"); inferred alignment note under §3.5.2. The firmware writes MEMSIZ = 0x0000FF and DLENGTH = 0xFFFFFF (one program 0x1F7DFF).
- Ruling R13 (Task 9 fix round): read-back returns the written 24-bit value; arithmetic ignores bits 3..0. DLENGTH, ABASE, BBASE and DBASE therefore store the full 24-bit value as written (the option (a) text above "writes keep bits 23..4 and reads return the low 4 bits as 0" is superseded), and every use in address arithmetic (Agen::physical, endOfSample, DBASE reload, RAM clear, host RAM access) goes through the bits-23..4 view (`dlength20()` etc.), so address behaviour is unchanged (DLENGTH = 0xFFFFFF still behaves as 0xFFFFF0 and gives the 64K ring). The decrement result and a DBASE reload have bits 3..0 clear. Reason: the KS-32 boot writes DLENGTH = 0xFFFFFF and verifies it by read-back with retries (esp_init_sequence.json); a read-back of 0xFFFFF0 would fail that compare. Read-back of bits 3..0 of DLENGTH, ABASE and BBASE was refined by E32 (they read as 1).
- Confidence: Medium (refinement (1) and (2): Low-medium, pure interpretation of an unstated edge).
- Status: Implemented in ens::esp::Agen (Task 7; R13 in Task 9 fix round). The wrap became a full modulo by `len + unit` in E31 (2026-10-21); it gives the single-subtraction result above whenever the sum is below twice the ring length. Tests: agen_memsiz_64k, agen_delay_no_wrap, agen_delay_wraps_circular, agen_delay_top_of_buffer_with_fraction_does_not_wrap, agen_delay_dlength_all_ones_stays_in_range, agen_dbase_decrements_and_reloads, agen_period_matches_wrap, agen_period_odd_dlength, agen_table_not_masked_by_dlength, agen_io_absolute, agen_reg_alignment.

## E20: MEMSIZ write side effect
- Options: (a) writing MEMSIZ also sets `DBASE &= ~MEMSIZ`; (b) no side effect.
- Choice: (a).
- Evidence: ESP notes part 1 §3.2 register-map text "Writing the MEMSIZ register affects the DBASE register directly"; the datasheet gives no detail of the effect, masking is the natural reading since DBASE's bits below the address LSB are unused.
- Confidence: Low.
- Status: Implemented in Agen::writeReg(244) (Task 7). Test: agen_reg_alignment.

## E18 (FIFO part): DOL FIFO behaviour
- Options: (a) 2-entry queue; push when full drops the oldest; RAM write pops the oldest and on an empty FIFO re-outputs the last pushed word; dump pops and discards (nothing when empty); reset on halt-at-END empties the FIFO but keeps the last-pushed register; (b) reset also clears the last-pushed word.
- Choice: (a). The FIFO stores full 24-bit words; the 16-bit left-justified truncation belongs to the RAM write (Task 8). The RAM pipeline timing half of E18 is completed in Task 8.
- Evidence: ESP notes part 1 §3.6.3 ("Two DOL writes will fill both words. Two RAM writes will empty both words. Writing an empty FIFO to RAM will cause the last word written to the DOL to be written to RAM. Overstuffing the FIFO will cause the oldest data to be bumped out"; halt at END "will reset the FIFO pointers ... a subsequent RAM write may not output the last word written"); dump on empty does nothing (p27).
- Confidence: Medium.
- Status: DolFifo implemented (Task 7). Test: fifo_semantics, fifo_two_writes_two_reads_in_order. Pipeline timing: see "E18 (pipeline part)" (Task 8).

## E18 (pipeline part): RAM pipeline timing
- Options: (a) at step k the RAM cycle of word k-1 runs (its address was computed at step k-1 from `GPR[k-1]`); a read latches `DIL = data << 8` after step k's operand fetches, so it is visible from step k+1, two words after the issuing word; (b) a one-step pipeline (read visible to the next word); (c) the RAM cycle in the same step as the address computation.
- Choice: (a). A write pops the DOL FIFO (empty: re-uses the last pushed word) and drives bits 23..8 of the 24-bit FIFO word onto the bus. Read-delay/dump (RAM op 5) performs a delay read into DIL and dumps one FIFO word. An ALU or MUL FIFO destination in word k is pushed in step k+1 (phase 3 or 1) before word k's own write cycle (step k+1, phase 7), so a write assembled in the same word as its data stores that data ("write addresses are pipelined back zero cycles"). The pending address and RAM op carry across END into the next sample; the END word's RAM address uses DBASE from before the END decrement.
- Evidence: ESP notes part 2 §4.3.4 (pipeline length 2; read addresses pipelined back 2 words, writes 0; control bits pass a 1-cycle delay register), §4.3.1 timing figure ("RAM R/W Cycle Offset @ N-1"); part 1 §3.6.3 (DIL left-justified, persists until the next read). With DLENGTH = 0x00FF00 and MEMSIZ = 0xFF, a write at offset 0 and a read at offset 0x000400 give exactly 4 samples of lag.
- Confidence: Medium.
- Status: Implemented in Esp::step and Esp::ramCycle (Task 8). Tests: esp_delay_line_roundtrip (lag pinned at 4), esp_ram_read_visible_two_words_later, esp_mul_to_fifo_write, esp_end_step_executes_mul_and_ram_fields.

## E17: Execution order within a step
- Options: (a) nine phases: (1) commit MUL(k-1) (C register and/or DOL push), (2) MUL(k) operand fetch and product, (3) commit ALU(k-1) (register, DOL push, CCR, MACH/MACL preload), (4) skip evaluation for word k, (5) ALU(k) operand fetch and compute (pending), (6) MUL(k) accumulate, (7) RAM cycle of word k-1, (8) AGEN address for word k from `GPR[pc]`, (9) END handling; (b) execute each word atomically (MAME-like).
- Choice: (a). Consequences: an ALU result reaches the ALU on the next step but the multiplier only two steps later; a multiplier result reaches the ALU on the next step; an ALU write to MACH/MACL at step k-1 is the preload for the accumulate at step k (a preload in the same word as the accumulate is too late); because the preload lands in phase 3 and ALU(k) fetches in phase 5, before MUL(k) accumulates in phase 6, an ALU read of MACH/MACL in word k returns the preloaded value (word k's own multiply is seen only from word k+1); the previous multiply's result is a valid multiplier source on the next step (phase 1 before phase 2); the CCR written by ALU(k-1) is what word k's skip test sees (phase 3 before phase 4). The pipeline registers persist across `runSample()` calls. Within a commit the CCR is updated before the destination register is written, so an ALU instruction whose destination is CCR (250) leaves the written value, not its own flags. The MUL Z-bus result is MACH after the accumulate; MACH/MACL reads are saturated (E6).
- Evidence: ESP notes part 2 §4.3.1 timing figure (Write Mul N-2, Read Mul Op N-1, Write ALU N-2, Read ALU Op N-1), §4.3.2 ("ALU operations lag Multiplier operations by one T cycle"; leave one step between an ALU result and its use in the MUL; MUL results usable by the ALU on the very next step), §4.3.3 (preload on the step prior to the accumulation). The CCR-destination ordering is not documented.
- Confidence: High for the ordering; medium for the MAC preload slot; low for the CCR-destination detail.
- Status: Implemented in Esp::step, commitMul and commitAlu (Task 8). Tests: esp_mov_and_end, esp_alu_result_not_visible_to_next_mul, esp_alu_result_visible_two_steps_later_to_mul, esp_mul_result_visible_to_next_alu, esp_mul_result_visible_to_next_mul, esp_ccr_from_previous_alu_drives_skip, esp_alu_reads_mac_before_same_word_accumulates, esp_alu_reads_preloaded_mach, esp_mac_preload_slot, esp_mac_saturates_in_core.

## E4: CMP and skip
- Options: (a) CMP ignores the Skip bit (always evaluates and sets the CCR); every other instruction with Skip=1 never sets the CCR, whether or not it is skipped; (b) skippable instructions set the CCR when they execute; (c) a skippable CMP is skipped like any other instruction.
- Choice: (a). The skip condition is not evaluated for CMP. For other opcodes the CCR update depends on the word's Skip bit, not on the skip outcome. The Task 8 brief's "CCR is only written ... if `!skipped || op==CMP`" is read with "skipped" meaning "skippable", as this entry and the datasheet require.
- Evidence: ESP notes part 1 §3.3.1 (CMP is not affected by the skip bit), §4.2.1 ("Instructions which are skippable cannot set the condition codes, regardless of whether they were executed or not"). Agrees with MAME after PR #14979.
- Confidence: Medium.
- Status: Implemented in Esp::step (phase 4) and Esp::commitAlu (Task 8). Tests: esp_cmp_ignores_skip, esp_skip_suppresses_writes_not_ram (control half).

## E5: What a skip suppresses
- Options: (a) suppress the ALU and MUL register writes, DOL pushes and the accumulator update, but still perform the word's RAM-control action; (b) also suppress the RAM cycle.
- Choice: (a). A skipped word's operand fetches and its RAM cycle (read into DIL, FIFO pop and write, dump) still happen; the MAC keeps its previous contents.
- Evidence: ESP notes part 1 §4.2.1 ("executing each phase of the instruction except writing of its final results to the specified target register, including the Multiplier's accumulator. This method ... maintains consistency both in the ESP's internal pipelines and in the overall timing").
- Confidence: Medium.
- Status: Implemented in Esp::step, commitMul and commitAlu (Task 8). Test: esp_skip_suppresses_writes_not_ram.

## E8: SER registers
- Options: (a) 16-bit registers left-justified at bits 23..8 (read `v << 8`, write keeps bits 23..8); (b) right-justified; (c) 24-bit storage.
- Choice: (a). `setSerialIn(port, l, r)` latches SERnL (235 + 2n) and SERnR (234 + 2n); `serialOut` reads the same latches. Until Task 9's serial control the ports are plain latches shared by both directions.
- Evidence: ESP notes part 1 §3.2 (SER registers are 16 bits wide), §3.6.2 (16-bit data maps to GPR bits 23..8); same convention as DIL.
- Confidence: Medium.
- Status: Implemented in Esp::readReg, writeReg, setSerialIn and serialOut (Task 8). Test: esp_ser_regs_left_justified.

## E9: Addresses 192..233
- Options: (a) read 0, writes ignored; (b) extra GPRs; (c) aliases of other registers.
- Choice: (a), for program, multiplier and direct (host) access alike.
- Evidence: ESP notes part 1 §3.2 and §4.1.1 (0..191 are GPRs, 234..255 are listed; 192..233 are not described).
- Confidence: Low.
- Status: Implemented in Esp::readReg and writeReg (Task 8). Test: esp_constants_and_holes.

## E11: Program without END
- Options: (a) after executing word 159, behave as if it were END; (b) wrap the PC to 0 without the END actions; (c) run until an END is found (can hang).
- Choice: (a). `runSample()` therefore runs at most 160 steps and always returns with PC = 0.
- Evidence: Undocumented (the instruction memory has 160 words).
- Confidence: Low.
- Status: Implemented in Esp::step and runSample (Task 8). Test: esp_runsample_terminates_without_end (also runs 160 pseudo-random non-END words).

## E12: Constants 252..255
- Options: (a) read -1, 0x800000, 0x7FFFFF, 0, with writes discarded; (b) writable.
- Choice: (a).
- Evidence: ESP notes part 1 §3.2 (constants in ROM, "can be written to when dummy destinations are needed").
- Confidence: High.
- Status: Implemented in Esp::readReg and writeReg (Task 8). Test: esp_constants_and_holes.

## E19: END step
- Options: (a) END executes as an ALU NOP (no register write, no DOL push, no CCR change) while its MUL and RAM fields execute normally; after phase 8 of the END step DBASE -= unit (reloading DLENGTH below 0), then at most one pending host transfer, then HALT is sampled; the next step is PC 0; (b) END also suppresses the MUL and RAM fields.
- Choice: (a). The END word's MUL result and RAM cycle complete in the first step of the next sample (the pipeline carries across END). The host-transfer and HALT hook was filled in by Task 9 (see "E19 (host part)").
- Evidence: ESP notes part 2 §5.2.1 (END actions: PC reset, HALT sampling, serial clocks, DBASE decrement, host access). KS-32 firmware END words carry live MAC and RAM fields, e.g. a MAC with a delay write (docs/rom-investigation/findings.md).
- Confidence: Medium.
- Status: Implemented in Esp::step and commitAlu (Task 8). Tests: esp_mov_and_end, esp_end_step_executes_mul_and_ram_fields.

## E22: Narrow SPR read-back
- Options: (a) keep only the documented bits (SIGREG bit 22, CCR bits 23..19, CMR bits 23..18), so other bits read 0; (b) store all 24 bits.
- Choice: (a). The firmware writes SIGREG with all bits set; only bit 22 has an effect either way.
- Evidence: ESP notes part 1 §3.2 widths (SIGREG 1 bit = bit 22, CCR 5, CMR 6) and the DIL text ("unused 8 bits are read back as zeros"). Read-back of unused bits is not documented.
- Confidence: Low.
- Status: Implemented in Esp::writeReg (Task 8). Test: esp_narrow_spr_widths. Read-back of the spare bits was changed by E33.

## E23: ESP reset state
- Options: (a) reset clears GPRs, instruction memory, SPRs, the MAC, the AGEN registers, DIL, the DOL FIFO (including its last-pushed word) and the pipeline registers, with PC = 0; (b) leave GPR and instruction memory as they were (power-up contents are undefined in hardware).
- Choice: (a), for deterministic emulation. The host loads the program and GPRs after reset anyway.
- Evidence: Undocumented.
- Confidence: Low.
- Status: Implemented in Esp::reset (Task 8); Task 9 extends it to the host-interface state (host data/address/control/select registers return to 0, the ESP is running, the external HALT pin level is kept). Test: esp_reset_clears_pipeline.

## E10: Host DOL low byte
- Options: (a) host register $0E reads 0x00 (the text: "the low bytes will read back as zero"); (b) reads 0xFF (the memory-map table).
- Choice: (a). Writes to $0E are ignored. DIL low byte $0B also reads 0.
- Evidence: ESP notes part 2 §5.1.1 table versus §5.1.3 text (DIL and DOL are 16 bits wide, left-justified).
- Confidence: Low.
- Status: Implemented in Esp::hostRead (Task 9). Test: host_register_map_details.

## E18 (halt part): END with HALT high
- Options: (a) an END taken while HALT is high resets the DOL FIFO pointers (empties it; the last-pushed word is kept, see the FIFO part); (b) leave the FIFO alone.
- Choice: (a). The reset happens after the END-step DBASE decrement and the host transfer. The remaining in-flight pipeline (the END word's MUL result and RAM cycle) is kept and completes in the first step after the ESP resumes. The halt-enable write (E24) also resets the FIFO.
- Evidence: ESP notes part 2 §5.1.3 ("During HALT, the FIFO pointers are reset"); part 1 §3.6.3.
- Confidence: Medium.
- Status: Implemented in Esp::step (Task 9). Test: halt_at_end.

## E19 (host part): host transfer and HALT sampling at END
- Options: see E19. The hook is now filled: after the END-step DBASE decrement, at most one pending host GPR/INSTR request is performed, then the HALT level is sampled; if high, the ESP halts with pc = 0 (so the next resume starts at word 0) and `runSample()` returns 0 until HALT goes low. A request issued by the host between samples therefore lands at the next END.
- Choice: as stated. DBASE still decrements on an END that halts (§5.4.1).
- Evidence: ESP notes part 2 §5.1.2, §5.2.1, §5.4.1.
- Confidence: Medium.
- Status: Implemented in Esp::step and Esp::hostTransfer (Task 9). Tests: host_write_gpr_running, host_read_instr, host_write_instr_and_gpr, host_one_transfer_per_sample, halt_at_end.

## E24: Halt-enable write mid-program
- Options: (a) with HALT high, a write to $1F stops the ESP at once, wherever it is (also inside `runSample()`, e.g. from a bus callback); the in-flight pipeline (pending ALU/MUL results and RAM cycle) is discarded, the DOL FIFO is reset and pc = 0, so the next resume starts at word 0; (b) keep the pipeline; (c) the write only arms a halt at the next END.
- Choice: (a). With HALT low the write does nothing. The datasheet calls the machine state unknown after this; discarding stale results avoids committing a half-finished word's results into the next program.
- Evidence: ESP notes part 2 §5.1.6 ("forces the ESP to halt immediately, without waiting for an END statement"; "the state of the machine could be unknown"); the firmware writes $1F = 0x02 then 0xFF with ESPHALT asserted (`docs/rom-investigation/data/esp_init_sequence.json`).
- Confidence: Medium for halting at once; low for discarding the pipeline.
- Status: Implemented in Esp::haltNow and the step() early return (Task 9). Test: halt_enable_immediate.

## E25: Host control register, RAM clear and refresh
- Options: (a) bit 0 Refresh Disable is stored; bit 1 RAM Clear takes effect only when written 1 while halted and with Refresh Disable = 0 (DBASE is loaded with DLENGTH, masked like DBASE reload, at that moment), advances one write per idle step and clears itself once address 0 (DBASE = 0) has been written; writing bit 1 = 0 aborts, writing it 1 with Refresh Disable = 1 or while running is ignored; releasing HALT abandons a clear in progress; bit 2 is read only; (b) a clear started with Refresh Disable = 1 runs anyway; (c) a clear continues across a resume.
- Choice: (a). The firmware writes 0x03 and then 0x02 before every clear and waits; under (a) the 0x03 is the "reset the RAM Clear bit" step and 0x02 starts the clear, so the behaviour matches the power-up sequence. Writes: zero to `(DBASE & ~MEMSIZ) >> shift`, then DBASE -= MEMSIZ + 1, so DLENGTH = 0x00FF00 with MEMSIZ = 0xFF clears addresses 0xFF..0 (256 writes, DBASE = 0 at the end). Refresh (Refresh Disable = 0, no clear) decrements DBASE once per idle step with the END-step rule (decrement, reload DLENGTH below 0) and performs no bus access; the external DRAM refresh itself is not modelled. Refresh Disable = 1 freezes DBASE while halted.
- Evidence: ESP notes part 2 §5.1.4, §5.4.1 (RAM Clearing, Enabling RAM Refresh), §5.4.2 steps 4-6; `docs/rom-investigation/data/esp_init_sequence.json` (host control 0x03 then 0x02).
- Confidence: Medium.
- Status: Implemented in Esp::hostWrite, setHaltPin and idleStep (Task 9). Tests: ram_clear, ram_clear_needs_halt_and_refresh_enabled, refresh_disable_freezes_dbase, host_firmware_boot_sequence.

## E26: Host GPR/INSTR registers and select protocol
- Options: (a) one request latch: a write to $80/$A0/$C0/$E0 replaces any pending request (data registers keep their contents), sets Host Access OK/ busy ($12 bit 2 = 1) and is performed at the next END (running) or the next idle step (halted); read requests capture GPR/SPR via the program-side register map (SPR semantics, holes read 0) and INSTR (0 above word 159) into $00-$08; each select register reads back its last value; INSTR byte $08 reads `& 0xF8` while writes store the byte as given; writes beyond word 159 or into holes are ignored; select addresses decode exactly ($80, $A0, $C0, $E0), other unlisted host addresses read 0 and ignore writes; (b) a queue of requests; (c) select addresses decode only bits 7:5.
- Choice: (a). Matches the single address latch in the hardware description and the "one transfer per sample" rule. The two-request behaviour is documented by host_one_transfer_per_sample (the second request wins, the first address is never written).
- Evidence: ESP notes part 2 §5.1.1, §5.1.2 ("only one transfer is allowed per sample period"), §5.4.1.
- Confidence: Medium.
- Status: Implemented in Esp::hostRead, hostWrite and hostTransfer (Task 9). Tests: host_write_gpr_running, host_read_instr, host_write_instr_and_gpr, host_one_transfer_per_sample, host_transfer_reaches_spr_and_ignores_holes, host_register_map_details.

## E27: Host RAM access
- Options: (a) a write to DADR HI ($0F) while halted performs one RAM or I/O cycle immediately; RAM address = `(DADR & ~MEMSIZ) >> shift` (the table rule), I/O address = DADR >> 4 (20-bit); the access mode comes from $14 (bit 7 read, bit 6 RAM, other bits stored); a write sends the host DOL word, a read latches DIL = data << 8; DADR LO keeps bits 7:4; the same write while running is ignored; (b) model the 6-clock latency; (c) allow the access while running.
- Choice: (a). The latency is not observable in an instruction-period model. DADR is left-justified: with MEMSIZ = 0xFF, DADR 0x001230 addresses word 0x12; the firmware's table writes (DADR $0F/$10 = word address) then land on that word.
- Evidence: ESP notes part 2 §5.1.3 and §5.4.1 ("Access to RAM is available ... only when the ESP is Halted"), the firmware table-load sequence (esp_init_sequence.json, "dram_tables").
- Confidence: Medium.
- Status: Implemented in Esp::hostRamAccess (Task 9). Tests: host_ram_write_read_halted, host_ram_access_io_space_and_running, host_firmware_program_load_with_dram_table.

## E28: What one idle step does
- Options: (a) a halted instruction period services at most one pending GPR/INSTR request and, independently, performs either one RAM-clear write or one DBASE refresh decrement; (b) exactly one of the three per step (a pending request would stall the clear/refresh for a period); (c) transfers are immediate.
- Choice: (a). The GPR/INSTR memories are internal DRAM refreshed by the internal counter; the clear and refresh use the external-RAM address generator. They are separate resources, so a host request does not delay them. `idleStep()` does nothing while running, and a request made while halted waits for the next idle step (the caller supplies one per ~300 ns of emulated time).
- Evidence: ESP notes part 2 §5.4.1 ("approximately one access per microinstruction period, or 300ns"; "host GPR/INSTR accesses are substituted for refresh cycles"; "A new refresh address will be generated every microinstruction period").
- Confidence: Low-medium.
- Status: Implemented in Esp::idleStep (Task 9). Tests: host_transfers_while_halted_one_per_idle_step, ram_clear, refresh_disable_freezes_dbase.

## E29: Serial control register and test-only registers
- Options: (a) $18 stores the written byte and reads back `(v & 0xFC) | 3` (bits 1:0 are fixed 1); the master/slave, Sony/I2S and SERn direction bits do not change `setSerialIn`/`serialOut`, which stay plain latches (the trace runner decides routing); $16 reads the PC (always 0 at a sample boundary, so the firmware's "PC < 0x28" poll passes); $17 and the $1F read (refresh and frame counters, test only) read 0; (b) model the direction bits in the port latches; (c) a live refresh/frame counter.
- Choice: (a). Nothing in the emulation depends on the bits, and the firmware only writes 0x63.
- Evidence: ESP notes part 2 §5.1.1, §5.1.5, §5.1.7; findings.md (host serial control 0x63, PC poll).
- Confidence: Low.
- Status: Implemented in Esp::hostRead and hostWrite (Task 9). Tests: serial_control_register, host_register_map_details, host_firmware_boot_sequence.

## E30: Sample-rate model and halted time
- Options: (a) one `Esp::runSample()` per OTIS frame runs the program to END with no instruction budget; halted time advances only through explicit `idleStep()` calls; (b) a slots-per-sample budget (one instruction per 375 ns: 3 MCLK per instruction at 8 MHz) with an advance API that interleaves running and halted time.
- Choice: (b), implemented as `Esp::advance(int slots)` (ruling R17), with (a) kept as `runSample()`/`idleStep()`, whose behaviour is unchanged. `advance` spends one serial frame of `slots` instruction periods. Running: it calls `runSample()`; if that END halted the chip (HALT pin high) it calls `idleStep()` for the rest of the frame (`max(0, slots - steps)` times); otherwise the frame is spent. Halted: it calls `idleStep()` `slots` times. It returns the instructions executed (0 while halted). Overrun: a program longer than `slots` still runs to END (P1); the over-budget instructions take no extra time from a following frame, so such a program is not hardware-faithful, but the KS-32 programs are at most 89 words (`docs/rom-investigation/findings.md` section B). The board calls `advance(89)` once per frame, so a halted ESP progresses with emulated time (host requests, RAM clear, refresh).
- Evidence: findings.md section B (89-word program, 89 slots per frame at 8 MHz); decisions E11, E25, E28; ESP notes part 2 section 5.
- Confidence: Medium.
- Status: Implemented in Esp::advance, `// DECISION E30` markers in `src/esp/esp.h` and `src/esp/esp.cpp`. Tests: esp_advance_running_matches_runSample, esp_advance_halted_runs_idle_slots, esp_advance_halt_at_end_idles_rest_of_frame, esp_advance_overlong_program_runs_to_end; `runSample`/`idleStep` behaviour is covered by the tests named under E11 and E28.

## E31: Delay wrap when DBASE + offset is more than one ring length past DLENGTH
- Options: (a) full modulo: when the sum is above DLENGTH, the address is `s mod (len + unit)` (address bits only, E7 refinement (1)), so every delay access stays in words 0..DLENGTH for any DBASE and offset; (b) one subtraction of `len + unit` (E7 as first implemented), which leaves sums above `2 * len + unit` outside the ring; (c) keep (b) and have the loader put DBASE inside the ring before it releases HALT (write DBASE, or hold HALT for one refresh cycle, 65536 idle steps, about 737 frames).
- Choice: (a). (a) and (b) give the same address whenever the sum is below twice the ring length. That covers any DBASE inside the ring with an offset of at most one ring length: all the datasheet examples, the Plan 1 traces, and programs 0 to 9, whose ring is the whole 64K. They differ only when DBASE (or an offset) is far above DLENGTH, which is the state program 10 starts in after every load (P16).
- Evidence for (a): the datasheet's step list for delay addressing (ESP notes part 1, section 3.5.4) gives the out-of-range case as "(ASUM - DLENGTH) modulo (DLENGTH)". The RAM-control table (section 4.1.6) describes both delay modes as "DBASE + offset, modulo DLENGTH". MAME's es5510.cpp computes `(dbase + offset) % (dlength + memincrement)`. The firmware's program load (`esp_init_sequence.json` program_load) never writes DBASE. After its RAM clear and halted refresh, DBASE is wherever the whole-memory refresh count happens to be. With (b), program 10 would then overwrite its own gain tables after most loads on the real machine, which is unlikely in a shipped effect.
- Evidence for (b): the prose of section 3.5.4 says that DLENGTH "is subtracted from the sum". The datasheet asks that DBASE and DLENGTH be carefully initialised at program load so that the delay lines do not overwrite table data (section 3.5). That warning matters most if a stray DBASE can reach the tables, though a wrong DLENGTH does the same under (a). The ESP2 document (`docs/reference/esp2-full-text.txt` sections 7.1.3 and 7.1.5) describes a later Ensoniq chip. It specifies one subtraction, `address - (SIZEM1 + 1)`, and warns that computed offsets can land outside the modulus. A 20-bit compare-and-subtract also fits one instruction period more easily than a divider.
- Evidence for (c): the firmware's halted phase after it writes program 10's DLENGTH may last longer than the 24.6 ms one refresh cycle takes. That phase covers verified record writes, the table write and read-back, the record verify and the ramped mix GPRs. If it does, DBASE is inside the ring by the time HALT is released on the hardware, even under (b). This timing is not measured, and the player's loader gives host accesses almost no emulated time, so (c) would be a stand-in.
- Confidence: Low-medium. The ES5510 text points to (a) in two places and to (b) in one, and the hardware may do (b) and rely on the timing in (c). The only observable difference is the start of a program whose DBASE is above DLENGTH. If a hardware recording ever shows program 10 cutting off held notes right after a load, go back to (b) and model the firmware's halted time instead.
- Status: implemented in Agen::physical (`// DECISION E31`, `src/esp/esp_agen.cpp`). Tests: agen_delay_wrap_is_modulo_when_dbase_above_dlength, agen_delay_never_leaves_ring_for_any_dbase, agen_delay_walk_continuous_from_dbase_above_dlength, and synth_rom_held_note_sustains_on_every_program (program 10 now sustains: AC RMS 2546 at 0.75 s, 3004 at 2.5 s, against 16 before). The E7 tests pass unchanged. The Plan 1 traces (smoke, esp_mix, reverb_tank) and the default-effect render of `player_smoke.mid` are byte-identical before and after the change, so the MAME comparisons in `docs/verification-plan1.md` and the identity hash in `docs/verification-plan3.md` still hold.

## E32: Read-back of DLENGTH, ABASE and BBASE bits 3..0
- Options: (a) read back the written 24-bit value (R13); (b) bits 3..0 read as 1 whatever was written; (c) bits 3..0 read as 0.
- Choice: (b) for DLENGTH, ABASE and BBASE. DBASE keeps (a). The stored value and the address arithmetic are unchanged (R13: arithmetic ignores bits 3..0).
- Evidence: OS v3.10 verifies every ESP load by reading each register back and comparing (routine at 0xC4C78C; mismatch at 0xC4C868 reloads the whole effect, counter at RAM $130, after 10 tries flag $12E is set and the effect's table-A data is never written through host RAM access). For SPR 245-247 it compares the third byte against `written | 0x0F` (0xC4C838-0xC4C844), so the hardware returns 1s in bits 3..0. DBASE (248) and MEMSIZ/DIL (244) are skipped. Under (a), effects that set ABASE = 0x3CFE00 (64 GUITAR AMP 2, 71 EQ--COMPRESSOR) failed all 10 tries: CRUNCH-ELEC was silent and ZEP-KIT clicked after release. Under (b) they load first time and the OS writes their tables. The KS-32 boot's DLENGTH = 0xFFFFFF check (R13's evidence) passes under (a) and (b).
- Confidence: High for DLENGTH/ABASE/BBASE; DBASE has no evidence.
- Status: Implemented in Agen::readReg. Tests: agen_low_nibble_reads_as_ones, ts_effect_tables_load. The same routine expects other read-back bits (SIGREG bits 21..16, CCR 18..16, CMR 17..16 and the SER3 low byte read as 1); none of these changed a load that was tried, so they are left as E22/E8 say until a load needs them.

## E33: Read-back of the SIGREG, CCR and CMR spare bits
- Options: (a) E22: only the documented bits are kept and the rest read 0; (b) the spare bits of the top byte read as 1 (SIGREG 21..16, CCR 18..16, CMR 17..16) and SIGREG bit 23 reads back as written; bits 15..0 read 0.
- Choice: (b). What the bits do is unchanged: SIGREG bit 22 picks the MAC shift, CCR keeps N C V LT Z, CMR keeps its six mask bits.
- Evidence: OS v3.10's load verification (E32) compares the top byte of SIGREG, CCR and CMR against `written | 0x3F`, `| 0x07` and `| 0x03` and skips their lower bytes (0xC4C7FE-0xC4C82E). The boot writes SIGREG = 0xFFFFFF, so bit 23 is kept. Under (a) every load that writes CMR failed 10 times and dropped the rest of its records: U0-4 SUITCASE-74, U0-5 TRANS-CLAV, U1 program 25 and their R2/R3 copies (a survey of every sound and preset in banks U0, U1, R2-R4). Under (b) they load first time.
- Confidence: High for the top byte; the lower bytes are not checked by the OS.
- Status: Implemented in Esp::readReg and writeReg. Tests: esp_narrow_spr_widths, ts_effect_loads_verify_first_time. The same routine expects the low byte of SER3L/SER3R (240, 241) to read 0xFF and SER0-SER2 to read 0x00; E8 is left as it is (0x00) because SER3 is an audio input and no load was found that writes it.

## P1: ESP slots per frame
- Options: (a) 89 instruction periods per frame (8 MHz ESP, 3 clocks per instruction, one 29.76 kHz frame; every KS-32 ESP program is exactly 89 words); (b) 160 slots (the full program memory); (c) run-to-END only (no budget, choice (a) of E30).
- Choice: (a): the board calls `esp.advance(89)` once per frame. A program longer than the budget still runs to END (no mid-program cut-off, no carry-over into the next frame); the only effect of the budget is the number of idle periods after a halting END.
- Evidence: `docs/rom-investigation/findings.md` section B (89-word ESP program, 89 slots per frame at 8 MHz); decision E30.
- Confidence: Medium.
- Status: Used by `Esp::advance` callers (the KS-32 board, later Plan 3 tasks). Test: esp_advance_overlong_program_runs_to_end.

## 68000 core

## M1: Pass criterion for the per-instruction suite
- Options: (a) exact match of registers (including the prefetch queue), cycle count and RAM, with no exceptions; (b) exact match, except two classes that are documented deviations and counted separately: `SelfModifyingPrefetch` and `AddressErrorFrame`; (c) compare only registers and RAM, not cycles or the prefetch queue.
- Choice: (b). A test passes when every register (d0-d7, a0-a6, usp, ssp, pc, sr, both prefetch words), the total cycle count (`length`) and the RAM bytes agree. Bus transactions are not compared. RAM is compared at every address the CPU wrote and every address in the test's final RAM; an address the final RAM does not list is expected to hold its initial value (else 0). d, a, usp, ssp, pc, sr and cycles must always be exact. Two classes may absorb the remaining differences. `SelfModifyingPrefetch`: the test's own final RAM shows a write inside the prefetch window [instrAddr, instrAddr + 6) (an expected final byte in the window differs from its initial value, else 0), and every difference between expected and got is a RAM byte inside that window or one or both prefetch words (the closing fetch reads the old or the new value depending on the order of write and fetch, so the symptom is usually a different prefetch word, not different RAM). `AddressErrorFrame`: the step took vector 3, the prefetch words are equal, and the only differing RAM byte is the low byte of the frame's status word, at the final SSP + 1, and only in bits 3..0 (I/N and FC2..FC0, the M12 choices); the R/W bit, the high status byte, the access address, IR, SR and PC of the 14-byte group-0 frame are exact. `SelfModifyingPrefetch` never occurs in the suite (smp 0 in every file), but the class is kept: it guards self-modifying prefetch cases the suite happens not to contain and keeps the runner's output format stable. A third class, `ManualDeviation`, is by name only: a fixed table in `src/m68k/harte_format.cpp` (`kManualDeviations`) lists exact suite test names, each with the id of the decision that follows the PRM/UM against the suite for that test; a test that would fail and whose name is in the table is a manual deviation only when every difference lies in the fields the entry allows (D registers, SR bits, one RAM byte); any other difference is a failure. An entry needs such a decision id; entries are never added just to silence a failure. A listed test that passes is reported as a warning (`WARNING stale manual deviation`), since the entry no longer describes a real difference. Table: `80ef [DIVU (d16, A7), D0] 5745` (M15; only RAM 0x7FF, the low byte of the stacked PC, may differ: expected 0x00, got 0x04); the two inconsistent ASL.b cases of M19 (`e502 [ASL.b Q, D2] 1583`: D2 and the condition codes X N Z V C; `... 1761`: D2 only). Anything else is a failure. `m68k_harte` counts the three classes as passes in `pass/total` (so they do not set exit code 1) and lists them as `smp`, `aef` and `dev`. (c) would hide the timing errors the suite exists to catch.
- Evidence: the suite README (fields `length`, `transactions`, `ram`) and its stated aim of exact per-instruction behaviour; (b) gives a hard pass criterion without hiding a real bug, and the two classes are the cases where the recorded RAM depends on the order of a write against a prefetch, or on the status-word bits the UM defines differently from the suite's generator (UM 6.3.9.1 I/N, M12 function code). Final review: all 31371 aef cases differ only in status-word bits 3..0 (the suite shows no other frame difference), and the three listed cases only in the fields above; the classes were narrowed to exactly that, and the whole suite is unchanged at 1000060/1000060.
- Confidence: Medium (the two classes are a prediction from the suite's design; Tasks 3-6 confirm or shrink them when real data hits them: any other deviation needs its own entry; a class that never occurs would be deleted, except `SelfModifyingPrefetch`, kept on purpose, see Choice).
- Status: `// DECISION M1` at `kManualDeviations`, `deviationCovers` and `classifyCore` (`src/m68k/harte_format.cpp`), `HarteResult` and `manualDeviation` (`src/m68k/harte_format.h`) and in `tools/m68k_harte/main.cpp` (Task 2; the manual-deviation class Task 4, ruling R9). Tests: harte_sample_parses_and_runs, harte_failure_classes (address-error frame limited to status bits 3..0), harte_manual_deviation_by_name (allowed fields per entry). On the real suite (this task) NOP passes 8065/8065 with smp 0, aef 0. Task 3: the 23 data-movement files pass with smp 0 and aef 766, every aef case being the M12 function-code difference (aef 0 with M12 switched to the suite's choice). Task 4: the 38 arithmetic files pass 306470/306470 with smp 0, aef 1998 (again all M12: aef 0 with M12 switched) and dev 1 (DIVU, M15). Task 5: the 42 logic, shift and bit files pass 338730/338730 with smp 0, aef 339 (all M12) and dev 2 (ASL.b, M19). Task 6: the 20 program-control and system files pass 161300/161300 with smp 0, dev 0 and aef 28268: 27970 are the I/N bit of a fault on the first fetch at an odd jump target (Bcc 2200, BSR 3980, DBcc 1964, JMP 3806, JSR 3882, RTS 4057, RTR 4027, RTE 4054; M12) and 298 the M12 function code of PC-relative sources (MOVEtoSR 139, MOVEtoCCR 159); with I/N and M12 both switched to the suite's choice these files show aef 0, so nothing else hides in the class. The whole suite after Task 6: 1000060/1000060 (smp 0, aef 31371, dev 3) on both builds, no stale manual deviation. Task 9: the same total on both builds with byte-identical `m68k_harte` output (`docs/verification-plan5.md`); smp is 0 in every file of the real suite; the class is kept (final review, see Choice).

## M2: Undefined flags and results
- Options: (a) follow the suite where the PRM says "undefined"; (b) leave such flags unchanged; (c) clear them.
- Choice: (a), per instruction:
  - ABCD, SBCD, NBCD (PRM: N and V undefined): the binary sum a + b + X (difference a - b - X) is decimal-corrected by 0x06 when the low digit exceeds 9 or there is a half carry (half borrow), and by 0x60 on a decimal carry (borrow, which also includes a borrow out of the 0x06 correction); X = C = that carry. N = bit 7 of the corrected result; V = bit 7 turned on by the ABCD correction (`~binary & result & 0x80`) or turned off by the SBCD/NBCD correction (`binary & ~result & 0x80`). Z is cleared by a non-zero result and unchanged otherwise (defined by the PRM).
  - DIVU, DIVS overflow (PRM: N and Z undefined): N and Z unchanged, V = 1, C = 0, the destination unchanged.
  - DIVU, DIVS by zero (PRM: N, Z, V undefined): N = Z = V = C = 0, X unchanged, set before the exception (the stacked SR has them).
  - CHK (PRM: N set for Dn < 0, cleared for Dn > bound, undefined otherwise; Z, V, C undefined): on a trap N = (Dn < 0), so a negative Dn above a negative bound still sets N; without a trap N is unchanged; V = C = 0; Z = (Dn == 0).
  - Task 5 adds its instructions here.
- Evidence: suite register-operand cases, all matching: ABCD 4004, SBCD 3948, NBCD 1315 (result, X, N, Z, V, C); DIVU 613 and DIVS 333 quotients, and every overflow case (DIVU 536, DIVS 887: N and Z equal their initial values in all four combinations); the one divide-by-zero case (DIVU "80ef [DIVU (d16, A7), D0] 5745": SR 0x271D -> 0x2710); CHK Dn,Dn 1216 (trap: N = (Dn < 0) in all 1000; no trap: N equals its initial value in all 216; Z = V = C = 0 throughout). No CHK case has Dn = 0, so Z = (Dn == 0) is an unchecked choice that agrees with every case (Z = 0 would too; MAME's 68000, comparison only, sets Z from Dn). The 13 files of these instructions pass, apart from M15.
- Confidence: High for BCD, DIV overflow and CHK N/V/C (thousands of cases); Medium for divide by zero (one case) and CHK Z for Dn = 0 (no case).
- Status: `// DECISION M2` at `abcd`/`sbcd` (`src/m68k/m68k_alu.cpp`, `src/m68k/m68k_alu.h`), `Cpu::opDiv` and `Cpu::opChk` (`src/m68k/m68k_ops_arith.cpp`, Task 4). Tests: m68k_alu_bcd, m68k_alu_divs_overflow_leaves_operand, m68k_divu_by_zero_traps, m68k_chk_traps_with_next_pc.

## M3: PC and prefetch convention
- Options: (a) `Registers::pc` is the address of the next instruction and `prefetch` = {word at pc, word at pc + 2}, the suite's convention; (b) `pc` is the internal address register of the last prefetch (pc + 2 under (a)), as the hardware's address bus shows it; (c) do not expose the prefetch queue and refill it from memory in `setRegs`.
- Choice: (a). The core models the two-word queue as the UM describes: IR holds the opcode, IRC the next word, and the internal `pc_` is the address of the word in IRC, so `regs().pc = pc_ - 2` and `regs().prefetch = {IR, IRC}`. `setRegs` loads IR/IRC from `r.prefetch` without touching memory and sets `pc_ = r.pc + 2`. An instruction's closing fetch moves IRC to IR and reads the word at `pc_ + 2`; an extension word is taken from IRC and refilled the same way; a jump (reset, exception) reads IR and IRC from the target. (c) would hide the self-modifying-prefetch effect that M1 has to classify.
- Evidence: the suite's README defines `pc` as "the formal program counter" (the location of the next instruction) and `prefetch` as the 2-word queue, first item fetched first. NOP.json "4e71 [NOP] 1": initial pc 3072, prefetch [0x4E71, 0x2A53]; final pc 3074, prefetch [0x2A53, 0x0679], with one read at 3076 = pc + 4. That is exactly (a) with the closing fetch at `pc_ + 2`.
- Confidence: High.
- Status: `// DECISION M3` on `Registers` (`src/m68k/m68k.h`) and at `Cpu::regs` and `Cpu::setRegs` (`src/m68k/m68k_cpu.cpp`); implemented in those and in `Cpu::prefetch`, `Cpu::nextWord`, `Cpu::fillPrefetch` (Task 1). Tests: m68k_reset_loads_ssp_and_pc, m68k_nop_takes_4_cycles, m68k_set_regs_round_trips.

## M4: Interrupt exception timing
- Options: (a) 44 clocks, UM table 8-14 (interrupt 44(5/3), "the interrupt acknowledge cycle is assumed to take four clock periods"), with no dependence on the E clock; (b) 44 plus the 0..9 clocks the VPA/VMA E-clock synchronisation adds to an autovectored acknowledge on real hardware; (c) 34, the other group-1 figure, with the acknowledge not counted.
- Choice: (a), for every interrupt whatever the vector source. Order inside the 44 (the UM gives totals, not the sequence, and the suite has no interrupt case, so this is the UM-total-compatible order closest to the suite's group-1 entry): 6 internal clocks, the acknowledge cycle (`Bus::interruptAcknowledge(level)`, 4 clocks), 4 internal clocks, the frame (PC low, SR, PC high), the vector long, the first prefetch, 2 internal clocks, the second prefetch. The acknowledge cycle is the table's fifth read; the other 12 clocks are internal. On entry the SR is copied, S set, T cleared and the mask loaded with the level (UM 6.3.2); the frame is the 6-byte group-1/2 frame with the next instruction's address, on the SSP. The group (I/N bit, trace block) is passed to the entry explicitly: an interrupt is group 1 (I/N = 1 in the frame of a fault during it) whatever vector a device supplies, including 32..47 (the TRAP range) and 5..7, which `exception()` would otherwise infer as group 2.
- Evidence: UM table 8-14 and its footnote; UM 6.3.2 (SR copy, S, T, mask, vector fetch, "The saved value of the program counter is the address of the instruction that would have been executed had the interrupt not been taken"). The KS-32 machine (Plan 6) slices the CPU by OTIS voice slot, so a cycle count that depended on a free-running E clock would break the slice invariance of M6.
- Confidence: High for 44 and the frame; Medium for the position of the acknowledge cycle among the internal clocks (only a device that watches the bus timing against the frame writes could tell).
- Status: `// DECISION M4` at `Cpu::interrupt` (the 44-clock sequence) and `Cpu::enterException` (the mask load) (`src/m68k/m68k_cpu.cpp`) and on the `interrupt` declaration (`src/m68k/m68k.h`, Task 7). Tests: m68k_autovector_interrupt, m68k_interrupt_from_user_mode_and_trace, m68k_vectored_and_spurious.

## M5: Bus errors and function codes
- Options: (a) no bus-error return and no function-code lines on `Bus`, so vector 2 is never taken; (b) a bus-error return on every access with the 14-byte group-0 frame; (c) pass FC2..FC0 with each access.
- Choice: (a). No KS-32 source of BERR is known and no KS-32 decoding of the function codes is known; Plan 6 may add either. The 24-bit address the core passes and the access width are all a device sees.
- Evidence: UM 3.8 and table 3-3 (function codes), UM 5.4.1 (bus error operation). The KS-32 board's BERR and FC wiring is not documented in this repository.
- Confidence: Medium (it holds until a KS-32 source of BERR is found).
- Status: `// DECISION M5` on `struct Bus` (`src/m68k/m68k.h`, Task 1).

## M6: Run slices
- Options: (a) `run(n)` executes whole instructions and exceptions until at least n cycles have passed and returns the cycles used (overshoot less than one instruction); a halted or stopped CPU consumes exactly the rest of the slice; (b) stop before an instruction that would cross n (undershoot), which makes the result depend on how the caller slices time; (c) split instructions at bus-cycle granularity.
- Choice: (a). `run(n)` with n <= 0 returns 0 and runs nothing. If the CPU halts or stops in the middle of a slice, the rest of the slice is consumed as idle time (added to `totalCycles()`). `step()` on a halted or stopped CPU idles 4 cycles (one bus-cycle time) and returns 4, so a loop of `step()` calls still advances time. A caller that loops `run(1)` until `totalCycles()` reaches T ends at the same instruction boundary as one `run(T - start)`, so results do not depend on slicing (Task 7 adds interrupts and checks this).
- Evidence: none in the manuals (an emulator interface choice). (c) costs speed and nothing in Plan 5 needs it; Plan 6 can revisit it if a device needs bus-cycle timing.
- Confidence: High.
- Status: `// DECISION M6` at `Cpu::step` and `Cpu::run` (`src/m68k/m68k_cpu.cpp`, Task 1). Tests: m68k_run_returns_whole_instructions; the halted and stopped cases are tested in Tasks 3, 6 and 7 (m68k_double_fault_halts, m68k_runaway_into_fill_halts_cleanly, m68k_stop_loads_sr_and_stops, m68k_stop_wakes_on_unmasked_interrupt_only, m68k_slicing_is_invisible). Task 6: a pending trace exception (UM 6.3.8) is a step of its own and ends a STOP, so `step()` and `run()` treat a stopped CPU with a trace pending as running. Task 7: `run()` likewise treats a stopped CPU with an interrupt to take as running (`step()` takes it), counts the slice in 64 bits and clamps the returned cycles to `INT_MAX`; repeated `run(1)` until a cycle count is reached and one `run(n)` end at the same instruction boundary with the same registers, memory and `totalCycles()`, also with interrupt levels that change between slices (m68k_slicing_is_invisible, m68k_slicing_is_invisible_with_interrupts, m68k_run_is_overflow_safe).

## M7: Interrupt level sampling
- Options: (a) `setIpl(level)` latches the level; at each instruction boundary (the start of every `step()`, also of a stopped CPU) the latched level is compared with the SR mask: a level above the mask is taken (levels 1..6 are level-sensitive), and level 7 is taken in addition once for each rise to 7 whatever the mask (an edge latch); (b) level 7 only on the edge, never by level comparison; (c) sample the level inside instructions (bus-cycle granularity).
- Choice: (a). `pendingInterrupt()` returns the latched level when it is above the mask, or equals 7 with an undelivered rise; delivering a 7 clears the latch. `setIpl(7)` from a level below 7 arms the latch; `setIpl` to any other level disarms it (a request that came and went between two boundaries is never sampled); `reset()` disarms it. Priority at a boundary: a pending trace exception, then an interrupt, then the next instruction (UM 6.3.9: trace, then interrupt; the trace handler's first instruction runs after the interrupt entry). An interrupt ends a STOP (stacked PC: the instruction after STOP, M22); a masked level leaves a STOP running; a halted CPU (M9) ignores interrupts. A group-1 exception inside an instruction still blocks that instruction's trace (Task 6 `traceBlocked_`). The brief's wording for level 7 ("edge-triggered once per rise to 7 regardless of mask") holds for mask 7, where only the edge can deliver it. For a mask below 7 the UM's explicit sentence also applies ("A level 7 interrupt may still be caused by the level comparison if the request level is a 7 and the processor priority is set to a lower level by an instruction"), so a level held at 7 is taken again when an instruction or an RTE drops the mask. That is the only place (a) differs from (b).
- Evidence: UM 6.3.2 (pending interrupts are detected between instruction executions; a priority above the mask is taken; the level 7 rules quoted above); UM 6.3.9 (trace before interrupt); PRM STOP (an interrupt above the mask ends it).
- Confidence: High for levels 1..6, the edge and the order against trace; Medium for the held-7 re-take (UM sentence, no suite case; the KS-32 is not known to use level 7).
- Status: `// DECISION M7` at `Cpu::pendingInterrupt` (the level comparison and the level 7 latch), `Cpu::setIpl` (the latch), `Cpu::step`, `Cpu::run` and `Cpu::reset` (`src/m68k/m68k_cpu.cpp`), `Cpu::opStop` (`src/m68k/m68k_ops_flow.cpp`), and on `setIpl` and the `irq7Pending_` member (`src/m68k/m68k.h`, Task 7). Tests: m68k_masked_level_waits, m68k_level7_is_edge_triggered, m68k_stop_wakes_on_unmasked_interrupt_only, m68k_halted_cpu_ignores_interrupts, m68k_interrupt_from_user_mode_and_trace.

## M8: RESET instruction
- Options: (a) 132 cycles as UM table 8-12 gives (132(1/0)); `Bus::resetPeripherals()` is called once; every register except the PC (and the prefetch queue, which the closing fetch moves on) is unchanged; privileged; (b) also reset CPU state (SR, vectors), as an external reset would; (c) a fixed cost without the bus call.
- Choice: (a). Sequence: 4 internal clocks, `resetPeripherals()`, 124 internal clocks (the RESET line asserted for 124 clocks, PRM RESET), the closing fetch. In user mode it takes the privilege violation (vector 8, 34 cycles) and the bus is not called.
- Evidence: PRM RESET ("Asserts the RSTO signal for ... 124 [clocks] for MC68000 ... The processor state, other than the program counter, is unaffected"); UM table 8-12 (132(1/0)); UM 6.3.7 (RESET is privileged). Suite RESET.json: all 8065 cases 132 cycles with the bus log n4, n124, prefetch, and no register change ("4e70 [RESET] 1"). Where in the 124 clocks a device sees the reset does not show in the suite; at their start is the PRM's reading.
- Confidence: High.
- Status: `// DECISION M8` at `Cpu::opReset` (`src/m68k/m68k_ops_flow.cpp`, Task 6). Test: m68k_reset_instruction_calls_bus (and m68k_privilege_violation for user mode); RESET passes 8065/8065.

## M9: Double fault
- Options: (a) an address error during group-0 processing (the address-error exception itself, or reset) halts the CPU: `halted()` becomes true and only `reset()` clears it; (b) take another address error (an odd SSP or vector would loop forever); (c) treat it as a reset.
- Choice: (a). `step()` runs the address-error exception inside a second `try`; a fault there sets `halted_`. `reset()` halts when its initial PC is odd. A fault during group-1 or group-2 exception processing (an odd SSP while stacking, or an odd handler address) is an ordinary address error with I/N = 1 for group 1 (UM 6.3.9.1); if its own frame push faults too (odd SSP), that is the double fault. A halted CPU idles under M6.
- Evidence: UM 6.3.9.1 ("If a bus error occurs during the exception processing for a bus error, an address error, or a reset, the processor halts") and 6.3.10 (the same for an address error). The suite's Task 3 files contain no double fault.
- Confidence: High.
- Status: `// DECISION M9` at `Cpu::step` and `Cpu::reset` (`src/m68k/m68k_cpu.cpp`, Task 3). Tests: m68k_double_fault_halts, m68k_reset_with_odd_pc_halts, m68k_odd_exception_vector_takes_address_error.

## M10: DIVU/DIVS cycles
- Options: (a) the UM's upper bounds (DIVU 140, DIVS 158, table 8-4) for every case; (b) a data-dependent count that reproduces the suite; (c) a fixed average.
- Choice: (b). `divCycles(dividend, divisor, isSigned)` returns the total clocks without the source's address calculation (the closing prefetch included). The instruction spends `divCycles - 4` internal clocks and then runs the closing fetch, as in the suite's transaction logs. Counted in 2-clock units u, total = 2u:
  - Divisor 0: 38 (UM table 8-14, divide by zero, exception included).
  - DIVU: if dividend >> 16 >= divisor (overflow), 10. Otherwise u = 38, then 15 steps of the shift-and-subtract loop with D = divisor << 16: shift the dividend left by 1; if the bit shifted out was 1, subtract D (u unchanged); else u += 2 and, if the shifted dividend >= D, subtract D and u -= 1. Range 76..136.
  - DIVS: u = 6, plus 1 for a negative dividend. With q = |dividend| / |divisor| (magnitudes in unsigned 32-bit arithmetic, so 0x80000000 / -1 is defined): if q does not fit a signed 16-bit quotient (q > 0x7FFF for a positive quotient, q > 0x8000 for a negative one), overflow: u + 2 (16 or 18 clocks). Otherwise u += 55; for a non-negative divisor u -= 1 when the dividend is non-negative and u += 1 when it is negative; then u += 1 for each 0 among bits 15..1 of q. Range 120..156.
- Evidence: UM table 8-4 gives only the maxima and says the algorithm "provides less than 10% difference between the best- and worst-case timings", which the suite does not show (DIVU 76..136). Every register-operand case of the suite matches: DIVU 1149/1149 (613 quotients, 536 overflows), DIVS 1220/1220. The DIVS file passes 8065/8065 and DIVU 8064/8065 (the one failure is the stacked PC of M15; its cycles match). The loop is the 68000's restoring division as published by Jorge Cwik ("68000 DIVU/DIVS timing"), used as comparison only. With that analysis a DIVS quotient that is too large only for the signed range would run the whole loop, but the suite has the early overflow time in all 285 such register cases (16 for a non-negative dividend, 18 for a negative one), so that is what is implemented. A quotient of exactly -32768 (q = 0x8000, negative) is in range; the suite has no such case.
- Confidence: High for DIVU and for DIVS without overflow (every case matches); Medium for the DIVS signed-range overflow time (the suite's model, no second source); Low for the -32768 edge.
- Status: `// DECISION M10` at `divCycles` (`src/m68k/m68k_alu.cpp`, `src/m68k/m68k_alu.h`) and `Cpu::opDiv` (`src/m68k/m68k_ops_arith.cpp`, Task 4). Tests: m68k_alu_div_cycles, m68k_alu_divs_overflow_leaves_operand.

## M11: Address-error frame contents and the state at the fault
- Options: (a) follow the suite where the UM leaves the frame open (the UM says the stacked PC "is unpredictable and may be incremented from the address of the instruction", and figure 6-7 leaves status-word bits 15..5 undefined); (b) stack the instruction's address and zero bits 15..5; (c) stack the address of the next instruction.
- Choice: (a), with the timing and register effects the suite shows:
  - The faulting access is not run and takes no clocks. The step costs the clocks run before it plus 50 (UM table 8-14: 50(4/7)): n4, PC low, SR, PC high, IR, address low, status word, address high, the vector long, prefetch, n2, prefetch.
  - Stacked PC: `pc_ - 2` at the fault, where `pc_` is the address of the word in IRC. So it advances by 2 with every extension word or closing fetch run before the fault (MOVE.w `(xxx).L, Dn` stacks instruction + 4; MOVE.w `Dn, -(An)` stacks instruction + 2 because its closing fetch runs before the write). A fault on the first fetch at an odd jump target stacks target - 4 (`fetchJumpIr`, used by `fillPrefetch`): every odd-target case of Bcc, BSR, DBcc, JMP, JSR, RTS, RTR and RTE in the suite (27970, Task 6) matches, with the register effects that precede the fetch (BSR's push, DBcc's decrement, the SP of RTS/RTR/RTE after the pop, and RTE's new SR, which is the SR stacked). JSR faults before its push.
  - Access address: the full 32-bit address, not masked to 24 bits. IR: the instruction's opcode, even if its closing fetch already ran. SR: the SR at the fault (MOVE has set its flags before the write). Status word: bits 15..5 are IR bits 15..5, bit 4 R/W, bit 3 I/N (0 while executing an instruction or a group-2 exception, 1 in group-0/1 processing, UM 6.3.9.1), bits 2..0 the function code (M12).
  - Register effects up to the fault: a source (An)+ steps before its read and -(An) before its access; a MOVE (An)+ destination steps after the write (a faulting write leaves An); MOVE.l to -(An) steps An by 2 per word, low word first, so an odd An faults at An - 2 and keeps that value; MOVEM -(An) writes An only at the end; MOVEM (An)+ to registers has An + 2 at the first read.
- Evidence: the suite's address-error cases in the Task 3 files (MOVE.w 4697, MOVE.l 4795, MOVEA.w 2743, MOVEA.l 2686, MOVEM.w 3784, MOVEM.l 3843, CLR.w 3169, CLR.l 3162), all exact in registers, SR, PC, SSP, cycles and the stacked SR and PC. With M12 switched to the suite's function code every frame byte matches too (aef 0, checked). Examples: MOVE.w "3010 [MOVE.w (A0), D0] 4754" (50 cycles, PC = instruction), MOVE.l "2506 [MOVE.l D6, -(A2)] 92" (54, A2 - 2), MOVE.l "22cb [MOVE.l A3, (A1)+] 32" (A1 unchanged), MOVEM.w "4c98 [MOVEM.w (A0)+, #] 1" (A0 + 2).
- Confidence: Medium (the suite is one model of behaviour the UM calls unpredictable; firmware does not depend on it).
- Status: `// DECISION M11` at `Cpu::addressFault`, `Cpu::fetchJumpIr` and `Cpu::addressError` (`src/m68k/m68k_cpu.cpp`); the per-instruction orders are commented in `src/m68k/m68k_ops_data.cpp` (Task 3). Tests: m68k_address_error_frame, m68k_odd_exception_vector_takes_address_error; the suite files of Task 3.

## M12: Function code of PC-relative operand reads
- Options: (a) program space, as the PRM says (2.0: "Data items in the instruction stream can be accessed with the program counter relative addressing modes; these accesses classify as program references"); (b) data space, as the suite records them.
- Choice: (a). Without function codes on `Bus` (M5) the only place it shows is the FC field of an address-error frame's status word. The I/N bit likewise follows UM 6.3.9.1 (M11), while the suite sets I/N = 1 when the first fetch at an odd jump target faults (all 27970 such cases of Bcc, BSR, DBcc, JMP, JSR, RTS, RTR and RTE, Task 6). UM 6.3.9.1 is explicit ("The processor is processing an instruction if it is in the normal state or processing a group 2 exception"), so under ruling R8 the core keeps I/N = 0 there. Both are differences in bits 3..0 of the frame's status word, which M1's `aef` class allows (and nothing else).
- Evidence: PRM 2.0; the suite's transaction logs show FC 5 for `(d16,PC)` reads (MOVE.w "3e3a [MOVE.w (d16, PC), D7] 759"). In the Task 3 files this gives aef 766 (MOVE.w 129, MOVE.l 124, MOVEA.w 150, MOVEA.l 137, MOVEM.w 111, MOVEM.l 115), and switching to (b) gives aef 0, so no other frame difference is hidden in that count.
- Confidence: Medium (the PRM is explicit; the suite's generator differs; nothing on the KS-32 depends on it).
- Status: `// DECISION M12` at `Cpu::load` (`src/m68k/m68k_ea.cpp`), `Cpu::opMovemToReg` (`src/m68k/m68k_ops_data.cpp`), `Cpu::opMoveToSr` (`src/m68k/m68k_ops_flow.cpp`; Task 6: a PC-relative source) and, for the I/N bit of the status word (0 on a fault at an odd jump target, against the suite's 27970 cases), at `Cpu::addressFault` (`src/m68k/m68k_cpu.cpp`, Task 3).

## M13: Bus order of long accesses and of stores against the closing fetch
- Options: (a) the suite's transaction order; (b) always high word first, stores before the closing fetch.
- Choice: (a). Long reads go high word first. Long writes go high word first (MOVE to every mode but -(An), PEA and LINK pushes, MOVEM to control modes), except predecrement and read-modify-write stores, which go low word first (MOVE.l and MOVEM.l to -(An), CLR.l in every memory mode): `Cpu::write32` and `Cpu::write32Desc`. MOVE to -(An) runs its closing fetch before the write; MOVE from memory to (xxx).L writes before the fetch that replaces the low address word in IRC; CLR to memory reads, fetches, then writes. Task 4: every read-modify-write to memory (ADD, SUB, ADDI, SUBI, ADDQ, SUBQ, NEG, NEGX, NBCD) reads, fetches, then writes (a long low word first, `Cpu::storeRmw`); ADDX.l/SUBX.l -(Ay),-(Ax) read both operands low word first (An stepping by 2 per word), then write the low word, fetch, and write the high word; their byte and word forms and ABCD/SBCD read, fetch, then write. Task 5: AND, OR, EOR, ANDI, ORI, EORI, NOT, memory shifts, BCHG, BCLR, BSET, Scc and TAS follow the same read, fetch, write order (Scc reads the byte it replaces; TAS writes before the closing fetch, M20). Task 6: MOVE from SR to memory reads, fetches, then writes; BSR and JSR push the return address high word first; JSR runs the first fetch at the target before the push and the second after it; RTS pops high word first; RTE and RTR read PC high (SP + 2), SR (SP), PC low (SP + 4); MOVE to SR/CCR and ANDI/ORI/EORI to SR/CCR refetch both queue words from the next instruction after their 4 or 8 internal clocks; TRAPV's trap runs the closing fetch first, then the frame (M23 has the read counts).
- Evidence: the UM gives only access counts. Suite transactions: ADD.l "d392 [ADD.l D1, (A2)] 1513", ADDX.l "d98e [ADDX.l -(A6), -(A4)] 1" and "db8a [ADDX.l -(A2), -(A5)] 7" (an odd A2 faults at A2 - 2), MOVE.l "2100 [MOVE.l D0, -(A0)] 2394", "2080 [MOVE.l D0, (A0)] 5367", "23d0 [MOVE.l (A0), (xxx).l] 675", CLR.l "42a0 [CLR.l -(A0)] 1", MOVEM.l "48e0 [MOVEM.l #, -(A0)] 34", PEA "4879 [PEA (xxx).l] 30", LINK "4e56 [LINK A6, #] 1". The order decides which word faults (the stacked access address and PC, M11) and what a self-modifying store does to the prefetch queue (M1 `smp`); totals do not depend on it.
- Confidence: High for the cases listed (they fix the fault frames exactly).
- Status: `// DECISION M13` on `struct Bus` (`src/m68k/m68k.h`) and at `Cpu::write32Desc` (`src/m68k/m68k_cpu.cpp`, Task 3), `Cpu::storeRmw` (`src/m68k/m68k_ea.cpp`) and `Cpu::readPredec` and `Cpu::opAddxSubx` (`src/m68k/m68k_ops_arith.cpp`, Task 4). Tests: m68k_predecrement_long_write_goes_low_word_first, m68k_addx_long_predecrement_order.

## M14: ADDQ/SUBQ to an address register: cycles
- Options: (a) the suite: word 8 clocks, long 6; (b) UM table 8-5: ADDQ word 4(1/0), long 8(1/0); SUBQ word 8(1/0), long 8(1/0).
- Choice: (a). ADDQ/SUBQ #q,An change the whole register without flags; the closing fetch runs, then 4 internal clocks for the word form and 2 for the long form. Reason (ruling R8: an explicit manual statement wins unless the manual contradicts itself on that point, or the suite gives overwhelming consistent evidence that the manual's single figure cannot cover a data-dependent case): UM table 8-5 contradicts itself on these rows, so the suite decides.
- Evidence: suite ADD.w "Q, An" 407 cases all 8, ADD.l 380 cases all 6, SUB.w 384 all 8, SUB.l 351 all 6. UM table 8-5's An column contradicts itself (ADDQ.w 4 against SUBQ.w 8 for the same operation) and the suite in both sizes. MAME's 68000 and the published "Yacht" tables (comparison only) give 8 for both sizes, so the suite's 6 for the long form has no second source.
- Confidence: Medium-Low (the suite against a self-contradicting UM table; 2 clocks per ADDQ.l/SUBQ.l to An). If hardware evidence shows 8, the change is one line in `Cpu::opAddSubQuick`, and ADD.l/SUB.l would then fail exactly those 731 cases.
- Status: `// DECISION M14` at `Cpu::opAddSubQuick` (`src/m68k/m68k_ops_arith.cpp`, Task 4). Suite ADD.w, ADD.l, SUB.w, SUB.l pass.

## M15: Stacked PC of the divide-by-zero exception
- Options: (a) the address of the next instruction, as UM 6.3.5 states for every trap ("The saved value of the program counter is the address of the instruction following the instruction that generated the trap"); (b) the instruction's own address, as the suite's only divide-by-zero case stacks it.
- Choice: (a). DIVU/DIVS by zero: N Z V C cleared (M2), 4 internal clocks, then the vector-5 exception with the PC after the instruction's extension words (`pc_`): 38 + <ea> clocks.
- Evidence: UM 6.3.5. Suite DIVU "80ef [DIVU (d16, A7), D0] 5745" (the only divide by zero in DIVU and DIVS) stacks 0x0C00 for the instruction at 0x0C00, whose next instruction is at 0x0C04; its cycles (46), SR, registers and the rest of the frame match (a). CHK, the other group-2 trap of this task, stacks the next instruction's address in the suite as well.
- Confidence: Medium (an explicit UM statement against one suite case; nothing on the KS-32 is known to divide by zero).
- Status: `// DECISION M15` at `Cpu::opDiv` (`src/m68k/m68k_ops_arith.cpp`, Task 4) and at its entry in `kManualDeviations` (`src/m68k/harte_format.cpp`). Test: m68k_divu_by_zero_traps. The suite case differs only in RAM 0x7FF (the low byte of the stacked PC: expected 0x00, got 0x04) and is listed in M1's manual-deviation table, with that byte as the only field it may differ in, so DIVU shows 8065/8065 with dev 1 (ruling R9).

## M16: CHK trap cycles
- Options: (a) the suite: 38 + <ea> when Dn > bound (whatever the sign of Dn), 40 + <ea> when Dn < 0 and Dn <= bound; (b) UM table 8-14: 40(4/3)+ for every CHK trap.
- Choice: (a). After the closing fetch, a trap for Dn > bound enters the exception at once; one for Dn < 0 runs 2 more internal clocks first. Without a trap: 6 internal clocks, 10(1/0)+ (UM table 8-12).
- Reason (ruling R8: an explicit manual statement wins unless the manual contradicts itself on that point, or the suite gives overwhelming consistent evidence that the manual's single figure cannot cover a data-dependent case): the second exception applies. The UM gives one figure for a trap with two distinct causes, and the suite separates them consistently: 40 is right for the Dn < 0 cause, so the UM's figure is kept there.
- Evidence: suite CHK register-operand traps: Dn > bound 549 cases all 38 (78 of them with a negative Dn above a negative bound), Dn < 0 within the bound 451 cases all 40 (the transaction logs show n4 against n6 before the frame, e.g. "4581 [CHK D1, D2] 443" and "4581 [CHK D1, D2] 100"). The CHK file passes 8065/8065, with 3989 traps across every addressing mode.
- Confidence: Medium (the suite's model against the UM's single figure; nothing else on the 68000 is known to split it).
- Status: `// DECISION M16` at `Cpu::opChk` (`src/m68k/m68k_ops_arith.cpp`, Task 4). Test: m68k_chk_traps_with_next_pc.

## M17: BCHG, BSET and BCLR cycles on a data register
- Options: (a) UM table 8-8 as printed: BCHG 8, BSET 8, BCLR 10 for the dynamic register forms, 12/12/14 for the static ones; (b) the table's figures as maxima, with 2 clocks less when the bit number (modulo 32) is below 16.
- Choice: (b). After the closing fetch the register forms run 2 internal clocks (BCHG, BSET) or 4 (BCLR) for a bit number below 16, and 2 more from 16 up. BTST is 6 (10 static) for every number. BTST Dn,#imm (the immediate byte as operand) runs as a register operand: 6 + the immediate word's fetch = 10 (the suite; the UM has no row for it). Memory operands are bus-cycle exact (read, closing fetch, write) and need no internal clocks.
- Reason: the UM marks these figures "Indicates maximum value" (footnote *) without giving the smaller one, so the manual is silent on the rule and the suite decides (M2).
- Evidence: suite BCHG/BCLR/BSET register cases (BCHG dynamic 574 cases with a number below 16 all 6, 580 from 16 up all 8; BCLR 550 all 8 and 585 all 10; BSET 581 all 6 and 588 all 8; the static forms 4 more in each class, e.g. BCHG #n,Dn 69 all 10 and 78 all 12; BTST 6/6 dynamic and 10/10 static in both classes, including BTST Dn,#imm "073c [BTST D3, #] 91").
- Confidence: High (all 8065 cases of each file).
- Status: `// DECISION M17` at `Cpu::opBit` (`src/m68k/m68k_ops_logic.cpp`, Task 5). Tests: m68k_bset_bclr_bchg_register, m68k_btst_modulo; BTST, BCHG, BCLR and BSET suite files.

## M18: ASR by more places than the operand has bits
- Options: (a) the PRM's "C: set according to the last bit shifted out", which for a negative operand is the sign bit (1); (b) C = X = 0 for any count above the size.
- Choice: (b), for both signs. A count equal to the size still gives C = X = sign, and the result is all sign bits either way. `shift()` clears C after the loop when kind is ASR and count > 8 * size. ASL, LSL, LSR, ROx and ROXx follow the PRM's wording everywhere (their suite files pass without exceptions).
- Reason (ruling R8): the PRM does not say what is shifted out once every original bit has left, so its sentence does not decide this case; the suite shows one answer consistently.
- Evidence: ASR file cases with a register count above the size and a negative operand: ASR.b 1642, ASR.w 1063 and ASR.l 1031 cases, all with C = X = 0 (X cleared even when it was set; positive operands give 0 too, as every reading has it); with a count equal to the size the negative cases (35 in ASR.b) all have C = X = 1. Implementing (a) would fail 3736 suite cases.
- Confidence: Medium-High (consistent suite evidence, no manual statement either way).
- Status: `// DECISION M18` at `shift` (`src/m68k/m68k_alu.cpp`, Task 5). Test: m68k_shift_helper; ASR.b/w/l suite files.

## M19: Two suite cases whose recorded states are inconsistent
- Options: (a) leave them as failures; (b) list them in M1's manual-deviation table.
- Choice: (b). The suite's ASL.b file has two cases, "e502 [ASL.b Q, D2] 1583" and "e502 [ASL.b Q, D2] 1761" (ASL.b #2,D2), whose recorded final D2 differs from the recorded initial D2 in bits 31..8 (initial 0xCDFB7FBE, final 0x2E5E4304; initial 0x417C7E7D, final 0x6461D390), which no byte shift can do. The final low byte and the flags in both match a shift of a different initial byte (0x41 or 0xC1, and 0x64), so the initial D2 recorded with the test is not the one the final state came from. The core's result follows the recorded initial state and the PRM: the mechanism of M1 exists for exactly such entries, although the reason here is an inconsistent suite entry rather than a manual-versus-suite choice.
- Evidence: the original `ASL.b.json.gz` entries (initial and final state, and a normal 10-cycle transaction log); the other 8063 ASL.b cases pass, including every ASL.b #2 case.
- Confidence: High that the cases are inconsistent; the 8063 others show the shift itself is right.
- Status: `// DECISION M19` at the entries in `kManualDeviations` (`src/m68k/harte_format.cpp`, Task 5); ASL.b shows 8065/8065 with dev 2. Final review: the entries allow only D2 (and, for 1583, the condition codes X N Z V C, which the same inconsistent final state also affects); the recorded differences are exactly those.

## M20: TAS cycles on a memory operand
- Options: (a) UM table 8-6 as printed: 14(2/1)+ plus the operand's effective address time; (b) 10(1/1)+ plus that time (14 for (An)).
- Choice: (b). The effective address time of a memory mode already holds the operand read, so the table's two reads count it twice (a read-modify-write is one read, one write and the closing fetch: 12 clocks of bus cycles, 2 internal). The sequence: address calculation, byte read, 2 internal clocks, byte write, closing fetch. The read-modify-write is two plain accesses (no bus lock signal on `Bus`); N and Z come from the byte read, V = C = 0, X unchanged, and bit 7 is set in the byte written back. TAS Dn is 4(1/0).
- Evidence: suite TAS "4ad2 [TAS (A2)] 1" 14 cycles, "4aea [TAS (d16, A2)] 2" 18, "4af5 [TAS (d8, A5, Xn)] 6" 20: each is the address calculation, one 10-clock indivisible read-modify-write cycle, and the closing fetch. TAS passes 8065/8065.
- Confidence: High.
- Status: `// DECISION M20` at `Cpu::opTas` (`src/m68k/m68k_ops_logic.cpp`, Task 5). Test: m68k_tas_sets_bit7.

## M21: DBcc when the count expires
- Options: (a) n2, the first fetch at the branch target (read and dropped; an odd target is an address error, as in the looping case), then the two fetches after the displacement word: 14(3/0); (b) n2, 4 more internal clocks and the two fetches: 14 cycles with 2 reads, against the table's 3; (c) the target fetch without the address error.
- Choice: (a). Dn.w is decremented first; when it becomes -1 the branch target's first word is fetched as in the taken case, dropped, and the queue is refilled from the instruction after DBcc.
- Reason: UM table 8-9 gives 14(3/0) but not which three reads; the suite has no expired case (its 8065 DBcc cases have random counts: 4096 cc true, 2005 looping, 1964 looping to an odd target), so it is silent too. (a) is the only reading with the table's 3 reads that reuses the looping path's own first fetch.
- Evidence: UM table 8-9; suite DBcc (no expired case); the looping path "59c8 [DBcc D0, #] 1" (n2, two fetches at the target) and "53c8 [DBcc D0, #] 28" (n2, fault at the odd target with D0 already decremented).
- Confidence: Medium-Low for the third read's address and the odd-target fault (no suite case; assembled code never has an odd DBcc displacement); High for the 14 cycles.
- Status: `// DECISION M21` at `Cpu::opDbcc` (`src/m68k/m68k_ops_flow.cpp`, Task 6). Test: m68k_branch_cycles.

## M22: Prefetch queue of a stopped CPU
- Options: (a) STOP runs no bus cycle (UM table 8-12: 4(0/0)): SR is loaded from the immediate word already in IRC, `regs().pc` is the next instruction's address, and IR/IRC keep the STOP opcode and its immediate until the exception that ends the STOP refills them; (b) refill the queue from the next instruction (8 more cycles, 2 reads the table does not have).
- Choice: (a). The exception that ends the STOP (trace, and since Task 7 interrupt; a reset reloads everything) stacks `regs().pc` and refills the queue from its vector, so the stale words are never executed. Privileged: in user mode STOP takes the privilege violation and does not stop. With T = 1 at its start the trace exception follows as the next step and resumes execution (PRM STOP).
- Reason: the manuals give the timing and the PC but say nothing about the queue; the suite has no STOP file.
- Evidence: PRM STOP; UM table 8-12; UM 6.3.8.
- Confidence: Medium (only `regs().prefetch` of a stopped CPU can show it).
- Status: `// DECISION M22` at `Cpu::opStop` (`src/m68k/m68k_ops_flow.cpp`, Task 6). Test: m68k_stop_loads_sr_and_stops.

## M23: Bus reads of RTR and MOVE to CCR
- Options: (a) the suite's bus cycles: RTR is three stack reads and two fetches (5 reads), MOVE to CCR the source operand, 4 internal clocks and two fetches (2 reads besides the operand); (b) UM table 8-12's read counts: RTR 20(2/0), MOVE to CCR 12(1/0).
- Choice: (a). The cycle totals are the UM's either way (20, and 12 + <ea>).
- Reason (ruling R8): table 8-12 contradicts itself on these rows: RTR pops three words (CCR and a long PC) and must refill the queue, which no 2-read sequence can do, and RTE, which pops the same frame, is 20(5/0); MOVE to CCR does what MOVE to SR does (12(2/0)) with a byte of the same word. So the suite decides.
- Evidence: suite RTR "4e77 [RTR] 1" (reads SP + 2, SP, SP + 4, then the target and target + 2), MOVEtoCCR "44c0 [MOVEtoCCR D0] 131" (n4, two fetches) and "44d0 [MOVEtoCCR (A0)] 30"; RTR and MOVEtoCCR pass 8065/8065. The read count shows only in which program words are fetched (the queue contents) and in an address error's frame.
- Confidence: High.
- Status: `// DECISION M23` at `Cpu::opReturnFrame` and `Cpu::opMoveToSr` (`src/m68k/m68k_ops_flow.cpp`, Task 6).

## M24: Interrupt acknowledge result outside 0..255
- Options: (a) any answer of `Bus::interruptAcknowledge` other than a vector 0..255 or `kAutovector` is the spurious interrupt (vector 24); (b) mask it to 8 bits; (c) treat it as a bus error.
- Choice: (a). `kSpurious` (-2) is the documented spurious answer; an out-of-range number (negative or above 255) is a device-side error, which the 68000 handles as a failed acknowledge, that is the spurious vector (UM 6.3.4). (b) would alias a vector the device did not name, and (c) is not modelled (M5).
- Evidence: UM 6.3.4 (a failed acknowledge goes to the spurious interrupt vector).
- Confidence: Medium (an interface convention; no KS-32 device answers out of range).
- Status: `// DECISION M24` at `Cpu::interrupt` (`src/m68k/m68k_cpu.cpp`) and on `Bus::interruptAcknowledge` (`src/m68k/m68k.h`, Task 7). Test: m68k_vectored_and_spurious.

## DUART

Evidence cites the Motorola MC68681 data sheet (MC68681/D, Sep 1985, `datasheets/MC68681_DUART.pdf`, git-ignored) by paragraph and PDF page.

## D1: Serial timing
- Options: (a) bit-level TxD/RxD lines with start-bit detection, break and error detection; (b) character level: a character is a count of 16x baud ticks.
- Choice: (b). A character takes 1 start + data bits (MR1[1:0], 5 to 8) + a parity bit if MR1[4:3] is not 10 + the stop length of MR2[3:0] bit times. It is counted in 16x ticks so the sixteenth-bit stop lengths of table 4-5 are exact (6 to 8 bits: codes 0 to 7 give 9 to 16 sixteenths, 8 to F give 25 to 32; 5 bits: codes 0 to F give 17 to 32). The receiver uses the same length. A transmitted character finishes at the end of its last stop bit; a received one reaches the FIFO at the same point. Data is masked to the programmed length. There is no break detection and no receive parity or framing error: SR[7:5], ISR[2] and ISR[6] never set.
- Evidence: 3.1, 3.2 (character format; PDF 22, 23); 4.2.1.4, 4.2.1.6, 4.2.2.4 and table 4-5 (length, parity, stop codes; PDF 37, 39, 32); 4.2.9.5, 4.2.9.6 (TxEMT sets after the last stop bit, TxRDY sets when THR moves to the shift register; PDF 43). 9600 baud, 8N1 is 10 bits x 16 x 24 = 3840 X1 clocks.
- Confidence: High for the lengths; Medium for the receiver's finishing point (the real receiver samples the centre of the first stop bit, half a bit earlier).
- Status: `// DECISION D1` at `Duart::charTicks` (`src/duart/duart.cpp`, Task 1). Tests: duart_tx_character_timing, duart_char_length_parity_and_stop_bits, duart_rx_one_char_per_char_time.

## D2: Receive path
- Options: (a) receive() puts a byte straight into the FIFO; (b) bytes wait in a queue and enter the receiver one per character time at the channel's receive rate.
- Choice: (b). A byte given to receive() while the receiver is enabled joins a queue standing for the wire; the receiver takes them back to back, each finishing one character time (D1) after it starts, at the rate the channel's CSR (or the C/T) gives. A byte given while the receiver is disabled is lost, and so are the queued bytes and the character in progress when a disable or reset-receiver command arrives; characters already in the FIFO stay readable. The FIFO is 3 deep plus the shift register: a character finished with the FIFO full waits in the shift register; the start bit of the next character overruns it (the waiting character is lost, OE sets, `overruns()` counts), and a read of RHR moves a waiting character into the FIFO. OE stays until the reset-error-status command; reset-receiver leaves it alone (4.2.7.2: "All other registers are unaltered"). A receiver with no clock (an external IP clock pin with no clock wired, or a C/T that is not in a timer mode from X1 or X1/16) takes nothing.
- Evidence: 3.2 (FIFO, shift register, "held in the receive shift register until a FIFO position is available", overrun on the start bit of the next character; PDF 23, 24); 4.2.9.4 (OE; PDF 43); 4.2.7.2 and 4.2.7.4 (reset receiver, disable receiver; PDF 41, 42); 4.2.9.7, 4.2.9.8 and 4.2.15.3 (FFULL and RxRDY semantics; MR1[6] selects which drives ISR[1] and ISR[5]; PDF 43, 47).
- Confidence: High for the FIFO and overrun rules (the data sheet states them); Medium for the queue (a host convention the data sheet cannot give).
- Status: `// DECISION D2` at `Duart::startRx` (`src/duart/duart.cpp`, Task 1). Tests: duart_rx_fifo_and_overrun, duart_reset_receiver_keeps_overrun, duart_rx_disabled_drops_bytes, duart_rx_ready_interrupt_and_ffull_select.

## D3: Unmodelled features
- Options: (a) model them; (b) leave them out and count the writes that select them.
- Choice: (b). A register write that selects one of these increments `unsupportedWrites()` once and otherwise stores the value: automatic echo, local and remote loopback (MR2[7:6] not 00); RTS/CTS control (MR1[7], MR2[5], MR2[4]); multidrop (MR1[4:3] = 11, timed like a parity bit); the break commands start break and stop break (CR[6:4] = 110, 111; 101, "reset break-change interrupt", is supported: it clears ISR[2]/ISR[6], which never set); 1x external clocks (a CSR nibble 1111, which leaves that direction unclocked); a 16x external clock (CSR nibble 1110) on a pin with no clock wired; C/T clock sources other than X1 and X1/16 (ACR[6:4] = 001, 010, 100 or 101; 000, the reset value, is not counted, and the C/T does not count from it); any non-zero OPCR (OP7..OP0 stay the complement of OPR; the TxRDY, RxRDY, clock and C/T outputs are not driven). The C/T as a baud clock is supported in the timer modes from X1 and X1/16. External 16x clocks are modelled (amended in Plan 6 Task 3, because the KS-32 firmware writes CSRA = CSRB = 0xEE): `setExternalClock(pin, hz)` wires a rate to an IP pin, which reset() keeps; CSR code 1110 then ticks every X1 / hz X1 clocks (exact when hz divides X1, as 4 MHz / 500 kHz = 8 does). Pins per table 4-2: TxCA = IP3, RxCA = IP4, TxCB = IP5, RxCB = IP2. Also left out: the 25 to 50 microsecond filter on input-port changes.
- Evidence: 3.3, 3.4 (echo, loopback, multidrop; PDF 25, 26); 4.2.2.1 to 4.2.2.3 (PDF 37 to 39); 4.2.7.2 (break commands; PDF 41); 4.2.11, 4.2.14 and tables 4-2, 4-3, 4-4 (PDF 44, 46, 29, 30, 31).
- Confidence: Medium. They are features the KS-32's MIDI and panel-link use is not expected to need; the later tasks that run the ROM show whether `unsupportedWrites()` stays 0.
- Status: `// DECISION D3` at `Duart::writeMode`, `Duart::tick16` and the counting sites in `Duart::write` and `Duart::command` (`src/duart/duart.cpp`, Task 1; external 16x clocks and command 101 in Plan 6 Task 3). Tests: duart_unsupported_mode_counted, duart_external_16x_clock.

## D4: Reset state
- Options: (a) only what the data sheet lists; (b) also clear the registers it does not mention.
- Choice: (b), with the sheet's listed values taken verbatim: SRA, SRB, IMR, ISR, OPR and OPCR clear, IVR = 0x0F, OP7..OP0 pins high (the pin is the complement of OPR, so OPR = 0), channels inactive and TxD marking (transmitter and receiver disabled, so TxRDY = 0). MR1/MR2 (pointer at MR1), CSR, ACR, CTUR/CTLR and the counter also clear; the data sheet is silent on them. The sheet also says the C/T is in timer mode after reset (2.4, 3.5), but ACR = 0 encodes counter mode from IP2 (table 4-4): the model follows the ACR bits, so the C/T does nothing until ACR selects a mode (a counter also waits for its start command, D5; a timer runs, D6). The input pins are external: `reset()` keeps their levels, and they start high (unconnected TTL inputs).
- Evidence: 2.4 (PDF 17); 4.2.15 ("ISR initialized to 00"; PDF 46); 4.2.19 (IVR = 0F; PDF 48); 3.5 (timer mode after reset; PDF 26) against table 4-4 (ACR[6:4] = 000 is counter mode; PDF 31); 4.1 (mode register pointer set to MR1 by RESET; PDF 31).
- Confidence: High for the listed values; Medium for the unlisted ones.
- Status: `// DECISION D4` at `Duart::reset` (`src/duart/duart.cpp`, Task 1). Test: duart_reset_state.

## D5: Time base
- Options: (a) advance() in baud ticks; (b) in whole X1 clocks, with the C/T's X1/16 prescaler counted exactly.
- Choice: (b). `advance(n)` counts n X1 clocks. The C/T keeps the X1 clocks already spent on the current count (`ctPhase_`), so one call of n clocks and n calls of one clock count alike; a start command restarts the phase (in either mode). Counter mode, which counts only after a start command and stops when ACR leaves counter mode (D6): terminal count `preload` counts after the start, ISR[3] sets, the count rolls over from 0 to 0xFFFF and runs on; the stop command clears ISR[3] and stops it. Timer mode: terminal count every `preload` counts (square wave period 2 x preload); ISR[3] sets on every second terminal count (the sheet's "every other time"), counted from the start command or, without one, from D6's phase, so with preload 100 from X1 it sets 200 clocks after the start; the stop command clears ISR[3] but the timer runs on; a new preload is picked up at the terminal count; the minimum preload is 2. Baud rates use the fixed integer X1 divisors of table 4-6's 3.6864 MHz column: 9600 baud is 24 X1 clocks per 16x tick at any X1 (the rates scale with X1); the C/T as baud source gives 2 x preload x (1 or 16) X1 clocks per 16x tick.
- Evidence: 3.5.1 and 3.5.2 (PDF 26, 27); 4.2.15.5 (PDF 47); table 4-4 (PDF 31); tables 4-5 and 4-6 (PDF 33, 45: the 110, 134.5 and 1050 baud 16x clocks of 1.759, 2.153 and 16.756 kHz give the divisors 2096, 1712 and 220, and 2000 baud at 32.056 kHz gives 115).
- Confidence: High for the periods; Medium for the exact phase of a first terminal count on real hardware (the prescaler is free-running there; a start command is assumed to align it).
- Status: `// DECISION D5` at `Duart::advanceCounter` (`src/duart/duart.cpp`, Task 1). Tests: duart_counter_mode_ready, duart_counter_prescaler_exact_across_calls, duart_counter_reads_and_rollover, duart_timer_mode_period, duart_timer_new_preload_at_terminal_count, duart_rx_bit_clocks, duart_baud_table.

## D6: Free-running timer phase
- Options: (a) the timer runs only after a start command; (b) it runs from the moment ACR selects a timer mode, counting from the preload; (c) it runs from reset with an unknown count.
- Choice: (b). The sheet says the timer "runs continuously and cannot be started or stopped by the CPU" (3.5, 3.5.2) and gives no phase for a timer that has not been started; a start command (read of reg 14) reloads the preload, restarts the phase and the terminal-count parity (D5). Without one, a write that changes ACR[6:4] clears the count; the next advance() loads the preload then (so a preload written after ACR, before the first advance, counts), and ISR[3] sets at the second terminal count, 2 x preload source clocks later. The same write drops the counter run state, so a counter left and re-entered must be started again. Only counter mode needs the start command.
- Evidence: 3.5 and 3.5.2 (PDF 26, 27: "runs continuously", the start command "terminates the current countdown sequence ... reinitializes itself"); 4.2.13.2 (ACR[6:4]; PDF 46). The sheet's advice to issue the start command before routing the timer to OP3 (3.5.2, PDF 27; OPCR[3:2] note, PDF 44) is about configuration order and does not make the start a precondition for running.
- Confidence: Medium (the real phase of an unstarted timer depends on its prescaler and the count at power-up; any ROM that starts the timer explicitly, as the sheet recommends, is unaffected).
- Status: `// DECISION D6` at the ACR write in `Duart::write` (`src/duart/duart.cpp`, Task 1 fix round). Tests: duart_timer_runs_without_start_command, duart_leaving_counter_mode_stops_it.

## D7: Disabling the transmitter with a character in THR
- Options: (a) a disable lets only the shift-register character finish and drops one waiting in THR; (b) it lets both finish.
- Choice: (a). The disable-transmitter command resets TxRDY, TxEMT and THR (the loaded character is lost), and a character already in the shift register completes. The reset-transmitter command drops both.
- Evidence: the sheet is ambiguous. For (a): 3.1 says the RTS output negates "one bit time after the character in the transmit shift register is completely transmitted" (PDF 23), and 4.2.7.3 (CRA[3:2] = 10) says "if a character is being transmitted when the transmitter is disabled, the transmission of the character is completed before assuming the inactive state" (PDF 41). For (b): 4.2.2.2, the MR2A[5] text, says OPR[0] clears "one bit time after the characters in the channel A transmit shift register and in the transmit holding register, if any, are completely transmitted" (PDF 38), which implies a THR character is sent; but that text describes the automatic-RTS mode, not the command. 4.2.9.5 (PDF 43) says TxEMT clears when the transmitter is disabled.
- Confidence: Low to Medium (the two passages cannot both be taken as a statement about the command).
- Status: `// DECISION D7` at the disable-transmitter branch of `Duart::command` (`src/duart/duart.cpp`, Plan 6 final fix wave). No test pins it.

## A1: MIDI-in bound
- Options: (a) an unbounded receive queue (Plan 6's deque), which allocates on the audio thread and grows without limit under a flood; (b) a fixed queue per channel that drops bytes beyond it, newest first, and counts them.
- Choice: (b), 1024 bytes per DUART channel (`ByteRing<1024> pending`, `src/common/ring.h`). A byte that finds the queue full is dropped and counted in `Duart::rxDropped(ch)`; the count clears on reset, like `overruns`.
- Evidence: Plan 7's real-time constraint and ruling F-1 of `docs/verification-plan6.md`. At 31 250 baud the line carries about 3 125 bytes a second, so 1024 bytes is about 330 ms of back-to-back MIDI, far more than one audio block brings. The queue stands for the MIDI cable, which has no buffer of its own, so the bound is an emulator choice, not a hardware fact.
- Confidence: High.
- Status: settled, Plan 7 Task 1. `// DECISION A1` at `Channel::pending` (`src/duart/duart.h`) and in the test. Test: duart_rx_queue_is_bounded; rt_machine_runframe_does_not_allocate (`tests/rt_main.cpp`) shows the TS machine allocates nothing after the boot.

## A2: Transmit and panel rings
- Options: (a) take-by-swap vectors (Plan 6), which allocate every slot; (b) fixed rings that drop the newest byte when full and count it.
- Choice: (b). The DUART's sent-characters ring holds 256 per channel (`Duart::popTransmitted`, `Duart::txDropped`), and the panel's bytes for the firmware 256 (`Panel::popToCpu`). `Machine::slot` drains the panel channel both ways every slot; the caller pops MIDI out with `Machine::popMidiOut` (the engine every block, Plan 4; `tsemu render` every frame, `renderMidi` in `tests/ts_render.h`).
- Evidence: a slot is 16 CPU clocks (about 1 us), and one character takes about 300 slots at 31 250 baud, so the panel rings never hold more than a few bytes; 256 bytes of MIDI out is about 80 ms of the line. The rings were copied with the cores, so the TS reference hashes in AGENTS.md (boot trace 3bfc7336098a6def; renders 4a751109deae8b89, 524fcc64af78f175, 01e3c8622cc8c8be) were measured with them in place.
- Confidence: High.
- Status: settled, Plan 7 Task 1. `// DECISION A2` at `Channel::sent` (`src/duart/duart.h`), `Panel::toCpu_` (`src/ts/panel.h`) and `Machine::slot` (`src/ts/machine.cpp`). Tests: duart_pop_transmitted_in_order, panel_pop_to_cpu, rt_machine_runframe_does_not_allocate (`tests/rt_main.cpp`).

## OTTO

## OT1: Host byte latching
- Options: one latch shared by all registers, or one per register; which byte triggers the read.
- Choice: byte 0 of a read performs the 32-bit read into one shared latch and returns bits 31:24; bytes 1-3 return the latch. Bytes 0-2 of a write go into one shared latch; a byte-3 write commits latch + byte to the register it addresses on the current page. Latches are 0 after reset and keep their bytes after a commit.
- Evidence: notes §1 (§13/p39). Whether the latches are shared is not stated.
- Confidence: High for the byte order and for reads (TS OS v3.10); medium for the shared latch, which that firmware cannot show.
- Status: implemented (Plan 2 Task 1); test otto_byte3_commits_shared_latch. Checked against TS OS v3.10 (Plan 3 Task 7, bus trace of a boot and of notes on all 60 bank programs): every OTTO access is a byte access at an odd address, never a word. Reads are always bytes 0, 1, 2, 3 in that order. Writes are bytes 0-1-2-3, bytes 2-3 (only to registers of 16 bits or fewer: CR, volumes, ramps, ECOUNT, K1, K2) or byte 3 alone (only PAGE, ACTV, MODE, W_ST, W_END, LR_END). A partial write always covers every implemented bit, so shared and per-register latches give the same result with this firmware. Byte 0 = MSB agrees with those patterns and with the boot's ACTV and PAGE writes (docs/verification-plan2.md, "Left for Plan 3").

## OT2: Pages and unused bits
- Options: PS ignored or not with TEST; what unused bits and the test page read.
- Choice: PAGE keeps 7 bits; TEST=1 selects the test page whatever PS is. Writes keep each register's implemented bits; reads return them with other bits 0. Test-page reads and EMPTY return 0. Filter temps read back their 18 bits, not sign-extended. PAR and IRQV ignore writes.
- Evidence: notes §1, §2, §9.11, §9.15.
- Confidence: Low-medium.
- Status: implemented (Plan 2 Task 1).

## OT3: Reset state
- Options: only ACTV and MODE are documented.
- Choice: ACTV 0x1F, MODE 0x17, PAGE 0, IRQV 0x80 (free), PAR 0, W_ST/W_END/LR_END 0. Each voice: CR 0x0003 (STOP1|STOP0), every other voice register and filter temp 0, FILTCOUNT 0. Channel sums, test-page values and latches 0.
- Evidence: notes §8; the rest follows KS-32 O10.
- Confidence: Low.
- Status: implemented (Plan 2 Task 1).

## OT17: Write timing
- Options: model the 3-voice pipeline that can overwrite CPU writes, or apply writes at once.
- Choice: host writes take effect at once.
- Evidence: notes §4 (§4.1.9/p10, §8/p21 advise writing twice, so firmware tolerates either).
- Confidence: Medium.
- Status: implemented (Plan 2 Task 1).

## OT18: Serial port and MODE
- Options: bit-level serial model, or whole frames.
- Choice: MODE, W_ST, W_END and LR_END are stored and read back; output is whole frames. Dual-OTTO and slave modes are not modelled.
- Evidence: notes §4, §7. The TS has one OTTO.
- Confidence: High for the TS.
- Status: implemented (Plan 2 Task 1).

## OT4: Address generation and loop modes
- Options: FC added once or twice (§11.1 steps 3 and 5); >= or > at END; DIR polarity; reverse and bidirectional arithmetic; LEI and the IRQ; the accumulator after a one-shot stop.
- Choice: only when STOP0 = STOP1 = 0. DIR=0 forward: n = acc + FC, boundary when n > END32 (strict). DIR=1: n = acc - FC, boundary when n < START32. Computed in 64 bits, stored mod 2^32. At a boundary (unless LEI): no loop: set STOP0, acc = END32 (fwd) / START32 (rev); uni loop: acc = START32 + (n - END32) / END32 - (START32 - n); bidirectional: acc = END32 - (n - END32) / START32 + (START32 - n) and DIR toggles; transwave (BLE=1, LPE=0): as uni loop, then BLE cleared and LEI set. LEI: acc = n, no boundary, no IRQ. START32 = START & 0xFFFFF800, END32 = END & 0xFFFFFF80.
- Evidence: notes §3, §9.2-9.4. The §4.1.5 text (0 = forward) wins over the poorly printed bit map; the p29 strict-compare note wins over step 5.
- Confidence: Medium.
- Status: implemented (Plan 2 Task 2); tests in tests/test_otto_addr.cpp.

## OT5: Fetch addresses
- Options: S2 at int + 1 or int - 1 in reverse; fetch before or after the update.
- Choice: S1 at acc >> 11, S2 at (acc >> 11) + 1 wrapped to 21 bits, in both directions, same bank (CR.BS1:BS0), fetched with the accumulator before this slot's update.
- Evidence: notes §3 steps 1-2.
- Confidence: Medium.
- Status: implemented (Plan 2 Task 2).

## OT14: Interrupts and IRQV
- Options: as the datasheet's §14 text, with the timing of the CR.IRQ clear left open.
- Choice: a boundary with IRQE sets CR.IRQ. At a voice's slot, if its CR.IRQ is set and IRQV is free (bit 7 = 1), IRQV = voice number with bit 7 = 0 and the pin asserts. A byte-0 read of IRQV with bit 7 = 0 returns it, sets bit 7, releases the pin and marks the voice; its CR.IRQ is cleared at the start of its next slot (so a new request later in that slot is kept). A read with bit 7 = 1 has no effect. A host write of CR with IRQE|IRQ forces a request. IRQV reads bits 4:0 and bit 7, others 0.
- Evidence: notes §5.
- Confidence: Medium.
- Status: implemented (Plan 2 Task 2); tests otto_irq_sequence, otto_forced_irq, otto_irqv_read_without_pending_is_harmless.

## OT6: Compressed sample decoder
- Options: the datasheet gives only the encoder.
- Choice: e = word >> 13, m = word & 0x1FFF. e >= 1: v = (int16)(((m >> 12 ^ 1) << 15) | (m << 2)) >> (7 - e). e = 0: v = (int16)(m << 3) >> 7. Unused low bits are whatever the memory returns (the board forces them to 0).
- Evidence: notes §3; reproduces the p31 examples (7FFF->FF, 8000->E0, FFF0->1F, 00FF->0F) and re-encodes to the same code for all 65536 words.
- Confidence: Medium.
- Status: implemented (Plan 2 Task 3); tests otto_decompress_examples, otto_decompress_roundtrip.

## OT8: Interpolation
- Options: which 9 of the 11 fraction bits; rounding; output units.
- Choice: f9 = ACCUM bits 10:2 (F10..F2). otto_interp = (s1 << 1) + (((s2 - s1) * f9) >> 8), rounding down; the result is in half-LSB units (one fraction bit, as the filter registers' FDF0).
- Evidence: notes §3, §9.5.
- Confidence: Medium.
- Status: implemented (Plan 2 Task 3).

## OT9: Filter arithmetic
- Options: K scaling; the HP pole's feedback: the datasheet's "+0.5" read literally (K/65536 + 0.5, above 1 for K above 0x7FF0), K/65536 with no 0.5, K read as signed, or 0.5 + K/131072; saturation.
- Choice: k12 = K >> 4. LP: sat18(y1 + ((k12 * (x - y1)) >> 12)). HP: sat18(x - x1 + (((k12 + 4096) * y1) >> 13)): feedback 0.5 + K/131072, from 0.5 at K = 0 to just under 1 at K = 0xFFF0. sat18 clamps to [-131072, 131071]; floor rounding (a decaying HP can rest at -1).
- Evidence: notes §3, §9.7 (HP `Yn = Xn - Xn-1 + K*Yn-1`, `K_H = 65535*{cosW - 0.5 - ...}`: the 0.5 "caps one HP pole near 8 kHz"). TS OS v3.10 (Plan 3 Task 7) writes K2 up to 0xFF00 on sounding voices whose CR selects a high-pass pole (LP4:LP3 = 00 or 01). With the literal reading (Plan 2's choice) the feedback reaches 1.5 and pole 4's output sat on the 18-bit rail (+-131071) in 20 of the 60 bank programs, one note each; with 0.5 + K/131072 none does (largest |O4| on an HP voice 67 825; LP-only voices stay within +-65 491). So the firmware uses the whole K range for HP poles. 0.5 + K/131072 is the one reading that keeps the datasheet's 0.5 floor and is stable over that range; K/65536 and a signed K both lose the floor.
- Confidence: LP medium; HP medium-low (stable and consistent with the firmware's K range; the exact scale is not heard yet).
- Status: implemented in `otto_hp` (Plan 2 Task 3; HP changed in Plan 3 Task 7, controller ruling). Tests: otto_hp_values, ts_hp_voices_stay_off_the_rail. User to run: listen to `tsemu render tests/data/filtered_patch.mid` (WOW-SWEEP) and program 8 (HONKY-TONK) for a high-pass layer that sounds right.

## OT10: Filter routing
- Options: the p9 table and the p34 diagram disagree on pole 3's coefficient in modes 10 and 11.
- Choice: the p9 table: pole 3 = 00 HP K2, 01 LP K1, 10 LP K2, 11 LP K1; pole 4 LP K2 (LP4=1) or HP K2. Poles 1-2 LP K1. HP poles take Xn-1 from O2(n-2) (pole 3) and O3(n-2) (pole 4), which hold the previous output of the pole before.
- Evidence: notes §3; the ES5505 datasheet's table is identical to p9.
- Confidence: Medium.
- Status: implemented (Plan 2 Task 3); test otto_filter_mode_routing. TS OS v3.10 (Plan 3 Task 7) uses mode 11 for nearly every voice (6986 of 7124 CR writes in a run of notes on all 60 bank programs) and mode 10 in 12 of the 60 programs; on sounding voices K1 and K2 differ (12 MSBs) in 89 % of mode-11 and 86 % of mode-10 samples, so the table-versus-drawing choice changes most sounds. Not settled by the firmware; user to run: listen to `tsemu render tests/data/filtered_patch.mid` (WOW-SWEEP) and other sounds.

## OT11: Filter output to the pan stage
- Options: which 16 of the 18 bits.
- Choice: x16 = sat16(O4 >> 1): the integer part, dropping the fraction bit.
- Evidence: notes §3, §9.7.
- Confidence: Medium.
- Status: implemented (Plan 2 Task 3).

## OT7: Stopped voices
- Options: a stopped voice is fully idle; or it fetches and filters but is muted.
- Choice: STOP0 or STOP1: the voice still fetches, interpolates and filters (its filter temps keep updating), adds nothing to its channel, and its envelope, address and loop/IRQ check are frozen. FILTCOUNT still advances.
- Evidence: notes §3 (§4.6, §4.8 force volume 0); fetching and filtering on a stopped voice are unstated (§9.6). Kept as the KS-32 OTIS did, whose firmware read a stopped voice's O1(n-1) (KS-32 O7).
- Confidence: Low, and not observable with TS OS v3.10.
- Status: implemented (Plan 2 Task 4). Checked against TS OS v3.10 (Plan 3 Task 7): the boot reads no filter temp; a run of notes on all 60 bank programs and a 32-note chord reads only O1(n-1), 26 times, always of a running voice. The firmware writes the six temps as 0 (1272 writes, none non-zero), so it does not depend on what a stopped voice's filter does, unlike the KS-32's.

## OT12: Volume
- Options: rounding; what exponent 0 does.
- Choice: otto_vol(x16, vol) = ((x16 * (0x100 | M)) >> 5) >> (15 - EX), arithmetic shifts, M = vol bits 11:4, EX = bits 15:12. Exponent 0 is a shift of 15, not a mute.
- Evidence: notes §3 (§11.6/p36).
- Confidence: Medium; the exponent-0 case is inaudible either way.
- Status: implemented (Plan 2 Task 4). TS OS v3.10 (Plan 3 Task 7) writes LVOL/RVOL with exponent 0 and a non-zero mantissa 18 times in 4894 volume writes of a run of notes on all 60 bank programs, none at boot. A shift of 15 leaves at most 16 LSB of a full-scale voice (below -66 dBFS), so mute versus shift cannot be heard.

## OT13: Channels and saturation
- Options: 23-bit accumulators that wrap, or sums that saturate; CA 6/7; TEST mode.
- Choice: CA 0-5 add L/R into that channel; CA 6/7 add nothing. Sums are kept in 32 bits and saturated to 20 bits ([-524288, 524287]) into the frame at the end of the frame, then cleared. With PAGE.TEST set the frame carries the host-written CH values (23-bit, sign-extended, saturated to 20 bits) and voice sums are dropped.
- Evidence: notes §3, §9.10, §9.15. The datasheet's 3 guard bits allow 8 full-scale voices; past that a listener expects clipping, not wrap noise.
- Confidence: Low (saturating sums), medium (the rest).
- Status: implemented (Plan 2 Task 4); test otto_channel_saturates_not_wraps. Plan 3 Task 7: the TS reaches the limit. `tests/data/full_chord.mid` (16 notes at velocity 127 on BABY-GRAND) drives OTTO channel 0 to the 20-bit limit in 123 samples, where wrapping sums would give full-scale steps; Main peaks at 32767. User to run: listen to `tsemu render tests/data/full_chord.mid` for clipping that does not belong.

## OT15: Envelopes
- Options: ramp alignment; FILTCOUNT width and scope; the K2 line testing K1SLOW; stopped voices.
- Choice: per voice, while running: if ECOUNT != 0, LVOL and RVOL ramp; K1 ramps unless K1RAMP bit 0 (SLOW) is set and FILTCOUNT != 0; K2 likewise with K2RAMP's own SLOW; ECOUNT -= 1. FILTCOUNT is 3 bits per voice, advanced every slot. otto_ramp(v, r8) = clamp(v + (int8)r8, 0, 0xFFFF): the ramp adds at the register's LSB.
- Evidence: notes §3 (§11.5, §11.7), §9.8.
- Confidence: Medium-low.
- Status: implemented (Plan 2 Task 4); tests otto_volume_ramp, otto_filter_ramp_slow.

## OT19: Fewer than five voices
- Options: refuse, clamp to 5, or run ACTV+1 slots.
- Choice: ACTV 0-3 is allowed and runs ACTV+1 slots per frame.
- Evidence: notes §4 says at least 5 voices (for the serial port).
- Confidence: Medium.
- Status: implemented (Plan 2 Task 4); test otto_actv_below_4_still_produces_frames.

## OT16: Pot A/D
- Options: conversion started by the PAR read, or by a free-running 2^13-CLK clock; when the value lands.
- Choice: a byte-0 read of PAR returns PAR, sets it to 0x3FF and starts a conversion; 512 slots later PAR = the pin's value at that moment (setPot). A read during a conversion restarts it.
- Evidence: notes §6 (512 us at 16 MHz = 512 slots of 16 CLKs, so the same in slots at any clock); §9.13 conflicts.
- Confidence: Medium-high: the TS firmware's timing rules out a sample taken at the read; where between about 128 and 1024 slots after it the pin is sampled is not pinned.
- Status: implemented (Plan 2 Task 5); tests in tests/test_otto_pot.cpp. TS OS v3.10 (Plan 3 Task 7): the OS reads PAR every 1024-8191 slots (about 2 ms) and changes the mux select (DUART OP2-OP0) within 127 slots after a read in 24 388 of 24 398 changes (the rest within 511, or before the first read). So each conversion a read starts ends on the newly selected input, and the next read returns it: the Analog Test page pairs every reading with its own input (ts_analog_resting_values). A conversion that sampled the pin when it starts would pair each reading with the input before.

## OT20: Host transfers to sound memory
- Options: model the chip's host-to-memory slots, or leave sound memory to the board.
- Choice: not in the core. The TS machine owns sound memory and the CPU's path to it (Plan 3).
- Evidence: notes §1: OTTO has no DMA address register; the address comes through external latches.
- Confidence: High.
- Status: no code (by design).

## TS machine

## T2: OS ROM image
- Options: (a) even bytes from h, odd bytes from the first 256 KB of l, a 512 KB image; (b) treat the l upper half as mapped (A18 tied high, as the schematic shows).
- Choice: (a). A 256 KB l file (v1.20, v2.02) is used whole.
- Evidence: in every 512 KB l dump the reset vector (SSP 0xFF80A0, PC 0xC0073E) and all version differences are in the lower half; the upper half is identical in TS-10 3.05/3.10 and TS-12 3.05/3.10 and 69 % 0xFF. The dumps win over the schematic.
- Confidence: Medium-high.
- Status: Implemented in `ens::ts::buildOsImage` (Plan 3 Task 1). Tests: ts_os_image_interleaves_lower_half, ts_roms_wrong_size_named.

## T3: Wave memory layout
- Options: bank 0 and bank 1 layouts from the schematic; word byte order of U45/U46 either way.
- Choice: bank 0 word = `u41[a] << 8 | nib << 4`, `nib = (a & 0x100000) ? u43[a & 0xFFFFF] >> 4 : u43[a & 0xFFFFF] & 0xF`, `a` = 21-bit address. Bank 1: `a < 0x100000` is U45 word `a`; `0x100000 <= a < 0x180000` is U46 word `a - 0x100000`; above is 0 (empty socket). U45/U46 words are read with the first file byte as the high byte.
- Evidence: schematic sheet 2 for the layout. Byte order measured on the files (mean |s[i+1]-s[i]| over 16-bit signed words): U45 first-byte-high 1387.7, first-byte-low 21351.7; U46 first-byte-high 1431.8, first-byte-low 21464.8 (factor 15). Test: ts_word_rom_byte_order.
- Confidence: Medium (layout), high (byte order).
- Status: Implemented in `ens::ts::WaveMemory` (Plan 3 Task 1). Tests: ts_bank0_builds_12_bit_samples, ts_bank1_regions.

## T4: Sample RAM
- Options: 2 MB (one 1M x 8 pair), 4 MB, none.
- Choice: 2 MB: OTTO bank 3 words 0-0xFFFFF (BankSet S8); the rest of bank 3 and all of bank 2 (S9's sockets) read 0 and ignore writes (empty sockets). The CPU sees bank 3 at 0x400000-0x7FFFFF and bank 2 at 0x800000-0xBFFFFF, word A21..A1 (CPU A22 = BS0); a byte access reads or writes one half of the word. `WaveMemory::sampleRam` wraps its index; the machine bounds it first, so an empty socket never aliases the installed pair.
- Evidence: design section 4; service manual SIMM text; the OS's socket probe at boot (words written at 0x400000, 0x600000, 0x800000, 0xA00000 and read back: steps of one 1M x 8 pair). None, 2 MB, 2 MB aliased over the window, and empty sockets reading 0xFFFF all boot with the same event sequence and no "WRONG SOCKETS" text (docs/ts-hardware-interface.md, "Memory map"). Plan 5 Task 4 (the OS's own use of the bank): the OS's socket probe (c14f7a) stores 0x400000 as S8's base and 0x800000 as S9's. A sampled sound loaded into S8 from disk is written at 0x400000 + 2 w (ED-001's FLUTE 1: w = 0xE7E04-0xFFFFF); played, its voices get CR BS1:BS0 = 11 and START/END inside those words. Bank 2 was the earlier choice (Plan 3, untested by any sound): the sound then loads but plays silence. The change moves one sample of the player_smoke render by 1 LSB (hash 4a751109deae8b89 -> 3374892bfdc150e6): the OS parks voice 0 on bank 3 words 0-0x20000 (CR 0xC308), which now reads the memory manager's header the OS writes at S8's start (c14ea8, words 4-5 = 0x4010 0x1F60).
- Confidence: High for S8 = bank 3 at 0x400000 (the OS's own use); medium for the window base; S9 = bank 2 at 0x800000 follows (not testable without 4M x 8 SIMMs); low for the installed size and empty-socket reads (the boot cannot tell).
- Status: Storage in `WaveMemory::sampleRam` (Plan 3 Task 1); window in `Machine::hostWord` and `Machine::write8` (Plan 3 Task 4); bank 3 since Plan 5 Task 4. Tests: ts_sample_ram_is_otto_bank3, ts_ram_alias_and_sample_window, ts_loads_asr10_instrument, ts_loads_eps_instrument.

## T11: Audio routing
- Options: how the 20-bit OTTO values enter the 16-bit ESP ports; which channels feed what; ESP instructions per frame.
- Choice: OTTO ch0-2 to ESP SER0, SER2, SER3 as `sat16(v20 >> 4)`. Main L/R = ESP SER1. Aux L/R = OTTO ch3 as `sat16(v20 >> 4)`. ch4/5 dropped. `Esp::advance(103)` per frame (9 216 000 / 3 / 29 761.9 = 103.2, E30's 3 clocks per instruction); 0 while the ESP is halted.
- Evidence: schematic sheets 1 and 3 (SER0-2 re-clocked to ESP SER0, SER2, SER3; SER3 to the analog board).
- Confidence: Bit alignment medium-low (check levels by ear in Plan 4).
- Status: implemented in `ens::ts::Board::route` (Plan 3 Task 2). Tests: tests/test_ts_board.cpp.

## T1: Clocks and scheduling
- Options: the CPU/OTTO crystal (the schematic's digit is unreadable); how the CPU, OTTO, DUART and ESP share time.
- Choice: CPU = OTTO = 15 238 090 Hz (30.47618 MHz / 2, the KS-32's crystal), so a 32-voice frame is 512 CPU clocks (29 761.9 Hz). DUART X1 4 MHz; 16x serial clocks 500 kHz on IP3/IP4 (channel A, 31 250 baud) and 1 MHz on IP5/IP6 (channel B, 62 500 baud). The board's 2681 takes RxCB from IP6, the core (68681) from IP2, so the 1 MHz RxCB clock is set on the core's IP2 clock input; the IP2 level (DENS) stays a separate input. ESP 9 216 000 Hz (103 instructions per frame, T11). Per voice slot the CPU runs 16 clocks less the carried overshoot (a carry of 16 or more skips the CPU), then `Otto::stepVoice`, then the DUART by the exact rational accumulator `acc += 16 * 4 000 000; x1 = acc / 15 238 090`. `Machine::cycles()` is the raw CPU total (the time base of `BusEvent.cycle`): 40 reset clocks + 16 per slot, plus up to one instruction's overshoot still carried.
- Evidence: schematic sheet 1 (crystals); notes §4 (16 CLKs per voice); the KS-32 scheduler. Task 4: the OS writes CSRA = CSRB = 0xEE; without a clock on channel B it never sends (it polls TxRDYB). Its boot clock check (a 10 ms DUART timer, then a read of a running OTTO voice's ACCUM) passes; the boot is unchanged for X1 rates from 3.7 to 4.7 MHz relative to the CPU and stalls at 3.4 MHz, so the check pins the CPU clock only loosely.
- Confidence: Low-medium for the CPU clock (the firmware's check is loose); medium for the serial clocks; the scheduling is exact.
- Status: implemented in `ens::ts::Machine::slot` and `Machine::create` (Plan 3 Tasks 3-4). Test: ts_frame_cadence.

## T7: DUART wiring
- Options: which DUART pins carry which board signal.
- Choice: channel A = MIDI (`kMidiChannel` 0), channel B = the display/keyboard link (`kPanelChannel` 1). OP0-2 select the analog mux input on the OTTO POT pin (the mux switches at once; the OTTO conversion, OT16, samples it when it ends); OP6 is ESPHALT, active high (the ESP's HALT pin is high while OPR bit 6 is 0, i.e. `outputPins()` bit 6 set). IP0-5 rest high. OP3 is SEQLED/, the Seqs/Songs LED (T15); OP4 KRES, OP5 DRES and OP7 SACK/ go to the panel (T19).
- Evidence: schematic sheet 1 (DUART U10 pin list); the core's `outputPins()` returns the pin level, the inverse of OPR.
- Confidence: High for the signals. Medium for the OP6 polarity: with it the OS halts the ESP at 1.3 ms, downloads to it while halted and releases it at 1819 ms (halted again from 3301 to 3376 ms, then running; Task 6 measurement); the reverse polarity runs the ESP during the downloads and leaves it halted after the boot (Task 4).
- Status: implemented in `Machine::updateLines` (Plan 3 Task 3).

## T14: RESET instruction
- Options: reset all devices, or only those on BRES/.
- Choice: the OTTO only. The DUART and FDC are on the power-on-only BRES and are untouched.
- Evidence: schematic sheet 1.
- Confidence: Medium.
- Status: implemented in `Machine::resetPeripherals` (Plan 3 Task 3).

## T13: Address map and RAM
- Options: RAM size (128 KB, 256 KB, 512 KB) and windows; whether the top window (reset SSP 0xFF80A0) is the RAM at 0 or a separate RAM; the reset overlay; where the I/O1 and I/O2 devices sit; the ROM mirror. SEQEXP (U1/U2, the SQX-70 kit): fitted or not, and where. The battery image's layout. What a power cycle keeps.
- Choice: OSRAM (U3/U4) 256 KB: 0x000000-0x037FFF, and its top 32 KB again at 0xFF8000-0xFFFFFF (the window 0xFC0000-0xFFFFFF aliases all 256 KB; only 0xFF8000 up is seen in use). SEQEXP (U1/U2) 256 KB at 0x038000-0x077FFF; nothing above it. **Default: a stock TS-10, SEQEXP not fitted** (design section 4 lists the SQX-70 as a non-goal): SEQEXP/ is decoded but its sockets are empty, so 0x038000-0x077FFF reads 0 and ignores writes, untraced (as T4's empty sample-RAM sockets), the OS's probe fails and it offers the stock 31 775 events. `Machine::create(roms, err, sqx70 = true)` (`tsemu --sqx70`) fits the expander: 97 311 events. Reads of 0-7 come from the ROM until the first write into 0-7 (the KS-32 K1 rule). I/O1 at 0x200000 + 0x40000 n (n = 0 DUART, 1 SCSI, 2 FDC, 3 IEXP), I/O2 at 0x300000 (OTTO) and 0x380000 (ESP); each device decoded over its whole block. ROM 0xC00000-0xC7FFFF, mirrored to 0xCFFFFF. The battery image (`Machine::ramImage`, 512 KB, the same layout with or without the expander, so the file format does not depend on the option) is OSRAM in CPU order (0xFF8000 at offset 0x38000) then SEQEXP in CPU order (0x038000 at offset 0x40000); on a stock TS the SEQEXP half is unused. `loadRamImage` is a power cycle with the battery: the RAM is replaced, the sample DRAM (T4) and the ESP delay DRAM (no battery) are cleared, the CPU and every device restart, the analog and IEXP inputs keep their values; a wrong size fails and changes nothing.
- Evidence: the OS's first accesses land in exactly these blocks, in the schematic's U7 decoder order, with 0 unmapped accesses in the boot. 512 KB at 0 with the top aliasing offset 0 crashes at 203 ms ("SYSTEM ERROR 12"). SEQEXP (Task 8): the boot's only RAM access between 0x020000 and 0xFF8000 is a word written and read back at 0x038000 (RAM mapped everywhere in a scratch build to see it). With RAM there the Sequencer Control page shows FREE=97311, without it FREE=31775: 32 768 and 98 304 four-byte events less the same 993, the manual's "30,000 notes" and "over 97,000 notes with the SQX-70" (the difference is 65 536 events, 256 KB). A kept sequence is stored from the top of sequencer memory down: 0x077FFF with the probe answered, 0x037FFF without, so the sequencer memory ends at 0x078000 with the SQX-70 and 0x038000 without; 0x038000-0x03FFFF is never used through the low window, and 0xFF8000-0xFFFFFF (the stack, used from 0.2 ms) is OSRAM's top 32 KB. So OSRAM cannot also answer at 0x038000 (a stock TS would then report the SQX-70), and SEQEXP's 256 KB sit at 0x038000-0x077FFF, a 32 KB-aligned window PAL U68 can decode. Before Task 8 OSRAM answered at 0x038000: the OS reported the SQX-70 and a kept sequence went to unmapped space (lost). With the expander absent (the default) the probe fails, FREE is 31 775 and a kept sequence lands at 0x037FFF down, in OSRAM. A power cycle on a running machine and on a fresh one give the same audio from power-on (ts_power_cycle_on_the_same_machine). Warm and garbage power-ons: docs/ts-hardware-interface.md, "Battery RAM". Details: docs/ts-hardware-interface.md, "Memory map".
- Confidence: High for the device windows and the low RAM window; medium for the top window and the OSRAM size; medium-high for SEQEXP's window (the probe, the free counts and where the data lands agree); low for what an empty SEQEXP socket reads (open bus on the board; 0 makes the probe fail, which is what a stock TS must do).
- Status: implemented in `Machine::decode`, `Machine::ramIndex`, `Machine::ramImage`, `Machine::loadRamImage` (Plan 3 Tasks 4 and 8); `tsemu --ram`. Tests: ts_boot_is_fully_mapped, ts_ram_alias_and_sample_window, ts_seqexp_window_and_ram_image, ts_stock_has_no_seqexp, ts_ram_image_load_clears_volatile_memory, ts_ram_image_keeps_settings, ts_garbage_ram_boots, ts_stock_sequence_is_kept_in_osram, ts_sqx70_sequence_is_kept_in_seqexp, ts_power_cycle_on_the_same_machine.

## T5: Interrupt levels
- Options: DUART, OTTO and the disk line on levels 1-3 in any order; vectored or autovectored.
- Choice: OTTO level 1, DUART level 3, every level autovectored (AVEC); level 2 (DISKIRQ/, pull-up only) has no source.
- Evidence: schematic (IPL2 tied high, AVEC from U67). Booting each placement: DUART on 3 boots (all acknowledges level 3, vector 27); DUART on 1 gives "SYSTEM ERROR 17" (unexpected OTTO interrupt) at its first interrupt, DUART on 2 gives "SYSTEM ERROR 139". The OTTO never asserts IRQB in the boot, so its level comes from the OS's level-1 handler. The OS writes IVR = 0x1B (27, the level-3 autovector), which AVEC ignores.
- Confidence: High for the DUART; medium-high for the OTTO.
- Status: implemented in `Machine::updateLines` and `interruptAcknowledge` (Plan 3 Task 4); `setIrqLevels` keeps the levels switchable for experiments. Tests: ts_boot_takes_no_fault_vectors.

## T6: Wait states
- Options: model the DTACK delays (I/O1 +3 clocks, OTTO +2) or not.
- Choice: not modelled.
- Evidence: no firmware timing check fails without them: the boot's clock check passes and the boot reaches its steady state with no error text.
- Confidence: Low (nothing in the boot needs them; a later timing-sensitive path might).
- Status: Plan 3 Task 4: nothing implemented.

## T8: Analog inputs
- Options: the resting raw value of each mux input; how the mux select reaches the conversion; what `setAnalog` does with a value beyond 10 bits.
- Choice: `kAnalogRest`: pitch wheel 0x200 (PTCH=064), mod wheel 0 (MOD=000), volume 0x3FF (VOL=127), CV pedal 0x3FF (PED=127, unplugged), data entry 0x14E (KNOB=127), battery 0 (BAT=000), reference 0x324 (REF=200). The mux switches at once; the OTTO's conversion (OT16) takes the select in force when it ends. `setAnalog` clamps to 0-1023 and ignores an input outside the enum.
- Evidence: the Analog Test page (hold Presets, press Compare; service manual p28) read with one input stepped at a time (docs/ts-hardware-interface.md, "Analog inputs"). The manual's expected readings: wheels and sliders 000 down / 127 (KNOB 255) up, PED=127 with no pedal, BAT below 010, REF 180-220. The OS reads PAR every 2 ms and changes the select 30 µs after the read, so the conversion (0.54 ms) ends on the new select and each read returns the input selected 2 ms before. The pitch wheel's centre is its power-on value. A battery raw of 458 or more stops the boot at the low-battery warning. Data entry at exactly 0 makes the OS rewrite the selected value at its minimum (and "SYSTEM ERROR 144" on the Analog Test page); 1 and up are quiet. Volume at 0 is silent. Clamping: a host asking for 1024 or 0xFFFF means "all the way up".
- Confidence: High for the path and the select table; medium for the resting values (real voltages unknown; each gives the manual's reading).
- Status: implemented in `kAnalogRest`, `Machine::create`, `Machine::setAnalog` (Plan 3 Task 6); `tsemu boot --analog NAME=raw`. Tests: ts_cold_boot_is_clean, ts_analog_resting_values, ts_slider_moves_volume, ts_key_sounds.

## T9: IEXP port
- Options: the value read at 0x2C0001; D4-D7 high, low or floating; which footswitch number is which contact.
- Choice: `0xFF & ~down`: D0 FSW3B/ (`footswitch(3)`, Foot Switch 2 right), D1 FSW4B/ (`footswitch(4)`, Foot Switch 2 left), D2 PSRB/ (`patchSelect(1)`), D3 PSLB/ (`patchSelect(0)`), low = pressed; D4-D7 high.
- Evidence: schematic (U12/A, D0-D3 active low). The OS sends the patch selects as MIDI controller 70 (left 0x40, right 0x20, both 0x7F, none 0) and, with SW2-R=SUSTAIN, D0 as controller 64 while D1 needs SW2-L. Upper nibbles 0x0, 0x5, 0x7, 0xA, 0xE and 0xF give the same bus trace apart from the IEXP values: the OS masks them (docs/ts-hardware-interface.md, "IEXP port").
- Confidence: High for D0-D3; D4-D7's real level unknown but unobservable.
- Status: implemented in `Machine::footswitch`, `Machine::patchSelect`, `Machine::ioRead` (Plan 3 Task 6). Tests: ts_iexp_inputs, ts_patch_select_is_read, ts_footswitch2_is_read.

## T10: Floppy controller and the empty drive
- Options: no FDC (status 0), a static status, a uPD765 command/result handshake; index pulses or none; the DSKCHG and DENS levels; the ST3 and ST0 an empty drive gives.
- Choice: `ens::ts::Fdc`: RQM always, CB while a command is open, DIO/CB while result bytes wait; uPD765 command lengths; Sense Interrupt Status ST0 0x80 (no interrupt), Sense Drive Status ST3 = drive bits only, reads "not ready"; A0 = 0 writes (the 72069's own registers) ignored. No index pulse on IP0 (DSTAT); IP1 (DSKCHG) and IP2 (DENS) high.
- Evidence: with no index pulse the OS's drive check (Sense Drive Status, then 300 000 IPCR polls at c0ea1c, testing only IPCR bit 4, the delta of IP0) fails, and a disk command from the panel shows "DISK DRIVE NOT READY", the Musician's Manual's "No disk in the drive", then the disk page; at boot it goes on silently. Index pulses give "DISK DRIVE NOT RESPONDING" instead. IP1/IP2 levels, ST3 values (0x00-0x70) and ST0 0xC0 + PCN change nothing with the drive empty. Status 0 or a static 0x80 make the OS time out (Task 4). Details: docs/ts-hardware-interface.md, "Floppy controller and the empty drive".
- Confidence: High for the empty-drive behaviour (no index pulse, the no-disk message); low for ST3, ST0 and the IP1/IP2 levels, which an empty drive cannot show.
- Status: `src/ts/fdc.h`, `Machine::create` (input pins) (Plan 3 Tasks 4 and 6). Plan 5: with a disk in, T22-T24; the empty drive is unchanged except that the A0 = 0 writes 0x36 and 0x88/0xC8 now reset the controller and pick the rate (T24), which changes no empty-drive trace. Tests: ts_floppy_reports_no_disk, ts_cold_boot_is_clean, ts_empty_drive_still_reports_no_disk.

## T12: SCSI and unmapped space
- Options: reads of the SCSI block (no board) and of unmapped space return 0 or open-bus 0xFF.
- Choice: 0; writes ignored; every access traced (SCSI as `Device::Scsi`, the rest as `Device::Unmapped`). For the SCSI block this is the machine without the SP-4 board (the default, `create(..., scsi = false)`); with the board the block is the WD33C93 (T25).
- Evidence: the OS's SCSI probe at 110 ms (write 0x02 to 0x240001, write and read 0x240003) reads 0, then writes 0x55 and reads 0, and goes on; with 0xFF it stops after the first read. Both boot the same way after that: neither reads as a SCSI board. Nothing unmapped is read in the boot. Plan 7: the probe (c0f820) writes register 2 with 0x00, 0x55, 0xAA, 0xFF, rewriting the address before each read-back; a 0 read fails it at 0x55, so without the board the STORAGE page has no SCSI field, as on a TS without the SP-4.
- Confidence: Medium.
- Status: implemented in `Machine::ioRead` (Plan 3 Task 4); the board-less case since Plan 7 Task 4. Test: ts_scsi_absent_is_unchanged (boot trace hash `3bfc7336098a6def`).

## T15: LED states
- Options: what the parameter of `FF nn` carries beyond the LED number (bits 3-0); how `PanelState` holds a flashing LED.
- Choice: bit 7 = lit, bit 6 = flashing; `PanelState::leds` bit n = LED n lit, bit 16 + n = LED n flashing. 16 LEDs: 0 Bank 0, 1 Presets, 2-5 Bank 2/4/6/8, 6 Replace Track Sound, 7 Seq/Song Tracks 1-6, 8 Bank 1, 9 Sounds, 10-13 Bank 3/5/7/9, 14 Compare, 15 Seq/Song Tracks 7-12. The Seqs/Songs LED is DUART OP3 (SEQLED/), not on the link.
- Evidence: every page refreshes all 16 (`FF 0F` ... `FF 00`) with bit 7 on the lit ones; `FF C9` appears exactly where the manual says the Sounds LED flashes (Replace Track Sound). OP3 goes low on Seqs/Songs and high on Sounds (docs/ts-hardware-interface.md, "Display and LEDs").
- Confidence: High for lit and the numbering; medium for flashing.
- Status: implemented in `Panel::run` (Plan 3 Task 5). Tests: ts_panel_commands, ts_boot_shows_play_screen, ts_button_changes_page.

## T16: Button codes
- Options: the byte forms of a button event; which code is which button.
- Choice: press `code | 0x80, 00`, release `code, 00`, codes 0x00-0x3F. `Button` holds the 62 panel buttons and the two Foot Switch 1 contacts (00 right, 01 left), which fill all 64 codes.
- Evidence: from the play screen every first byte followed by `00` was sent and the display, LEDs and MIDI out compared; each code was named by the page, LED or MIDI message it gives, checked against the Musician's Manual and the service manual's self-test chart (p11); Up/Down on the System page, Record with Play, the foot switch on the System foot-switch page (docs/ts-hardware-interface.md, "Buttons").
- Confidence: High for the forms and the codes with a page or MIDI message; medium for Compare and the foot switch naming.
- Status: implemented in `Panel::button`, `kButtons` (Plan 3 Task 5). Tests: ts_panel_button_bytes, ts_panel_button_table, ts_button_changes_page.

## T17: Keys and Poly-Key pressure
- Options: the byte forms of key and pressure events; what `key()`'s velocity argument is.
- Choice: key down `0x80 | k, strike`, key up `k, 0x40`, pressure `0x40 | k, amount`, k = note - 36 for the 61 keys C2-C7 (others ignored). `key()` takes the strike value the keyboard sends, clamped to 1-127; the OS maps it through its TOUCH table (VELTBL-4: strike 64 gives velocity 92 on a white key, 84 on a black one, 112 and up give 127).
- Evidence: MIDI out: note-on `90 (k+36) vel`, note-off `80 (k+36) 40` for any nonzero release byte, aftertouch `A0 (k+36) amount` (docs/ts-hardware-interface.md, "Keys and pressure"). A strike of 0 reads as a button press, so 0 is clamped to 1.
- Confidence: High for the forms and the mapping; low for what the strike value is physically.
- Status: implemented in `Panel::key`, `Panel::pressure` (Plan 3 Task 5). Tests: ts_panel_button_bytes, ts_key_sounds, ts_poly_pressure_is_sent.

## T18: Display command set
- Options: which bytes are characters and which commands; their parameters; what each attribute byte does to the cells written after it.
- Choice: 0x00-0x7E characters; 0x80-0xCF cursor to position 0-79; `D6` clear; `F5`/`F6` save/restore the cursor; `D4`/`D5` cursor right/left; `D0` flashing, `D1` plain, `D2` flashing underline, `D3` underline for the characters written after them; `E8` lets later characters keep their cells' attributes; `D9`/`DB` set/remove the underline of the cell at the cursor; `7F` and `E7` no effect; `FF nn` LED (T15); `FB nn` keyboard command (T19); anything else counted in `unknown` with no parameter. Past position 79 the cursor wraps to 0 (never seen).
- Evidence: the boot and a sweep of every button code with the six soft buttons on each page it opens decode with `unknown` 0; field widths fix the zero-width bytes and `D5`; the manual's descriptions of the screens fix the attributes (docs/ts-hardware-interface.md, "Display and LEDs").
- Confidence: High for text, positions, `D6`, `F5`/`F6`, `D3`, `D5`; medium for `D0`, `D1`, `D2`, `E8`, `D4`, `D9`, `DB`; low for `7F` and `E7` (effect unknown) and 0x00-0x1F (never seen).
- Status: implemented in `Panel::fromCpu` (Plan 3 Task 5). Tests: ts_panel_commands, ts_boot_shows_play_screen.

## T19: Keyboard start-up and the link lines
- Options: what the keyboard must send for the OS to accept it; when; what KRES, DRES and SACK/ do.
- Choice: the keyboard answers `FF` at once to `FB FD` and ignores its other commands (`FB FF`, `FB F0`, `FB DD`, `FB FC`). KRES (OP4) and DRES (OP5) are active-high resets: KRES empties the keyboard's queue and mutes it, DRES blanks the display and LEDs. The keyboard sends only while SACK/ (OP7) is low, one byte at a time when RxDB is idle.
- Evidence: of all 256 single bytes during "CALIBRATING KEYBOARD", only `FF` ends the OS's wait, and only from the moment it queues `FB FF FB FD` to its next `FB F0`. The OS holds OP4/OP5 high only until 0.24 ms (active-low resets would keep the keyboard in reset). SACK/ goes low just before the first keyboard command and stays low; it pulses high only under a continuous line-rate flood (docs/ts-hardware-interface.md, "Keyboard link").
- Confidence: High for the `FF` answer and the reset polarity; low for SACK/'s role (ignoring it changes no firmware test) and for what the other keyboard commands mean.
- Status: implemented in `Panel::run`, `Panel::setLines`, `Panel::popToCpu`, `Machine::slot` and `Machine::updateLines` (Plan 3 Task 5). Tests: ts_panel_lines, ts_boot_shows_play_screen.

## T20: MIDI in and out
- Options: MIDI in straight onto DUART A's wire queue, or a second machine-side queue fed one byte at a time when the wire is idle (as the keyboard link does); what program change selects.
- Choice: `Machine::midiIn` puts the byte on DUART A's wire queue (1024 characters, A1), which hands them to the receiver back to back at the line rate, 320 us a byte at 31 250 baud (D2). `popMidiOut` pops what left TxDA (256 kept, A2). No second queue: the keyboard link holds bytes back for SACK/ (T19), MIDI has no flow control, and the wire queue already paces a burst.
- Evidence: the OS writes CSRA = 0xEE: channel A runs from the 500 kHz IP3/IP4 clocks, `rxBitClocks(0) == 128`. Middle C at velocity 100 on channel 1 peaks at 2256 within 300 ms (silence before: 3). 33 note-ons and 33 note-offs queued at once (198 bytes) give no overrun, nothing dropped and no warning text, and the notes release. Program change n selects bank n / 6, sound n % 6 of the current bank set (U0-0 BABY-GRAND to U0-9 REMEMBER-ME for 0-59; 60 and up not tried); after it the play screen underlines the selected sound (docs/ts-hardware-interface.md, "MIDI").
- Confidence: High.
- Status: implemented in `Machine::midiIn`, `Machine::popMidiOut` (Plan 3 Task 7); `tsemu render`, `tsemu bench`. Tests: ts_midi_rate, ts_midi_note_sounds, ts_midi_burst_no_overrun, ts_program_change_changes_display, ts_render_is_deterministic.

## T21: Disk images
- Options: which image files to read and write; how to tell them apart; how to lay out and store a Giebler file's blocks; whether to write omitted blocks back as omitted.
- Choice: three formats, told apart by content, never by name: Raw (exactly 819 200 or 1 638 400 bytes, blocks in order), GKH (58-byte header starting "TDDFI", then Raw; the header is kept and written back), Giebler (starts 0D 0A). Giebler file: 512-byte header, a bitmap of one bit per block, MSB first, 1 = block left out (reads as zero), at 0xA0 for DD (200 bytes, 1600 blocks) and at 0x60 for HD (400 bytes, 3200 blocks); the disk-type byte at 0x1FF (EPS 3, TS-DD 7, ASR 0xCB, TS-HD 0xCC); 0D 0A 1A just before the bitmap (so the EDA text block is 0x40 bytes shorter than the EDE one); then the stored blocks in block order, 512 bytes each, and one trailing 1A. Density is taken from the type byte first, then confirmed by file size = 512 + 512 x stored blocks + 1. Block order is `(cyl * 2 + head) * sectorsPerTrack + id`, ids from 0. On saving, a block is stored when it is not all zero or when the kept header already stored it (a decoded file re-encodes byte for byte); a block that was left out and got data is stored. Unformatted tracks are written as zeros. A Giebler file is recognised only when 0D 0A is at 0x4E, 0D 0A 1A sits just before the bitmap (0x9D DD, 0x5D HD, as in all four blank files) and the size is exactly 512 + 512 x stored blocks + 1; anything else (a text file starting with CRLF, trailing bytes, a truncated body) is refused. A Giebler file with no kept header gets a new one ("TS-10 (DD) Disk" with type 7, or "(HD)" with 0xCC). A GKH file with no kept header (a new disk saved as .gkh, a raw or Giebler disk saved as .gkh) gets the real 58-byte header of the user's ED-001 (DD) and AD-001 (HD) files, not zeros: "TDDFI", version 01, five 10-byte tags, tag 0A the geometry (80 cylinders, 2 heads, sectors per track 10 or 20 at 0x18, 512-byte sectors), tag 0B the image size (LE32 at 0x1E, 0x000C8000 or 0x00190000) and offset (0x3A at 0x22); the two files differ only at 0x18 and 0x1E-0x21.
- Evidence: the four blank files EPSBLANK.EDE, ASRBLANK.EDA, TS-DD.EDT, TS-HD.EDT (8193, 12801, 9729 and 14337 bytes: 15, 24, 18 and 27 stored blocks, as their bitmaps give); the Giebler "Ensoniq Floppy Diskette Format" article for the block formula; the user's GKH and IMG pairs (EPS-16 ED-001, ASR-10 AD-001) decode to the same sectors. Layout shape follows the Mirage .edm pattern (different machine). The disk-type bytes other than these four are not seen.
- Confidence: High for the four blank files and the raw and GKH pairs; medium for other type bytes.
- Status: implemented in `src/ts/disk.*` and `src/ts/disk_image.*` (Plan 5 Task 1). `// DECISION T21` in `disk_image.cpp`. Tests: disk_geometry, disk_blank_is_unformatted, disk_copy_into_needs_same_density, image_raw_sizes, image_gkh_roundtrip, image_giebler_omitted_block_written, image_format_for_name, image_giebler_refuses_damaged_files, image_gkh_fresh_header_bytes, image_user_files (needs `ENS_DISK_DIR`; also checks the fresh GKH header against the real files').

## T22: Floppy controller commands
- Options: how much of the uPD765 set to model with a disk in the drive; the ST0/ST1/ST2 bits per error; how a transfer ends in non-DMA mode (no TC); what Read ID answers; which cylinder a read means; the default data rate.
- Choice: `Fdc::setDisk` switches from the empty drive (T10, unchanged) to a Disk. Specify, Recalibrate, Seek, Sense Interrupt Status, Sense Drive Status, Read ID, Read Data, Write Data and Format Track are modelled; Read Track, deleted-data and Scan commands end abnormally (ST0 0x40, ST1 MA). No timing: Seek and Recalibrate finish at once and leave ST0 0x20 (SE) + unit and PCN for `08`; every byte is ready on every poll. MSR in the execution phase is RQM | EXM | CB, plus DIO when reading. ST3 with a disk: ready 0x20, two-sided 0x08, track 0 0x10 at cylinder 0, write protect 0x40, plus the command's head/unit bits. Read/Write Data: the command's C is the cylinder used (the head position is not compared), sector ids are 0..N-1 with N = 2, a bad C, head, R or N gives ST0 0x40 + ST1 ND (0x04); an unformatted track, or a disk whose density differs from the selected rate, gives ST0 0x40 + ST1 MA (0x01); Write Data on a protected disk gives ST0 0x40 + ST1 NW (0x02) and writes nothing. A multi-sector transfer advances R and ends after sector EOT or at the end of the track with ST0 0x40 + unit and ST1 EN (0x80), as a uPD765 whose TC is never asserted, and R one past the last sector (Task 3: TC is tied to ground on the TS; before Task 3 the end was ST0/1/2 = 0). Each written sector counts one `Disk::noteWrite`. Format Track takes 4 ID bytes per sector (SC of them) through the data register, then formats the track under the head with the filler D whatever the IDs said, and answers with the last ID as C H R N (ST0 0x40 + NW on a protected disk). Read ID answers the head's cylinder, the command's head and the next sector id, cycling. `setHighRate` defaults to false (DD), matching the T10 world; `reset()` clears every execution and result state (MSR back to 0x80) and keeps the disk, the rate and the head position. The execution phase ends only by its own end, an error, a disk change or removal (`setDisk` during it ends the command with ST0 0x48 + unit and C H R N where it stopped, no pointer into a disk kept) or `reset()`; no TC line is modelled (on the TS it is grounded); the 72069's control-register reset (0x36) calls `Fdc::reset()` (T24).
- Evidence: the uPD765/uPD72069 command, result and ST0-ST3 definitions as commonly documented; the OS's polling of MSR/data register (docs/ts-hardware-interface.md, "Floppy controller and the empty drive"). No MAME code used. EN (Task 3): the digital board schematic (sheet 3, U20 pin TC) ties TC to ground, so a real transfer can only end at EOT with IC 01 + EN; the OS's result check (c0ec36-c0ec4c) takes ST0 IC 00, or IC 01 with ST1 exactly 0x80, as success, and maps ST1 MA or ND to error 6, OR to 8, DE to 7, anything else to 0x0A; its Format Track check (c0f132) also accepts EN. The OS never sends Read ID (its command senders c0e9f8, c0ea68, c0eb42, c0f0e6, c0f564, c0f67c, c0f6ae send only 04, 08, 03, 4D, 45/46, 0F, 07), so Read ID's answer is unconstrained. After an abort the controller sits in the result phase; the OS never writes the data register there (c0f6fe waits for RQM with DIO clear, else times out with error 0x0A and resets the controller with 0x36), so a stray command cannot wedge it.
- Confidence: Medium-high: the OS lists directories of DD and HD disks through it (Task 3); Task 4 loads and writes.
- Status: implemented in `src/ts/fdc.cpp` (Plan 5 Tasks 2 and 3). `// DECISION T22` in `fdc.cpp`. Tests: tests/test_ts_fdc.cpp (`fdc_*`), tests/test_ts_floppy.cpp (`ts_*`).

## T23: Drive lines with a disk
- Options: the index pulse's period and width, and whether it runs only with the motor on; when DSKCHG (IP1) goes high and when it clears (on a step, as on PC drives; or simply "drive empty"); how a disk swapped in one call is shown.
- Choice: with a disk in, IP0 (DSTAT) is low for 2 ms every 200 ms (a 300 rpm drive), clocked from `cycles()`, the first falling edge 198 ms after the insert, whatever the motor bits say. IP1 (DSKCHG) goes high when a disk is put in or taken out and stays high until the head steps with a disk in (`Fdc::cylinder()` changes); the drive empty holds it high (the T10 levels 0x3F). `Machine::insertDisk` swaps the disk at a frame boundary, hands the pointer to the Fdc (which ends a running transfer, T22) and sets the pins at once; it never allocates. IP3-IP5 stay high.
- Evidence: OS v3.10 disassembly and runs with the user's ED-001 and AD-001 images (docs/ts-hardware-interface.md, "Floppy drive with a disk"). The drive check (c0e9fe, once per disk command after the motor-on write 0x1E at c0e974) reads the IPCR to clear it, then polls IPCR bit 4 (delta IP0) 300 000 times (0.9 s); an edge within the first 235 400 polls (0.71 s) passes, a later one retries (up to 10 times), none is "not ready" (error 4). Either edge counts; no other read waits for the index (the controller's own INDEX pin is not modelled). The disk change is IPCR bit 5 (delta IP1), polled every 0.5 s from the main loop (c12990, c0e958-c0e96e) while a directory is cached ($a26) and the floppy is the current drive; it drops the cache (c0edca). The OS never reads IP1's level (no IPR reads; IPCR reads only at c0e964, c0e998, c0e9d4, c0ea10, c0ea1c, c0ea7e). Each directory read starts with Seek to cylinder 1 and Recalibrate (c0e372-c0e3f6), the PC way of stepping DSKCHG/ clear; the schematic names drive pin 34 DSKCHG/, inverted by U56/E onto IP1. Measured: index wait 190 ms (5807.67 to 5998.00 ms), directory listed 375 ms after the second Storage press; a disk swapped in shows IPCR 0x7F at c0e964 and the next Disk Load page lists the new disk.
- Confidence: high for the index (any edge within 0.71 s works; 200 ms is a real drive's); medium for DSKCHG clearing on a step: the OS only looks at edges, so the "drive empty" alternative passes the same tests.
- Status: implemented in `Machine::insertDisk`, `Machine::drivePins`, `Machine::slot` (Plan 5 Task 3); `tsemu --disk`. Tests: ts_disk_load_page_lists_directory, ts_eject_mid_read_reports_error, ts_reads_both_densities, ts_empty_drive_still_reports_no_disk, rt_disk_in_the_drive_does_not_allocate.

## T24: Density
- Options: the density from the 0x280001 control writes, from IP2 (DENS), or both; which control value selects the high rate; what a disk of the other density does.
- Choice: IP2 is high for an HD disk (DD: low). The control writes are routed: 0x36 calls `Fdc::reset()`; a write with low six bits 0x08 sets the rate from bit 6 (0x88 DD, 0xC8 HD); the others (0x0B, 0x4F, 0xE3/0xF3, 0x1E/0x0E) are only remembered. A disk read at the other rate gives ST1 MA (T22).
- Evidence: at each disk command (c0d8ac) and at the controller set-up (c0e880) the OS sends Sense Drive Status, then reads IPCR bit 2 (IP2's level, c0ea7e): low writes 0x88 and 0xF3 to the control register and Specify 03 E1 09; high writes 0xC8, 0xE3 and Specify 03 C2 11 (0x19 later). Those Specify values give the same step rate and head-load time at 250 and 500 kbps (the uPD765 timer units halve at the higher rate), so high is HD. The schematic inverts drive pin 2 (DENS/) onto IP2 (U56/D). The reset: the OS's recovery (c0f7a6) writes 0x36, waits, then sends Sense Interrupt Status; 0x36 is the uPD72065 family's software reset (van Gilluwe, "The Undocumented PC", p. 535). Which of 0x88/0xC8 and 0xF3/0xE3 carries the rate cannot be seen from the OS, which always writes both from IP2. With IP2 forced to the wrong level, the OS reads with ST1 MA and shows "DISK NOT FORMATTED", then "PRESS ANY BUTTON TO CONTINUE...", for an HD disk read at DD and a DD disk read at HD alike.
- Confidence: high for IP2's meaning and the reset; low for 0x88/0xC8 being the rate register (unobservable).
- Status: implemented in `Fdc::write` (A0 = 0) and `Machine::drivePins` (Plan 5 Task 3). Tests: fdc_control_reset_and_rate, fdc_wrong_rate, ts_reads_both_densities.

## T25: The SP-4 SCSI board
- Options: fit the board always, never, or as an option; how the WD33C93 sits on the bus (which CPU address line is its A0, odd or even bytes); how its address register steps; which commands to model.
- Choice: an option of `Machine::create` (`scsi`, default off, like `sqx70`; `tsemu --cd` and the app fit it). I/O1 n = 1 (0x240000-0x27FFFF) goes to a WD33C93 on odd bytes, chip A0 = CPU A1, as the FDC: 0x240001 writes the address register and reads Auxiliary Status, 0x240003 is the addressed register. Indirect addressing steps the address after every register-port access except at Command (0x18) and Data (0x19) (datasheet 3.1.2). Commands: Reset (0x00) and Select-and-Transfer (0x08/0x09); any other is "invalid command" (0x40). One CD-ROM drive is attached at SCSI ID 4. `powerOn` resets the chip; the drive keeps its disc (an external drive has its own supply).
- Evidence: OS v3.10: the probe (c0f820) holds 0x240001 in A4 and 0x240003 in A3 and writes register 2 with 0x00, 0x55, 0xAA, 0xFF, writing the address 2 before each write and each read-back (c0f846); on a match (c0f7d4) Own ID = 3, Reset, a 15 ms wait, INT polled and SCSI Status read; `$12b6` bit 3 is set and the STORAGE page shows SCSI on its bottom row (service manual p. 53 step 14). The CDB is written into registers 3.. through the register port, which needs the step rule. The only commands the OS wrote in the runs are 0x00 and 0x09 (`tsemu boot --cd ... --trace scsi`). Runs (ts_scsi_probe_and_reset): all three probe values read back, Reset is written to register 0x18, the boot ends on the usual play screen, and STORAGE shows "SELECT STORAGE OPTION *LOAD*" over "SCSI DISK SYS-EX". The Musician's Manual (Section 13, "Direct-Macros") says CD-ROM drives holding Ensoniq CDs "should always be set to SCSI ID #4".
- Confidence: High for the decode and the step rule (the OS works only with them); high for the command set (the OS writes no other).
- Status: implemented in `Machine::create`, `Machine::ioRead`/`ioWrite` and `Wd33c93` (Plan 7 Tasks 3, 4). Tests: wd_* (Task 3), ts_scsi_probe_and_reset, ts_scsi_absent_is_unchanged, ts_cd_status_shows_volume, ts_cd_loads_and_plays, ts_cd_empty_drive.

## T26: The SCSI interrupt
- Options: connect the WD33C93's INTRQ to an interrupt level, or to none.
- Choice: none. `Machine::updateLines` never raises an IPL for it; the OS polls Auxiliary Status (bit 7 INT).
- Evidence: OS v3.10: the autovectors of levels 2 and 4-7 all go to the fatal-exception path (c00d62, code 0x8B), so an interrupt from the chip would stop the OS. Every SCSI wait polls Auxiliary Status instead; the Control register is written 0x00 (polled I/O, no DMA) before every command (c1563e). The runs mount and load a CD with no interrupt (ts_cd_status_shows_volume, ts_cd_loads_and_plays).
- Confidence: High.
- Status: implemented (`// DECISION T26` at `Machine::updateLines`), Plan 7 Task 4.

## T27: SCSI timing
- Options: model selection, command, data and status phases with bus timings, the Timeout Period register and the drive's seek time; or let the chip answer at once except where the data is not there yet.
- Choice: selection, command and status complete inside the register access that starts them; a missing ID answers at once with 0x42 (selection timeout); the Timeout Period register is not modelled. The data phase waits only while a CD sector is on its way (DBR clear, no interrupt); the CD target asks for it through the sector port (T29) and the test and tool loops answer between frames.
- Evidence: the OS polls Auxiliary Status in long counted loops (c1563e) and checks INT before each data byte, so a reply between frames (a frame is about 34 us, T1) is far inside its waits. Runs: the empty IDs 0-2 on the STATUS page answer INVALID at once; CDR-13 mounts and ETHNIC PERC2 (3927 blocks) loads well inside the test's 60 s limit with every sector answered one frame after it is asked (ts_cd_loads_and_plays).
- Confidence: Medium: a real drive is slower (seeks, spin-up); nothing in the OS was seen to depend on it.
- Status: implemented in `Wd33c93::command` (`// DECISION T27`) and `CdTarget::ready`, Plan 7 Tasks 3, 4.

## T28: What the CD-ROM drive answers
- Options: which commands the target answers and how; its INQUIRY strings; its block length at power-on; what an empty drive and a disc change answer.
- Choice: INQUIRY type 5 (CD-ROM), removable, "TOSHIBA " / "CD-ROM XM-3401TA" / "3605". TEST UNIT READY: GOOD with a disc, CHECK CONDITION with NOT READY, MEDIUM NOT PRESENT (2/0x3A) without. After any insert or eject the next command other than INQUIRY and REQUEST SENSE gets one UNIT ATTENTION (6/0x28). MODE SELECT(6) sets the block length (512 or 2048; it starts at 2048 as on real drives); MODE SENSE(6) gives the block descriptor; READ CAPACITY and READ(10) use the current block length; data the target lacks is padded to the Transfer Count. WRITE(10): DATA PROTECT (7/0x27). A sector the reader cannot read: MEDIUM ERROR (3/0x11). A disc ejected or changed while a command is moving data ends it CHECK CONDITION (NOT READY 2/0x3A for an eject, UNIT ATTENTION 6/0x28 for a change), so the OS never accepts a short transfer. Others: ILLEGAL REQUEST (5/0x20).
- Evidence: the service manual (p. 62) only says a drive with a Sony or Toshiba mechanism should work and names no model, so the strings are a Toshiba XM-3401's. The OS (c1548c): INQUIRY type 0 asks READ CAPACITY and needs 512-byte blocks; type 5 sends MODE SELECT(6) with a block descriptor of length 0x200; a CHECK CONDITION is followed by REQUEST SENSE, and sense key 6 with ASC 0x28 is a media change (c154cc). Seen on the STATUS page with CDR-13 inserted at power-on (ts_cd_status_shows_volume; the OS polls the ID about every 500 ms while the page is up): Reset, TEST UNIT READY (CHECK CONDITION), REQUEST SENSE (14 bytes: `70 00 06 .. 28`), STANDBY shown; then TEST UNIT READY (GOOD), INQUIRY (36 bytes), Reset, TEST UNIT READY, MODE SELECT(6) (`00 00 00 08 00 00 00 00 00 00 02 00`), READ(10) of block 1 (512 bytes), and STATUS=CDR-013. No MODE SENSE or READ CAPACITY was sent for the CD. With the drive empty the page shows STANDBY (ts_cd_empty_drive), the manual's "removable media device ... contains no volume"; INVALID is what the empty IDs give (selection timeout).
- Confidence: High for the commands the OS uses (it mounts and loads with them); low for the INQUIRY strings (any Sony or Toshiba drive would do).
- Status: implemented in `CdTarget` (Plan 7 Task 2). Tests: cd_target_* (Task 2), ts_cd_status_shows_volume, ts_cd_loads_and_plays, ts_cd_empty_drive.

## T29: The CD sector port
- Options: the CD target reads the image itself (file I/O under `src/`, a wait on the audio thread), holds the whole image in memory, or asks for each 2048-byte sector through a hand-over that another thread or the test loop answers.
- Choice: the hand-over (`SectorPort`): the target asks for one sector by (medium, lba) and reports busy (DBR clear) until the answer to that exact ask is in; whoever owns the image answers with `pending`/`reply`. `Machine::cdPort()` exposes it (nullptr without the board) and `Machine::insertCd(medium, sectors)` puts a disc in (0 sectors ejects) between frames, never allocating. Medium ids name the image in asks and replies so that a late reply for an earlier disc is never served; a caller never reuses an id for a different image. The tests' `ServedCd` and `tsemu --cd` answer from `CdFile` after every frame, so runs are deterministic. The images `CdFile` reads (`cd_image.h`): `.iso` (size a multiple of 2048) or a `.cue` with one `TRACK 01 MODE1/2352` from `INDEX 01 00:00:00` (16 header bytes, then the 2048 data bytes of each 2352-byte sector); a `FILE` path that does not exist falls back to that name in the `.cue`'s folder, matched without case. Other layouts are refused.
- Evidence: design "Approaches" (read on demand, chosen by the user); all 16 of the user's Ensoniq CDs are `.iso` or such a `.cue` (design "Goals"), and CDR-13's `.cue` names `E:\ENSONIQ-CDR-13-PERCUSSION-INVISION\DLM-CDR13.BIN`; AGENTS.md (no file I/O under `src/`, nothing that blocks or allocates in the render path). The OS waits for DBR (T27), so a sector that arrives a frame later is never a timeout: ts_cd_loads_and_plays loads a 3927-block sound this way and plays it from sample RAM.
- Confidence: High.
- Status: implemented in `SectorPort` (Plan 7 Task 2) and `Machine::cdPort`/`insertCd` (Task 4); `tsemu --cd`. Tests: cd_target_waits_for_sector and cd_target_ignores_stale_reply (Task 2), ts_cd_loads_and_plays.

# App

Plan 4's engine and app (TS Reborn). Where a KS-32 Reborn decision is carried over, the Evidence names it.

## A100: Frame rate
- Options: (a) the WAV writer's rounded 29 762 Hz (`kWavRate`, `tests/ts_render.h`) as the engine's input rate; (b) the exact frame rate.
- Choice: (b). `kFrameHz = ts::kCpuHz / 512.0` (29 761.9 Hz) in `src/app/engine.h`: one OTTO frame of 32 voices is 512 CPU clocks (T1). The resampler's input rate and the MIDI offset scale (A101) use it. It lives in `src/app`, not `src/ts/machine.h`, so `src/ts` stays integer-only (controller ruling, Plan 4 Task 2).
- Evidence: T1. The engine's four outputs equal, as floats, a machine run frame by frame through `Resampler(kFrameHz, 48000, 4)` for 48 000 frames (engine_matches_machine_and_resampler).
- Confidence: High.
- Status: settled, Plan 4 Task 2. `// DECISION A100` at `kFrameHz` (`src/app/engine.h`) and `Engine::prepare` (`src/app/engine.cpp`). Test: engine_matches_machine_and_resampler.

## A101: MIDI timing in a block
- Options: (a) every byte of a block enters at its start; (b) a byte at output offset `o` enters before machine frame `floor(o * kFrameHz / outRate)` of the block, with a fixed store per block.
- Choice: (b), as KS-32 A3. `Engine::midi` stores up to 1024 bytes a block with their machine frame; `render` feeds them before that frame runs, and any left after the block's last frame enter at its end. Bytes beyond 1024 are dropped and counted in `Snapshot::midiDropped`, with the DUART's own drops (A1).
- Evidence: as KS-32 A3. A note at offset 479 starts 477 output frames after one at offset 0 (engine_midi_offset_places_note: 194 and 671). 3000 bytes in one block drop 1976 (engine_midi_overflow_is_counted). 1024 bytes is DUART A's whole wire queue (A1).
- Confidence: High.
- Status: settled, Plan 4 Task 2. `// DECISION A101` at `Engine::midi` (`src/app/engine.cpp`) and `kMidiStore` (`src/app/engine.h`). Tests: engine_midi_offset_places_note, engine_midi_overflow_is_counted.

## A102: GUI events
- Options: (a) the GUI calls the machine under a lock; (b) a fixed single-producer queue the audio thread drains at the start of each block.
- Choice: (b), as KS-32 A4. `Engine::post` pushes into a 256-event `SpscRing` and returns false when it is full; `render` applies the events at the start of the next block, in post order. A button or key goes on the panel link only while `Machine::panelRoom()` is 2 or more, so none is dropped; the rest wait for a later block, still in order. On the TS the panel's ring (256 bytes) is the only limit: `Machine::slot` hands it to DUART B only while B's wire queue is empty. Analog and patch select events do not use the link and apply at once.
- Evidence: as KS-32 A4. 256 key events at once all arrive (engine_posts_never_split_a_panel_event: no panel or channel B drops, and the keys reach MIDI out). System pressed with 100 ms of blocks held opens the System page (engine_buttons_reach_system_page). The left patch select button reaches IEXP: the OS sends `B0 46 40` within 100 ms (engine_patch_select_reaches_machine).
- Confidence: High.
- Status: settled, Plan 4 Task 2. `// DECISION A102` at `Engine::post` and `Engine::render` (`src/app/engine.cpp`) and `kEventQueue` (`src/app/engine.h`). Tests: spsc_ring_fifo_and_full, engine_buttons_reach_system_page, engine_posts_never_split_a_panel_event, engine_patch_select_reaches_machine.

## A103: Snapshot hand-off
- Options: (a) a sequence counter the GUI retries on; (b) a mutex the audio side takes with `try_lock`, skipping the block when the GUI holds it, while the GUI locks normally.
- Choice: (b), as KS-32 A5. After each block the engine copies the panel state, the Seqs/Songs LED (`Machine::seqLed`, DUART OP3 low), `booted`, the frame count, the MIDI counters and the sound into `Snapshot` under `try_lock`; `Engine::snapshot()` returns a copy under the lock.
- Evidence: as KS-32 A5. `PanelState` is plain data, so the copy does not allocate (rt_engine_render_does_not_allocate). The Seqs/Songs LED is not on the panel link (interface doc, "Display and LEDs"): `seqLed` is true after Seqs/Songs and false after Sounds (engine_seq_led).
- Confidence: High.
- Status: settled, Plan 4 Task 2. `// DECISION A103` at `Engine::render` (`src/app/engine.cpp`). Tests: engine_boot_snapshot, engine_seq_led, rt_engine_render_does_not_allocate.

## A104: Booted
- Options: (a) watch the display for the play screen; (b) a fixed emulated time since power-on.
- Choice: (b), as KS-32 A6. `Snapshot::booted` is true once the engine has run `kBootMs` (5300 ms) of machine frames since `create` or `loadRam`, counted as `kBootMs * kCpuHz / 512 000` whole frames.
- Evidence: `kBootMs` is the measured boot end (3491 ms) plus 50 % (`src/ts/machine.h`). After 5000 ms of 48 kHz blocks it is false; after 5400 ms it is true, row 0 starts `U0-0 BABY-GRAND` and the LEDs are 0x201 (engine_boot_snapshot).
- Confidence: High.
- Status: settled, Plan 4 Task 2. `// DECISION A104` at `kBootFrames` (`src/app/engine.cpp`). Test: engine_boot_snapshot.

## A105: Sound tracking and restore
- Options: as KS-32 A15: (a) send a bare program change after every boot; (b) send the requested sound only if the machine is not already on it, and only on a request, so a panel or MIDI-in pick is never undone; (c) the program change alone; (d) the bytes that made the selection, bank select included. And, for the TS, whether the OS sending a selection on MIDI out again counts as a new change.
- Choice: (b) and (d), as KS-32 A15 and A18. After the boot (A104) the engine parses MIDI out and MIDI in with a running-status parser and tracks the program, its channel and the last bank select (CC 0, CC 32); `Snapshot::select` is `Bn 00 msb 20 lsb` (once a bank select has been seen) then `Cn pp`. The boot ends on U0-0 (program 0, bank 0/0), cold or warm. `setProgram(n)` records a request; at the first block start once booted, and later whenever the request changes, a request that differs from the current sound sends `setSelect`'s bytes if they select it, else `Cn n`, once, counted in `programsSent`. At the boot's end the `setSelect` bytes go out even when the program number already matches (R2-0 against the boot's U0-0). `programSeq` counts tracked program changes that change the program, never the engine's own sends: the OS at times answers a MIDI-in program change, the engine's own included, by sending that selection on MIDI out, and that answer is not a second change.
- Evidence: a panel pick sends `B0 00 00 20 ll C0 pp` (interface doc, "MIDI"). Soft2 tracks as program 1 with that select (engine_tracks_panel_pick); `C0 05` from MIDI in as program 5 with nothing sent (engine_tracks_midi_in_program). After a warm boot `C0 20` comes back as U0-5 with one send (engine_restores_program_after_warm_boot); R2-2 (`B0 00 00 20 02 C0 0E`) and preset 81 (`B0 00 00 20 00 C0 51`) come back with the display as before (engine_restores_bank_and_preset). The restore goes out once, by 5400 ms, and a note at 5500 ms sounds (peak 0.163 against 0 before it), clear of the about 50 ms in which the OS ignores notes after a program change that loads an effect (engine_restore_happens_once_at_boot_end). In the runs tried the OS answered the first one or two MIDI-in program changes after a boot with `B0 00 00 20 00 C0 pp` on MIDI out, and later ones with nothing (engine_program_seq_and_adopt, engine_set_select_ignores_bad_bytes).
- Confidence: High for the bytes and the restore; Medium for other channels and the multi pages (not tried); Low for when the OS answers a MIDI-in program change (only seen, not explained).
- Status: settled, Plan 4 Task 2. `// DECISION A105` at the parser group (above `Engine::MidiParse::feed`), `Engine::track`, `Engine::setProgram` and `Engine::blockStart` (`src/app/engine.cpp`). Tests: engine_tracks_boot_as_program_zero, engine_tracks_panel_pick, engine_tracks_midi_in_program, engine_restores_program_after_warm_boot, engine_panel_pick_is_not_undone, engine_program_seq_and_adopt, engine_restores_bank_and_preset, engine_set_select_ignores_bad_bytes, engine_restore_happens_once_at_boot_end.

## A106: Analogs after the boot
- Options: (a) pass `setAnalog` to the machine at once; (b) hold the wanted raw values and apply them at a block start once the boot has ended.
- Choice: (b), as KS-32 A16, for the inputs the host has set. At each block start, once booted, an input whose wanted value differs from `Machine::analog` is applied; until then the machine holds `kAnalogRest` (T8). A GUI `Event::Kind::Analog` also records its value, so a block start does not undo it. `loadRam` puts every input back to its rest value, since the machine keeps them over a power cycle.
- Evidence: as KS-32 A16: the OS reads the inputs while it boots. The pitch wheel asked for at 0x100 stays at the rest 0x200 at 1000 ms and is 0x100 at 5400 ms, also after a `loadRam` (engine_analog_waits_for_boot, engine_load_ram_resets_analogs). Data Entry applied at the boot's end is a slider move to the OS, so it was checked that it edits nothing (Plan 4 final review, app_tests `dataEntryRestored`): a project saved on R2-2 with TUNE=+02 and Data Entry at 0.9 (raw 921, rest 0x14E = 334) comes back after the boot and 500 ms on R2-2 with the saved select and TUNE=+02, with the machine's Data Entry at the project's value. The same slider moved to 0.1 on the TUNE page then changes TUNE (to -96), so the input is live and the check can fail. On the play screen the OS takes the move without an edit.
- Confidence: High.
- Status: settled, Plan 4 Task 2. `// DECISION A106` at `Engine::setAnalog` (`src/app/engine.cpp`). Tests: engine_analog_waits_for_boot, engine_load_ram_resets_analogs; app_tests `dataEntryRestored`.

## A107: MIDI note mask
- Options: (a) none; (b) a 128-bit mask of the notes held on MIDI in, published in the snapshot, for the editor to draw those keys pressed.
- Choice: (b), as KS-32 A19. Note on (velocity above 0) sets bit n of `Snapshot::notes`, note off (or note on with velocity 0) clears it, on any channel; All Notes Off and All Sound Off (CC 123, 120) clear all. It follows MIDI in only, from the first byte, and `loadRam` clears it.
- Evidence: as KS-32 A19. Test: engine_note_mask.
- Confidence: High for the rule; a note held on two channels clears on the first off (a limit, not worked around).
- Status: settled, Plan 4 Task 2. `// DECISION A107` at `Engine::track` (`src/app/engine.cpp`).

## A108: MIDI in
- Options: (a) only channel messages; (b) every byte of every message, sysex and real-time included; and whether the engine converts pressure.
- Choice: (b), as KS-32 A12. Every byte of every `MidiBuffer` message goes to MIDI IN as the jack would carry it (`Engine::midi`, unchanged). The engine converts nothing: the firmware itself takes Poly-Key and channel pressure from MIDI (Musician's Manual, MIDI Control "PRESSURE": "Either Channel pressure or Poly-Key pressure will be received via incoming MIDI"). This replaces design section 10's pressure line.
- Evidence: as KS-32 A12: the jack receives the whole stream and the firmware decides what to read. The block store (A101) and the DUART (A1) bound a flood (engine_midi_overflow_is_counted).
- Confidence: High.
- Status: the engine side settled, Plan 4 Task 2: `// DECISION A108` at `Engine::midi` (`src/app/engine.cpp`). The processor side, Plan 4 Task 4: `// DECISION A108` at `TsProcessor::processBlock` (`app/source/Processor.cpp`).

## A109: Outputs
- Options: (a) one stereo output, Aux mixed into Main always; (b) two stereo output buses, Main and Aux, with Aux added into Main when the host or the Standalone leaves the Aux bus off.
- Choice: (b). `TsProcessor` declares output buses "Main" (stereo, enabled) and "Aux" (stereo, enabled except under `wrapperType_Standalone`). `isBusesLayoutSupported` takes no inputs, Main stereo, and Aux stereo or disabled. With Aux on, `processBlock` renders Main and Aux into their buses. With Aux off (disabled or no channels), it renders Aux into two scratch buffers of the prepared block size, allocated in `prepareToPlay`, and adds them into Main; a block longer than prepared is rendered in pieces of that size, each with its own MIDI, and a 0-sample block (the VST3 wrapper makes them) is one piece of 0 frames, so its MIDI still reaches the machine. `processBlockBypassed` (host bypass) runs `processBlock` and then clears the buffer: the machine keeps running and takes MIDI, so a note-off sent while bypassed is not lost, and the output is silent. The Standalone's default layout has Aux off, so it plays both on one stereo pair (design section 10); its wrapper also calls `disableNonMainBuses`.
- Evidence: the TS-10 has Main and Aux stereo outputs (design section 10), and the engine renders four channels (`Engine::render`). Two processors get the same blocks. After the boot the running OS routes track 1 to Aux: Track Effects, then Up four times steps Effects Bus Routing from VOICE to `-AUX-`, and row 1 shows it on both. Then both get `90 3C 7F`. Aux sounds (peaks 0.2207 L, 0.0406 R; Main 0.0001), and the one with Aux off plays on Main exactly the other's Main + Aux, sample for sample, from power-on through 500 ms after the note and a 1000-frame block (processor_aux_bus_off_mixes_into_main). The test fails with the two adds into Main removed (49 606 mismatches) and with Aux L and R swapped (49 362). With the boot's VOICE routing Aux is silent, so the route is needed for the test to mean anything. The built VST3 loaded through JUCE's host shows two output buses and plays with Aux off. app_tests `zeroAndBypassedBlocks`: with Aux off, after the boot, `90 3C 64` in a 480-frame block sets note 60 in `Snapshot::notes`; `80 3C 00` in a 0-sample block clears it (it stayed set before the fix); a note-off in `processBlockBypassed` clears it too (it stayed set with JUCE's default) and that block's output is silent.
- Confidence: High.
- Status: settled, Plan 4 Task 4. `// DECISION A109` at the `TsProcessor` constructor, `isBusesLayoutSupported`, `prepareToPlay` and `processBlock` (`app/source/Processor.cpp`). Tests: processor_aux_bus_off_mixes_into_main, `zeroAndBypassedBlocks` (`app/tests/app_tests.cpp`). `// DECISION A109` also at `processBlockBypassed`; the zero-length block and the bypass were added in the Plan 4 final review.

## A110: Analog positions
- Options: (a) as KS-32 A17, the wheels inverted; (b) no inversion; and whether Data Entry may reach raw 0.
- Choice: (b). A host or GUI position is 0-1, 0 at the bottom or toward the player, and the raw value is `lround(p * 1023)`, clamped to 0-1023. NaN gives the rest value, an infinity clamps. Data Entry is clamped to 1-1023. The defaults are the `kAnalogRest` positions.
- Evidence: nothing is inverted on the TS (docs/ts-hardware-interface.md, "Analog inputs"; KS-32's wheels read inverted, the TS's do not). Raw 0 on Data Entry makes the OS rewrite the selected value and show SYSTEM ERROR 144, so a slider or host lane at its bottom must never send it.
- Confidence: High.
- Status: settled, Plan 4 Task 3. `// DECISION A110` at `analogRaw` (`src/app/analog_map.h`). Tests: analog_map_round_trips_rest_values, analog_map_data_entry_never_zero.

## A111: Parameters
- Options: as KS-32 A17 and A18: (a) the analogs stay GUI events and nothing is automatable; (b) host parameters (an `AudioProcessorValueTreeState`) for the analogs and the sound, read by the audio thread as atomic raw values; and for the sound, (c) follow any change of `Snapshot::program`, or (d) follow only a change tracked from the panel or MIDI (`Snapshot::programSeq`) and mark the followed value so the audio thread adopts it without sending.
- Choice: (b) and (d), as KS-32 A17 plus the CV pedal, and KS-32 A18. Six parameters: `volume`, `dataEntry`, `pitchWheel`, `modWheel`, `cvPedal` (floats 0 to 1, named Volume, Data Entry, Pitch Wheel, Mod Wheel, CV Pedal, defaulting to the `kAnalogRest` positions, T8) and `sound` (an int 0 to 127, default 0). A position maps to raw by A110 (`analogRaw`); a non-finite `sound` reads as 0. `processBlock` passes the five analogs (`setAnalog`, applied once booted, A106) and `sound` to the engine before rendering. One 30 Hz timer, `tick()` (public, so the checks call it without a message loop), follows a panel or MIDI pick into `sound` inside a begin/end gesture: it stores the value in `followed_` first, then sets `sound`; `processBlock` adopts a value that matches the marker (`Engine::adoptProgram`, nothing sent) and requests any other (`setProgram`). A marker the host overwrote is cleared at the next tick, and no new follow starts while one waits for a block. A new engine resets the tick's last-seen sequence. The 60 s save counts 1800 ticks (A116). The battery and reference inputs are not parameters; pressure comes over MIDI (A108).
- Evidence: as KS-32 A17 and A18 (the order of the stores and the remaining panel-against-host race are argued there). Tests: app_tests `parameters` (six parameters by ID and name, `sound` 0 to 127, the defaults give `kAnalogRest`), `sound_follows_panel` (on the real ROMs a panel pick of Soft2 after the boot makes `sound` read 1 at the next tick; the block that takes it adopts it, and the engine has sent nothing), `follow` (as KS-32's: Soft2 then Soft3, and MIDI `C0 05` then `C0 06` a block apart, end on `sound` 2 and 6 with nothing sent; the machine moving on inside tick()'s writes, staged by a parameter gesture listener that runs blocks in which `C0 08` lands after tick() has read program 7, ends on 8 with nothing sent; the host setting `sound` to 13 then 14 a block apart sends exactly twice, ends on U0-2 with program 14, and a tick against the stale snapshot leaves `sound` at 14), `state_round_trip_project` (Volume 0.5 reaches the machine as `analogRaw(Volume, 0.5)`). `sound_follows_panel` fails with the follow removed from `tick()`; `follow` fails with `setProgram` in place of `adoptProgram` (the race sends program 7 back; the panel and MIDI picks alone pass with it, since in a single thread the followed value always equals the machine's sound at the next block) and with `adoptProgram` in place of `setProgram` (the host's 13 and 14 never reach the machine).
- Confidence: High.
- Status: settled, Plan 4 Task 5. `// DECISION A111` at `TsProcessor::layout`, the constructor's timer, `tick` and `processBlock` (`app/source/Processor.cpp`). The editor's sliders and wheels drive the parameters from Task 6.

## A112: Layout units
- Options: (a) the photo's pixels; (b) the photo scaled to a fixed design size and scaled uniformly to the window, as KS-32 A13.
- Choice: (b), 2576 x 920 design units. Default window 1546 x 552 (60 %), minimum 1030 x 368 (40 %), aspect fixed.
- Evidence: the arrangement and proportions follow `datasheets/ts10-photo.webp`, the left section and labels the Musician's Manual page 3 (design section 10). The table's rects were measured on the photo scaled to this size.
- Confidence: High for the units; Medium for single rects (measured by eye on the photo, within about 10 units).
- Status: settled, Plan 4 Task 3. `// DECISION A112` at `kDesignW` (`src/app/panel_layout.h`) and the `PanelEditor` constructor (`app/source/PanelEditor.cpp`, Task 6; the largest size is twice the design units, as KS-32 A13). Tests: panel_layout_fits_and_does_not_overlap, panel_layout_has_every_button; app_tests `editor` and `vst3` (the editor opens at 1546 x 552). Readability at the minimum size is the user's check.

## A113: Display
- Options: as KS-32 A7 and A8: for codes 0x00-0x1F and 0x7F, (a) guess glyphs or (b) draw them blank with a hex overlay on demand; the blink rate, (c) measured on a real TS-10 or (d) a fixed rate; and for a flashing underlined cell, (e) blink the character or (f) blink the underline.
- Choice: (b), (d) and (f). Codes 0x20-0x7E are drawn as ASCII in a monospaced font (Consolas on Windows, JUCE's default monospaced font elsewhere), one per cell of the 2 x 40 grid in `kDisplayText`, in the photo's teal (#7fe8d6 on #050807); the others are blank. An underlined cell draws a bar under its character. Flashing cells and flashing LEDs blink at 2 Hz, 50 % duty, from the message-thread clock: a flashing cell blinks its underline if it is underlined, else its character. A LED is lit when bit n of `PanelState::leds` is set and blinks when bit 16 + n is (T15); the Seqs/Songs LED follows `Snapshot::seqLed`. The display's right-click "View" menu toggles "Show display codes", every cell as two hex digits. With no engine the display shows `romError()` (or "No audio device" before `prepareToPlay`) and the LEDs are dark.
- Evidence: as KS-32 A7 and A8. The panel protocol gives each cell its own blink and underline bits (T15, `ts/panel.h`); the OS sends a flashing underline for a layered sound, where the Musician's Manual says "the underline beneath the name of the layered sound will flash" (docs/ts-hardware-interface.md, command `D2`), so (f) keeps the name readable while the underline flashes. app_tests `editor`, headless (the editor is never put on the desktop): with no ROMs the display shows the reason; on the real ROMs, after Presets, Bank 3 and Soft 4 clicked through the editor's parts, the image painted offscreen has lit display cells, LEDs 1 and 10 red and LED 9 dark, and the code overlay draws a different picture of the same cells. `TSR_SNAPSHOT_DIR=<folder>` writes the images.
- Confidence: High for 0x20-0x7E and the attribute bits; Low for the blink rate (a choice, not measured).
- Status: settled, Plan 4 Task 6. `// DECISION A113` at `LedPart::paint`, `DisplayPart::paint` and `PanelEditor::timerCallback` (`app/source/PanelEditor.cpp`).

## A114: Chord latch and stuck buttons
- Options: as KS-32 A9.
- Choice: as KS-32 A9. A right-click or Shift-click latches a button down until it is clicked again. A plain press is sent on mouse-down and its release on mouse-up, wherever the mouse is. Space presses the focused button while held. When the app stops being the foreground process, every unlatched button is released and a slider or wheel drag ends (the pitch wheel springs back); closing the editor releases everything, latched buttons too. A press or release the full queue (A102) turns away is retried from the 30 Hz timer, never dropped. The patch buttons are buttons too and post `PatchSelect` 0 (left) or 1 (right). A new engine (Erase, a ROM folder change) resets every part without posting.
- Evidence: as KS-32 A9 (a stuck button breaks later chords; each button sends only on a change of its own state, so a late mouse-up after a focus-loss release sends nothing). app_tests `editor` clicks Presets, Bank 3 and Soft 4 through the parts' mouse handlers and the OS shows a preset (row 1 starts `PSET`) with LEDs 1 and 10 lit.
- Confidence: High for the rules; the drag-off, chord and Alt-Tab checks are the user's.
- Status: settled, Plan 4 Task 6. `// DECISION A114` at `ButtonPart` and `PanelEditor::timerCallback` (`app/source/PanelEditor.cpp`).

## A115: Key strike
- Options: as KS-32 A10: the strike of a key clicked with the mouse, (a) fixed, (b) from where the key is clicked along its length, (c) from a modifier.
- Choice: (b), as KS-32 A10. Strike = 1 + round(126 x y / keyLength), y from the key's back edge and keyLength its drawn length (white and black keys alike): the front is loud. The value is the keyboard's strike (`Machine::key`), which the OS maps through its TOUCH table. A drag releases the key it leaves and plays the key it enters with the strike where it entered; the release carries the press's strike. Keys held on MIDI in (`Snapshot::notes`, A107) are drawn pressed.
- Evidence: as KS-32 A10. app_tests `editor`: C4 clicked 5 units from its back peaks at 0.0193 on Main, 340 units from it (the front) at 0.1276, on the preset the test selected.
- Confidence: Medium (a choice; the feel is the user's to judge).
- Status: settled, Plan 4 Task 6. `// DECISION A115` at `KeyPart::strikeAt` (`app/source/PanelEditor.cpp`).

## A116: Memory file
- Options: as KS-32 A11: when to write the battery memory file, (a) on close only, or (b) on close and every 60 s while running, never before the machine has booted; and for Erase, (c) delete the file, (d) restart with zero RAM and let the next save write it, or (e) write the zero image to the file at once and restart the machine from it.
- Choice: (b) and (e), as KS-32 A11. The memory (`ts-memory.bin` in `%APPDATA%\TS Reborn`, Linux `~/.config/TS Reborn`, or the folder in `TSR_SETTINGS_DIR`, unless changed) is the 512 KB `Machine::ramImage()` (T13). It is loaded at start; a file of the wrong size is reported on the settings page and ignored, the machine boots cold, and the first save replaces the file with the running memory. It is saved on close from `machine().ramImage()` (audio stopped) and every 60 s (1800 ticks of a 30 Hz timer) through `requestRam`/`takeRam`, each time only when `Snapshot::booted` (A104). Each save writes `<file>.tmp` and then replaces the file; a read-only file is not replaced and the settings page says "Memory not saved". Erase asks first, then writes 524 288 zero bytes to the file at once and restarts the machine from them, with no sound selection to replay. A restart during the boot keeps the image the machine started from, never a half-booted one. These are the Standalone's rules (File mode); a plugin instance starts from the file and keeps its memory in the project (A117).
- Evidence: as KS-32 A11: a save before the play screen could store a half-booted image, the temporary file keeps the previous image whole through a crash, and the 60 s save bounds what a crash loses. `app_tests` covers load, save over an existing file, no `.tmp` left, Erase leaving a 512 KB zero file, the wrong size, a read-only file, and a memory file inside the ROM folder refused in either order (`os/ts10_310l.bin` is 512 KB too). The quick-close and survive-reopen checks are the controller's manual check (Plan 4 Task 4 Step 5).
- Confidence: High.
- Status: settled, Plan 4 Task 4. `// DECISION A116` at `TsProcessor::~TsProcessor`, `eraseMemory`, `restart` and `tick` (`app/source/Processor.cpp`), `saveMemoryFile` (`app/source/MemoryFile.cpp`) and the memory note (`app/source/SettingsPage.cpp`).

## A117: State, RAM and memory per format
- Options: as KS-32 A20 to A23: (a) the parameters only, the memory file staying every format's memory; (b) one XML element holding the parameters, the selection to replay and, for a plugin, the RAM, with the Standalone keeping the memory file and each plugin instance keeping its memory in the project; and for an odd blob, (c) reset the instance, or (d) restore what it can and leave the machine as it is.
- Choice: (b) and (d), as KS-32 A20 to A23. `MemoryMode::File` under `wrapperType_Standalone` (`PluginHostType::getPluginLoadedAs()`), `MemoryMode::Project` for every plugin format (the checks construct directly: Project). The state is `copyXmlToBinary` of `<TSState version="1" select="b000002000c020">`: the parameter tree (A111), `select` (the replay bytes in hex, A105: once booted the running selection, before that the one the start image came with) and, in Project mode only, a `<RAM>` child with the 524 288-byte battery image (T13) in base64 (about 700 KB in all). On restore a `select` that ends `Cn pp` writes `pp` into the restored tree's `sound` before `replaceState`, so a state taken before the follow caught up still brings back the right sound. In Project mode a version 1 RAM of the right size restarts the machine from it (a warm boot) with that selection; with no ROMs it waits as the start image for `setRomFolder`. A blob that is not a `TSState` is ignored; no RAM, a wrong size or another version restores the parameters and `select` and leaves the machine as it is. Which RAM: the running RAM once booted; before that the image the machine started from (never a half-booted one); with no machine the image a state gave, unchanged. On the thread that last ran `processBlock` it is read directly; on any other the audio thread copies it (`requestRam`/`takeRam`) and the caller waits at most 300 ms, else reads it under `suspendProcessing`. One mutex, never taken by `processBlock`, guards the engine swap, that read and the start image. Project mode: the constructor starts from the shared memory file; there is no close save and no 60 s save; Erase restarts this instance cold from 512 KB of zeros and leaves the file alone (in both modes Erase clears the selection and sets `sound` to 0, so the machine stays on U0-0); "Copy to shared memory..." asks first and then writes the RAM to the shared file (A116's temporary file and replace), refused before the boot. File mode is A116; its state has no RAM, so the memory file stays the Standalone's only memory. Since A121 a File processor ignores every state given to it.
- Evidence: as KS-32 A20 to A23. JUCE's `StandalonePluginHolder` (`juce_StandaloneFilterWindow.h`, read in the KS-32 checkout's JUCE) calls `getStateInformation` when the window closes (`savePluginState`) and `setStateInformation` in `init` (`reloadPluginState`) before `startPlaying`, and makes the processor with `createPluginFilterOfType(wrapperType_Standalone)`. Tests (app_tests, real ROMs unless noted): `state_round_trip_project` (TUNE=+02, U0-5 by `C0 20` and Volume 0.5 come back in a new instance; the state is 699 406 bytes with `select` `b000002000c020`), `state_saved_during_boot_keeps_start_image` (a state 1000 ms into a restored boot holds the restored RAM byte for byte and its `select`), `state_without_roms_round_trips` (with an empty ROM folder the state comes back unchanged; once the ROMs are good it boots to U0-5), `state_rejects_bad_blobs` (100 random bytes, a 1000-byte RAM and `version="2"` keep the same engine and render on), `project_mode_leaves_shared_file` (past the 60 s save with a block to copy, Erase and a booted instance's destruction leave the file byte for byte; `copyToSharedMemory` is refused before the boot and then makes the file equal to the RAM), `stateThreads` (saves from this thread, another thread and the audio thread itself, with `processBlock` on its own thread, each restore TUNE=+02), `standalone_state_has_no_ram` (dummy ROMs: a File state has no `<RAM>`, a Project state from the same start has one), `bank_survives_restarts` (R2-2, `B0 00 00 20 02 C0 0E`, comes back from a state, across a ROM-folder change and across a new memory file; program 14 alone would land on U0-2; Erase then gives U0-0, `sound` 0 and nothing sent), `standalone_state_before_boot` (a File processor given that R2-2 state with TUNE=+02 before its first block came back on R2-2 with TUNE=+00; since A121 it boots on U0-0 instead). These fail with, in turn, the RAM restart removed, the 60 s save or the close save allowed in Project mode, RAM saved in File mode, `select` dropped on restore, a Keep or File restart not keeping the select, and the no-RAM branch of `setStateInformation` removed.
- Confidence: High.
- Status: settled, Plan 4 Task 5. `// DECISION A117` at `MemoryMode`, `startImage_`, `stateMu_` and `audioThread_` (`app/source/Processor.h`), the destructor, `eraseMemory`, `restart`, `tick`, `processBlock` (the thread store), `ram`, `getStateInformation`, `setStateInformation` and `copyToSharedMemory` (`app/source/Processor.cpp`), and the memory note (`app/source/SettingsPage.cpp`). Known limits as KS-32 A21 (a host that migrates `processBlock` between threads). The Standalone part of the state is replaced by A121.

## A118: Disk hand-over between threads
- Options: (a) a mutex around the machine's drive, (b) a lock-free queue of disk commands, (c) single-slot atomics as `requestRam`/`takeRam` (A116), one slot per direction.
- Choice: (c). `Engine::insertDisk` stores the new disk (nullptr ejects) in `swapIn_` and sets `swapWanted_` with release; the next `render` applies it with `Machine::insertDisk` before the block, parks the disk taken out in `removed_` (`removedFull_`, release) and counts `Snapshot::diskSwaps`. The message thread frees it from `takeRemoved`, so the audio thread never frees a disk. A swap waits while `removed_` is untaken, so none is lost. `requestDisk` hands over a scratch disk the caller allocated; the block start calls `Disk::copyInto` into it (`copyState_` 0, 1, 2) and `takeDisk` returns the scratch with `ok` false when the drive was empty or of another density. `Snapshot` also carries `diskIn` and `diskWrites`. `takeDisk` also returns `swapsAtCopy`, the swap count when the copy was served (set with the result, after any swap applied in that block): the snapshot's `diskSwaps` can be stale when its try_lock misses, so the app matches a copy to its disk by counting its own accepted inserts and ejects against `swapsAtCopy`. Rules: take the removed disk you are owed (an insert into an empty drive parks none) before the next swap, and free `takeDisk`'s scratch also when `ok` is false; the destructor frees what is left in a slot. `copyInto` copies 0.8 to 1.6 MB inside `render`, as A117's RAM copy does.
- Evidence: `rt_engine_disk_swap_does_not_allocate` (0 allocations in `render` for insert, copy and eject after the boot); `engine_insert_applies_at_block`, `engine_insert_twice_before_render`, `engine_copy_out`, `engine_copy_out_density_mismatch`, `engine_insert_into_empty_drive_does_not_stall`, `engine_copy_identifies_its_disk`, `engine_destroy_with_pending_slots`.
- Confidence: High.
- Status: settled, Plan 5 Task 5. `// DECISION A118` at `Engine::insertDisk` and the block start in `Engine::render` (`src/app/engine.cpp`) and in `engine.h`.

## A119: The disk drive's menu, saving and write protect
- Options: for the control, (a) a menu bar or settings-page section, (b) the drive drawn on the panel as the control; for saving, (c) write the file in place, (d) `<file>.tmp` then replace; for the copy to save, (e) suspend audio and read the machine, (f) ask the audio thread for a copy (A118) and write it when it comes; for write protect, (g) set the `Disk` flag from the message thread, (h) a request the engine applies at the block start; for a restart (ROM folder, Erase, a project's RAM), (i) the drive comes back empty, (j) the disk goes into the new machine.
- Choice: (b), (d), (f), (h), (j). The "Disk drive" control (`ControlKind::Decor`) is `DrivePart`: a click opens a menu with Open Disk Image..., New Blank Disk (DD), New Blank Disk (HD), Save Disk Image, Save Disk Image As..., Eject and a "Write Protected" tick; a file dropped anywhere on the panel is opened. The slot draws `DiskDrive::slotText()`: the file name, " (modified)" when the OS has written since the last open, new or save, "Untitled (blank)" for a new disk ("Untitled (modified)" once written), nothing with the drive empty. Open decodes by content (`decodeImage`; a raw file's size decides the density and another size is refused naming 819200 and 1638400 bytes); a file that fails leaves the drive as it was, and opening does not reset the machine (the OS sees a disk change). Save writes the file the disk came from in its format; a disk with no file or a read-only file can only be saved with Save As (the menu item is greyed and `save` refuses). Save As takes the format from the extension typed (`formatForName`: `.gkh` GKH, `.ede`/`.eda`/`.edt` Giebler, anything else raw), keeps the header read only when the format stays the same, and the file becomes the drive's. Giebler saves get their bitmap from the disk's contents (`encodeImage`). Saving is asynchronous: `save`/`saveAs` check what they can at once, ask the engine for a copy (a scratch of the disk's density) and return; `TsProcessor::tick` (30 Hz) calls `DiskDrive::poll`, which takes the copy, encodes it and writes `<file>.tmp`, then replaces the file (`saveMemoryFile`, A116), so a failed save keeps the old file; the message thread never waits. A copy is matched to its disk by `swapsAtCopy` against the drive's own count of accepted inserts and ejects (A118); a copy of another disk (an insert still waiting) or in a scratch of another density is asked for again while the disk to save is in, else the save fails. The drive keeps one copy request in the engine at a time and takes every answer, dropping one whose save is gone, so a request left unserved while the audio was stopped never blocks the next save. A disk put in or ejected gives up a save still waiting for its copy (its result is a failure), since without audio the copy never comes; the old file is untouched. From the menu, Open, New and Eject first go through the prompt (A120), which may wait for that save instead. "Modified" is the snapshot's `diskWrites` against the disk's count at the last open, new or save, only once the snapshot's `diskSwaps` is the drive's count; `Disk::copyInto` now copies the writes count, so the copy saved gives the new baseline. Write protect is the drive's tick, off at start: it is set on each disk before it goes in, and for the disk in the drive `Engine::setWriteProtect` stores a request in an atomic that the next block start applies after any swap of that block, so only the audio thread touches the inserted `Disk`. A restart takes the disk out of the old engine under `suspendProcessing` (`Engine::releaseDisk`, a pending insert or eject applied first) and inserts it into the new one; `TsProcessor::engineGeneration` tells the drive to count from 1 (0 with no disk), to send its write protect again and to ask the new engine for a pending save's copy. Without an engine (no ROMs) New and Eject say the machine is not running, not that the drive is busy. The drive is message-thread only (`setStateInformation` may run off the message thread in some hosts, the A117 exposure: a disk opened at the same moment can race with the restart it triggers, and the two may race; open a disk when the project is not loading); the disk is not remembered across a restart of the app, and neither the disk nor its path is in the plugin state. The same menu and JUCE's async `FileChooser` serve the Standalone and the VST3 on Windows and Linux; the chooser lists `.img .gkh .ede .eda .edt` in both cases (Linux matches case).
- Evidence: design `2026-11-14-ts-floppy` section 9 (the user's calls). `app_tests` `disk_drive` (real ROMs, temp files): a raw DD file opens as `disk.img`; a 1000-byte file is refused naming both sizes and the disk stays; a Write Data put into the controller between blocks (MSR at 0x280001, data at 0x280003) makes the slot read `disk.img (modified)`; Save clears it, leaves no `.tmp` and the file holds the written sector; with Write Protected the controller ends with ST1 NW (0x02) and nothing is modified; a read-only file refuses Save and is unchanged; a new HD disk refuses Save, and Save As `x.img` writes 1 638 400 bytes and becomes `x.img`; a ROM-folder restart keeps the disk in; a save asked for just before a restart is written by the new engine (and fails to be written without `follow`'s new request); a save followed at once by New fails, leaves the old file byte for byte and no `.tmp` (and overwrites it with the blank disk when both the give-up on insert and the copy's swap check are removed; either one alone holds); the slot paints its text and is empty after Eject. `engine_write_protect_at_block`, `engine_release_disk` and `disk_copy_into_needs_same_density` (writes copied) in `ens_tests`; the first and the last fail with the protect request or the copy of the count removed; `rt_engine_disk_swap_does_not_allocate` stays at 0.
- Final review: with an engine but no audio running (`!TsProcessor::prepared()`) Open, New and Eject are refused at once with "no audio device" (no swap is queued that nothing would apply); the "busy" message is only for a real pending swap. `TsProcessor::releaseResources` sets the rate back to 0, so `prepared()` is false once the device stops (before, it stayed true and a stopped device read as running).
- Confidence: High.
- Status: settled, Plan 5 Task 6. `// DECISION A119` in `app/source/DiskDrive.cpp`, `DrivePart` and `filesDropped` (`app/source/PanelEditor.cpp`), `restart` (`app/source/Processor.cpp`), `Engine::setWriteProtect`, `releaseDisk` and the block start (`src/app/engine.h`, `engine.cpp`) and `Disk::copyInto`. Not checked by a person yet: the menu, the dialogs and a drag and drop in a real window (user to run).

## A120: Prompts before a disk with unsaved writes is lost
- Options: (a) never ask, the slot's "(modified)" being the warning; (b) ask before Open, New, Eject, a dropped file and the Standalone's close; for the Standalone's close, (c) JUCE's default application, which quits at once, (d) the wrapper's documented hook `JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP` with an application of our own.
- Choice: (b) and (d). When `DiskDrive::modified()`, an async `AlertWindow` asks Save, Don't Save or Cancel. While a save is in flight (its copy not yet arrived) the disk still reads as modified: an action asked then does not ask again but waits for that save, runs when it succeeded and is dropped when it failed (its handler shows why), so the writes are never given up unasked. Only one action waits, and only while the audio runs (`DiskDrive::waitsForSave`): a copy that may never come (the device stopped, or a host that stopped calling `processBlock`) must not swallow every later close, so a second action, or any with the device stopped, gets the prompt, where Don't Save goes on. Save saves (Save As, with its chooser, when the disk has no writable file) and goes on once the file is written; a failed save shows why and stops; Don't Save goes on; Cancel stops. `app/source/StandaloneApp.cpp` is JUCE's `StandaloneFilterApp` (the same settings file, `TS Reborn.settings`, and the same `StandalonePluginHolder`) with a window whose close button, and the system's quit request, go through that prompt before saving the wrapper state and quitting. A close while any dialog is up is ignored: on Windows JUCE already drops `WM_CLOSE` on a window blocked by a modal component, on Linux it does not (`juce_XWindowSystem_linux.cpp` calls `handleUserClosingWindow` for WM_DELETE_WINDOW), so the window checks `ModalComponentManager` itself. The system's quit request keeps JUCE's behaviour: open dialogs are cancelled and the request retried after 100 ms, then the close (with the prompt) runs. The VST3 cannot ask: a plugin removed or a project closed with unsaved writes loses them, and the slot's "(modified)" is the only warning (design section 9).
- Evidence: design section 9; JUCE 8.0.16 `juce_audio_plugin_client_Standalone.cpp` (the default application, compiled only without `JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP`, and the `juce_CreateApplication` it then calls) and `juce_StandaloneFilterWindow.h` (`closeButtonPressed` saves the state and quits); `juce_Windowing_windows.cpp` (`WM_CLOSE` ignored while blocked by a modal component) and `juce_XWindowSystem_linux.cpp` (not on Linux). The dialogs are not driven by the checks (no window is opened); the logic they call is (A119). `disk_drive`: a save left without its copy lets one action wait and no second one, none with the device stopped (`releaseResources`), and the waiting action runs once the audio is back; these checks fail without the one-waiter rule and the `releaseResources` change.
- Confidence: Medium: the prompts have not been seen in a window yet. Gaps: the wrapper's Options menu "Reset to default state" deletes and re-creates the processor without asking, and a session end (log off) may not wait for the prompt.
- Status: settled, Plan 5 Task 6. `// DECISION A120` at `DiskDrive::askThen` (`app/source/DiskDrive.cpp`) and `TsWindow` (`app/source/StandaloneApp.cpp`). User to run: close the Standalone with a modified disk (Save, Don't Save, Cancel), and Open, New and Eject with one.

## A121: The Standalone does not restore a state
- Options: (a) as A117, the Standalone restores the sound and the controls from JUCE's wrapper state (`filterState` in `TS Reborn.settings`); (b) only a plugin restores a state: the Standalone ignores any state it is given.
- Choice: (b), the user's decision (2026-11-15): "Only the VST version should ever do that." `TsProcessor::setStateInformation` returns at once in `MemoryMode::File`. The Standalone starts with the six controls at their defaults and no sound to replay, so it boots on the sound its memory gives (U0-0 from a cold memory). The memory file is unchanged (A116): it still loads at start and is saved on close and every 60 s. The audio device, MIDI inputs and ROM folder are still kept. JUCE still writes its wrapper state on close and its Options menu still offers "Load a saved state..."; both are ignored.
- Evidence: the user's request. `app_tests` `standalone_state_before_boot`: a File processor given an R2-2 state with TUNE=+02 boots on U0-0, sends no program and keeps every parameter at its default (it came back on R2-2 with the saved controls before the change).
- Confidence: High.
- Status: settled 2026-11-15. `// DECISION A121` at `TsProcessor::setStateInformation` (`app/source/Processor.cpp`). User to run: change the sound and Volume in the Standalone, close it and start it again; the controls are back at their defaults.

## A122: The TS-10 logo and the Ensoniq badge on the panel
- Options: (a) as Plan 4, the model name "TS-10" as plain text and no Ensoniq name or logo; (b) the TS-10 logo as on the instrument and the Ensoniq badge, at the photo's places.
- Choice: (b), the user's decision (2026-11-15), replacing Plan 4's "no Ensoniq name or logo". The TS-10 logo is traced from the TS-10 Service Manual's cover (page 6 of the PDF, its 2096 x 3120 scan): the black letters, the triangle and the outline rings were separated by thickness and traced with potrace into `app/resources/ts10_logo.svg` (letters, triangle, outline as three paths). Its colours come from `datasheets/ts10-photo.webp`: letters #85A5A2, triangle #8894AF, outline #B4B8C4. The Ensoniq badge is the user's `app/resources/ensoniq_logo.svg`, drawn in the photo's silver (#A9A7A7). The line under the logo is text in #97938F. Places, measured on the photo at its layout scale (2576 / 3941): the logo in {2010, 152, 377, 106} (the photo's width, kept to the cover's shape, since the photo is seen at an angle), the line in {2052, 261, 330, 21}, the badge in {2307, 330, 162, 30}. Both SVGs are compiled in as JUCE binary data (`TsArt`).
- Evidence: the service manual cover and the photo; `ens_tests` `panel_layout_decals_are_clear` (no control under the three decals); `app_tests` `editor` (the logo's triangle and the badge are painted).
- Confidence: High for the shapes, Medium for the colours (one photo, under its lighting).
- Status: settled 2026-11-15. `// DECISION A122` at the decals in `src/app/panel_layout.cpp` and at the decal colours and drawing in `app/source/PanelEditor.cpp`.

## A123: Panel label size
- Options: (a) as Plan 4, each label line drawn in a font as high as the line (Segoe UI's letters then fill about 70 % of it: capitals about 3.5 px at the default size); (b) the same boxes and places, with a larger font and a gap kept between neighbours; (c) larger boxes, moving the labels away from the photo's places.
- Choice: (b), the user's request (2026-11-15: the placement is good, the text is small). A button's two-line label (box 20 or more high) uses a font 1.3 times its line height, a one-line label 1.12 times. The two lines of a two-line label are 1.12 lines apart: the second stays where it was, next to its button, and the first moves up into the free space above. A line may fill 80 % of a two-line box's width (the Track Parameters, Sequencer and Programming boxes are 44 wide on a 40 pitch, so neighbours keep a gap) and all of a one-line box's; a wider line is squeezed sideways down to half width, never made smaller. No layout rect changes.
- Evidence: the editor snapshot (`TSR_SNAPSHOT_DIR`, `panel.png`) before and after at the default 1546 x 552: the labels are about 30 % taller, and "Program Control" / "Program Effects" stay apart. `app_tests` `editor` passes unchanged.
- Confidence: Medium: judged by eye on the snapshot; Linux draws a fallback for Segoe UI.
- Status: settled 2026-11-15. `// DECISION A123` at `kLabelGrow` and the label drawing in `app/source/PanelEditor.cpp`. User to run: read the labels in a real window.

## A124: The power-on auto-load question in the VST3
- Options: (a) leave it: OS v3.10 asks "AUTO-LOAD ALL SAMPLED-SOUNDS *YES* / *NO *" at every power-on while its memory remembers a loaded sampled sound (Musician's Manual, "About the Auto-Load Feature": sample RAM is lost at power-off, the names, disks and bank places are kept); (b) the app answers *NO * for the player during the boot; (c) keep sample RAM across restarts.
- Choice: (b) for the VST3 (`MemoryMode::Project`), (a) for the Standalone: the user's decision (2026-11-15). `Engine::setAnswerAutoLoad(true)`, set by `TsProcessor::restart` in Project mode, makes the engine watch the display until the boot ends (A104): when row 0 starts "AUTO-LOAD ALL SAMPLED-SOUNDS" and row 1 shows "*NO *" at columns 35-39, it holds Soft6 (the soft button under it) for 100 ms on the panel link, as a player would, once per power-on. The firmware is unchanged. The OS then goes on as after a player's *NO *: no sampled sound is loaded, and the bank pages keep the remembered names, so the Standalone, sharing the memory file, still asks. The same question after loading a SAMPLE-BANKS or 60-SEQ/SONGS file comes after the boot and is left to the player.
- Evidence: a copy of the user's memory file boots to the question at 2.78 s (`tsemu boot --ram`). `ens_tests` `engine_answers_power_on_auto_load` (needs `ENS_DISK_DIR`): memory left by loading ED-001's first sound into S8-0 asks in both engines; the one told to answer is on U0-0 after 6 s, the other still asks (it failed before the engine answered).
- Confidence: High.
- Status: settled 2026-11-15. `// DECISION A124` at `Engine::setAnswerAutoLoad` (`src/app/engine.h`), `Engine::answerAutoLoad` (`src/app/engine.cpp`) and `TsProcessor::restart` (`app/source/Processor.cpp`). User to run: open the VST3 in a host with memory that remembers a sampled sound; it boots to the sound page without asking.
- Update (Plan 7, A130): with stand-in media given to the engine (`Engine::setRestore`), the same watch presses *YES* (Soft3) instead, and the boot end waits for the restore (A131). Without stand-ins the answer stays *NO * as above.

## A125: Where the app finds the ROMs
- Options: (a) as Plan 4, the stored folder, else `roms` beside the app (stored); a stored folder that is gone is an error; (b) the first of: the stored folder if it holds the six ROMs, `roms` beside the program (stored), the ROMs inside the VST3 package (not stored); else the stored folder as it is.
- Choice: (b), from the Plan 6 design (the user's decision 2026-11-15 to put the ROMs inside the VST3 too). `findRomFolder(stored, program)` in `app/source/RomFolder.cpp`: "holds the six ROMs" is every `checkRomFolder` entry `Found`. `program` is `currentExecutableFile`: the exe in the Standalone, the plugin binary in the VST3. The package case applies only when the program's parent's parent is named `Contents` (`TS Reborn.vst3/Contents/x86_64-win/TS Reborn.vst3`, `.../x86_64-linux/TS Reborn.so`); the folder is `Contents/Resources/roms`. It is used through `setRomFolder(dir, err, false)`, so `settings.xml` does not point at the package's `Resources/roms`. The processor keeps the folder in use (`romFolder()`, `romFolderStored()`): the settings page shows it, marked `(inside the VST3 package)` when it is not the stored one, and the memory-file guard (A116) checks against it, so a memory file inside the package's ROM folder is refused (final review). With nothing found, a stored folder is kept so the settings page and the display still name its problem.
- Evidence: `app_tests` `rom_folder_order`: stored good, stored missing (beside taken and stored), package (taken, not stored), not a package (a `../Resources/roms` ignored), only a bad stored folder (kept), beside before the package. `rom_folder_beside_app` unchanged. `rom_folder_in_use`: a package folder used without storing is the folder in use, a memory file inside it is refused (it was accepted before the final-review fix), and the stored folder taken again without storing still reads as stored.
- Confidence: High.
- Status: settled, Plan 6 Task 1. `// DECISION A125` at `findRomFolder` and the `TsProcessor` constructor.

## A126: The release folder
- Options: (a) as KS-32 Reborn Plan 8, a folder with the program, the VST3 and the docs, the ROMs only on request (`-RomFolder`); (b) the same with the six ROMs always, beside the program and inside the VST3 package; (c) an installer or a zip.
- Choice: (b), the user's decisions (2026-11-15): the release includes the ROMs, only those the program uses, also inside the VST3. `tools/dist.ps1` (Windows) and `tools/dist.sh` (Linux) check the six files (`os/ts10_310h.bin` 262 144 bytes, `os/ts10_310l.bin` 524 288, `waves/901401_u41.bin` 2 097 152, `901301_u43.bin` 1 048 576, `901402_u45.bin` 2 097 152, `901302_u46.bin` 1 048 576) in the ROM folder (default the repo's `roms`) before building anything, build `TSReborn_Standalone` and `TSReborn_VST3` in Release in `build-dist` / `build-dist-linux`, then replace `dist/TS Reborn-<version>` (Linux `-linux`) with: the program, the `TS Reborn.vst3` package, `README.md`, `LICENSE`, `docs/tsapp.md` as `APP-GUIDE.md`, and the six ROMs in `roms/` and in `TS Reborn.vst3/Contents/Resources/roms/` (A125 finds them there). Windows links the C++ runtime statically (`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`), so the Windows release needs nothing installed. Linux links libstdc++ and libgcc statically but uses the system's glibc (2.38 or later: built on Ubuntu 24.04) and its desktop libraries (ALSA, fontconfig, freetype, libpng, zlib, bzip2, brotli, expat), as `ldd` and `objdump -T` show. JUCE: downloaded by CMake into the build folder unless `-JuceSource` / `--juce-source` names a checkout. The version is 1.0.0. No zip, installer or signing; `dist/` is git-ignored.
- Evidence: the runs in `docs/verification-plan6.md`; `app_tests` with `TSR_RELEASE_VST3` set loads the release VST3 with no ROM folder set: it boots and plays on its own ROMs and leaves no ROM folder in the settings (with a package that has no ROMs it plays nothing).
- Confidence: High.
- Status: settled, Plan 6. `DECISION A126` in the header comments of `tools/dist.ps1` and `tools/dist.sh`.

## A130: The power-on auto-load answers on restore (updates A124)
- Options: (a) *NO * always (A124); (b) *YES* when the engine holds stand-ins for the remembered media (A131), *NO * when it holds none; for the boot end after *YES*, (c) A104's fixed 5300 ms, (d) the end of the restore.
- Choice: (b) and (d). `Engine::setRestore` with stand-ins makes the A124 watch press Soft3 (under *YES*, row 0 columns 35-39) for 100 ms; without stand-ins `setAnswerAutoLoad` keeps *NO *. The boot (A104: the tracker's boot flag and `Snapshot::booted`) waits until `Snapshot::restoring` ends (A131), so no program change goes into MIDI in while the OS loads.
- Evidence: throwaway runs on the machine (2026-11-16, not committed):
  - ED-001's FLUTE 1 loaded into S8-0 (the load read 314 blocks), the RAM kept, a power-on with the drive empty: the question at 2.79 s ("AUTO-LOAD ALL SAMPLED-SOUNDS       *YES*" over "*NO *", columns 35-39); *YES* pressed at 3.5 s; at 5.54 s the prompt, row 0 "INSERT DISK WITH NAME  ED-001     * OK *", row 1 "INTO DISK DRIVE                   *SKIP*". ED-001 put in and *OK* pressed: LOADING FLUTE 1 at 9.3 s, the play screen (U0-0) at 11.45 s, the same 314 blocks read.
  - The same with ED-001 in the drive from power-on: no prompt, LOADING FLUTE 1 at 3.46 s, U0-0 at 5.60 s, after A104's 5300 ms.
  - CDR-13's ETHNIC PERC2 (3927 blocks) loaded from ID 4, a power-on with no disc, *YES*: at 7.6 s row 0 "INSERT DISK WITH NAME CDR-013     * OK *", row 1 "INTO SCSI DEVICE 4                *SKIP*": the OS asks for a SCSI volume by its name and the device ID (as the manual's Section 14 prompts 3 and 4 say). With the disc in at power-on: LOADING at 7.7 s, U0-0 at 23.6 s.
  - The prompt is built from the OS's words near 0xc20a61 ("INSERT ", "* OK *", "*SKIP*", "DEVICE "). *OK* is Soft3 and *SKIP* Soft6 (columns 34-39 of rows 0 and 1). The name is the 7 label bytes of the ID block at columns 22-28 (A131), so ED-001's " ED-001" shows with its leading space.
  - `engine_restore_floppy`, `engine_restore_cd`, `engine_restore_two_cds`, `engine_restore_skips_unknown_disk` (A131).
- Confidence: High.
- Status: settled, Plan 7 Task 7. `// DECISION A130` at `Engine::answerAutoLoad` and `Engine::blockStart` (`src/app/engine.cpp`). The VST3 uses it once the app passes stand-ins (Tasks 8, 9); until then it answers *NO * as A124.

## A131: Stand-in media for the restore
- Options: for what the stand-ins hold, (a) the image files (gone when a project moves), (b) the blocks the OS read, kept in the project; for two CDs at ID 4, (c) told apart by the volume name the OS asks for, (d) only the CD loaded from last (the design's fallback); for the label, (e) block 2, (f) block 1.
- Choice: (b), (c), (f).
  - `MediaRecord` (`src/app/media_record.h`) keeps per medium its kind (floppy DD, HD, CD), label and the blocks read, each block from its first capture: floppy 512-byte blocks from a disk copy's read marks (`captureFloppy`), CD 2048-byte sectors as served (`captureCd`). A floppy is keyed by kind and label; an unnamed one goes to the last medium when that is an unnamed floppy of the same kind, else a new one. A CD sector goes to the medium whose sector 0 (blocks 0-3, the ID block at 1) was captured last, found by label and size; sectors before any sector 0 are dropped. A disk copy with nothing read adds no medium.
  - `ensoniqLabel` reads the Device ID block, block 1: the 7 bytes at 31-37 (after 0xFF at 30, before "ID" at 38-39), spaces and NULs trimmed; "" without "ID". `standInFloppy` is a formatted disk of the medium's density with the saved blocks and zeros elsewhere, no reads marked.
  - `Engine::setRestore(StandIns)` (after `loadRam`, before the audio, with the drive empty) puts the first floppy and the first CD in and sets `restoring`. At each block start while restoring, after the question is answered (A130), `restoreStep` answers a prompt (row 0 starts "INSERT DISK WITH NAME " with "* OK *" at 34-39, row 1 "*SKIP*" at 34-39) by the name at columns 22-28 trimmed: row 1 "INTO DISK DRIVE " looks among the floppies, "INTO SCSI DEVICE 4 " among the CDs. A match goes in (a floppy moves from its slot to the drive and the one there back to its slot; a CD by its medium id) and *OK* is held 100 ms; no match: *SKIP*. The next prompt is answered only after the display has left this one.
  - The restore ends once A104's boot time has passed and row 0 starts "U0-0 " (the play screen the OS shows after the auto-load as after any boot, A105), or at 120 s of emulated time (an error page the OS waits on). The floppy stand-in then goes back to its slot (the drive is left empty) and the CD is ejected. While restoring, the disk, CD, write-protect and copy slots (A118, A119, A128) wait, so a disk or CD queued after `setRestore` goes in once the restore ends. `takeStandIns` (once `restoring` is false) returns the floppy stand-ins, the blocks the auto-load read marked. The audio thread only moves preallocated objects.
- Evidence:
  - The label: block 1 of ED-001 holds " ED-001" at 31-37, CDR-13's "CDR-013", CDR-15's "CDR-015", each after 0xFF and before "ID". Block 2 of ED-001 has a copy at 58-63 and CDR-13's block 2 none, so block 2 (the task brief's guess) is not where it is. The OS prints those 7 bytes in its prompt (A130) and reads block 1 on a CD mount (T28).
  - The OS asks for a volume by name (A130), so the two-CD case works as the design hoped and the fallback (d) is not needed.
  - Tests (ROMs and `ENS_DISK_DIR`; the stand-in CDs are served by the test from the recorded sectors only, a missing one answered as a failure): `engine_restore_floppy`: FLUTE 1, 314 blocks recorded; the auto-load from the stand-in read 314 blocks, none outside the record, with no prompt; the restore ends at 5.31 s; C-E-G is sample-identical to a restore from the full ED-001. `engine_restore_cd`: CDR-13's BONGOS 1 (546 blocks, PERCUSSION/DRUMS), 143 sectors recorded, none missing at restore; ends at 9.5 s; identical to a restore served from the image. `engine_restore_two_cds`: BONGOS 1 into S8-0 and CDR-15's LIS-10 SCR 4 (1004 blocks, L A RIOT-1/LIS-10) into S8-1 (1550 of the 4018 free blocks): the OS loads S8-0 from the first stand-in, prompts once for CDR-015 at ID 4, the engine swaps it in and presses *OK*, ends at 14.2 s, and both sounds play from sample RAM; with the engine pressing *SKIP* instead, S8-1 shows "--NOT LOADED--" and starts no sample voice. `engine_restore_skips_unknown_disk` (Review Focus 4): the only stand-in is ED-001's record renamed OTHER; the OS prompts for ED-001, the engine presses *SKIP*, the boot ends on U0-0. `engine_restore_without_question_ends_at_boot`: stand-ins for a cold memory (no question): the restore ends at the boot time on U0-0, the drive empty (it never ended before the prompt step ran outside the question). `media_label_from_id_block`, `media_capture_first_wins`, `media_capture_cd_by_volume`. `rt_engine_restore_does_not_allocate`: a wrong first floppy, the prompt, the swap, the load and the end: 0 allocations in render from power-on.
- Confidence: High for the prompts, the matching and the record; Medium for the end rule (the play screen) and the 120 s cap, which no test reaches.
- Status: settled, Plan 7 Task 7. `// DECISION A131` at `ensoniqLabel` (`src/app/media_record.h`, `.cpp`), `StandIns` (`src/app/engine.h`), `kRestoreCap`, `Engine::restoreStep` and the slots in `Engine::render` (`src/app/engine.cpp`).

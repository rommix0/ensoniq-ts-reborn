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
- Ruling R13 (Task 9 fix round): read-back returns the written 24-bit value; arithmetic ignores bits 3..0. DLENGTH, ABASE, BBASE and DBASE therefore store the full 24-bit value as written (the option (a) text above "writes keep bits 23..4 and reads return the low 4 bits as 0" is superseded), and every use in address arithmetic (Agen::physical, endOfSample, DBASE reload, RAM clear, host RAM access) goes through the bits-23..4 view (`dlength20()` etc.), so address behaviour is unchanged (DLENGTH = 0xFFFFFF still behaves as 0xFFFFF0 and gives the 64K ring). The decrement result and a DBASE reload have bits 3..0 clear. Reason: the KS-32 boot writes DLENGTH = 0xFFFFFF and verifies it by read-back with retries (esp_init_sequence.json); a read-back of 0xFFFFF0 would fail that compare.
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
- Status: Implemented in Esp::writeReg (Task 8). Test: esp_narrow_spr_widths.

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
- Status: settled, Plan 7 Task 1. `// DECISION A1` at `Channel::pending` (`src/duart/duart.h`) and in the test. Test: duart_rx_queue_is_bounded; Plan 6's hashes are unchanged.

## A2: Transmit and panel rings
- Options: (a) take-by-swap vectors (Plan 6), which allocate every slot; (b) fixed rings that drop the newest byte when full and count it.
- Choice: (b). The DUART's sent-characters ring holds 256 per channel (`Duart::popTransmitted`, `Duart::txDropped`), and the panel's bytes for the firmware 256 (`Panel::popToCpu`). `Machine::slot` drains the panel channel both ways every slot; the caller pops MIDI out with `Machine::popMidiOut` (the engine every block, Task 2; `ks32emu` every frame).
- Evidence: a slot is 16 CPU clocks (about 1 us), and one character takes about 300 slots at 31 250 baud, so the panel rings never hold more than a few bytes; 256 bytes of MIDI out is about 80 ms of the line. The order is the old FIFO's, so the boot trace, render and smoke PCM hashes are unchanged (d2afd0fed8861cbd, 2bae46b3acc34cd0, a479ba867298cc55).
- Confidence: High.
- Status: settled, Plan 7 Task 1. `// DECISION A2` at `Channel::sent` (`src/duart/duart.h`), `Panel::toCpu_` (`src/ks32/panel.h`) and `Machine::slot` (`src/ks32/machine.cpp`). Tests: duart_pop_transmitted_in_order, panel_pop_to_cpu, rt_machine_runframe_does_not_allocate.

## OTTO

(None yet.)

## TS machine

(None yet.)

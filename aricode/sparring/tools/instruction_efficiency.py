#!/usr/bin/env python3
"""
instruction_efficiency.py - Analyze instruction-level efficiency of compiled binaries.

Parses objdump output and calculates efficiency metrics, identifies redundant
instructions, and suggests missed optimizations.

Usage:
    python3 instruction_efficiency.py <binary> [function_name]
    python3 instruction_efficiency.py --objdump-file <file> [function_name]
"""

import sys
import re
import subprocess
import argparse
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from typing import Optional


@dataclass
class Instruction:
    address: int
    raw_bytes: list[int]
    mnemonic: str
    operands: str
    line_num: int

    @property
    def byte_count(self) -> int:
        return len(self.raw_bytes)


@dataclass
class Optimization:
    severity: str           # "high", "medium", "low"
    instruction_num: int    # 1-based index
    description: str
    current: str
    suggested: str


@dataclass
class EfficiencyReport:
    function_name: str
    binary_name: str
    instructions: list[Instruction] = field(default_factory=list)
    optimizations: list[Optimization] = field(default_factory=list)
    redundancies: list[str] = field(default_factory=list)
    total_bytes: int = 0
    minimum_bytes: int = 0
    efficiency_score: float = 0.0


# Minimum possible encoding sizes for common x86_64 instructions (in bytes)
MIN_ENCODING = {
    "nop": 1,
    "ret": 1, "retq": 1,
    "push": 1, "pushq": 1,
    "pop": 1, "popq": 1,
    "syscall": 2,
    "int": 2,
    "cdq": 1, "cqo": 2, "cdqe": 2,
    "leave": 1, "leaveq": 1,
}

# Instructions that can typically be encoded in 2 bytes (reg,reg form)
TWO_BYTE_REG_REG = {
    "add", "addl", "sub", "subl", "xor", "xorl", "and", "andl",
    "or", "orl", "cmp", "cmpl", "test", "testl", "mov", "movl",
}

# 3-byte reg,reg for 64-bit (REX.W prefix)
THREE_BYTE_REG_REG = {
    "addq", "subq", "xorq", "andq", "orq", "cmpq", "testq", "movq",
}


def parse_objdump(text: str, function_name: str = "main") -> list[Instruction]:
    """Parse objdump -d output and extract instructions for the given function."""
    lines = text.split("\n")
    instructions = []
    in_function = False
    func_patterns = [f"<{function_name}>:", f"<{function_name}()>:"]

    for line in lines:
        # Check for function start
        if any(pat in line for pat in func_patterns):
            in_function = True
            continue

        if in_function:
            # Empty line = end of function
            if not line.strip():
                break

            # Parse instruction line
            # Format: "  addr:  hex bytes        mnemonic operands"
            match = re.match(
                r'\s+([0-9a-f]+):\s+((?:[0-9a-f]{2}\s)+)\s*(\S+)\s*(.*?)(?:\s*#.*)?$',
                line
            )
            if not match:
                continue

            addr = int(match.group(1), 16)
            raw_hex = match.group(2).strip().split()
            raw_bytes = [int(b, 16) for b in raw_hex]
            mnemonic = match.group(3).strip()
            operands = match.group(4).strip()

            instructions.append(Instruction(
                address=addr,
                raw_bytes=raw_bytes,
                mnemonic=mnemonic,
                operands=operands,
                line_num=len(instructions) + 1,
            ))

    return instructions


def estimate_minimum_bytes(insn: Instruction) -> int:
    """Estimate the minimum possible encoding for this instruction's semantics."""
    m = insn.mnemonic.lower()

    # Check fixed-size encodings
    if m in MIN_ENCODING:
        return MIN_ENCODING[m]

    ops = insn.operands

    # Check for register-only operands (no memory references)
    has_mem = "(" in ops or "[" in ops

    if not has_mem:
        # Pure register operations
        if m in TWO_BYTE_REG_REG:
            return 2
        if m in THREE_BYTE_REG_REG:
            return 3

        # Conditional jumps (short form)
        if m.startswith("j") and m != "jmp":
            return 2  # short Jcc
        if m == "jmp" or m == "jmpq":
            return 2  # short jmp

        # LEA
        if m in ("lea", "leaq", "leal"):
            return 3

        # CALL (near)
        if m in ("call", "callq"):
            return 5  # near call

        # MOV immediate to register
        if ops.startswith("$"):
            imm_str = ops.split(",")[0].replace("$", "").strip()
            try:
                imm = int(imm_str, 0)
                if m.endswith("q") or m.endswith("absq"):
                    if 0 <= imm <= 0xFFFFFFFF:
                        return 5  # mov $imm32, %r32 (zero-extends)
                    return 10  # movabs
                return 5  # mov $imm32, %r32
            except ValueError:
                pass

        # SHL/SHR by 1 can be 2 bytes
        if m in ("shl", "shll", "shr", "shrl", "sar", "sarl") and ops.startswith("$0x1,"):
            return 2

        # XOR/SUB same-reg (dependency break) - 2 bytes for 32-bit
        if m in ("xor", "xorl", "sub", "subl"):
            parts = [p.strip() for p in ops.split(",")]
            if len(parts) == 2 and parts[0] == parts[1]:
                return 2

    else:
        # Memory operations
        if m.startswith("mov"):
            return 3  # Minimum for simple addressing
        return 3

    # Default: assume current encoding is near-optimal
    return max(1, insn.byte_count - 1)


def find_redundant_instructions(instructions: list[Instruction]) -> list[str]:
    """Identify potentially redundant instructions."""
    redundancies = []

    for i, insn in enumerate(instructions):
        m = insn.mnemonic.lower()
        ops = insn.operands

        # MOV reg, reg where src == dst (useless move)
        if m.startswith("mov") and "(" not in ops:
            parts = [p.strip() for p in ops.split(",")]
            if len(parts) == 2 and parts[0] == parts[1]:
                redundancies.append(
                    f"  #{insn.line_num}: {m} {ops} - Redundant self-move (can be eliminated)"
                )

        # PUSH immediately followed by POP to same register
        if i + 1 < len(instructions):
            next_insn = instructions[i + 1]
            if m.startswith("push") and next_insn.mnemonic.lower().startswith("pop"):
                push_reg = ops.strip()
                pop_reg = next_insn.operands.strip()
                if push_reg == pop_reg:
                    redundancies.append(
                        f"  #{insn.line_num}-{next_insn.line_num}: "
                        f"push/pop {push_reg} - Cancel each other out"
                    )
                elif push_reg and pop_reg:
                    redundancies.append(
                        f"  #{insn.line_num}-{next_insn.line_num}: "
                        f"push {push_reg} / pop {pop_reg} - Could use mov {push_reg},{pop_reg}"
                    )

        # NOP in the middle of a function (not alignment at start)
        if m in ("nop", "nopw", "nopl", "nopq") and i > 0 and i < len(instructions) - 1:
            redundancies.append(
                f"  #{insn.line_num}: {m} - Unnecessary padding within function body"
            )

        # TEST reg, reg followed by another TEST/CMP that would set same flags
        if (m.startswith("test") or m.startswith("cmp")) and i + 1 < len(instructions):
            next_m = instructions[i + 1].mnemonic.lower()
            if next_m.startswith("test") or next_m.startswith("cmp"):
                redundancies.append(
                    f"  #{insn.line_num}: {m} {ops} - Flags immediately overwritten by "
                    f"next instruction ({next_m})"
                )

        # MOV to register, then immediately MOV from it to another register
        # (could potentially combine with LEA or addressing mode)
        if (m.startswith("mov") and "(" not in ops and i + 1 < len(instructions)):
            next_insn = instructions[i + 1]
            next_m = next_insn.mnemonic.lower()
            if next_m.startswith("mov") and "(" not in next_insn.operands:
                parts_cur = [p.strip() for p in ops.split(",")]
                parts_next = [p.strip() for p in next_insn.operands.split(",")]
                if (len(parts_cur) == 2 and len(parts_next) == 2
                        and parts_cur[1] == parts_next[0]
                        and parts_cur[0].startswith("$")):
                    redundancies.append(
                        f"  #{insn.line_num}-{next_insn.line_num}: "
                        f"mov {ops} then mov {next_insn.operands} - "
                        f"Could directly mov {parts_cur[0]},{parts_next[1]}"
                    )

    return redundancies


def find_missed_optimizations(instructions: list[Instruction]) -> list[Optimization]:
    """Identify cases where different instructions could be more efficient."""
    optimizations = []

    for i, insn in enumerate(instructions):
        m = insn.mnemonic.lower()
        ops = insn.operands

        # ADD + MOV could be LEA
        if m.startswith("add") and "(" not in ops and i + 1 < len(instructions):
            next_insn = instructions[i + 1]
            if next_insn.mnemonic.lower().startswith("mov") and "(" not in next_insn.operands:
                parts_add = [p.strip() for p in ops.split(",")]
                parts_mov = [p.strip() for p in next_insn.operands.split(",")]
                if (len(parts_add) == 2 and len(parts_mov) == 2
                        and parts_add[1] == parts_mov[0]):
                    optimizations.append(Optimization(
                        severity="medium",
                        instruction_num=insn.line_num,
                        description="ADD + MOV sequence could be replaced with LEA",
                        current=f"{m} {ops}; {next_insn.mnemonic} {next_insn.operands}",
                        suggested=f"lea ({parts_add[1]},{parts_add[0]}),{parts_mov[1]}",
                    ))

        # MOV $0, reg -> XOR reg, reg (shorter encoding, breaks dependencies)
        if m.startswith("mov") and "(" not in ops:
            parts = [p.strip() for p in ops.split(",")]
            if len(parts) == 2 and parts[0] in ("$0x0", "$0", "$0x00000000"):
                optimizations.append(Optimization(
                    severity="high",
                    instruction_num=insn.line_num,
                    description="MOV $0 should be XOR reg,reg (shorter, breaks deps)",
                    current=f"{m} {ops}",
                    suggested=f"xor {parts[1]},{parts[1]}",
                ))

        # IMUL by power of 2 -> SHL
        if m.startswith("imul") and "$" in ops:
            imm_match = re.search(r'\$0x([0-9a-f]+)', ops)
            if imm_match:
                try:
                    val = int(imm_match.group(1), 16)
                    if val > 0 and (val & (val - 1)) == 0:
                        shift = val.bit_length() - 1
                        optimizations.append(Optimization(
                            severity="high",
                            instruction_num=insn.line_num,
                            description=f"IMUL by {val} (power of 2) should be SHL by {shift}",
                            current=f"{m} {ops}",
                            suggested=f"shl ${shift}, <reg>",
                        ))
                except ValueError:
                    pass

        # ADD reg, reg (same register) -> SHL $1, reg
        if m.startswith("add") and "(" not in ops and "$" not in ops:
            parts = [p.strip() for p in ops.split(",")]
            if len(parts) == 2 and parts[0] == parts[1]:
                optimizations.append(Optimization(
                    severity="low",
                    instruction_num=insn.line_num,
                    description="ADD reg,reg (doubling) - SHL $1 is equivalent, same speed",
                    current=f"{m} {ops}",
                    suggested=f"shl $1,{parts[1]} (equivalent, context-dependent)",
                ))

        # SUB $1, reg -> DEC reg (shorter)
        if m.startswith("sub") and ops.startswith("$0x1,"):
            reg = ops.split(",")[1].strip()
            saved = insn.byte_count - 1 if "q" not in m else insn.byte_count - 3
            if saved > 0:
                optimizations.append(Optimization(
                    severity="low",
                    instruction_num=insn.line_num,
                    description="SUB $1 can be replaced with DEC (shorter encoding)",
                    current=f"{m} {ops}",
                    suggested=f"dec {reg}",
                ))

        # ADD $1, reg -> INC reg (shorter)
        if m.startswith("add") and ops.startswith("$0x1,"):
            reg = ops.split(",")[1].strip()
            saved = insn.byte_count - 1 if "q" not in m else insn.byte_count - 3
            if saved > 0:
                optimizations.append(Optimization(
                    severity="low",
                    instruction_num=insn.line_num,
                    description="ADD $1 can be replaced with INC (shorter encoding)",
                    current=f"{m} {ops}",
                    suggested=f"inc {reg}",
                ))

        # Byte count check: overly long instruction when shorter exists
        if insn.byte_count > 7:
            optimizations.append(Optimization(
                severity="low",
                instruction_num=insn.line_num,
                description=f"Long instruction ({insn.byte_count} bytes) - check if shorter encoding exists",
                current=f"{m} {ops} ({insn.byte_count} bytes)",
                suggested="Review addressing mode or use alternative instruction",
            ))

    return optimizations


def generate_report(report: EfficiencyReport) -> str:
    """Generate a formatted text report."""
    lines = []
    sep = "=" * 80

    lines.append(sep)
    lines.append(f"  INSTRUCTION EFFICIENCY REPORT")
    lines.append(f"  Binary: {report.binary_name}")
    lines.append(f"  Function: {report.function_name}()")
    lines.append(sep)
    lines.append("")

    # Basic metrics
    lines.append("--- ENCODING METRICS ---")
    lines.append("")
    lines.append(f"  Total instructions:     {len(report.instructions)}")
    lines.append(f"  Actual code bytes:      {report.total_bytes}")
    lines.append(f"  Minimum possible bytes: {report.minimum_bytes}")
    lines.append(f"  Efficiency score:       {report.efficiency_score:.1%}")
    lines.append(f"  Wasted bytes:           {report.total_bytes - report.minimum_bytes}")
    lines.append("")

    # Instruction length distribution
    lengths = Counter(insn.byte_count for insn in report.instructions)
    lines.append("--- INSTRUCTION LENGTH DISTRIBUTION ---")
    lines.append("")
    max_count = max(lengths.values()) if lengths else 1
    for length in sorted(lengths.keys()):
        count = lengths[length]
        bar_len = int((count / max_count) * 40)
        bar = "#" * bar_len
        pct = (count / len(report.instructions)) * 100 if report.instructions else 0
        lines.append(f"  {length:2d} bytes: {count:4d} ({pct:5.1f}%)  {bar}")
    lines.append("")

    # Mnemonic frequency
    mnem_counts = Counter(insn.mnemonic for insn in report.instructions)
    lines.append("--- TOP INSTRUCTIONS BY FREQUENCY ---")
    lines.append("")
    for mnem, count in mnem_counts.most_common(15):
        total_bytes_for_mnem = sum(
            insn.byte_count for insn in report.instructions if insn.mnemonic == mnem
        )
        avg_bytes = total_bytes_for_mnem / count if count else 0
        lines.append(f"  {mnem:12s}  {count:4d} occurrences  avg {avg_bytes:.1f} bytes/insn")
    lines.append("")

    # Redundancies
    lines.append("--- REDUNDANT INSTRUCTIONS ---")
    lines.append("")
    if report.redundancies:
        for r in report.redundancies:
            lines.append(r)
    else:
        lines.append("  No obvious redundancies detected.")
    lines.append("")

    # Missed optimizations
    lines.append("--- MISSED OPTIMIZATIONS ---")
    lines.append("")
    if report.optimizations:
        for opt in sorted(report.optimizations, key=lambda o: {"high": 0, "medium": 1, "low": 2}.get(o.severity, 3)):
            sev_label = {"high": "[HIGH]", "medium": "[MED ]", "low": "[LOW ]"}.get(opt.severity, "[????]")
            lines.append(f"  {sev_label} Instruction #{opt.instruction_num}: {opt.description}")
            lines.append(f"           Current:   {opt.current}")
            lines.append(f"           Suggested: {opt.suggested}")
            lines.append("")
    else:
        lines.append("  No missed optimizations detected.")
    lines.append("")

    # Per-instruction detail table
    lines.append("--- PER-INSTRUCTION DETAIL ---")
    lines.append("")
    lines.append(f"  {'#':>4s}  {'Bytes':>5s}  {'Min':>3s}  {'Waste':>5s}  {'Mnemonic':<12s}  Operands")
    lines.append(f"  {'----':>4s}  {'-----':>5s}  {'---':>3s}  {'-----':>5s}  {'--------':<12s}  --------")

    for insn in report.instructions:
        min_b = estimate_minimum_bytes(insn)
        waste = insn.byte_count - min_b
        waste_str = f"+{waste}" if waste > 0 else "0"
        ops_short = insn.operands[:45]
        lines.append(
            f"  {insn.line_num:4d}  {insn.byte_count:5d}  {min_b:3d}  {waste_str:>5s}  "
            f"{insn.mnemonic:<12s}  {ops_short}"
        )
    lines.append("")

    # Summary
    lines.append(sep)
    high_count = sum(1 for o in report.optimizations if o.severity == "high")
    med_count = sum(1 for o in report.optimizations if o.severity == "medium")
    low_count = sum(1 for o in report.optimizations if o.severity == "low")
    lines.append(f"  Optimization opportunities: {high_count} high, {med_count} medium, {low_count} low")
    lines.append(f"  Redundancies found: {len(report.redundancies)}")
    lines.append(f"  Overall efficiency: {report.efficiency_score:.1%}")

    if report.efficiency_score >= 0.95:
        lines.append("  Verdict: EXCELLENT - near-optimal instruction encoding")
    elif report.efficiency_score >= 0.85:
        lines.append("  Verdict: GOOD - minor improvements possible")
    elif report.efficiency_score >= 0.70:
        lines.append("  Verdict: FAIR - several optimization opportunities exist")
    else:
        lines.append("  Verdict: POOR - significant encoding overhead detected")

    lines.append(sep)
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(
        description="Analyze instruction-level efficiency of compiled binaries"
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("binary", nargs="?", help="Path to the binary to analyze")
    group.add_argument("--objdump-file", help="Path to pre-generated objdump output")
    parser.add_argument("function", nargs="?", default="main",
                        help="Function to analyze (default: main)")
    parser.add_argument("--json", action="store_true", help="Output as JSON")

    args = parser.parse_args()
    func = args.function or "main"

    if args.objdump_file:
        with open(args.objdump_file) as f:
            objdump_text = f.read()
        binary_name = args.objdump_file
    else:
        binary_name = args.binary
        try:
            result = subprocess.run(
                ["objdump", "-d", binary_name],
                capture_output=True, text=True, check=True
            )
            objdump_text = result.stdout
        except FileNotFoundError:
            print(f"Error: objdump not found", file=sys.stderr)
            sys.exit(1)
        except subprocess.CalledProcessError as e:
            print(f"Error running objdump on '{binary_name}': {e.stderr}", file=sys.stderr)
            sys.exit(1)

    instructions = parse_objdump(objdump_text, func)

    if not instructions:
        print(f"Error: function '{func}' not found in '{binary_name}'", file=sys.stderr)
        # List available functions
        func_names = re.findall(r'<(\S+?)>:', objdump_text)
        unique_funcs = sorted(set(func_names))
        if unique_funcs:
            print(f"Available functions: {', '.join(unique_funcs[:20])}", file=sys.stderr)
        sys.exit(1)

    report = EfficiencyReport(
        function_name=func,
        binary_name=binary_name,
        instructions=instructions,
    )

    # Calculate metrics
    report.total_bytes = sum(insn.byte_count for insn in instructions)
    report.minimum_bytes = sum(estimate_minimum_bytes(insn) for insn in instructions)
    report.efficiency_score = (
        report.minimum_bytes / report.total_bytes if report.total_bytes > 0 else 1.0
    )

    # Find issues
    report.redundancies = find_redundant_instructions(instructions)
    report.optimizations = find_missed_optimizations(instructions)

    if args.json:
        import json
        data = {
            "function": report.function_name,
            "binary": report.binary_name,
            "total_instructions": len(report.instructions),
            "total_bytes": report.total_bytes,
            "minimum_bytes": report.minimum_bytes,
            "efficiency_score": round(report.efficiency_score, 4),
            "wasted_bytes": report.total_bytes - report.minimum_bytes,
            "redundancies": len(report.redundancies),
            "optimizations": {
                "high": sum(1 for o in report.optimizations if o.severity == "high"),
                "medium": sum(1 for o in report.optimizations if o.severity == "medium"),
                "low": sum(1 for o in report.optimizations if o.severity == "low"),
            },
            "instruction_lengths": dict(Counter(i.byte_count for i in instructions)),
            "top_mnemonics": dict(Counter(i.mnemonic for i in instructions).most_common(15)),
        }
        print(json.dumps(data, indent=2))
    else:
        print(generate_report(report))


if __name__ == "__main__":
    main()

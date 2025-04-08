regs = [
    "regs.b","regs.c","regs.d","regs.e",
    "regs.h","regs.l","readZ80(REG_HL)","regs.a"
]

flags = [
    "!flags.z", "flags.z", "!flags.c", "flags.c",
    "!flags.p", "flags.p", "!flags.s", "flags.s"
]

# ld reg, #imm

print("        // ld reg, #imm")
for i in range(8):
    op = (i<<3)|6
    src = regs[i]
    if src == "readZ80(REG_HL)": # (HL)
        print(f"        case {op:#04x}: writeZ80(REG_HL,readZ80(regs.pc++)); break;")
    else:
        print(f"        case {op:#04x}: {src} = readZ80(regs.pc++); break;")

print("")

print("        // ld src, dst")
# ld src, dst
for i in range(64):
    op = i|0x40
    src = regs[i>>3&7]
    dst = regs[i&7]
    if src == "readZ80(REG_HL)": # (HL)
        if dst == src: # check for HALT
            print(f"        case {op:#04x}: halt(); break; // halt")
        else:
            print(f"        case {op:#04x}: writeZ80(REG_HL,{dst}); break;")
    else:
        print(f"        case {op:#04x}: {src} = {dst}; break;")

print("")

print("        // jp cond")
# jp cond
for i in range(8):
    op = (i<<3)|0xC0|2
    flag = flags[i]
    print(f"        case {op:#04x}: jp({flag}); break;")

print("")

print("        // call cond")
# jp cond
for i in range(8):
    op = 0xC4+(i<<3)
    flag = flags[i]
    print(f"        case {op:#04x}: call_cond({flag}); break;")

print("        // ret cond")
# jp cond
for i in range(8):
    op = 0xC0+(i<<3)
    flag = flags[i]
    print(f"        case {op:#04x}: ret({flag}); break;")

print("")

print("        // add r8")
for i in range(8):
    op = i|0x80
    reg = regs[i]
    print(f"        case {op:#04x}: regs.a = add8(regs.a,{reg},0); break;")

print("        // adc r8")
for i in range(8):
    op = i|0x88
    reg = regs[i]
    print(f"        case {op:#04x}: regs.a = add8(regs.a,{reg},flags.c); break;")

print("        // sub r8")
for i in range(8):
    op = i|0x90
    reg = regs[i]
    print(f"        case {op:#04x}: regs.a = sub8(regs.a,{reg},0); break;")

print("        // sbc r8")
for i in range(8):
    op = i|0x98
    reg = regs[i]
    print(f"        case {op:#04x}: regs.a = sub8(regs.a,{reg},flags.c); break;")

print("        // and r8")
for i in range(8):
    op = i|0xA0
    reg = regs[i]
    print(f"        case {op:#04x}: regs.a = and8(regs.a,{reg}); break;")

print("        // xor r8")
for i in range(8):
    op = i|0xA8
    reg = regs[i]
    print(f"        case {op:#04x}: regs.a = xor8(regs.a,{reg}); break;")

print("        // or r8")
for i in range(8):
    op = i|0xB0
    reg = regs[i]
    print(f"        case {op:#04x}: regs.a = or8(regs.a,{reg}); break;")

print("        // cp r8")
for i in range(8):
    op = i|0xB8
    reg = regs[i]
    print(f"        case {op:#04x}: cp(regs.a,{reg}); break;")

print("        // inc r8")
for i in range(8):
    op = (i<<3)|4
    reg = regs[i]
    if reg == "readZ80(REG_HL)": # (HL)
        print(f"        case {op:#04x}: {{ bool flags_c = flags.c; writeZ80(REG_HL,add8(readZ80(REG_HL),1,0)); flags.c = flags_c; break; }}")
    else:
        print(f"        case {op:#04x}: {{ bool flags_c = flags.c; {reg} = add8({reg},1,0); flags.c = flags_c; break; }}")

print("        // dec r8")
for i in range(8):
    op = (i<<3)|5
    reg = regs[i]
    if reg == "readZ80(REG_HL)": # (HL)
        print(f"        case {op:#04x}: {{ bool flags_c = flags.c; writeZ80(REG_HL,sub8(readZ80(REG_HL),1,0)); flags.c = flags_c; break; }}")
    else:
        print(f"        case {op:#04x}: {{ bool flags_c = flags.c; {reg} = sub8({reg},1,0); flags.c = flags_c; break; }}")

print("        // rst")
# rst
for i in range(8):
    op = 0xC7+(i<<3)
    rst = i<<3
    print(f"        case {op:#04x}: call({rst:#04x}); break;")
#define GL_GLEXT_PROTOTYPES
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_memory_editor.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include "portable-file-dialogs.h"
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <sstream>

#include "config.h"

// from zx.c
extern "C" {
    struct regs_Struct {
        uint8_t a,b,c,d,e,f,h,l;
        uint8_t as,bs,cs,ds,es,fs,hs,ls;
        uint16_t ix,iy;
        uint8_t iff1,iff2,i,r;
        int16_t has_int;
        uint8_t im;
        uint16_t pc,sp;
        SHARED_BOOL halt;
        int cycles;
    };
    
    struct flags_Struct {
        SHARED_BOOL s,z,x,h,y,p,n,c;
    };

    extern struct regs_Struct regs;
    extern struct flags_Struct flags;

    extern uint16_t read_AF();
    extern uint8_t readZ80_no_highlight(uint16_t addr);
}

// from Mesen2's SMS core
// https://github.com/SourMesen/Mesen2/blob/master/Core/SMS/Debugger/SmsDisUtils.cpp
const char* _opTemplate[256] = {
	"NOP",		"LD BC, e",	"LD (BC), A",	"INC BC",	"INC B",			"DEC B",		"LD B, d",	"RLCA",		"EX AF,AF'",	"ADD w, BC",	"LD A, (BC)",	"DEC BC",	"INC C",		"DEC C",		"LD C, d",	"RRCA",
	"DJNZ r",	"LD DE, e",	"LD (DE), A",	"INC DE",	"INC D",			"DEC D",		"LD D, d",	"RLA",		"JR r",			"ADD w, DE",	"LD A, (DE)",	"DEC DE",	"INC E",		"DEC E",		"LD E, d",	"RRA",
	"JR NZ, r",	"LD w, e",	"LD (a), w",	"INC w",		"INC y",			"DEC y",		"LD y, d",	"DAA",		"JR Z, r",		"ADD w, w",		"LD w, (a)",	"DEC w",		"INC z",		"DEC z",		"LD z, d",	"CPL",
	"JR NC, r",	"LD SP, e",	"LD (a), A",	"INC SP",	"INC x",			"DEC x",		"LD x, d",	"SCF",		"JR C, r",		"ADD w, SP",	"LD A, (a)",	"DEC SP",	"INC A",		"DEC A",		"LD A, d",	"CCF",
	"LD B, B",	"LD B, C",	"LD B, D",		"LD B, E",	"LD B, y",		"LD B, z",	"LD B, x",	"LD B, A",	"LD C, B",		"LD C, C",		"LD C, D",		"LD C, E",	"LD C, y",	"LD C, z",	"LD C, x",	"LD C, A",
	"LD D, B",	"LD D, C",	"LD D, D",		"LD D, E",	"LD D, y",		"LD D, z",	"LD D, x",	"LD D, A",	"LD E, B",		"LD E, C",		"LD E, D",		"LD E, E",	"LD E, y",	"LD E, z",	"LD E, x",	"LD E, A",
	"LD y, B",	"LD y, C",	"LD y, D",		"LD y, E",	"LD y, y",		"LD y, z",	"LD H, x",	"LD y, A",	"LD z, B",		"LD z, C",		"LD z, D",		"LD z, E",	"LD z, y",	"LD z, z",	"LD L, x",	"LD z, A",
	"LD x, B",	"LD x, C",	"LD x, D",		"LD x, E",	"LD x, H",		"LD x, L",	"HALT",		"LD x, A",	"LD A, B",		"LD A, C",		"LD A, D",		"LD A, E",	"LD A, y",	"LD A, z",	"LD A, x",	"LD A, A",
	"ADD A, B",	"ADD A, C",	"ADD A, D",		"ADD A, E",	"ADD A, y",		"ADD A, z",	"ADD A, x",	"ADD A, A",	"ADC A, B",		"ADC A, C",		"ADC A, D",		"ADC A, E",	"ADC A, y",	"ADC A, z",	"ADC A, x",	"ADC A, A",
	"SUB B",		"SUB C",		"SUB D",			"SUB E",		"SUB y",			"SUB z",		"SUB x",		"SUB A",		"SBC A, B",		"SBC A, C",		"SBC A, D",		"SBC A, E",	"SBC A, y",	"SBC A, z",	"SBC A, x",	"SBC A, A",
	"AND B",		"AND C",		"AND D",			"AND E",		"AND y",			"AND z",		"AND x",		"AND A",		"XOR B",			"XOR C",			"XOR D",			"XOR E",		"XOR y",		"XOR z",		"XOR x",		"XOR A",
	"OR B",		"OR C",		"OR D",			"OR E",		"OR y",			"OR z",		"OR x",		"OR A",		"CP B",			"CP C",			"CP D",			"CP E",		"CP y",		"CP z",		"CP x",		"CP A",
	"RET NZ",	"POP BC",	"JP NZ, a",		"JP a",		"CALL NZ, a",	"PUSH BC",	"ADD A, d",	"RST $00",	"RET Z",			"RET",			"JP Z, a",		"PREFIX_CB","CALL Z, a","CALL a",	"ADC A, d",	"RST $08",
	"RET NC",	"POP DE",	"JP NC, a",		"OUT (p), A","CALL NC, a",	"PUSH DE",	"SUB d",		"RST $10",	"RET C",			"EXX",			"JP C, a",		"IN A, (p)","CALL C, a","PREFIX_DD","SBC A, d",	"RST $18",
	"RET PO",	"POP w",		"JP PO, a",		"EX (SP), w","CALL PO, a",	"PUSH w",	"AND d",		"RST $20",	"RET PE",		"JP w",			"JP PE, a",		"EX DE, w",	"CALL PE, a","PREFIX_ED","XOR d",	"RST $28",
	"RET P",		"POP AF",	"JP P, a",		"DI",			"CALL P, a",	"PUSH AF",	"OR d",		"RST $30",	"RET M",			"LD SP, w",		"JP M, a",		"EI",			"CALL M, a","PREFIX_FD","CP d",		"RST $38"
};

const char* _cbTemplate[256] = {
	"RLC vB",		"RLC vC",		"RLC vD",		"RLC vE",		"RLC vH",		"RLC vL",		"RLC x",		"RLC vA",		"RRC vB",		"RRC vC",		"RRC vD",		"RRC vE",		"RRC vH",		"RRC vL",		"RRC x",		"RRC vA",	
	"RL vB",			"RL vC",			"RL vD",			"RL vE",			"RL vH",			"RL vL",			"RL x",		"RL vA",			"RR vB",			"RR vC",			"RR vD",			"RR vE",			"RR vH",			"RR vL",			"RR x",		"RR vA",	
	"SLA vB",		"SLA vC",		"SLA vD",		"SLA vE",		"SLA vH",		"SLA vL",		"SLA x",		"SLA vA",		"SRA vB",		"SRA vC",		"SRA vD",		"SRA vE",		"SRA vH",		"SRA vL",		"SRA x",		"SRA vA",	
	"SLL vB",		"SLL vC",		"SLL vD",		"SLL vE",		"SLL vH",		"SLL vL",		"SLL x",		"SWAP A",		"SRL vB",		"SRL vC",		"SRL vD",		"SRL vE",		"SRL vH",		"SRL vL",		"SRL x",		"SRL vA",	
	"BIT 0, vB",	"BIT 0, vC",	"BIT 0, vD",	"BIT 0, vE",	"BIT 0, vH",	"BIT 0, vL",	"BIT 0, x",	"BIT 0, vA",	"BIT 1, vB",	"BIT 1, vC",	"BIT 1, vD",	"BIT 1, vE",	"BIT 1, vH",	"BIT 1, vL",	"BIT 1, x",	"BIT 1, vA",	
	"BIT 2, vB",	"BIT 2, vC",	"BIT 2, vD",	"BIT 2, vE",	"BIT 2, vH",	"BIT 2, vL",	"BIT 2, x",	"BIT 2, vA",	"BIT 3, vB",	"BIT 3, vC",	"BIT 3, vD",	"BIT 3, vE",	"BIT 3, vH",	"BIT 3, vL",	"BIT 3, x",	"BIT 3, vA",	
	"BIT 4, vB",	"BIT 4, vC",	"BIT 4, vD",	"BIT 4, vE",	"BIT 4, vH",	"BIT 4, vL",	"BIT 4, x",	"BIT 4, vA",	"BIT 5, vB",	"BIT 5, vC",	"BIT 5, vD",	"BIT 5, vE",	"BIT 5, vH",	"BIT 5, vL",	"BIT 5, x",	"BIT 5, vA",	
	"BIT 6, vB",	"BIT 6, vC",	"BIT 6, vD",	"BIT 6, vE",	"BIT 6, vH",	"BIT 6, vL",	"BIT 6, x",	"BIT 6, vA",	"BIT 7, vB",	"BIT 7, vC",	"BIT 7, vD",	"BIT 7, vE",	"BIT 7, vH",	"BIT 7, vL",	"BIT 7, x",	"BIT 7, vA",	
	"RES 0, vB",	"RES 0, vC",	"RES 0, vD",	"RES 0, vE",	"RES 0, vH",	"RES 0, vL",	"RES 0, x",	"RES 0, vA",	"RES 1, vB",	"RES 1, vC",	"RES 1, vD",	"RES 1, vE",	"RES 1, vH",	"RES 1, vL",	"RES 1, x",	"RES 1, vA",	
	"RES 2, vB",	"RES 2, vC",	"RES 2, vD",	"RES 2, vE",	"RES 2, vH",	"RES 2, vL",	"RES 2, x",	"RES 2, vA",	"RES 3, vB",	"RES 3, vC",	"RES 3, vD",	"RES 3, vE",	"RES 3, vH",	"RES 3, vL",	"RES 3, x",	"RES 3, vA",	
	"RES 4, vB",	"RES 4, vC",	"RES 4, vD",	"RES 4, vE",	"RES 4, vH",	"RES 4, vL",	"RES 4, x",	"RES 4, vA",	"RES 5, vB",	"RES 5, vC",	"RES 5, vD",	"RES 5, vE",	"RES 5, vH",	"RES 5, vL",	"RES 5, x",	"RES 5, vA",	
	"RES 6, vB",	"RES 6, vC",	"RES 6, vD",	"RES 6, vE",	"RES 6, vH",	"RES 6, vL",	"RES 6, x",	"RES 6, vA",	"RES 7, vB",	"RES 7, vC",	"RES 7, vD",	"RES 7, vE",	"RES 7, vH",	"RES 7, vL",	"RES 7, x",	"RES 7, vA",	
	"SET 0, vB",	"SET 0, vC",	"SET 0, vD",	"SET 0, vE",	"SET 0, vH",	"SET 0, vL",	"SET 0, x",	"SET 0, vA",	"SET 1, vB",	"SET 1, vC",	"SET 1, vD",	"SET 1, vE",	"SET 1, vH",	"SET 1, vL",	"SET 1, x",	"SET 1, vA",	
	"SET 2, vB",	"SET 2, vC",	"SET 2, vD",	"SET 2, vE",	"SET 2, vH",	"SET 2, vL",	"SET 2, x",	"SET 2, vA",	"SET 3, vB",	"SET 3, vC",	"SET 3, vD",	"SET 3, vE",	"SET 3, vH",	"SET 3, vL",	"SET 3, x",	"SET 3, vA",	
	"SET 4, vB",	"SET 4, vC",	"SET 4, vD",	"SET 4, vE",	"SET 4, vH",	"SET 4, vL",	"SET 4, x",	"SET 4, vA",	"SET 5, vB",	"SET 5, vC",	"SET 5, vD",	"SET 5, vE",	"SET 5, vH",	"SET 5, vL",	"SET 5, x",	"SET 5, vA",	
	"SET 6, vB",	"SET 6, vC",	"SET 6, vD",	"SET 6, vE",	"SET 6, vH",	"SET 6, vL",	"SET 6, x",	"SET 6, vA",	"SET 7, vB",	"SET 7, vC",	"SET 7, vD",	"SET 7, vE",	"SET 7, vH",	"SET 7, vL",	"SET 7, x",	"SET 7, vA",	
};

const char* _edTemplate[256] = {
	"NOP",			"NOP",			"NOP",			"NOP",			"NOP",	"NOP",	"NOP",	"NOP",		"NOP",			"NOP",			"NOP",			"NOP",			"NOP",	"NOP",	"NOP",	"NOP",
	"NOP",			"NOP",			"NOP",			"NOP",			"NOP",	"NOP",	"NOP",	"NOP",		"NOP",			"NOP",			"NOP",			"NOP",			"NOP",	"NOP",	"NOP",	"NOP",
	"NOP",			"NOP",			"NOP",			"NOP",			"NOP",	"NOP",	"NOP",	"NOP",		"NOP",			"NOP",			"NOP",			"NOP",			"NOP",	"NOP",	"NOP",	"NOP",
	"NOP",			"NOP",			"NOP",			"NOP",			"NOP",	"NOP",	"NOP",	"NOP",		"NOP",			"NOP",			"NOP",			"NOP",			"NOP",	"NOP",	"NOP",	"NOP",

	"IN B, (C)",	"OUT (C), B",	"SBC HL, BC",	"LD (a), BC",	"NEG",	"RETN",	"IM 0",	"LD I, A",	"IN C, (C)",	"OUT (C), C",	"ADC HL, BC",	"LD BC, (a)",	"NEG",	"RETI",	"IM 0",	"LD R, A",
	"IN D, (C)",	"OUT (C), D",	"SBC HL, DE",	"LD (a), DE",	"NEG",	"RETI",	"IM 1",	"LD A, I",	"IN E, (C)",	"OUT (C), E",	"ADC HL, DE",	"LD DE, (a)",	"NEG",	"RETI",	"IM 2",	"LD A, R",
	"IN H, (C)",	"OUT (C), H",	"SBC HL, HL",	"LD (a), HL",	"NEG",	"RETI",	"IM 1",	"RRD",		"IN L, (C)",	"OUT (C), L",	"ADC HL, HL",	"LD HL, (a)",	"NEG",	"RETI",	"IM 2",	"LD A, R",
	"IN (C)",		"OUT (C), 0",	"SBC HL, SP",	"LD (a), SP",	"NEG",	"RETI",	"IM 0",	"NOP",		"IN A, (C)",	"OUT (C), A",	"ADC HL, SP",	"LD SP, (a)",	"NEG",	"RETI",	"IM 2",	"NOP",

	"NOP",			"NOP",			"NOP",			"NOP",			"NOP",	"NOP",	"NOP",	"NOP",		"NOP",			"NOP",			"NOP",			"NOP",			"NOP",	"NOP",	"NOP",	"NOP",
	"NOP",			"NOP",			"NOP",			"NOP",			"NOP",	"NOP",	"NOP",	"NOP",		"NOP",			"NOP",			"NOP",			"NOP",			"NOP",	"NOP",	"NOP",	"NOP",
	"LDI",			"CPI",			"INI",			"OUTI",			"NOP",	"NOP",	"NOP",	"NOP",		"LDD",			"CPD",			"IND",			"OUTD",			"NOP",	"NOP",	"NOP",	"NOP",
	"LDIR",			"CPIR",			"INIR",			"OTIR",			"NOP",	"NOP",	"NOP",	"NOP",		"LDDR",			"CPDR",			"INDR",			"OTDR",			"NOP",	"NOP",	"NOP",	"NOP",

	"NOP",			"NOP",			"NOP",			"NOP",			"NOP",	"NOP",	"NOP",	"NOP",		"NOP",			"NOP",			"NOP",			"NOP",			"NOP",	"NOP",	"NOP",	"NOP",
	"NOP",			"NOP",			"NOP",			"NOP",			"NOP",	"NOP",	"NOP",	"NOP",		"NOP",			"NOP",			"NOP",			"NOP",			"NOP",	"NOP",	"NOP",	"NOP",
	"NOP",			"NOP",			"NOP",			"NOP",			"NOP",	"NOP",	"NOP",	"NOP",		"NOP",			"NOP",			"NOP",			"NOP",			"NOP",	"NOP",	"NOP",	"NOP",
	"NOP",			"NOP",			"NOP",			"NOP",			"NOP",	"NOP",	"NOP",	"NOP",		"NOP",			"NOP",			"NOP",			"NOP",			"NOP",	"NOP",	"NOP",	"NOP",
};

const uint8_t _opSize[256] = {
	1,3,1,1,1,1,2,1,1,1,1,1,1,1,2,1,
	2,3,1,1,1,1,2,1,2,1,1,1,1,1,2,1,
	2,3,3,1,1,1,2,1,2,1,3,1,1,1,2,1,
	2,3,3,1,1,1,2,1,2,1,3,1,1,1,2,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,3,3,3,1,2,1,1,1,3,2,3,3,2,1,
	1,1,3,2,3,1,2,1,1,1,3,2,3,1,2,1,
	1,1,3,1,3,1,2,1,1,1,3,1,3,1,2,1,
	1,1,3,1,3,1,2,1,1,1,3,1,3,1,2,1,
};

const uint8_t _opSizeIxIy[256] = {
	1,3,1,1,1,1,2,1,1,1,1,1,1,1,2,1,
	2,3,1,1,1,1,2,1,2,1,1,1,1,1,2,1,
	2,3,3,1,1,1,2,1,2,1,3,1,1,1,2,1,
	2,3,3,1,2,2,3,1,2,1,3,1,1,1,2,1,
	1,1,1,1,1,1,2,1,1,1,1,1,1,1,2,1,
	1,1,1,1,1,1,2,1,1,1,1,1,1,1,2,1,
	1,1,1,1,1,1,2,1,1,1,1,1,1,1,2,1,
	2,2,2,2,2,2,1,2,1,1,1,1,1,1,2,1,
	1,1,1,1,1,1,2,1,1,1,1,1,1,1,2,1,
	1,1,1,1,1,1,2,1,1,1,1,1,1,1,2,1,
	1,1,1,1,1,1,2,1,1,1,1,1,1,1,2,1,
	1,1,1,1,1,1,2,1,1,1,1,1,1,1,2,1,
	1,1,3,3,3,1,2,1,1,1,3,2,3,3,2,1,
	1,1,3,2,3,1,2,1,1,1,3,2,3,1,2,1,
	1,1,3,1,3,1,2,1,1,1,3,1,3,1,2,1,
	1,1,3,1,3,1,2,1,1,1,3,1,3,1,2,1,
};

const uint8_t _opSizeEd[256] = {
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,

	2,2,2,4,2,2,2,2,2,2,2,4,2,2,2,2,
	2,2,2,4,2,2,2,2,2,2,2,4,2,2,2,2,
	2,2,2,4,2,2,2,2,2,2,2,4,2,2,2,2,
	2,2,2,4,2,2,2,2,2,2,2,4,2,2,2,2,

	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,

	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
};

uint8_t get_opsize(uint16_t pc) {
    uint8_t size = 0;
    bool is_index = false;
    uint8_t op = readZ80_no_highlight(pc);
    while (1) {
        if (op == 0xCB) { // CB opcode
            if (is_index) size += _opSizeIxIy[op];
            else size += _opSize[op];
            return size;
        } else if (op == 0xDD || op == 0xFD) { // IX/IY ops
            is_index = true;
            size++;
            if (size > 6) return 6;
            op = readZ80_no_highlight(pc+size);
        } else if (op == 0xED) { // ED/misc opcodes
            size += _opSize[readZ80_no_highlight(pc+size+1)];
            return size;     
        } else {
            if (is_index) {
                size += _opSizeIxIy[op];
                return size;
            } else {
                size += _opSize[op];
                return size;
            }
        }
    }   
    return 0;
}

// https://stackoverflow.com/questions/5100718/integer-to-hex-string-in-c
template< typename T >
std::string int_to_hex( T i ) {
  std::stringstream stream;
  stream << "$" 
         << std::setfill ('0') << std::setw(sizeof(T)*2) 
         << std::hex << i;
  return stream.str();
}

template< typename T >
std::string int_to_hex8( T i ) {
  std::stringstream stream;
  stream << "$" 
         << std::setfill ('0') << std::setw(sizeof(T)) 
         << std::hex << i;
  return stream.str();
}

extern "C" {
    extern void do_oneop();
    extern void do_onescan();
    extern int get_vcount();
    extern int get_hcount();
}

void do_disasm(bool *p_open) {
    int font_size = 13;
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::Begin("Z80 Debugger", p_open, ImGuiWindowFlags_NoScrollbar);

    if (ImGui::Button("Step (F10)")) do_oneop();
    ImGui::SameLine();
    if (ImGui::Button("Run one scanline (F7)")) do_onescan();
    ImGui::SameLine();
    ImGui::Text("Scanline: %3d   Cycle: %3d",get_vcount(),get_hcount());

    ImGui::Separator();
    int rows = (int)(ImGui::GetWindowSize().y/io.FontGlobalScale/font_size/1.3f);
    uint16_t pc = regs.pc;
    for (int i = 0; i < rows; i++) {
        // get opcode size
        uint16_t old_pc = pc;
        uint8_t opsize = get_opsize(pc);
        std::string opstr = "";
        const char *optemplate = NULL;
        uint8_t op = readZ80_no_highlight(pc);
        int8_t index_off;
        bool is_cb = false;
        bool is_ed = false;

        // 0: nothing
        // 1: IX reg
        // 2: IY reg
        uint8_t is_index = 0;

        // get op template
        uint8_t index_reg_type = 0;
        while (optemplate == NULL) {
            if (op == 0xCB) { // CB ops
                is_cb = true;
                if (is_index) index_off = readZ80_no_highlight(++pc);
                op = readZ80_no_highlight(++pc);
                optemplate = _cbTemplate[op];
            } else if (op == 0xDD || op == 0xFD) { // IX/IY ops
                if (op == 0xDD) is_index = 1; // IX
                else is_index = 2; // IY
                op = readZ80_no_highlight(++pc);
            } else if (op == 0xED) { // ED/misc. ops
                is_ed = true;
                is_index = 0;
                op = readZ80_no_highlight(++pc);
                optemplate = _edTemplate[op];
            } else {
                optemplate = _opTemplate[op];
            }
        }

        if (optemplate != NULL) {
            int str_ind = 0;
            while (optemplate[str_ind] > 0) {
                switch (optemplate[str_ind]) {
                    case 'r': {
                        // rel jumps
                        uint16_t addr = old_pc + opsize + ((int8_t)readZ80_no_highlight(pc+1));
                        opstr = opstr + int_to_hex(addr); 
                        break;
                    }

                    case 'a':
                    case 'e': {
                        // abs memory
                        uint16_t addr = readZ80_no_highlight(pc+1);
                        addr |= readZ80_no_highlight(pc+2)<<8;
                        opstr = opstr + int_to_hex(addr); 
                        break;
                    }

                    case 'p':
                    case 'd': { // 8-bit imm
                        uint8_t imm = readZ80_no_highlight(pc+1);
                        opstr = opstr + int_to_hex8((uint16_t)imm); 
                        break;
                    }

                    case 'v': {
                        if (is_index) {
                            if (is_index == 1) opstr = opstr + "(IX";
                            else opstr = opstr + "(IY";

                            if (index_off < 0) opstr = opstr + "-" + int_to_hex8((uint16_t)-index_off);
                            else opstr = opstr + "+" + int_to_hex8((uint16_t)index_off);

                            opstr = opstr + ")";
                        }
                        break;
                    }

                    case 'w': { // HL/IX/IY
                        const std::string HLs[3] = {"HL","IX","IY"};
                        opstr = opstr + HLs[is_index];
                        break;
                    }

                    case 'x': { // HL/IX/IY 2
                        if (is_index) {
                            if (is_index == 1) opstr = opstr + "(IX";
                            else opstr = opstr + "(IY";

                            if (index_off < 0) opstr = opstr + "-" + int_to_hex8((uint16_t)-index_off);
                            else opstr = opstr + "+" + int_to_hex8((uint16_t)index_off);

                            opstr = opstr + ")";
                        } else {
                            opstr = opstr + "(HL)";
                        }
                        break;
                    }

                    case 'y': { // H/IXH/IYH
                        const std::string HLs[3] = {"H","IXH","IYH"};
                        opstr = opstr + HLs[is_index];
                        break;
                    }

                    case 'z': { // L/IXL/IYL
                        const std::string HLs[3] = {"L","IXL","IYL"};
                        opstr = opstr + HLs[is_index];
                        break;
                    }

                    default:
                        opstr = opstr + optemplate[str_ind];
                        break;
                }
                str_ind++;
            }
        }

        pc = old_pc+opsize;
        ImGui::Text("%04x: %s",old_pc,opstr.c_str());
        
    }
    ImGui::End();
}
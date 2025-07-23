uint32_t *pixels;

#define CONV(col) (col|0xff000000)

const uint32_t RGB_pal[16] = {
    CONV(0x000000), CONV(0x0000D7), CONV(0xD70000), CONV(0xD700D7),
    CONV(0x00D700), CONV(0x00D7D7), CONV(0xD7D700), CONV(0xD7D7D7),
    CONV(0x000000), CONV(0x0000FF), CONV(0xFF0000), CONV(0xFF00FF),
    CONV(0x00FF00), CONV(0x00FFFF), CONV(0xFFFF00), CONV(0xFFFFFF),
};

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

struct ula_Struct {
    uint8_t rom_sel;
    uint8_t ram_bank;
    uint8_t bank_reg;
    uint8_t gfx_sel;
    bool do_contended;
    uint16_t cycles;
    uint16_t audio_cycles;
    uint8_t audio_cycles_beeper;
    uint16_t scanline;
    int frame, time;
    uint8_t key_matrix_buf[40];
    uint8_t key_matrix_buf_arrow[4];
    uint8_t key_matrix[40];
    bool quit;
    uint8_t ULA_FE;
    int16_t audio_buffer_temp[BUFFER_SIZE];
    int16_t audio_buffer[BUFFER_SIZE*4];
    uint16_t audio_buffer_ind; // for audio buffering 2: electric boogaloo
    int16_t audio_buffer_read; // for audio buffering
    int16_t audio_buffer_write; // for audio buffering
    int16_t beeper_filter;
    bool did_frame;
    uint16_t cycles_leftover;
    uint32_t debug_cycles;
    uint32_t contended_stolen_cycles;
};

#define swap(a,b) { uint8_t temp = a; a = b; b = temp; }

struct regs_Struct regs;
struct flags_Struct flags;
struct ula_Struct ula;

uint8_t mem[131072];
uint32_t *mem_highlight;
uint32_t *zx_rom_highlight;
uint8_t *zx_rom;

static inline uint8_t read_PC();

int get_vcount() {
    return (int)ula.scanline;
}
int get_hcount() {
    return (int)ula.cycles;
}

// for ZX Spectrum memory contention
static inline void add_cycles(uint8_t cycles) {
    //regs.cycles += cycles;
    ula.cycles += cycles;
    ula.audio_cycles += (uint16_t)cycles<<2;
    ula.audio_cycles_beeper += cycles;
    ula.debug_cycles += cycles;
}

// for ZX Spectrum memory contention
static inline void sub_cycles(uint8_t cycles) {
    //regs.cycles += cycles;
    ula.cycles -= cycles;
    ula.audio_cycles -= (uint16_t)cycles<<2;
    ula.audio_cycles_beeper -= cycles;
    ula.debug_cycles -= cycles;
}


static inline void add_contended_cycles() {
    //const static uint8_t scanline_cycle_lut[8] = {3,2,1,0,0,6,5,4};
    const static uint8_t scanline_cycle_lut[8] = {6,5,4,3,2,1,0,0};
    int register scanline_cycle = ula.cycles%228;
    if (scanline_cycle < 128) { // 24 + 128
        // "delay" the CPU by N cycles by just
        // adding to the cycles variable
        ula.contended_stolen_cycles += scanline_cycle_lut[scanline_cycle&7];
        add_cycles(scanline_cycle_lut[scanline_cycle&7]);
    }
}

static inline uint8_t floating_bus(uint16_t scanline_cycle, uint16_t scanline) {
    //const static uint8_t scanline_cycle_lut[8] = {3,2,1,0,0,6,5,4};
    if ((scanline_cycle) < 128 && ula.do_contended) { // 24 + 128
        // referenced from https://sinclair.wiki.zxnet.co.uk/wiki/Floating_bus
        uint16_t LY = scanline==62?0:scanline-63;
        uint16_t ypos = LY>>3; // get tile row number
        scanline_cycle--; // for late-timing ULA's
        scanline_cycle &= 0x7f;
        uint16_t x = scanline_cycle;
        switch (scanline_cycle&7) {
            case 0:   // bitmap
            case 2: { // bitmap+1
                register uint16_t tile_addr = ((x>>2)|((ypos&7)<<5))|((LY&7)<<8)+((ypos>>3)<<11);
                return mem[((tile_addr+(x>>1&1))&0x3fff)|((ula.gfx_sel?7:5)<<14)];
                break;
            }
            case 1:   // attr
            case 3: { // attr+1
                register uint16_t addr = ((x>>2)|(ypos<<5))+6144;
                return mem[((addr+(x>>1&1))&0x3fff)|((ula.gfx_sel?7:5)<<14)];
                break;
            }
            case 4:
            case 5:
            case 6:
            case 7:
            default:
                return 0xFF;
                break;
        }
    }
    return 0xff;
}

static inline void advance_ULA();

uint8_t readZ80(uint16_t addr) {
    switch (addr & 0xC000) {
        case 0x0000: { // ROM (not contended, obviously)
            if (visible_windows.read) zx_rom_highlight[(addr&0x3fff)|(ula.rom_sel<<14)] = 0xFF000000|visible_windows.read_col;
            return zx_rom[addr|(ula.rom_sel<<14)];
        }

        case 0x4000: { // RAM bank 5 (contended)
            if (ula.do_contended) add_contended_cycles();
            if (visible_windows.read) mem_highlight[(addr&0x3fff)|(5<<14)] = 0xFF000000|visible_windows.read_col;
            return mem[(addr&0x3fff)|(5<<14)];
        }

        case 0x8000: { // RAM bank 2 (not contended)
            if (visible_windows.read) mem_highlight[(addr&0x3fff)|(2<<14)] = 0xFF000000|visible_windows.read_col;
            return mem[(addr&0x3fff)|(2<<14)];
        }

        case 0xC000: { // RAM bank X (*COULD* be contended based off current RAM bank)
            if (ula.do_contended && (ula.ram_bank&1)) add_contended_cycles();
            if (visible_windows.read) mem_highlight[(addr&0x3fff)|((ula.ram_bank&7)<<14)] = 0xFF000000|visible_windows.read_col;
            return mem[(addr&0x3fff)|((ula.ram_bank&7)<<14)];
        }
    }
}

static inline uint8_t readZ80_contended(uint16_t addr) {
    switch (addr & 0xC000) {
        case 0x0000: // ROM (not contended, obviously)
            return zx_rom[addr|(ula.rom_sel<<14)];

        case 0x4000: { // RAM bank 5 (contended)
            if (ula.do_contended) add_contended_cycles();
            return mem[(addr&0x3fff)|(5<<14)];
        }

        case 0x8000: // RAM bank 2 (not contended)
            return mem[(addr&0x3fff)|(2<<14)];

        case 0xC000: { // RAM bank X (*COULD* be contended based off current RAM bank)
            if (ula.do_contended && (ula.ram_bank&1)) add_contended_cycles();
            return mem[(addr&0x3fff)|((ula.ram_bank&7)<<14)];
        }
    }
}

uint8_t readZ80_no_highlight(uint16_t addr) {
    switch (addr & 0xC000) {
        case 0x0000: // ROM (not contended, obviously)
            return zx_rom[addr|(ula.rom_sel<<14)];

        case 0x4000: { // RAM bank 5 (contended)
            return mem[(addr&0x3fff)|(5<<14)];
        }

        case 0x8000: // RAM bank 2 (not contended)
            return mem[(addr&0x3fff)|(2<<14)];

        case 0xC000: { // RAM bank X (*COULD* be contended based off current RAM bank)
            return mem[(addr&0x3fff)|((ula.ram_bank&7)<<14)];
        }
    }
}

void writeZ80(uint16_t addr, uint8_t val) {
    switch (addr & 0xC000) {
        case 0x0000: { // ROM (not contended, obviously)
            if (visible_windows.write) zx_rom_highlight[(addr&0x3fff)|(ula.rom_sel<<14)] = 0xFF000000|visible_windows.write_col;
            return;
        }

        case 0x4000: { // RAM bank 5 (contended)
            if (ula.do_contended) add_contended_cycles();
            mem[(addr&0x3fff)|(5<<14)] = val;
            if (visible_windows.do_event_viewer && visible_windows.do_event_viewer_bitmap) WRITE_EVENT(5);
            if (visible_windows.write) mem_highlight[(addr&0x3fff)|(5<<14)] = 0xFF000000|visible_windows.write_col;
            break;
        }

        case 0x8000: // RAM bank 2 (not contended)
            mem[(addr&0x3fff)|(2<<14)] = val;
            if (visible_windows.write) mem_highlight[(addr&0x3fff)|(2<<14)] = 0xFF000000|visible_windows.write_col;
            break;

        case 0xC000: { // RAM bank X (*COULD* be contended based off current RAM bank)
            if (ula.do_contended && (ula.ram_bank&1)) add_contended_cycles();
            if (visible_windows.do_event_viewer && visible_windows.do_event_viewer_bitmap) {
                if ((ula.ram_bank&7) == 5) {
                    WRITE_EVENT(5);
                } else if ((ula.ram_bank&7) == 7) {
                    WRITE_EVENT(6);
                }
            }
            mem[(addr&0x3fff)|((ula.ram_bank&7)<<14)] = val;
            if (visible_windows.write) mem_highlight[(addr&0x3fff)|((ula.ram_bank&7)<<14)] = 0xFF000000|visible_windows.write_col;
            break;
        }
    }
}

static inline uint8_t inZ80(uint16_t addr);
static inline void outZ80(uint16_t addr, uint8_t val);

uint8_t read_PC() {
    if (visible_windows.exec) {
        switch (regs.pc & 0xC000) {
            case 0x0000: // ROM (not contended, obviously)
                zx_rom_highlight[(regs.pc&0x3fff)|(ula.rom_sel<<14)] = 0xFF000000|visible_windows.exec_col;
                break;

            case 0x4000: // RAM bank 5 (contended)
                mem_highlight[(regs.pc&0x3fff)|(5<<14)] = 0xFF000000|visible_windows.exec_col;
                break;

            case 0x8000: // RAM bank 2 (not contended)
                mem_highlight[(regs.pc&0x3fff)|(2<<14)] = 0xFF000000|visible_windows.exec_col;
                break;

            case 0xC000: // RAM bank X (*COULD* be contended based off current RAM bank)
                mem_highlight[(regs.pc&0x3fff)|((ula.ram_bank&7)<<14)] = 0xFF000000|visible_windows.exec_col;
                break;
        }
    }
    uint8_t val = readZ80_contended(regs.pc++);
    add_cycles(3);
    return val;
}

// read a 16-bit word from memory
static inline uint16_t read16() {
    uint16_t imm = (uint16_t)read_PC();
    imm |= (uint16_t)read_PC()<<8;
    return imm;
}

void read_PC_ext(uint16_t addr) {
    if (visible_windows.exec) {
        switch (regs.pc & 0xC000) {
            case 0x0000: // ROM (not contended, obviously)
                zx_rom_highlight[(regs.pc&0x3fff)|(ula.rom_sel<<14)] = 0xFF000000|visible_windows.exec_col;
                break;

            case 0x4000: // RAM bank 5 (contended)
                mem_highlight[(regs.pc&0x3fff)|(5<<14)] = 0xFF000000|visible_windows.exec_col;
                break;

            case 0x8000: // RAM bank 2 (not contended)
                mem_highlight[(regs.pc&0x3fff)|(2<<14)] = 0xFF000000|visible_windows.exec_col;
                break;

            case 0xC000: // RAM bank X (*COULD* be contended based off current RAM bank)
                mem_highlight[(regs.pc&0x3fff)|((ula.ram_bank&7)<<14)] = 0xFF000000|visible_windows.exec_col;
                break;
        }
    }
}

#define READ_PC_EXT(addr) read_PC_ext(addr)

SDL_AudioDeviceID dev;

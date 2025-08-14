// I/O emulation

void EMU_IMGUI_process_event(SDL_Event *event);
extern bool EMU_IMGUI_is_emu_focused();
void do_rewind();
void reset_audio_buffer_and_unpause();

// draw one scanline of graphics
static void draw_ULA_scanline(uint16_t LY) {
    uint16_t ypos = LY>>3; // get tile row number
    for (size_t x = 0; x < 32<<3; x++) { // draw 32 8x8 chars
        // get attribute
        register uint16_t addr = ((x>>3)|(ypos<<5))+6144;
        uint8_t attr = mem[addr|((ula.gfx_sel?7:5)<<14)];

        // get tile data
        register uint16_t tile_addr = ((x>>3)|((ypos&7)<<5))|((LY&7)<<8)+((ypos>>3)<<11);
        uint8_t tile = mem[tile_addr|((ula.gfx_sel?7:5)<<14)];

        // now let's render it >:3
        if ((attr&0x80) && (ula.frame&32)) { // if flash attribute is enabled
            uint8_t pix = (tile>>(7-(x&7)))&1;
            if (!pix) pixels[(LY+32)*320+x+32] = RGB_pal[(attr&7)|((attr&64)>>3)];
            else pixels[(LY+32)*320+x+32] = RGB_pal[(attr>>3&7)|((attr&64)>>3)];
        } else {
            uint8_t pix = (tile>>(7-(x&7)))&1;
            if (pix) pixels[(LY+32)*320+x+32] = RGB_pal[(attr&7)|((attr&64)>>3)];
            else pixels[(LY+32)*320+x+32] = RGB_pal[(attr>>3&7)|((attr&64)>>3)];
        }
    }
}

// draw the borders (overwrites pixel data!!)
static void draw_ULA_border(uint16_t LY) {
    for (size_t x = 0; x < 320; x++) { // draw border
        pixels[LY*320+x] = RGB_pal[ula.ULA_FE&7];
    }
}

// keycodes for keyboard input
const SDL_Keycode sdl_key_matrix_codes[40] = {
    SDLK_LSHIFT, SDLK_z, SDLK_x, SDLK_c, SDLK_v, 
    SDLK_a, SDLK_s, SDLK_d, SDLK_f, SDLK_g,
    SDLK_q, SDLK_w, SDLK_e, SDLK_r, SDLK_t,
    SDLK_1, SDLK_2, SDLK_3, SDLK_4, SDLK_5,
    SDLK_0, SDLK_9, SDLK_8, SDLK_7, SDLK_6,
    SDLK_p, SDLK_o, SDLK_i, SDLK_u, SDLK_y,
    SDLK_RETURN, SDLK_l, SDLK_k, SDLK_j, SDLK_h,
    SDLK_SPACE, SDLK_LCTRL, SDLK_m, SDLK_n, SDLK_b,  
};

// alternative keycodes...
const SDL_Keycode sdl_key_matrix_codes_alt[40] = {
    SDLK_RSHIFT, -1, -1, -1, -1, 
    -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1,
    SDLK_RETURN, -1, -1, -1, -1,
    -1, SDLK_RCTRL, -1, -1, -1 
};

const SDL_Keycode sdl_arrow_codes[4] = {
    SDLK_LEFT, SDLK_RIGHT, SDLK_DOWN, SDLK_UP
};

const int sdl_arrow_matrix[4] = {
    19, 22, 24, 23
};

bool paused_prev;

struct debug_button {
    uint8_t step_pressed;
    uint8_t scan_pressed;
};
struct debug_button debug_pressed;

static inline void ULA_update_arrow_keys() {
    // clear buffer
    memcpy(ula.key_matrix,ula.key_matrix_buf,sizeof(ula.key_matrix));
    for (int i = 0; i < 4; i++) { // check arrow keys
        if (ula.key_matrix_buf_arrow[i]) {
            ula.key_matrix[0] |= 1;
            ula.key_matrix[sdl_arrow_matrix[i]] |= 1;
        }
    }
}

static void do_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        EMU_IMGUI_process_event(&event);
        switch (event.type) {
            case SDL_QUIT:
                ula.quit = true;
                break;
            case SDL_KEYDOWN: {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    pause_pressed = true;
                }

                if (event.key.keysym.sym == SDLK_F10) {
                    debug_pressed.step_pressed = true;
                }

                if (event.key.keysym.sym == SDLK_F7) {
                    debug_pressed.scan_pressed = true;
                }

                if (!EMU_IMGUI_is_emu_focused()) break;

                if (event.key.keysym.sym == SDLK_BACKSPACE) {
                    rewind_pressed = true;
                }
    

                for (int i = 0; i < 40; i++) { // check for normal keys
                    if (event.key.keysym.sym == sdl_key_matrix_codes[i] ||
                        event.key.keysym.sym == sdl_key_matrix_codes_alt[i]) {
                        ula.key_matrix_buf[i] = 1;
                        break;
                    }
                }

                for (int i = 0; i < 4; i++) { // check arrow keys
                    if (event.key.keysym.sym == sdl_arrow_codes[i]) {
                        ula.key_matrix_buf_arrow[i] = 1;
                        break;
                    }
                }
                break;
            }
            case SDL_KEYUP: {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    pause_pressed = false;
                }

                if (!EMU_IMGUI_is_emu_focused()) break;

                if (event.key.keysym.sym == SDLK_BACKSPACE) {
                    rewind_pressed = false;
                    reset_audio_buffer_and_unpause();
                    memset(ula.key_matrix_buf,0,sizeof(ula.key_matrix_buf));
                    memset(ula.key_matrix_buf_arrow,0,sizeof(ula.key_matrix_buf_arrow));
                }

                for (int i = 0; i < 40; i++) {
                    if (event.key.keysym.sym == sdl_key_matrix_codes[i] ||
                        event.key.keysym.sym == sdl_key_matrix_codes_alt[i]) {
                        ula.key_matrix_buf[i] = 0;
                        break;
                    }
                }

                for (int i = 0; i < 4; i++) { // check arrow keys
                    if (event.key.keysym.sym == sdl_arrow_codes[i]) {
                        ula.key_matrix_buf_arrow[i] = 0;
                        break;
                    }
                }
                break;
            }
        }
    }

    ULA_update_arrow_keys();
 
    if (pause_pressed != paused_prev && pause_pressed == true) {
        paused = !paused;
        audio_paused = paused;
        if (!paused) {
            reset_audio_buffer_and_unpause();
        }
    }

    paused_prev = pause_pressed;
}

// advances the ULA emulation per scanline
static inline void advance_ULA() {
    ula.cycles_leftover = (ula.cycles-228);
    ula.cycles -= 228;

    ula.scanline = (ula.scanline+1)%311;

    if (ula.scanline == 0) { // fire VBLANK at the start of each frame
        regs.has_int = 0xff;
        if (visible_windows.do_event_viewer) memset(event_viewer,0,228*311*sizeof(uint8_t));
        // TODO: remove this line
        // regs.r = 255;
    }


    if (ula.scanline >= 31 && ula.scanline < 287) {
        draw_ULA_border(ula.scanline-31);
    }

    if (ula.scanline >= 63 && ula.scanline < 255) {
        draw_ULA_scanline(ula.scanline-63);
    }

    if (ula.scanline == 62 && visible_windows.contended) {
        ula.do_contended = true;
    }
    if (ula.scanline == 255) {
        ula.do_contended = false;
    }

    if (ula.scanline == 310) {
        ula.did_frame = true;
        do_events();
        #ifdef VIDEO_SYNC
            do_rewind();
            int now = SDL_GetTicks();
            if (ula.time <= now) now = 0;
            else now = ula.time - now;
            SDL_Delay(now);
            ula.time += 20;
        #endif

        ula.frame = (ula.frame+1)&63;
    }
}

void update_ayumi_state(struct ayumi* ay, uint8_t* r, uint8_t addr);

#ifdef AY_EMULATION
  #ifdef AY_TURBOSOUND
    uint8_t AY_regs[32];
  #else
    uint8_t AY_regs[16];
  #endif
uint16_t AY_ind;
struct ayumi AY_chip1;
  #ifdef AY_TURBOSOUND
    struct ayumi AY_chip2;
  #endif
#endif

void add_cycles_io(uint16_t addr) {
    /*
    High byte   |         | 
    in 40 - 7F? | Low bit | Contention pattern  
    ------------+---------+-------------------
         No     |  Reset  | N:1, C:3
         No     |   Set   | N:4
        Yes     |  Reset  | C:1, C:3
        Yes     |   Set   | C:1, C:1, C:1, C:1
    */
    if (!ula.do_contended) {
        add_cycles(3);
        return;
    }
    if (addr >= 0x4000 && addr < 0x8000) {
        if (addr&1) {
            add_contended_cycles(); add_cycles(1);
            add_contended_cycles(); add_cycles(1);
            add_contended_cycles(); add_cycles(1);
        } else {
            add_contended_cycles(); add_cycles(3);
        }
    } else {
        if (addr&1) {
            add_cycles(3);
        } else {
            add_contended_cycles(); add_cycles(3);
        }
    }
}

static inline uint8_t inZ80(uint16_t addr) {
    // very crude i know
    if (addr >= 0x4000 && addr < 0x8000 && ula.do_contended) { add_contended_cycles(); } add_cycles(1);
    uint8_t val;
    #ifdef AY_EMULATION
        if ((addr&0x8000) && !(addr&2)) {
            WRITE_EVENT(3);
            #ifdef AY_TURBOSOUND
                #ifdef AY_TURBOSOUND_FM
                    if (visible_windows.do_tsfm) {
                        val = (
                                (AY_ind<0x0e)
                                ? AY_regs[(AY_ind&0xf)|(((AY_ind>>8)&1)<<4)]
                                : read_opn(AY_ind&0xff,AY_ind>>8&1)
                              );
                        if ((AY_ind&(2<<8))==0)
                            val = (val&0x7f) | (OPN_get_status(AY_ind>>8&1)&0x80);
                    } else val = AY_regs[(AY_ind&0xf)|(((AY_ind>>8)&1)<<4)];
                    #else
                    val = AY_regs[(AY_ind&0xf)|(((AY_ind>>8)&1)<<4)];
                #endif
            #else
                val = AY_regs[AY_ind&0xf];
            #endif
            add_cycles_io(addr);
            return val;
        } 
    #endif
    if (addr&1) {
        val = floating_bus(ula.cycles%228,ula.scanline);
    } else {
        WRITE_EVENT(1);
        // read keyboard matrix
        int key = 31;
        for (size_t bit = 0; bit < 8; bit++){
            if (!((addr>>8)&(1<<bit))) {
                int temp_key = ula.key_matrix[bit*5+0];
                temp_key |= ula.key_matrix[bit*5+1]<<1;
                temp_key |= ula.key_matrix[bit*5+2]<<2;
                temp_key |= ula.key_matrix[bit*5+3]<<3;
                temp_key |= ula.key_matrix[bit*5+4]<<4;
                key ^= temp_key; // xor with key
            }
        }
        val =  key|0xA0;
    }
    add_cycles_io(addr);
    return val;
}

static inline void outZ80(uint16_t addr, uint8_t val) {
    // very crude i know
    if (addr >= 0x4000 && addr < 0x8000 && ula.do_contended) { add_contended_cycles(); } add_cycles(1);
    if (!(addr&0x8000) && !(addr&2)) {
        // paging
        // 00DRGMMM
        // D = disable memory paging until reset
        // R = rom bank select
        // G = gfx bank select
        // M = ram bank select
        if (ula.bank_reg&32) return; // disabled paging until reset
        ula.bank_reg = val;
        ula.rom_sel = val>>4&1;
        ula.ram_bank = val&7;
        ula.gfx_sel = val>>3&1;
        WRITE_EVENT(4);
    } 
    #ifdef AY_EMULATION
    else if ((addr&0x8000) && !(addr&2)) {
        WRITE_EVENT(2);
        if (addr & 0x4000) {
            #ifdef AY_TURBOSOUND_FM
                if (visible_windows.do_tsfm) {
                    // turbosound chip
                    if ((val&0b11111000) == 0b11111000) {
                        // set AY chip (for TS)
                        AY_ind = (AY_ind&0x0ff)|((val&0x7)<<8);
                    } else {
                        AY_ind = (AY_ind&0xf00)|(val&0xff);                                         
                    }   
                } else {
                    // turbosound chip
                    if (val >= 16) {
                        // set AY chip (for TS)
                        AY_ind = (AY_ind&0x00f)|((val&0xf)<<8);
                    } else {
                        AY_ind = (AY_ind&0xf00)|(val&0xf);
                    }
                }
            #else
                // turbosound chip
                if (val >= 16) {
                    // set AY chip (for TS)
                    AY_ind = (AY_ind&0x0ff)|((val&0xf)<<8);
                } else {
                    AY_ind = (AY_ind&0xf00)|(val&0xff);
                }
            #endif
        } else {
            #ifdef AY_TURBOSOUND
                #ifdef AY_TURBOSOUND_FM
                    write_opn(AY_ind&0xff,val,AY_ind>>8&1);
                #endif
                // write reg
                #ifndef AY_TURBOSOUND_FM
                AY_regs[(AY_ind&0xf)|(((AY_ind>>8)&1)<<4)] = val;
                #endif

                #ifdef AY_TURBOSOUND_FM
                if ((AY_ind&0xff)<0x10) {
                    AY_regs[(AY_ind&0xf)|(((AY_ind>>8)&1)<<4)] = val;
                #endif
                    if (AY_ind&0x100)
                        update_ayumi_state(&AY_chip2,&AY_regs[16],AY_ind&0xf);
                    else
                        update_ayumi_state(&AY_chip1,AY_regs,AY_ind&0xf);
                #ifdef AY_TURBOSOUND_FM
                }
                #endif
            #else
                // write reg
                AY_regs[AY_ind&0xf] = val;
                update_ayumi_state(&AY_chip1,AY_regs,AY_ind&0xf);
            #endif
        }
    } 
    #endif
    else if ((addr&1) == 0) {
        ula.ULA_FE = val;
        WRITE_EVENT(0);
    }
    add_cycles_io(addr);
}

void update_ayumi_state(struct ayumi* ay, uint8_t* r, uint8_t addr) {
  switch (addr&0xf) {
    case 0:
    case 1:
        ayumi_set_tone(ay, 0, (r[1] << 8) | r[0]);
        break;
    case 2:
    case 3:
        ayumi_set_tone(ay, 1, (r[3] << 8) | r[2]);
        break;
    case 4:
    case 5:
        ayumi_set_tone(ay, 2, (r[5] << 8) | r[4]);
        break;
    case 6:
        ayumi_set_noise(ay, r[6]);
        break;

    case 7:
    case 8:
    case 9:
    case 10:
        ayumi_set_mixer(ay, 0, r[7] & 1, (r[7] >> 3) & 1, (r[8] >> 4) & 1);
        ayumi_set_mixer(ay, 1, (r[7] >> 1) & 1, (r[7] >> 4) & 1, (r[9] >> 4) & 1);
        ayumi_set_mixer(ay, 2, (r[7] >> 2) & 1, (r[7] >> 5) & 1, (r[10] >> 4) & 1);
        ayumi_set_volume(ay, 0, r[8] & 0xf);
        ayumi_set_volume(ay, 1, r[9] & 0xf);
        ayumi_set_volume(ay, 2, r[10] & 0xf);
        break;

    case 11:
    case 12:
        ayumi_set_envelope(ay, (r[12] << 8) | r[11]);
        break;

    case 13: {
        ayumi_set_envelope_shape(ay, r[13]);
        break;
    }
    default:
        break;
  }
}

#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_surface.h>
#include <SDL2/SDL_events.h>

#define SAMPLE_RATE 48000
#define BUFFER_SIZE (960*2) // 48000/50
#define CLAMP(x,y,z) ((x)>(z)?(z):((x)<(y)?(y):(x)))
#include "config.h"
#include "zx_128k_rom.h"

typedef enum { false, true } bool; // booleans yay

struct window_bool {
    uint8_t memviewer;
    uint8_t imgui_debugger;
    uint8_t settings;
    uint8_t read;
    uint8_t write;
    uint8_t exec;
    uint32_t read_col;
    uint32_t write_col;
    uint32_t exec_col;
    uint8_t is_ym1;
    uint8_t is_ym2;
    uint8_t aypan;
    int AY_seperation;
};

extern struct window_bool visible_windows;

#include "ayumi.h"
#include "misc.h"
#include "cpu.h"

bool audio_paused;
float audio_volume;

struct regs_Struct *regs_rewind;
struct flags_Struct *flags_rewind;
struct ula_Struct *ula_rewind;
uint32_t *tap_pos_rewind;
uint8_t *mem_rewind;
uint32_t *mem_pos_rewind;
int *time_rewind;
int rewind_frame;
bool rewind_pressed;
bool actually_rewind;

#include "io.h"

void reset_audio_buffer_and_unpause() {
    ula.audio_buffer_read = 0;
    ula.audio_buffer_write = BUFFER_SIZE;
    ula.time = SDL_GetTicks();
}

uint8_t get_ram_bank() {
    return ula.ram_bank;
}

uint8_t get_scr_rom_bank() {
    return (ula.gfx_sel?1:0)|(ula.rom_sel?2:0);
}

void callback(void *udata, uint8_t *stream, int len)
{
    SDL_memset(stream, 0, len);

    if (audio_paused) return;

    if (len > (BUFFER_SIZE<<1)) len = (BUFFER_SIZE<<1); // clamp length

    SDL_MixAudioFormat(
        stream, 
        (const Uint8 *)(&ula.audio_buffer[ula.audio_buffer_read]), 
        AUDIO_S16SYS, 
        len, 
        SDL_MIX_MAXVOLUME
    );

    ula.audio_buffer_read += len>>1;
    if (ula.audio_buffer_read >= (BUFFER_SIZE*4))
        ula.audio_buffer_read -= (BUFFER_SIZE*4);
}

FILE *tap;
int tap_file_size;
bool do_tap;
int cur_time_rewind = 0;
int cur_mempos_rewind = 0;

int rle_comp(uint8_t **mem_buffer, int mem_pos, uint8_t *mem_in) {
    int rle_len = 1;
    for (int pos = 1; pos < 131072; pos++) { // 128k ram
        if (mem_in[pos-1] == mem_in[pos] && (pos != 131071)) {
            rle_len++;
        } else {
            // output
            uint8_t rle_val = mem_in[pos-1];
            while (rle_len > (255+2)) {
                (*mem_buffer)[mem_pos] = rle_val;
                mem_pos = (mem_pos+1)%REWIND_MEM;
                (*mem_buffer)[mem_pos] = rle_val;
                mem_pos = (mem_pos+1)%REWIND_MEM;
                (*mem_buffer)[mem_pos] = 255;
                mem_pos = (mem_pos+1)%REWIND_MEM;
                rle_len -= (255+2);
            }
            if (rle_len >= 2) {
                rle_len -= 2;
                (*mem_buffer)[mem_pos] = rle_val;
                mem_pos = (mem_pos+1)%REWIND_MEM;
                (*mem_buffer)[mem_pos] = rle_val;
                mem_pos = (mem_pos+1)%REWIND_MEM;
                (*mem_buffer)[mem_pos] = rle_len;
                mem_pos = (mem_pos+1)%REWIND_MEM;
            } else if (rle_len == 1) {
                (*mem_buffer)[mem_pos] = rle_val;
                mem_pos = (mem_pos+1)%REWIND_MEM;
            }
            rle_len = 1;
        }
    }
    return mem_pos;
}

int rle_decomp(uint8_t *mem_buffer, int mem_pos, uint8_t *mem_in) {
    int mem_len = 0;
    while (mem_len < 131072) {
        uint8_t first_byte = mem_buffer[mem_pos];
        mem_pos = (mem_pos+1)%REWIND_MEM;
        uint8_t extra_byte = mem_buffer[mem_pos];
        if (first_byte == extra_byte) {
            mem_pos = (mem_pos+1)%REWIND_MEM;
            int count = (int)(mem_buffer[mem_pos])+2;
            mem_pos = (mem_pos+1)%REWIND_MEM;
            for (int i = 0; i < count; i++) {
                (mem_in)[mem_len] = first_byte;
                mem_len++;
                if (mem_len >= 131072) return mem_pos;
            }
        } else {
            (mem_in)[mem_len] = first_byte;
            mem_len++;
            if (mem_len >= 131072) return mem_pos;      
        }
    }
    return mem_pos;
}

int last_no_press_mem_pos;
bool mem_pos_underflow;

void do_rewind() {
    if (ula.frame&1) {
        int ula_frame = ula.frame;
        int ula_time = ula.time;
        if (rewind_pressed) {
            // load FROM rewind buffer
            actually_rewind = false;
            if ((cur_time_rewind-time_rewind[rewind_frame]) < (REWIND_FRAMES) &&
                time_rewind[rewind_frame] > -1) {

                if ((mem_pos_rewind[rewind_frame] <= last_no_press_mem_pos) && mem_pos_underflow) {
                        goto end_rewind;
                }

                rewind_frame--;
                if (rewind_frame < 0)
                    rewind_frame += REWIND_FRAMES;


                if (((cur_time_rewind-time_rewind[rewind_frame]) >= (REWIND_FRAMES)) || 
                    (time_rewind[rewind_frame] == -1)) {
                    goto end_rewind;
                }


                if ((mem_pos_rewind[rewind_frame] <= last_no_press_mem_pos) && mem_pos_underflow) {
                    goto end_rewind;
            }


                actually_rewind = true;

                memcpy(&regs,&regs_rewind[rewind_frame],sizeof(regs));
                memcpy(&flags,&flags_rewind[rewind_frame],sizeof(flags));
                memcpy(&ula,&ula_rewind[rewind_frame],sizeof(ula));
                int old_mempos = cur_mempos_rewind;
                cur_mempos_rewind = rle_decomp(mem_rewind,mem_pos_rewind[rewind_frame],mem);
                if (old_mempos < cur_mempos_rewind) {
                    mem_pos_underflow = true;
                }
                if (do_tap) fseek(tap, (long)tap_pos_rewind[rewind_frame], SEEK_SET);
            }
        } else {
            // save to rewind buffer
            rewind_frame = (rewind_frame+1)%REWIND_FRAMES;
            memcpy(&regs_rewind[rewind_frame],&regs,sizeof(regs));
            memcpy(&flags_rewind[rewind_frame],&flags,sizeof(flags));
            memcpy(&ula_rewind[rewind_frame],&ula,sizeof(ula));
            if (do_tap)
                tap_pos_rewind[rewind_frame] = ftell(tap);
            else
                tap_pos_rewind[rewind_frame] = 0;
            time_rewind[rewind_frame] = cur_time_rewind;
            mem_pos_rewind[rewind_frame] = (uint32_t)cur_mempos_rewind;
            mem_pos_underflow = false;
            cur_mempos_rewind = rle_comp(&mem_rewind,cur_mempos_rewind,mem);
            last_no_press_mem_pos = cur_mempos_rewind;
            cur_time_rewind++;
        }
        end_rewind:
            ula.frame = ula_frame;
            ula.time = ula_time;
    }
}

bool has_ULA_quit() {
    return ula.quit;
}

SDL_Renderer* get_SDL_renderer();

void load_file(char *file) {
    if (do_tap) fclose(tap);
    do_tap = true;
    tap_file_size = 0;
    tap = fopen(file,"rb");
    // get file size
    fseek(tap, 0L, SEEK_END);
    tap_file_size = (int)ftell(tap);
    fseek(tap, 0L, SEEK_SET);
}

void AY_set_pan(int pan_type);

void init_zx(int argc, char *argv[], bool init_files) {
    audio_paused = false;
    if (init_files) {
        do_tap = argc > 1;
        tap_file_size = 0;
        if (do_tap) {
            tap = fopen(argv[1],"rb");
            // get file size
            fseek(tap, 0L, SEEK_END);
            tap_file_size = (int)ftell(tap);
            fseek(tap, 0L, SEEK_SET);
        }

        zx_rom = (uint8_t*)malloc(32768*sizeof(uint8_t));

        if (argc > 2) { // read bios file from CLI arguments
            FILE *f = fopen("128k.bin","rb");
            assert(f);
            for (int i = 0; i < 32768; i++) {
                zx_rom[i] = fgetc(f);
            }
            fclose(f);
        } else {
            memcpy(zx_rom,zx_128k_rom,32768);
        }

    } else if (do_tap) {
        fseek(tap, 0L, SEEK_SET);
    }

    pixels = (uint32_t*)malloc(320*256*sizeof(uint32_t));
    mem_highlight = (uint32_t*)malloc(131072*sizeof(uint32_t));
    memset(mem_highlight,0,131072*sizeof(uint32_t));

    zx_rom_highlight = (uint32_t*)malloc(32768*sizeof(uint32_t));
    memset(zx_rom_highlight,0,32768*sizeof(uint32_t));

    // for rewind
    last_no_press_mem_pos = 0;
    rewind_frame = 0;

    regs_rewind = (struct regs_Struct*)malloc(REWIND_FRAMES*sizeof(struct regs_Struct));
    memset(regs_rewind,0,REWIND_FRAMES*sizeof(struct regs_Struct));

    flags_rewind = (struct flags_Struct*)malloc(REWIND_FRAMES*sizeof(struct flags_Struct));
    memset(flags_rewind,0,REWIND_FRAMES*sizeof(struct flags_Struct));

    ula_rewind = (struct ula_Struct*)malloc(REWIND_FRAMES*sizeof(struct ula_Struct));
    memset(ula_rewind,0,REWIND_FRAMES*sizeof(struct ula_Struct));

    tap_pos_rewind = (uint32_t*)malloc(REWIND_FRAMES*sizeof(uint32_t));
    memset(tap_pos_rewind,0,REWIND_FRAMES*sizeof(uint32_t));

    mem_rewind = (uint8_t*)malloc(REWIND_MEM*sizeof(uint8_t));
    memset(mem_rewind,0,REWIND_MEM*sizeof(uint8_t));

    time_rewind = (int*)malloc(REWIND_FRAMES*sizeof(int));
    memset(time_rewind,-1,REWIND_FRAMES*sizeof(int));

    mem_pos_rewind = (uint32_t*)malloc(REWIND_FRAMES*sizeof(uint32_t));
    memset(mem_pos_rewind,0,REWIND_FRAMES*sizeof(uint32_t));

    cur_time_rewind = 0;
    cur_mempos_rewind = 0;

    rewind_pressed = false;
    actually_rewind = false;
    mem_pos_underflow = false;

    // init Z80 registers
    memset(&regs,0,sizeof(regs));
    memset(&flags,0,sizeof(flags));
    memset(&ula,0,sizeof(ula));
    regs.pc = 0;
    regs.sp = 0xfffe;
    regs.has_int = -1;
    regs.im = 1;
    regs.iff1 = 0;
    regs.r = 255;
    memset(mem,0,sizeof(mem));
    //regs.cycles = 0;

    // init ULA
    ula.rom_sel = 0;
    ula.ram_bank = 0;
    ula.bank_reg = 0;
    ula.gfx_sel = 0;
    ula.scanline = 0;
    ula.ULA_FE = 0;
    memset(ula.key_matrix_buf,0,sizeof(ula.key_matrix_buf));

    // init ULA audio
    ula.audio_cycles = 0;
    ula.audio_buffer_read = 0;
    ula.audio_buffer_write = BUFFER_SIZE;
    ula.beeper_filter = 0;
    ula.audio_buffer_ind = 0;
    audio_volume = 1;
    memset(ula.audio_buffer,0,sizeof(ula.audio_buffer));

    // init AY if enabled
    #ifdef AY_EMULATION
        #ifdef AY_TURBOSOUND
            memset(AY_regs,0,32);
            AY_ind = 0;
            if (!ayumi_configure(&AY_chip1, visible_windows.is_ym1, AY_CLOCK, SAMPLE_RATE)) {
                printf("ayumi_configure error (AY chip 1)\n");
            }
            if (!ayumi_configure(&AY_chip2, visible_windows.is_ym2, AY_CLOCK, SAMPLE_RATE)) {
                printf("ayumi_configure error (AY chip 2)\n");
            }
            // set stereo (to actually MONO)
            ayumi_set_pan(&AY_chip1, 0, 0, 0);
            ayumi_set_pan(&AY_chip1, 1, 0, 0);
            ayumi_set_pan(&AY_chip1, 2, 0, 0);
            ayumi_set_pan(&AY_chip2, 0, 0, 0);
            ayumi_set_pan(&AY_chip2, 1, 0, 0);
            ayumi_set_pan(&AY_chip2, 2, 0, 0);
        #else
            memset(AY_regs,0,16);
            AY_ind = 0;
            if (!ayumi_configure(&AY_chip1, 0, AY_CLOCK, SAMPLE_RATE)) {
                printf("ayumi_configure error (AY chip 1)\n");
            }
            // set stereo (to actually MONO)
            ayumi_set_pan(&AY_chip1, 0, 0, 0);
            ayumi_set_pan(&AY_chip1, 1, 0, 0);
            ayumi_set_pan(&AY_chip1, 2, 0, 0);
        #endif
        AY_set_pan(visible_windows.aypan);
    #endif

    ula.time = SDL_GetTicks() + 20;
    memset(ula.key_matrix_buf,0,sizeof(ula.key_matrix_buf)); // clear key matrix buffer
    memset(ula.key_matrix,0,sizeof(ula.key_matrix)); // clear key matrix
    ula.quit = false;

    SDL_AudioSpec wanted;
    wanted.freq = SAMPLE_RATE;
    wanted.format = AUDIO_S16;
    wanted.channels = 2;
    wanted.samples = BUFFER_SIZE>>1;
    wanted.callback = callback;
    wanted.userdata = NULL;

    dev = SDL_OpenAudio(&wanted, NULL);
    SDL_PauseAudio(0);
}

void main_zx() {
    ula.did_frame = false;
    while (!ula.did_frame) {
        // HLE .tap loading yipeeeee
        if (regs.pc == 0x56C && ula.rom_sel && do_tap) {
            // get tap block size (in 16-bit LE format)
            if (ftell(tap) >= (tap_file_size-2)) {
                flags.c = 1;
                ret(true,false);
            } else {
                uint16_t tap_size = fgetc(tap);
                tap_size |= fgetc(tap)<<8;
                // compare it with DE
                if ((tap_size-2) == REG_DE) {
                    // now check if the first byte from the data block == A'
                    // if not then clear carry and execute ret
                    uint8_t data_block_byte = fgetc(tap);
                    if (data_block_byte == regs.as) {
                        // (description found from r/emudev IIRC)
                        // if carry is set in F' then this is a LOAD, not a VERIFY
                        // so only if it is set I then read DE bytes from the tape,
                        // writing them starting at IX and then set carry in regular F
                        if (regs.fs&1) {
                            uint16_t chunk_size = REG_DE;
                            for (int i = 0; i < chunk_size; i++) {
                                writeZ80(regs.ix++,fgetc(tap));
                            }
                            fgetc(tap); // skip one byte
                            regs.d = 0;
                            regs.e = 0;
                            regs.h = 0;
                            regs.a = 0;
                            regs.l = 170;
                            flags.c = 1;
                            ret(true,false);
                        }
                    } else {
                        // go back ONE byte
                        long pos = ftell(tap)-1;
                        fseek(tap, pos, SEEK_SET);
                        flags.c = 0;
                        ret(true,false);
                    }
                } else {
                    // go back 2 bytes
                    long pos = ftell(tap)-2;
                    fseek(tap, pos, SEEK_SET);
                    flags.c = 0;
                    ret(true,false);
                }
            }
        }

        ula.audio_cycles_beeper = 0;
        step();

        // ula video
        if (ula.cycles >= 228) advance_ULA();

        // audio buffer
        int16_t beeper = (ula.ULA_FE>>4&1?((8192-1024)*2):0);
        ula.beeper_filter = ((ula.beeper_filter*(ula.audio_cycles_beeper*2))+beeper)/(ula.audio_cycles_beeper*2+1);
        if (ula.audio_cycles >= 295) { // (3545400/48000*4)
            ula.audio_cycles -= 295;
            #ifdef AY_EMULATION
                // mix beeper with AY
                ayumi_process(&AY_chip1);
                ayumi_remove_dc(&AY_chip1);
                #ifdef AY_TURBOSOUND
                    ayumi_process(&AY_chip2);
                    ayumi_remove_dc(&AY_chip2);
                #endif

                #ifdef AY_TURBOSOUND
                ula.audio_buffer_temp[ula.audio_buffer_ind++] = 
                    CLAMP((ula.beeper_filter+(int16_t)(AY_chip1.left*(8192-1024)*2.0)
                    +(int16_t)(AY_chip2.left*(8192-1024)*2.0))*audio_volume,-32767,32767);
                ula.audio_buffer_temp[ula.audio_buffer_ind++] = 
                    CLAMP((ula.beeper_filter+(int16_t)(AY_chip1.right*(8192-1024)*2.0)
                    +(int16_t)(AY_chip2.right*(8192-1024)*2.0))*audio_volume,-32767,32767);
                #else
                ula.audio_buffer_temp[ula.audio_buffer_ind++] = 
                    CLAMP((ula.beeper_filter+(int16_t)(AY_chip1.left*(8192-1024)*2.0))*audio_volume,-32767,32767);
                ula.audio_buffer_temp[ula.audio_buffer_ind++] = 
                    CLAMP((ula.beeper_filter+(int16_t)(AY_chip1.right*(8192-1024)*2.0))*audio_volume,-32767,32767);
                #endif
            #else
                ula.audio_buffer_temp[ula.audio_buffer_ind++] = ula.beeper_filter;
            #endif
            if (ula.audio_buffer_ind == BUFFER_SIZE) {
                ula.audio_buffer_ind = 0;
                memcpy(&ula.audio_buffer[ula.audio_buffer_write], 
                       ula.audio_buffer_temp,
                       BUFFER_SIZE<<1);

                ula.audio_buffer_write += BUFFER_SIZE;

                if (ula.audio_buffer_write >= (BUFFER_SIZE*4))
                    ula.audio_buffer_write -= (BUFFER_SIZE*4);

                #ifdef AUDIO_SYNC
                    int now = SDL_GetTicks();
                    if (ula.time > now) {
                        now = ula.time - now;
                        SDL_Delay(now);
                    }
                    ula.time += 1000/50;
                    do_rewind();
                #endif
            }
        }
    }
}


void deinit_zx(bool deinit_file) {
    if (deinit_file) free(zx_rom);
    free(pixels);
    if (do_tap && deinit_file) fclose(tap);
    SDL_PauseAudioDevice(dev, 1);
    SDL_CloseAudioDevice(dev);
    free(mem_highlight);
    free(zx_rom_highlight);
    // rewind
    free(regs_rewind);
    free(flags_rewind);
    free(ula_rewind);
    free(tap_pos_rewind);
    free(mem_rewind);
    free(time_rewind);
    free(mem_pos_rewind);
}

void set_YM(bool is_ym, int chip) {
    // init AY if enabled
    #ifdef AY_EMULATION
        #ifdef AY_TURBOSOUND
            if (chip) {
                ayumi_set_ym(&AY_chip2, is_ym?1:0);
            } else {
                ayumi_set_ym(&AY_chip1, is_ym?1:0);
            }
        #else
            ayumi_set_ym(&AY_chip1, is_ym?1:0);
        #endif
    #endif
}

void AY_set_pan(int pan_type) {
    #ifdef AY_EMULATION
        float sep = ((float)(100-visible_windows.AY_seperation))/200.0f;
        switch (pan_type) {
            case 0:
            default: // Mono
                ayumi_set_pan(&AY_chip1, 0, 0.5, 0);
                ayumi_set_pan(&AY_chip1, 1, 0.5, 0);
                ayumi_set_pan(&AY_chip1, 2, 0.5, 0);
                break;

            case 1: // ABC Stereo
                ayumi_set_pan(&AY_chip1, 0, 0+sep, 0);
                ayumi_set_pan(&AY_chip1, 1, 0.5, 0);
                ayumi_set_pan(&AY_chip1, 2, 1-sep, 0);
                break;

            case 2: // ACB Stereo :flushed:
                ayumi_set_pan(&AY_chip1, 0, 0+sep, 0);
                ayumi_set_pan(&AY_chip1, 1, 1-sep, 0);
                ayumi_set_pan(&AY_chip1, 2, 0.5, 0);
                break;
        }
        #ifdef AY_TURBOSOUND
            switch (pan_type) {
                case 0:
                default: // Mono
                    ayumi_set_pan(&AY_chip2, 0, 0.5, 0);
                    ayumi_set_pan(&AY_chip2, 1, 0.5, 0);
                    ayumi_set_pan(&AY_chip2, 2, 0.5, 0);
                    break;
    
                case 1: // ABC Stereo
                    ayumi_set_pan(&AY_chip2, 0, 0+sep, 0);
                    ayumi_set_pan(&AY_chip2, 1, 0.5, 0);
                    ayumi_set_pan(&AY_chip2, 2, 1-sep, 0);
                    break;
    
                case 2: // ACB Stereo :flushed:
                    ayumi_set_pan(&AY_chip2, 0, 0+sep, 0);
                    ayumi_set_pan(&AY_chip2, 1, 1-sep, 0);
                    ayumi_set_pan(&AY_chip2, 2, 0.5, 0);
                    break;
            }
        #endif
    #endif
}

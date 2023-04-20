#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gem.h>
#include <mint/osbind.h>
#include <mint/ostruct.h>
#include <mint/sysvars.h>

enum {
    FRONT_BUFFER,
    BACK_BUFFER1,
    BACK_BUFFER2,
    BUFFER_COUNT
};

struct Screen {
    void* p;
    void* p_unaligned;
    short pxy[4];
};

static int width;
static int height;
static int bpp;
static short pxy[4];
static size_t screen_size;

static struct Screen screen[BUFFER_COUNT];
static struct Screen *p_screen[BUFFER_COUNT];
static struct Screen *p_work_screen;

static enum {
    DIRECT,
    SINGLE,
    DOUBLE,
    TRIPLE
} render_mode = DIRECT;

void set_rect_min() {
    pxy[0] = 0;
    pxy[1] = height / 2 - (height / 8) / 2;
    pxy[2] = (width / 8);
    pxy[3] = height / 2 + (height / 8) / 2;
}

static void reset_screens() {
    for (int i = 0; i < BUFFER_COUNT; ++i) {
        p_screen[i] = &screen[i];

        memset(screen[i].p, 0, width * height * bpp);
        memset(screen[i].pxy, 0, sizeof(screen[i].pxy));
    }

    p_work_screen = p_screen[FRONT_BUFFER];

    set_rect_min();
}

static void *atari_alloc(size_t size, uint16_t alloc_type) {
    // Mxalloc is available since GEMDOS 0.19
    if (((Sversion()&0xFF) >= 0x01) || (Sversion() >= 0x1900)) {
        return (void *)Mxalloc(size, alloc_type);
    } else {
        return (void *)Malloc(size);
    }
}

// don't trust system Vsync() as it's quite unreliable (esp. on FreeMiNT)
#undef Vsync
static void Vsync() {
    for (long old_frclock = get_sysvar(_frclock); old_frclock == get_sysvar(_frclock); )
        ;
}

int main(int argc, char *argv[]) {
    int16_t app_id = appl_init();
    bool aes_present  = aes_global[0] != 0x0000;
    int16_t vdi_handle;
    int16_t work_in[16] = {};
    int16_t work_out[57] = {};

    if (app_id == -1 && aes_present) {
        fprintf(stderr, "appl_init() failed.\r\n");
        getchar();
        return EXIT_FAILURE;
    }

    // work_in[0] = 1: current resolution; others: defaults
    for (int i = 0; i < 10; work_in[i++] = 1);
    // raster coordinates
    work_in[10] = 2;
    // work_in[11] to work_in[15] are only useful with NVDI

    if (app_id == -1) {
        // if AES is not present, clean up
        appl_exit();

        // open a physical workstation
        v_opnwk(work_in, &vdi_handle, work_out);

        if (vdi_handle == 0) {
            fprintf(stderr, "v_opnwk() failed.\r\n");
            getchar();
            return EXIT_FAILURE;
        }

        // flush keyboard
        while (Cconis() == -1)  {
            Cnecin();
        }
    } else {
        // get the ID of the current physical screen workstation
        int16_t wcell, hcell, wbox, hbox;
        vdi_handle = graf_handle(&wcell, &hcell, &wbox, &hbox);
        if (vdi_handle < 1) {
            appl_exit();

            fprintf(stderr, "graf_handle() failed.\r\n");
            getchar();
            return EXIT_FAILURE;
        }

        // open a virtual screen workstation
        v_opnvwk(work_in, &vdi_handle, work_out);

        if (vdi_handle == 0) {
            appl_exit();

            fprintf(stderr, "v_opnvwk() failed.\r\n");
            getchar();
            return EXIT_FAILURE;
        }

        graf_mouse(M_OFF, NULL);
        wind_update(BEG_UPDATE);
    }

    ///////////////////////////////////////////////////////////////////////////

    width = work_out[0] + 1;
    height = work_out[1] + 1;
    // if >32767 then assume 32-bit else 8-bit, precise number doesn't really matter
    bpp = work_out[39] == 0 ? 4 : 1;
    screen_size = width * height * bpp;

    for (int i = 0; i < BUFFER_COUNT; ++i) {
        const int ALIGN = 256;   // 256 bytes

        screen[i].p_unaligned = atari_alloc(screen_size + ALIGN-1, MX_STRAM);
        if (screen[i].p_unaligned == NULL) {
            fprintf(stderr, "atari_alloc() failed.\r\n");
            getchar();
            return EXIT_FAILURE;
        }

        screen[i].p = (void *)(((uintptr_t)screen[i].p_unaligned + ALIGN-1) & -ALIGN);
    }

    reset_screens();
    v_clrwk(vdi_handle);

    const EVMULT_IN evmult_in = {
        .emi_flags      = MU_KEYBD | MU_TIMER,

        .emi_bclicks    = 0,
        .emi_bmask      = 0,
        .emi_bstate     = 0,

        .emi_m1leave    = 0,
        .emi_m1         = { 0, 0, 0, 0 },   // m1x, m1y, m1w, m1h (not used)

        .emi_m2leave    = 0,
        .emi_m2         = { 0, 0, 0, 0 },   // m2x, m2y, m2w, m2h (not used)

        .emi_tlow       = 1,                // 1 ms
        .emi_thigh      = 0                 //
    };

    {
        // VDI setup
        int16_t hor_out, vert_out;
        vst_alignment(vdi_handle, TA_LEFT, TA_TOP, &hor_out, &vert_out);

        vsf_interior(vdi_handle, FIS_HATCH);
        vsf_perimeter(vdi_handle, PERIMETER_OFF);
    }

    void *old_logbase = Logbase();
    void *old_physbase = Physbase();

    int mul = 1;
    float pos = 0;
    int frames = 0;
    bool quit = false;
    do {
        char ascii = -1;

        if (aes_present) {
            int16_t msg_buffer[8];
            EVMULT_OUT evmult_out;
            uint16_t event = evnt_multi_fast(&evmult_in, msg_buffer, &evmult_out);

            if (event & MU_KEYBD)
                ascii = evmult_out.emo_kreturn & 0xFF;
        } else {
            if (Cconis() == -1)
                ascii = Cnecin() & 0xFF;
        }

        // as VDI draws into Logbase and we want to avoid using offscreen bitmaps
        // (and NVDI), use it as our logical buffer

        switch (ascii) {
        case ' ':
            quit = true;
            break;
        case '0':
            render_mode = DIRECT;
            // we're going to use pScreen[FRONT_BUFFER].pxy so call reset_screens() for init
            reset_screens();
            p_work_screen = p_screen[FRONT_BUFFER];
            Setscreen(old_logbase, old_physbase, SCR_NOCHANGE);
            Vsync();
            v_clrwk(vdi_handle);
            break;
        case '1':
            render_mode = SINGLE;
            reset_screens();
            p_work_screen = p_screen[FRONT_BUFFER];
            Setscreen(p_screen[FRONT_BUFFER]->p, p_screen[FRONT_BUFFER]->p, SCR_NOCHANGE);
            Vsync();
            break;
        case '2':
            render_mode = DOUBLE;
            reset_screens();
            p_work_screen = p_screen[BACK_BUFFER1];
            Setscreen(p_screen[BACK_BUFFER1]->p, p_screen[FRONT_BUFFER]->p, SCR_NOCHANGE);
            Vsync();
            break;
        case '3':
            render_mode = TRIPLE;
            reset_screens();
            p_work_screen = p_screen[BACK_BUFFER1];
            Setscreen(p_screen[BACK_BUFFER1]->p, p_screen[FRONT_BUFFER]->p, SCR_NOCHANGE);
            Vsync();
        }

        if (ascii != -1) {
            mul = 1;
            pos = 0.0f;
            frames = 0;
            set_rect_min();
        }

        /// drawing ///

        // do the screen handling now so we don't wait for evnt_multi just after Vsync()
        if (render_mode == SINGLE) {
            Vsync();
        } else if (render_mode == DOUBLE) {
            // swap
            struct Screen *tmp = p_screen[FRONT_BUFFER];
            p_screen[FRONT_BUFFER] = p_screen[BACK_BUFFER1];
            p_screen[BACK_BUFFER1] = tmp;
            // set (will be done in nearest vbl)
            p_work_screen = p_screen[BACK_BUFFER1];
            Setscreen(p_screen[BACK_BUFFER1]->p, p_screen[FRONT_BUFFER]->p, SCR_NOCHANGE);
            // wait for vbl
            Vsync();
        } else if (render_mode == TRIPLE) {
            set_sysvar_to_long(vblsem, 0);  // lock vbl

            static long old_vbclock;
            if (!old_vbclock)
                old_vbclock = get_sysvar(_vbclock);
            long curr_vbclock = get_sysvar(_vbclock);

            if (old_vbclock != curr_vbclock) {
                // at least one vbl has passed since setting new video base
                // guard BACK_BUFFER2 from overwriting while presented
                struct Screen *tmp = p_screen[FRONT_BUFFER];
                p_screen[FRONT_BUFFER] = p_screen[BACK_BUFFER2];
                p_screen[BACK_BUFFER2] = tmp;

                old_vbclock = curr_vbclock;
            }

            // swap back buffers
            struct Screen *tmp = p_screen[BACK_BUFFER1];
            p_screen[BACK_BUFFER1] = p_screen[BACK_BUFFER2];
            p_screen[BACK_BUFFER2] = tmp;

            // set BACK_BUFFER2 as the most recent frame content (will be done in nearest vbl)
            //                                                    ^^^^^^^^^^^^^^^^^^^^^^^^^^^ no it wont
            Setscreen(p_screen[BACK_BUFFER1]->p, p_screen[BACK_BUFFER2]->p, SCR_NOCHANGE);

            set_sysvar_to_long(vblsem, 1);  // unlock vbl

            p_work_screen = p_screen[BACK_BUFFER1];
        }

        static long old_ticks;
        if (!old_ticks)
            old_ticks = get_sysvar(_hz_200);

        long curr_ticks = get_sysvar(_hz_200);
        long diff = curr_ticks - old_ticks;
        old_ticks = curr_ticks;

        frames++;

        // clear screen
        if (p_work_screen->pxy[2] != 0) {
            vsf_color(vdi_handle, 0);
            v_bar(vdi_handle, p_work_screen->pxy);
        }

        char str[256] = {0};
        sprintf(str, "Buffers: %d, frame time (ms): %ld, frames: %d       ", (int)render_mode, 1000 * diff / 200, frames);
        v_gtext(vdi_handle, 0, 0, str);

        vsf_color(vdi_handle, 1);
        v_bar(vdi_handle, pxy);
        memcpy(p_work_screen->pxy, pxy, sizeof(pxy));

        /// end of drawing ///

        pos += 0.8f * diff;
        if (pos >= 1.0f) {
            pos = 0.0f;

            pxy[0] += 1;
            pxy[2] += 1;
        }

        if (pxy[2] >= width) {
            mul++;
            if (mul >= 4) {
                //mul = 1;
                //set_rect_min();
                quit = true;
            } else {
                set_rect_min();
                pxy[1] -= 40*mul;
                pxy[2] += 60*mul;
                pxy[3] += 40*mul;
            }
        }
    } while (!quit);

    Setscreen(old_logbase, old_physbase, SCR_NOCHANGE);

    for (int i = 0; i < BUFFER_COUNT; ++i) {
        Mfree(screen[i].p_unaligned);
        screen[i].p_unaligned = screen[i].p = NULL;
    }

    ///////////////////////////////////////////////////////////////////////////

    if (app_id != -1) {
        wind_update(END_UPDATE);
        // redraw screen
        form_dial(FMD_FINISH, 0, 0, 0, 0, 0, 0, width, height);
        graf_mouse(M_ON, NULL);

        v_clsvwk(vdi_handle);
        appl_exit();
    } else {
        v_clswk(vdi_handle);
    }

    printf("Frames: %d", frames);

    return EXIT_SUCCESS;
}

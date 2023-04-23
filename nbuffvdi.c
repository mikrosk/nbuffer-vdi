#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gem.h>
#include <mint/cookie.h>
#include <mint/falcon.h>
#include <mint/osbind.h>
#include <mint/ostruct.h>
#include <mint/sysvars.h>

enum {
    FRONT_BUFFER,
    BACK_BUFFER1,
    BACK_BUFFER2,
    BUFFER_COUNT
};

struct Rect {
    short x;
    short y;
    short w;
    short h;
};

struct Screen {
    void* p;
    void* p_unaligned;
    struct Rect rect;
};

static int width = 320;
static int height = 240;
static int bpp = 2;
static struct Rect rect;
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
    rect.x = 0;
    rect.y = height / 2 - (height / 8) / 2;
    rect.w = (width / 8);
    rect.h = (height / 8);
}

static void reset_screens() {
    for (int i = 0; i < BUFFER_COUNT; ++i) {
        p_screen[i] = &screen[i];

        memset(screen[i].p, 0, width * height * bpp);
        memset(&screen[i].rect, 0, sizeof(screen[i].rect));
    }

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

    if (app_id == -1 && aes_present) {
        fprintf(stderr, "appl_init() failed.\r\n");
        getchar();
        return EXIT_FAILURE;
    }

    if (app_id == -1) {
        // if AES is not present, clean up
        appl_exit();

        // flush keyboard
        while (Cconis() == -1)  {
            Cnecin();
        }
    }

    ///////////////////////////////////////////////////////////////////////////

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

    const EVMULT_IN evmult_in = {
        .emi_flags      = MU_KEYBD | MU_TIMER,

        .emi_bclicks    = 0,
        .emi_bmask      = 0,
        .emi_bstate     = 0,

        .emi_m1leave    = 0,
        .emi_m1         = { 0, 0, 0, 0 },   // m1x, m1y, m1w, m1h (not used)

        .emi_m2leave    = 0,
        .emi_m2         = { 0, 0, 0, 0 },   // m2x, m2y, m2w, m2h (not used)

        .emi_tlow       = 0,                // 0 ms
        .emi_thigh      = 0                 //
    };

    void *old_logbase = Logbase();
    void *old_physbase = Physbase();
    short old_mode = VsetMode(VM_INQUIRE);

    VsetMode(COL40|PAL|VGA|BPS16|VERTFLAG);

    int mul = 1;
    float pos = 0;
    int frames = 0;
    bool quit = false;
    do {
        char ascii = -1;
        static bool first_run = true;
        if (first_run) {
            ascii = '0';
            first_run = false;
        }

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

        switch (ascii) {
        case ' ':
            quit = true;
            break;
        case '0':
            render_mode = DIRECT;
            reset_screens();
            p_work_screen = p_screen[FRONT_BUFFER];
            Vsync();
            VsetScreen(SCR_NOCHANGE, p_screen[FRONT_BUFFER]->p, SCR_NOCHANGE, SCR_NOCHANGE);
            break;
        case '1':
            render_mode = SINGLE;
            reset_screens();
            p_work_screen = p_screen[FRONT_BUFFER];
            Vsync();
            VsetScreen(SCR_NOCHANGE, p_screen[FRONT_BUFFER]->p, SCR_NOCHANGE, SCR_NOCHANGE);
            break;
        case '2':
            render_mode = DOUBLE;
            reset_screens();
            p_work_screen = p_screen[BACK_BUFFER1];
            Vsync();
            VsetScreen(SCR_NOCHANGE, p_screen[FRONT_BUFFER]->p, SCR_NOCHANGE, SCR_NOCHANGE);
            break;
        case '3':
            render_mode = TRIPLE;
            reset_screens();
            p_work_screen = p_screen[BACK_BUFFER1];
            Vsync();
            VsetScreen(SCR_NOCHANGE, p_screen[FRONT_BUFFER]->p, SCR_NOCHANGE, SCR_NOCHANGE);
        }

        if (ascii != -1) {
            mul = 1;
            pos = 0.0f;
            frames = 0;
            set_rect_min();
        }

        /// drawing ///

        static long old_ticks;
        if (!old_ticks)
            old_ticks = get_sysvar(_hz_200);

        long curr_ticks = get_sysvar(_hz_200);
        long diff = curr_ticks - old_ticks;
        old_ticks = curr_ticks;

        // clear screen
        if (p_work_screen->rect.w != 0) {
            char *p = (char *)p_work_screen->p + (p_work_screen->rect.y * width + p_work_screen->rect.x) * bpp;
            for (int i = 0; i < p_work_screen->rect.h; ++i) {
                memset(p, 0, p_work_screen->rect.w * bpp);
                p += width * bpp;
            }
        }

        char *p = (char *)p_work_screen->p + (rect.y * width + rect.x) * bpp;
        for (int i = 0; i < rect.h; ++i) {
            memset(p, 0xff, rect.w * bpp);
            p += width * bpp;
        }
        memcpy(&p_work_screen->rect, &rect, sizeof(rect));

        /// end of drawing ///

        pos += 0.8f * diff;
        if (pos >= 1.0f) {
            pos = 0.0f;

            rect.x += 1;
        }

        if (rect.x + rect.w >= width) {
            mul++;
            if (mul >= 4) {
                //mul = 1;
                //set_rect_min();
                quit = true;
            } else {
                set_rect_min();
                rect.y -= 20*mul;
                rect.w += 60*mul;
                rect.h += 40*mul;
            }
        }

        /// swap ///

        if (render_mode == SINGLE) {
            Vsync();
        } else if (render_mode == DOUBLE) {
            // swap
            struct Screen *tmp = p_screen[FRONT_BUFFER];
            p_screen[FRONT_BUFFER] = p_screen[BACK_BUFFER1];
            p_screen[BACK_BUFFER1] = tmp;
            p_work_screen = p_screen[BACK_BUFFER1];
            Vsync();
            // setting new video base address happens on line defined by VDB (after VBL)
            VsetScreen(SCR_NOCHANGE, p_screen[FRONT_BUFFER]->p, SCR_NOCHANGE, SCR_NOCHANGE);
        } else if (render_mode == TRIPLE) {
            set_sysvar_to_short(vblsem, 0);  // lock vbl

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

            // queue BACK_BUFFER2 with the most recent frame content
            // (this will be set after VBL (Falcon) or before VBL (ST/STE/TT)
            //VsetScreen(SCR_NOCHANGE, p_screen[BACK_BUFFER2]->p, SCR_NOCHANGE, SCR_NOCHANGE);
            set_sysvar_to_long(screenptr, p_screen[BACK_BUFFER2]->p);

            set_sysvar_to_short(vblsem, 1);  // unlock vbl

            p_work_screen = p_screen[BACK_BUFFER1];
        }

        frames++;
    } while (!quit);

    VsetMode(old_mode);
    VsetScreen(old_logbase, old_physbase, SCR_NOCHANGE, SCR_NOCHANGE);

    for (int i = 0; i < BUFFER_COUNT; ++i) {
        Mfree(screen[i].p_unaligned);
        screen[i].p_unaligned = screen[i].p = NULL;
    }

    ///////////////////////////////////////////////////////////////////////////

    if (app_id != -1) {
        appl_exit();
    }

    printf("Frames: %d\n", frames);

    return EXIT_SUCCESS;
}

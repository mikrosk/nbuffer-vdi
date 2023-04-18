#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <gem.h>
#include <mint/osbind.h>

void *atari_alloc(size_t size, uint16_t alloc_type) {
    // Mxalloc is available since GEMDOS 0.19
    if (((Sversion()&0xFF) >= 0x01) || (Sversion() >= 0x1900)) {
        return (void *)Mxalloc(size, alloc_type);
    } else {
        return (void *)Malloc(size);
    }
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
    for(int i = 0; i < 10; work_in[i++] = 1);
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

    const int width = work_out[0] + 1;
    const int height = work_out[1] + 1;
    printf("all good: %d x %d\r\n", width, height);

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

    bool quit = false;
    do {
        if (aes_present) {
            int16_t msg_buffer[8];
            EVMULT_OUT evmult_out;
            uint16_t event = evnt_multi_fast(&evmult_in, msg_buffer, &evmult_out);

            if (event & MU_KEYBD)
                quit = true;
        } else {
            if (Cconis() == -1)
                quit = true;
        }

        // v_clrwk(vdi_handle);

        static int col;

        short pxy[4];
        vsf_color(vdi_handle, col);
        vsf_interior(vdi_handle, FIS_SOLID);
        vsf_perimeter(vdi_handle, PERIMETER_OFF);

        pxy[0] = pxy[1] = 0;
        pxy[2] = width - 1;
        pxy[3] = height - 1;
        v_bar(vdi_handle, pxy);

        col = ~col & 0x01;
    } while (!quit);

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

    return EXIT_SUCCESS;
}

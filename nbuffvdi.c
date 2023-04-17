#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <gem.h>
#include <mint/osbind.h>

void *AtariAlloc(size_t size, uint16_t alloc_type) {
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
    for(int i = 0; i < 10; i++)
        work_in[i] = 1;
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
    } else {
        // get the ID of the current physical screen workstation
        int16_t dummy;
        vdi_handle = graf_handle(&dummy, &dummy, &dummy, &dummy);
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

        wind_update(BEG_UPDATE);
    }

    // TODO: code
    printf("all good\r\n");

    if (app_id != -1) {
        v_clsvwk(vdi_handle);

        wind_update(END_UPDATE);
        appl_exit();
    } else {
        v_clswk(vdi_handle);
    }

    return EXIT_SUCCESS;
}

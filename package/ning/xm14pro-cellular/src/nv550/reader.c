/* Read-only, temporary probe for this device.  DIAG transport and NV decoding
 * are provided by catxeon/qcom-efs-browser (BSD-3-Clause). */
#include "diag.h"
#include "efs2.h"
#include <stdint.h>
#include <stdio.h>

static int decode_imei(const uint8_t data[NV_ITEM_DATA_SIZE], char out[16])
{
    if (data[0] != 8 || (data[1] & 15) != 10 || (data[1] >> 4) > 9)
        return -1;
    out[0] = (char)('0' + (data[1] >> 4));
    for (int i = 0; i < 7; i++) {
        uint8_t v = data[i + 2];
        if ((v & 15) > 9 || (v >> 4) > 9) return -1;
        out[1 + 2 * i] = (char)('0' + (v & 15));
        out[2 + 2 * i] = (char)('0' + (v >> 4));
    }
    out[15] = 0;
    return 0;
}

int main(void)
{
    diag_t diag;
    uint8_t data[NV_ITEM_DATA_SIZE];
    uint16_t status = 0;
    char imei[2][16];
    if (diag_open(&diag) < 0) {
        fputs("DIAG unavailable\n", stderr);
        return 2;
    }
    for (int index = 0; index < 2; index++) {
        if (nv_read_sub(&diag, 550, (uint16_t)index, data, &status, 4000) < 0 || status != 0 ||
            decode_imei(data, imei[index]) < 0) {
            fprintf(stderr, "NV 550 index %d unreadable\n", index);
            diag_close(&diag);
            return 3;
        }
    }
    diag_close(&diag);
    printf("{\"imei1\":\"%s\",\"imei2\":\"%s\"}\n", imei[0], imei[1]);
    return 0;
}

#ifndef COLD_CODE_STATUS_H
#define COLD_CODE_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    COLD_CODE_OK = 0,
    COLD_CODE_ERR_ARG = 1,
    COLD_CODE_ERR_IMAGE = 2,
    COLD_CODE_ERR_SOURCE = 3,
    COLD_CODE_ERR_STATE = 4,
    COLD_CODE_ERR_SIZE = 5,        /* storage too small or misaligned */
    COLD_CODE_ERR_ARCH = 6,        /* image arch does not match the port */
    COLD_CODE_ERR_UNSUPPORTED = 7, /* ABI or arch backend not supported */
} cold_code_status_t;

const char *cold_code_status_string(cold_code_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_STATUS_H */

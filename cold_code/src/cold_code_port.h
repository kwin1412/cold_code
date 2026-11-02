#ifndef COLD_CODE_PORT_H
#define COLD_CODE_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
#define COLD_CODE_NORETURN [[noreturn]]
#else
#define COLD_CODE_NORETURN _Noreturn
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Platform port for the cold-code loader.
 *
 * The host implementation reads the image from a file (simulated flash).
 * An embedded port replaces this file and reads from the flash mapping
 * instead; the core loader does not call any OS interface directly.
 */

bool cold_code_port_open(const char *path);
size_t cold_code_port_size(void);
void cold_code_port_read(uint32_t offset, void *dst, size_t len);
void cold_code_port_log(const char *fmt, ...);
COLD_CODE_NORETURN void cold_code_port_fatal(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* COLD_CODE_PORT_H */

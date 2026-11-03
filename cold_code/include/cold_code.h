#ifndef COLD_CODE_H
#define COLD_CODE_H

/*
 * cold_code v5 public API.
 *
 * Include this umbrella header from consumers.  The manager layout is private;
 * image metadata, loader callbacks and the per-CPU current manager are exposed
 * through the headers below.
 */

#include "cold_code/context.h"
#include "cold_code/fault.h"
#include "cold_code/image.h"
#include "cold_code/loader.h"
#include "cold_code/manager.h"
#include "cold_code/port.h"
#include "cold_code/stats.h"
#include "cold_code/status.h"
#include "cold_code/storage.h"
#include "cold_code/trace.h"
#include "cold_code/types.h"
#include "cold_code/version.h"

/*
 * Mark a function as cold code.  In a cold-image build the function is placed
 * in .cold_text and linked into the separate image; in a hot build this is a
 * no-op annotation.
 */
#ifdef COLD_CODE_BUILD_IMAGE
#define COLD_CODE() __attribute__((noinline, used, section(".cold_text")))
#else
#define COLD_CODE()
#endif

#endif /* COLD_CODE_H */

#ifndef CAPYOS_CAPYGFX_BROWSER_APP_H
#define CAPYOS_CAPYGFX_BROWSER_APP_H

#include <stdint.h>

/* Run the retained graphical static browser in an already-created ring-3
 * window. `pixels` is a caller-owned width*height ARGB32 surface. The function
 * returns after CLOSE (0) or a fatal window syscall error (-1). */
int capygfx_browser_run(int window, uint32_t *pixels, uint32_t width,
                        uint32_t height);

#endif /* CAPYOS_CAPYGFX_BROWSER_APP_H */

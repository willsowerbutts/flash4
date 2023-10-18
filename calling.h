#ifndef __CALLING_DOT_H__
#define __CALLING_DOT_H__

#if (__SDCC_VERSION_MAJOR > 4) || (__SDCC_VERSION_MAJOR == 4 && __SDCC_VERSION_MINOR >= 2)
#define CALLING __sdcccall(0)
#define REGISTER_CALLING
#else
#define CALLING /* nothing required for sdcc < 4.2.0 */
#undef REGISTER_CALLING
#endif

#endif

#ifndef _RAVENNA_COMMON_VERSION_H
#define _RAVENNA_COMMON_VERSION_H

#define STRINGIFY2(x) #x
#define STRINGIFY(x) STRINGIFY2(x)

static inline const char *ra_driver_version(void)
{
	return STRINGIFY(RA_DRIVER_VERSION);
}

#endif /* _RAVENNA_COMMON_VERSION_H */

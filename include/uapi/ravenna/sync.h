// SPDX-License-Identifier: MIT

#ifndef _UAPI_RAVENNA_SYNC_H
#define _UAPI_RAVENNA_SYNC_H

#include <linux/types.h>
#include <linux/ioctl.h>

#include "types.h"

#define RA_SYNC_SET_MCLK_FREQUENCY	_IOW('r', 100, __u32)

#endif /* _UAPI_RAVENNA_SYNC_H */

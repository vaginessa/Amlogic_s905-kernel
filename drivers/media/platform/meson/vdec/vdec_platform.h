/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 Maxime Jourdan <maxi.jourdan@wanadoo.fr>
 */

#ifndef __MESON_VDEC_PLATFORM_H_
#define __MESON_VDEC_PLATFORM_H_

#include "vdec.h"

struct vdec_format;

enum vdec_revision {
	VDEC_REVISION_GXBB,
	VDEC_REVISION_GXL,
	VDEC_REVISION_GXM,
};

struct vdec_platform {
	const struct vdec_format *formats;
	const u32 num_formats;
	enum vdec_revision revision;
};

extern const struct vdec_platform vdec_platform_gxbb;
extern const struct vdec_platform vdec_platform_gxm;
extern const struct vdec_platform vdec_platform_gxl;

#endif
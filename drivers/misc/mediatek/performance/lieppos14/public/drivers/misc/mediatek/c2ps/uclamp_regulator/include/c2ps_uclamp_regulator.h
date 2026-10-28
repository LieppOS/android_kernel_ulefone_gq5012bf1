// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#ifndef C2PS_UCLAMP_REGULATOR_INCLUDE_C2PS_UCLAMP_REGULATOR_H_
#define C2PS_UCLAMP_REGULATOR_INCLUDE_C2PS_UCLAMP_REGULATOR_H_

#include "../../common/include/c2ps_common.h"

int uclamp_regulator_init(void);
void uclamp_regulator_exit(void);

void send_regulator_req(struct regulator_req *req);
struct regulator_req* get_regulator_req(void);
void c2ps_uclamp_regulator_flush(void);


#endif  // C2PS_UCLAMP_REGULATOR_INCLUDE_C2PS_UCLAMP_REGULATOR_H_

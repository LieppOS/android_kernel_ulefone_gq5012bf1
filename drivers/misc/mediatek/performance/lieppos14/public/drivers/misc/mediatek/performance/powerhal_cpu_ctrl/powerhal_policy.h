/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LIEPPOS_POWERHAL_POLICY_H
#define LIEPPOS_POWERHAL_POLICY_H
#define POWERHAL_MAX_POLICIES 10
struct powerhal_policy_state {
	int min, max;
	unsigned int hardware_min, hardware_max;
};
int powerhal_policy_init(void);
void powerhal_policy_exit(void);
int powerhal_policy_count(void);
int powerhal_policy_set(int index, int min, int max);
int powerhal_policy_read(int index, struct powerhal_policy_state *state);
#endif

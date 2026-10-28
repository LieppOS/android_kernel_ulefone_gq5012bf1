/* SPDX-License-Identifier: GPL-2.0 */
#ifndef FPSGO_PRIVATE_LIFETIME_H
#define FPSGO_PRIVATE_LIFETIME_H
#include <linux/compiler.h>
#include <linux/stddef.h>
#include <linux/errno.h>

/* Internal governor calls, never an unsynchronized exported slot invocation.
 * enter holds both the sleepable SRCU lifetime lease and the private-state
 * mutex: FBT, FSTB and XGF otherwise use different locks for shared lists.
 */
int fpsgo_private_enter(void);
void fpsgo_private_leave(int idx);
int fpsgo_private_begin(void);
int fpsgo_private_commit(void);
void fpsgo_private_revoke(void);

#define FPSGO_PRIVATE_CALL(callback, fallback, ...) ({ \
	int __lease = fpsgo_private_enter(); \
	typeof(callback) __callback = NULL; \
	typeof(__callback(__VA_ARGS__)) __result = (fallback); \
	if (__lease >= 0) { \
		__callback = READ_ONCE(callback); \
		if (__callback) \
			__result = __callback(__VA_ARGS__); \
		fpsgo_private_leave(__lease); \
	} \
	__result; \
})

#define FPSGO_PRIVATE_VOID(callback, ...) do { \
	int __lease = fpsgo_private_enter(); \
	if (__lease >= 0) { \
		typeof(callback) __callback = READ_ONCE(callback); \
		if (__callback) \
			__callback(__VA_ARGS__); \
		fpsgo_private_leave(__lease); \
	} \
} while (0)

/* Only the registration writer calls these, after disabling new leases. */
void fpsgo_xgf_private_stop(void);
void fpsgo_xgf_private_clear(void);
void fpsgo_fstb_private_clear(void);
void fpsgo_fbt_private_clear(void);
void fpsgo_base_private_clear(void);
void fpsgo_composer_private_clear(void);
#endif

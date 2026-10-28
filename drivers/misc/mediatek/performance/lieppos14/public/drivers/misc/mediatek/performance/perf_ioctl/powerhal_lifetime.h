/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LIEPPOS_POWERHAL_LIFETIME_H
#define LIEPPOS_POWERHAL_LIFETIME_H

/* Source-owned provider protocol; not a stock ABI witness. Bind after queue
 * construction, publish after installing every slot. Revoke wakes blocking
 * boost readers before draining all ioctls and clearing the provider table.
 * Neither revoke nor its callbacks may run from a leased ioctl or with a
 * provider queue lock held.
 */
int powerhal_ioctl_provider_bind(void (*stop_readers)(void), void (*clear_slots)(void));
void powerhal_ioctl_provider_publish(void);
void powerhal_ioctl_provider_revoke(void);

#endif

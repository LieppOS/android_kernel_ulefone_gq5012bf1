# SPDX-License-Identifier: GPL-2.0
#
# MediaTek ISP subsystem: imgsys (v4l2, per-platform engines, cmdq),
# the HCP message protocol and the IPE statistics engine.
# The vendor module build compiles these directories individually; the
# in-tree Kbuild descends from here instead.

# C29 uses the C28-validated external mtkcam implementations of these
# providers. Keep their Kconfig API selections for the external build, but
# never build both copies (same module names and overlapping exports).
ifneq ($(CONFIG_LIEPPOS_EXTERNAL_IMGSYS),y)
obj-y += imgsys/
obj-y += mtk-hcp/
endif
obj-y += mtk-ipesys-me/

/* SPDX-License-Identifier: GPL-2.0-only */
/* H8: checked, bounded gesture transitions and balanced wake references.
 * Caller already selected gesture_mode; do not silently skip D0 enable merely
 * because the donor-only gesture_support configuration bit is unset.
 */
#ifndef FOCALTECH_C56_PM_GESTURE_H
#define FOCALTECH_C56_PM_GESTURE_H
int fts_gesture_suspend(struct fts_ts_data *ts)
{
    static const u8 regs[] = {0xD1, 0xD2, 0xD5, 0xD6, 0xD7, 0xD8};
    u8 state;
    int ret = -EIO, i;
    unsigned int j;

    if (!fts_c56_get())
        return -ESHUTDOWN;
    for (i = 0; i < 5; i++) {
        for (j = 0; j < ARRAY_SIZE(regs); j++) {
            ret = fts_write_reg(regs[j], 0xFF);
            if (ret)
                break;
        }
        if (!ret && ts->Fod_support)
            ret = fts_write_reg(FTS_REG_FOD_EN, FTS_REG_FOD_VALUE);
        if (!ret)
            ret = fts_write_reg(FTS_REG_GESTURE_EN, ENABLE);
        fts_msleep(1);
        state = 0xFF;
        if (!ret)
            ret = fts_read_reg(FTS_REG_GESTURE_EN, &state);
        if (!ret && state == ENABLE)
            break;
        if (!ret)
            ret = -EIO;
    }
    if (i == 5)
        goto out;
    if (!fts_c56_life.irq_wake) {
        ret = enable_irq_wake(ts->irq);
        if (ret)
            goto out;
        fts_c56_life.irq_wake = true;
    }
    ret = 0;
out:
    if (ret)
        FTS_ERROR("C56-H8 gesture suspend failed:%d", ret);
    fts_c56_put();
    return ret;
}

int fts_gesture_resume(struct fts_ts_data *ts)
{
    u8 state;
    int ret = 0, i;

    if (!fts_c56_get())
        return -ESHUTDOWN;
    if (fts_c56_life.irq_wake) {
        ret = disable_irq_wake(ts->irq);
        if (ret)
            goto out;
        fts_c56_life.irq_wake = false;
    }
    for (i = 0; i < 5; i++) {
        state = 0xFF;
        ret = fts_write_reg(FTS_REG_GESTURE_EN, DISABLE);
        fts_msleep(1);
        if (!ret)
            ret = fts_read_reg(FTS_REG_GESTURE_EN, &state);
        if (!ret && state == DISABLE)
            break;
        if (!ret)
            ret = -EIO;
    }
    if (i == 5)
        goto out;
    if (ts->Fod_support != FTS_FOD_DISABLE)
        ret = fts_write_reg(FTS_REG_FOD_EN, FTS_REG_FOD_VALUE);
out:
    if (ret)
        FTS_ERROR("C56-H8 gesture resume failed:%d", ret);
    fts_c56_put();
    return ret;
}
#endif

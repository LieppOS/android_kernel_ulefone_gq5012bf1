/* SPDX-License-Identifier: GPL-2.0-only */
/* H8: independently implemented, bounded and failure-aware PM sequencing.
 * Included only by generated H8 core source; H7 references remain unchanged.
 * pm_lock serializes PM and queued firmware restoration. No spin/bus/report
 * lock is held across IRQ/work drains. blocked is cleared only after readiness.
 */
#ifndef FOCALTECH_C56_PM_H
#define FOCALTECH_C56_PM_H

static int fts_ts_suspend(struct device *dev)
{
    struct fts_ts_data *ts = fts_data;
    int ret = 0, err;

    if (!fts_c56_get())
        return -ESHUTDOWN;
    mutex_lock(&fts_c56_life.pm_lock);
    if (fts_c56_stopping()) { ret = -ESHUTDOWN; goto out; }
    if (!ts) { ret = -ENODEV; goto out; }
    if (ts->suspended)
        goto out;
    if (ts->fw_loading) { ret = -EBUSY; goto out; }

    WRITE_ONCE(ts->c56_pm_blocked, true);
    fts_irq_disable();
    synchronize_irq(ts->irq);
    fts_esdcheck_suspend(ts);
    ts->gesture_mode = (tpgesture_status == 1);
    memset(tpgesture_value, 0, sizeof(tpgesture_value));
    fts_release_all_finger();

    if (ts->gesture_mode) {
        ret = fts_gesture_suspend(ts);
        if (ret)
            goto failed;
        ts->suspended = true;
        if (fts_c56_stopping()) { ret = -ESHUTDOWN; goto failed; }
        WRITE_ONCE(ts->c56_pm_blocked, false);
        fts_irq_enable();
    } else {
        ret = fts_write_reg(FTS_REG_POWER_MODE, FTS_REG_POWER_MODE_SLEEP);
        if (!ts->ic_info.is_incell) {
#if FTS_POWER_SOURCE_CUST_EN
            err = fts_power_source_suspend(ts);
            if (!ret)
                ret = err;
#endif
        }
        ts->suspended = true;
        /* Ordinary powered-off sleep is blocked too: late firmware work and
         * IRQ/debug enables must not restart a controller behind PM's back. */
        if (ret)
            goto failed;
    }
    FTS_INFO("C56-H8 suspend complete,gesture:%d", ts->gesture_mode);
    goto out;
failed:
    ts->suspended = true;
    ts->fw_is_running = false;
    WRITE_ONCE(ts->c56_pm_blocked, true);
    FTS_ERROR("C56-H8 suspend failed:%d; IRQ/ESD remain stopped", ret);
out:
    mutex_unlock(&fts_c56_life.pm_lock);
    fts_c56_put();
    return ret;
}

static int fts_ts_resume(struct device *dev)
{
    struct fts_ts_data *ts = fts_data;
    int ret = 0;

    if (!fts_c56_get())
        return -ESHUTDOWN;
    mutex_lock(&fts_c56_life.pm_lock);
    if (fts_c56_stopping()) { ret = -ESHUTDOWN; goto out; }
    if (!ts) { ret = -ENODEV; goto out; }
    if ((!ts->suspended && !READ_ONCE(ts->c56_pm_blocked)) || yft_aod_state)
        goto out;
    if (ts->fw_loading) { ret = -EBUSY; goto out; }

    WRITE_ONCE(ts->c56_pm_blocked, true);
    fts_irq_disable();
    synchronize_irq(ts->irq);
    /* Must precede fts_enter_normal_fw: fw_download's synchronous ESD cancel
     * is then a no-op, including on the single-threaded fts_wq. */
    fts_esdcheck_suspend(ts);
    fts_release_all_finger();
    if (fts_c56_life.irq_wake) {
        ret = disable_irq_wake(ts->irq);
        if (ret)
            goto failed;
        fts_c56_life.irq_wake = false;
    }
    if (!ts->ic_info.is_incell) {
#if FTS_POWER_SOURCE_CUST_EN
        ret = fts_power_source_resume(ts);
        if (ret)
            goto failed;
#endif
        ret = fts_reset_proc(ts, false, 50);
        if (ret)
            goto failed;
    }
    ret = fts_enter_normal_fw();
    if (ret)
        goto failed;
    if (ts->gesture_mode) {
        ret = fts_gesture_resume(ts);
        if (ret)
            goto failed;
    }
    fts_ex_mode_recovery(ts);
    if (ts->pocket_mode) {
        ret = fts_write_reg(FTS_REG_POWER_MODE, 0);
        if (ret)
            goto failed;
        ts->pocket_mode = DISABLE;
    }
    if (fts_c56_stopping()) { ret = -ESHUTDOWN; goto failed; }
    memset(tpgesture_value, 0, sizeof(tpgesture_value));
    ts->suspended = false;
    WRITE_ONCE(ts->c56_pm_blocked, false);
    fts_esdcheck_resume(ts);
    /* Both paths were disabled explicitly above, including gesture wake. */
    fts_irq_enable();
    FTS_INFO("C56-H8 resume complete; normal firmware ready");
    goto out;
failed:
    ts->suspended = true;
    ts->fw_is_running = false;
    WRITE_ONCE(ts->c56_pm_blocked, true);
    /* No automatic retry/work enqueue. A later explicit wake can retry. */
    FTS_ERROR("C56-H8 resume failed:%d; IRQ/ESD remain stopped", ret);
out:
    mutex_unlock(&fts_c56_life.pm_lock);
    fts_c56_put();
    return ret;
}
#endif

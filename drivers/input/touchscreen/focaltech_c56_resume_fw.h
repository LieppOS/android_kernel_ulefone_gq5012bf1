/* SPDX-License-Identifier: GPL-2.0-only */
/* H8 normal-app restoration: stock-required operation, independent error policy.
 * fts_fw_resume/fts_fw_download retain their existing bounded three attempts.
 */
#ifndef FOCALTECH_C56_RESUME_FW_H
#define FOCALTECH_C56_RESUME_FW_H
int fts_enter_normal_fw(void)
{
    struct fts_upgrade *upg = fwupgrade;
    struct fts_ts_data *ts;
    u8 id = 0;
    int ret;

    if (!upg || !upg->ts_data || !upg->setting_nf || !upg->fw)
        return -EINVAL;
    ts = upg->ts_data;
    if (fts_c56_stopping())
        return -ESHUTDOWN;
    if (ts->fw_loading)
        return -EBUSY;
    ret = fts_read_reg(FTS_REG_CHIP_ID, &id);
    if (ret >= 0 && (id == ts->ic_info.ids.chip_idh || !fts_check_cid(ts, id))) {
        ts->fw_is_running = true;
        return 0;
    }
    ts->fw_is_running = false;
    ret = fts_fw_resume(true, FW_NORMAL);
    if (ret)
        goto failed;
    ret = fts_wait_tp_to_valid();
    if (ret)
        goto failed;
    if (!ts->fw_is_running) {
        ret = -EIO;
        goto failed;
    }
    return 0;
failed:
    ts->fw_is_running = false;
    FTS_ERROR("C56-H8 normal firmware restore failed:%d", ret);
    return ret;
}
#endif

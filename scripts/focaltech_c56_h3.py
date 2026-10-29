#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Private FT3680 integration for C56-H3, owned by the isolated kernel tree.
Reads C29's sealed module source; writes ONLY the designated private build tree.
No vendor firmware/source payload is embedded in this script or committed.
Independently authored lifecycle replacements below are compiled/tested verbatim.
"""
from pathlib import Path
import hashlib
import json
import re

BASE = Path('/mnt/SamsungSSD/lieppos-c29/engine/builds/gq5012bf1-mt6878-c29-android17/android17-full-3/src/common/drivers/input/touchscreen/focaltech_touch')
OUT = Path.home() / 'LieppOS/build/c56/h3/private-source/drivers/input/touchscreen/focaltech_touch'
REL = 'drivers/input/touchscreen/focaltech_touch/'


def mask_c(s):
    def repl(m):
        return ''.join('\n' if c == '\n' else ' ' for c in m.group())
    return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', repl, s, flags=re.S)


def function_span(s, name):
    m = mask_c(s)
    for hit in re.finditer(r'(?m)^[^;{}\n]*\b' + re.escape(name) + r'\s*\(', m):
        start = hit.start()
        if re.search(r'\b(if|while|switch|return)\b', m[start:hit.end()]):
            continue
        i, depth = hit.end(), 1
        while depth:
            depth += (m[i] == '(') - (m[i] == ')')
            i += 1
        while m[i].isspace():
            i += 1
        if m[i] != '{':
            continue
        brace, depth = i, 1
        i += 1
        while depth:
            depth += (m[i] == '{') - (m[i] == '}')
            i += 1
        return start, brace, i
    raise ValueError('function not found: ' + name)


def counted(src, name, kind='int', error='-ESHUTDOWN', serialize_pm=False):
    """Wrap actual body without renaming __func__ or changing existing return values."""
    a, b, z = function_span(src, name)
    body = src[b + 1:z - 1]
    masked = mask_c(body)
    edits = []
    for hit in re.finditer(r'\breturn\b', masked):
        end = masked.index(';', hit.end()) + 1
        expr = body[hit.end():end - 1].strip()
        edits.append((hit.start(), end, ('{ _c56_result = (' + expr + '); goto _c56_out; }') if expr else 'goto _c56_out;'))
    for x, y, text in reversed(edits):
        body = body[:x] + text + body[y:]
    line = src.count('\n', 0, b) + 1
    if kind == 'void':
        prefix, suffix = 'if (!fts_c56_get()) return;\n', 'fts_c56_put();\nreturn;'
    else:
        prefix = kind + ' _c56_result = 0;\nif (!fts_c56_get()) return ' + error + ';\n'
        suffix = 'fts_c56_put();\nreturn _c56_result;'
    if serialize_pm:
        assert kind != 'void'
        prefix += 'mutex_lock(&fts_c56_life.pm_lock);\nif (fts_c56_stopping()) { _c56_result = -ESHUTDOWN; goto _c56_out; }\n'
        suffix = 'mutex_unlock(&fts_c56_life.pm_lock);\n' + suffix
    return src[a:b + 1] + '\n' + prefix + '{\n#line ' + str(line) + '\n' + body + '\n}\ngoto _c56_out;\n_c56_out:\n' + suffix + '\n}'


QUIESCE = r'''
/* stop_lock is not taken by callbacks/workers or counted user operations. */
int fts_c56_quiesce(struct fts_ts_data *ts_data)
{
    int ret = 0;
    int err;

    if (!ts_data)
        return -EINVAL;
    mutex_lock(&fts_c56_life.stop_lock);
    if (fts_c56_life.quiesced) {
        ret = fts_c56_life.stop_error;
        goto out;
    }
    fts_c56_begin_stop();
    /* Wake blocked file readers before waiting for any counted operation. */
    WRITE_ONCE(ts_data->ta_flag, 0);
    wake_up_all(&ts_data->ts_waitqueue);
    fts_c56_fhp_wake();
    fts_c56_fwdbg_wake();

    if (fts_c56_life.notifier_registered) {
        err = fts_notifier_callback_exit(ts_data);
        /* Blocking-notifier API returns only 0 or -ENOENT; both drain readers. */
        if (err && err != -ENOENT) {
            FTS_ERROR("C56-H3 notifier unregister unexpected error:%d", err);
            ret = err;
        }
        fts_c56_life.notifier_registered = false;
    }
    if (ts_data->resume_work.func)
        cancel_work_sync(&ts_data->resume_work);
    if (fts_c56_life.irq_registered) {
        fts_irq_disable();
        synchronize_irq(ts_data->irq);
    }
    /* No gate/spin/bus/report lock held across waits for users or workers. */
    fts_c56_wait_users();
    /* A pre-stop gesture/AOD request could have enabled wake: drain it first. */
    if (fts_c56_life.irq_registered && fts_c56_life.irq_wake) {
        err = disable_irq_wake(ts_data->irq);
        if (err) {
            FTS_ERROR("C56-H3 disable irq wake failed:%d", err);
            if (!ret) ret = err;
        } else {
            fts_c56_life.irq_wake = false;
        }
    }
    fts_c56_fwupg_cancel(ts_data);
    if (ts_data->esdcheck_work.work.func)
        cancel_delayed_work_sync(&ts_data->esdcheck_work);
    if (ts_data->prc_work.work.func)
        cancel_delayed_work_sync(&ts_data->prc_work);
    if (ts_data->fwdbg_work.work.func)
        cancel_delayed_work_sync(&ts_data->fwdbg_work);
    /* SPI entry points check stopping AFTER bus_lock acquisition. */
    mutex_lock(&ts_data->bus_lock);
    if (ts_data->pdata && gpio_is_valid(ts_data->pdata->reset_gpio)) {
        err = gpio_direction_output(ts_data->pdata->reset_gpio, 0);
        if (err) {
            FTS_ERROR("C56-H3 shutdown reset failed:%d", err);
            if (!ret) ret = err;
        }
    }
    mutex_unlock(&ts_data->bus_lock);
    /* Keep objects, queues, supplies and endpoint state alive for late close. */
    fts_c56_life.stop_error = ret;
    fts_c56_life.quiesced = true;
out:
    mutex_unlock(&fts_c56_life.stop_lock);
    return ret;
}
'''

REMOVE = r'''
int fts_ts_remove_entry(struct fts_ts_data *ts_data)
{
    int ret;

    if (!ts_data)
        return -EINVAL;
    ret = fts_c56_quiesce(ts_data);
    if (fts_c56_life.irq_registered) {
        free_irq(ts_data->irq, ts_data);
        fts_c56_life.irq_registered = false;
    }
    /* Remove/drain user interfaces before freeing their state. */
    fts_remove_sysfs(ts_data);
    fts_release_apk_debug_channel(ts_data);
    fts_fwdbg_exit(ts_data);
    fts_fhp_exit(ts_data);
    fts_fwupg_exit(ts_data);
    fts_esdcheck_exit(ts_data);
    fts_gesture_exit(ts_data);
    fts_ex_mode_exit(ts_data);
    fts_point_report_check_exit(ts_data);
    if (ts_data->input_dev) {
        input_unregister_device(ts_data->input_dev);
        ts_data->input_dev = NULL;
    }
#if FTS_PEN_EN
    if (ts_data->pen_dev) {
        input_unregister_device(ts_data->pen_dev);
        ts_data->pen_dev = NULL;
    }
#endif
    if (ts_data->ts_workqueue) {
        destroy_workqueue(ts_data->ts_workqueue);
        ts_data->ts_workqueue = NULL;
    }
    /* Power off WHILE the reset/DVDD GPIOs are still owned. */
#if FTS_POWER_SOURCE_CUST_EN
    {
        int err = fts_power_source_exit(ts_data);
        if (err) {
            FTS_ERROR("C56-H3 power cleanup failed:%d", err);
            if (!ret) ret = err;
        }
    }
#endif
    if (ts_data->pdata) {
        if (gpio_is_valid(ts_data->pdata->reset_gpio))
            gpio_free(ts_data->pdata->reset_gpio);
        if (gpio_is_valid(ts_data->pdata->irq_gpio))
            gpio_free(ts_data->pdata->irq_gpio);
        /* VDDI is requested by power_source_init, not gpio_configure. */
        if (fts_c56_life.dvdd_requested) {
            gpio_free(ts_data->pdata->dvdd_gpio);
            fts_c56_life.dvdd_requested = false;
        }
    }
    if (ts_data->ws) {
        wakeup_source_unregister(ts_data->ws);
        ts_data->ws = NULL;
    }
    kfree_safe(ts_data->ta_buf);
    kfree_safe(ts_data->touch_buf);
    kfree_safe(ts_data->bus_tx_buf);
    kfree_safe(ts_data->bus_rx_buf);
    kfree_safe(ts_data->pdata);
    return ret;
}
'''

SHUTDOWN = r'''
static void fts_ts_shutdown(struct spi_device *spi)
{
    struct fts_ts_data *ts_data = spi_get_drvdata(spi);
    int ret;

    if (!ts_data)
        return;
    ret = fts_c56_quiesce(ts_data);
    if (ret)
        FTS_ERROR("C56-H3 shutdown quiesce failed:%d", ret);
    /* No free/unregister/put here: system shutdown is NOT module removal. */
}
'''

SPI_REMOVE = r'''
static void fts_ts_remove(struct spi_device *spi)
{
    struct fts_ts_data *ts_data = spi_get_drvdata(spi);
    int ret;

    if (!ts_data)
        return;
    ret = fts_ts_remove_entry(ts_data);
    if (ret)
        FTS_ERROR("C56-H3 remove quiesce reported:%d", ret);
    spi_set_drvdata(spi, NULL);
    fts_data = NULL;
    kfree(ts_data);
}
'''

FW_CANCEL = r'''
void fts_c56_fwupg_cancel(struct fts_ts_data *ts_data)
{
    if (!fwupgrade)
        return;
    cancel_work_sync(&fwupgrade->upgrade_work);
    cancel_work_sync(&fwupgrade->fwrecover_work);
    cancel_work_sync(&fwupgrade->fwload_work);
}
'''

NOTIFIER_EXIT = r'''
static int fts_notifier_callback_exit(struct fts_ts_data *ts_data)
{
    int ret = 0;
#if IS_ENABLED(CONFIG_DRM_MEDIATEK_V2)
    ret = mtk_disp_notifier_unregister(&ts_data->disp_notifier);
#elif IS_ENABLED(CONFIG_FB)
    ret = fb_unregister_client(&ts_data->fb_notif);
#endif
    if (ret)
        FTS_ERROR("C56-H3 notifier unregister:%d", ret);
    return ret;
}
'''


def generate():
    original = {p.name: p.read_text() for p in BASE.iterdir() if p.suffix in ('.c', '.h')}
    result = {}
    for name, src in original.items():
        changes = []
        canonical = REL + name

        def replace_fn(fn, text):
            a, b, z = function_span(src, fn)
            endline = src.count('\n', 0, z) + 2
            changes.append((a, z, text.strip() + '\n#line ' + str(endline) + ' "' + canonical + '"'))

        def modify_fn(fn, transform):
            a, b, z = function_span(src, fn)
            old = src[a:z]
            replace_fn(fn, transform(old))

        def sub_fn(fn, old, new, count=1):
            def transform(t):
                assert t.count(old) == count, (name, fn, old, t.count(old))
                return t.replace(old, new)
            modify_fn(fn, transform)

        def count_fn(fn, kind='int', error='-ESHUTDOWN'):
            replace_fn(fn, counted(src, fn, kind, error))

        if name == 'focaltech_core.h':
            at = src.index('#include "focaltech_common.h"') + len('#include "focaltech_common.h"')
            text = '\n#include "focaltech_c56_lifecycle.h"\n'
            text += 'int fts_c56_quiesce(struct fts_ts_data *ts_data);\n'  # forward below, before prototype use
            # Declare the tag at file scope before the prototype (avoid prototype-scope tag).
            text = text.replace('int fts_c56_quiesce', 'struct fts_ts_data;\nint fts_c56_quiesce')
            text += 'void fts_c56_fwupg_cancel(struct fts_ts_data *ts_data);\nvoid fts_c56_fhp_wake(void);\nvoid fts_c56_fwdbg_wake(void);\n'
            changes.append((at, at, text + '#line ' + str(src.count('\n', 0, at) + 1) + ' "' + canonical + '"\n'))
        if name == 'focaltech_core.c':
            pos = src.index('struct fts_ts_data *fts_data;') + len('struct fts_ts_data *fts_data;')
            changes.append((pos, pos, '\nstruct fts_c56_lifecycle fts_c56_life = FTS_C56_LIFECYCLE_INITIALIZER(fts_c56_life);\n#line ' + str(src.count('\n', 0, pos) + 1) + ' "' + canonical + '"\n'))
            count_fn('fts_set_reset')
            count_fn('yft_fts_ts_aod_contorl')
            for fn in ['fts_ts_suspend', 'fts_ts_resume']:
                replace_fn(fn, counted(src, fn, serialize_pm=True))
            replace_fn('fts_power_source_ctrl', '#include "focaltech_c56_power.h"')
            replace_fn('fts_power_source_init', '')
            replace_fn('fts_power_source_exit', '')
            sub_fn('fts_irq_enable', 'if (fts_data->irq_disabled)', 'if (!fts_c56_stopping() && fts_c56_life.irq_registered && fts_c56_life.probe_ready && fts_data->irq_disabled)')
            replace_fn('fts_reset_proc', '''int fts_reset_proc(struct fts_ts_data *ts_data, bool force, u32 hdelayms)
{
    int ret;
    if (fts_c56_stopping()) return -ESHUTDOWN;
    if (!force && ts_data->fw_loading) return 0;
    ret = fts_set_reset(ts_data, 0);
    if (ret) return ret;
    usleep_range(2000, 4000);
    ret = fts_set_reset(ts_data, 1);
    if (ret) return ret;
    if (hdelayms) fts_msleep(hdelayms);
    return 0;
}''')
            # Do not touch data or schedule work in a newly arriving IRQ after stop.
            modify_fn('fts_irq_handler', lambda t: t.replace('{\n', '{\n    if (fts_c56_stopping()) return IRQ_HANDLED;\n', 1))
            modify_fn('fb_notifier_callback', lambda t: t.replace('{\n', '{\n    if (fts_c56_stopping()) return 0;\n', 1))
            replace_fn('fts_notifier_callback_exit', NOTIFIER_EXIT)
            # Request IRQ disabled before publishing optional interfaces; fatal request failure
            # then uses the existing early unwind, never leaving debug/FHP endpoints dangling.
            sub_fn('fts_irq_registration', 'IRQF_TRIGGER_FALLING | IRQF_ONESHOT', 'IRQF_TRIGGER_FALLING | IRQF_ONESHOT | IRQF_NO_AUTOEN')
            def probe(t):
                t = t.replace('    FTS_FUNC_ENTER();', '    FTS_FUNC_ENTER();\n    WRITE_ONCE(fts_c56_life.stopping, false);\n    fts_c56_life.quiesced = false;\n    fts_c56_life.probe_ready = false;\n    fts_c56_life.stop_error = 0;', 1)
                t = t.replace('        FTS_ERROR("device-tree parse fail");', '        FTS_ERROR("device-tree parse fail");\n        kfree_safe(ts_data->pdata);\n        return ret;', 1)
                t = t.replace('        FTS_ERROR("create fts workqueue fail");', '        FTS_ERROR("create fts workqueue fail");\n        kfree_safe(ts_data->pdata);\n        return -ENOMEM;', 1)
                # This item must exist before any notifier/firmware exposure.
                t = t.replace('    init_waitqueue_head(&ts_data->ts_waitqueue);', '    init_waitqueue_head(&ts_data->ts_waitqueue);\n    INIT_WORK(&ts_data->resume_work, fts_resume_work);', 1)
                a = t.index('    ret = fts_irq_registration(ts_data);')
                b = t.index('    ret = fts_fwupg_init(ts_data);', a)
                block = t[a:b]
                t = t[:a] + t[b:]
                block += '    fts_c56_life.irq_registered = true;\n    ts_data->irq_disabled = true;\n\n'
                at = t.index('    ret = fts_create_apk_debug_channel(ts_data);')
                t = t[:at] + block + t[at:]
                t = t.replace('    if (ts_data->ts_workqueue) {\n        INIT_WORK(&ts_data->resume_work, fts_resume_work);\n    }\n', '')
                t = t.replace('    FTS_FUNC_EXIT();\n    return 0;', '    if (!ret) fts_c56_life.notifier_registered = true;\n    fts_c56_life.probe_ready = true;\n    fts_irq_enable();\n    FTS_FUNC_EXIT();\n    return 0;', 1)
                t = t.replace('        gpio_free(ts_data->pdata->irq_gpio);\nerr_gpio_config:', '        gpio_free(ts_data->pdata->irq_gpio);\n    if (fts_c56_life.dvdd_requested) {\n        gpio_free(ts_data->pdata->dvdd_gpio);\n        fts_c56_life.dvdd_requested = false;\n    }\nerr_gpio_config:', 1)
                t = t.replace('err_bus_init:\n', 'err_bus_init:\n    if (ts_data->ws) {\n        wakeup_source_unregister(ts_data->ws);\n        ts_data->ws = NULL;\n    }\n', 1)
                return t
            modify_fn('fts_ts_probe_entry', probe)
            replace_fn('fts_ts_remove_entry', QUIESCE + '\n' + REMOVE)
        if name == 'focaltech_spi.c':
            for fn in ['fts_write', 'fts_read', 'fts_bus_transfer_direct', 'fts_bus_set_speed']:
                sub_fn(fn, 'mutex_lock(&ts_data->bus_lock);', 'mutex_lock(&ts_data->bus_lock);\n    if (fts_c56_stopping()) {\n        mutex_unlock(&ts_data->bus_lock);\n        return -ESHUTDOWN;\n    }')
            count_fn('fts_bus_configure')
            replace_fn('fts_ts_shutdown', SHUTDOWN)
            replace_fn('fts_ts_remove', SPI_REMOVE)
            sub_fn('fts_ts_probe', 'kfree(ts_data);', 'spi_set_drvdata(spi, NULL);\n        kfree(ts_data);')
            # Exposed unbind is not a supported substitute for reference-safe module unload.
            old = '.owner          = THIS_MODULE,'
            assert src.count(old) == 1
            a = src.index(old) + len(old)
            changes.append((a, a, '\n        .suppress_bind_attrs = true,\n#line ' + str(src.count('\n', 0, a) + 1) + ' "' + canonical + '"\n'))
        if name == 'focaltech_flash.c':
            old = '#define FTS_ALLOW_FW_PROGRAMMING\t0'
            assert src.count(old) == 1
            a = src.index(old)
            changes.append((a, a + len(old), old[:-1] + '1'))
            count_fn('fts_fw_recovery')
            a, b, z = function_span(src, 'fts_fwupg_exit')
            changes.append((a, a, FW_CANCEL + '\n#line ' + str(src.count('\n', 0, a) + 1) + ' "' + canonical + '"\n'))
        if name == 'focaltech_esdcheck.c':
            count_fn('fts_esdcheck_switch', 'void')
            count_fn('esdcheck_func', 'void')
        if name == 'focaltech_point_report_check.c':
            count_fn('fts_prc_queue_work', 'void')
            # Count the self-rearming callback too: stop waits for any pre-stop
            # requeue to complete before its final cancel_delayed_work_sync.
            count_fn('fts_prc_func', 'void')
        if name == 'focaltech_gesture.c':
            for fn in ['fts_gesture_suspend', 'fts_gesture_resume']:
                a, b, z = function_span(src, fn)
                t = counted(src, fn)
                if fn.endswith('suspend'):
                    key = 'if (enable_irq_wake(ts_data->irq)) {'
                    t = t.replace(key, 'if (enable_irq_wake(ts_data->irq)) {')
                    t = t.replace('FTS_DEBUG("enable_irq_wake(irq:%d) fail", ts_data->irq);\n    }', 'FTS_ERROR("enable_irq_wake(irq:%d) fail", ts_data->irq);\n    } else {\n        fts_c56_life.irq_wake = true;\n    }')
                else:
                    t = t.replace('if (disable_irq_wake(ts_data->irq)) {', 'if (fts_c56_life.irq_wake && disable_irq_wake(ts_data->irq)) {')
                    t = t.replace('FTS_DEBUG("disable_irq_wake(irq:%d) fail", ts_data->irq);\n    }', 'FTS_ERROR("disable_irq_wake(irq:%d) fail", ts_data->irq);\n    } else {\n        fts_c56_life.irq_wake = false;\n    }')
                replace_fn(fn, t)
        if name == 'focaltech_fhp.c':
            # An IRQ enqueue can race a closing file's queue release. Validate
            # only under the SAME mutex as close; bound payload to its stride.
            key = '#define FTS_FHP_FIX_ENQUEUE_OVERRUN 0'
            assert src.count(key) == 1
            at = src.index(key)
            changes.append((at, at + len(key), key[:-1] + '1'))
            replace_fn('fhpq_enqueue', '''static int fhpq_enqueue(struct fhp_queue *q, u8 *val)
{
    u8 *dst;
    u64 stamp;
    if (!q || !val) return -EINVAL;
    stamp = ktime_to_us(ktime_get_real());
    mutex_lock(&q->mutexq);
    if (!q->buffer || q->sizeq < sizeof(stamp) || q->sizeq > FHP_FRAME_SIZE_MAX ||
        !q->maxq || q->maxq > FHP_QUEUE_BUF_SIZE / q->sizeq || q->wridx >= q->maxq) {
        mutex_unlock(&q->mutexq);
        return -EINVAL;
    }
    dst = q->buffer + q->wridx * q->sizeq;
    memcpy(dst, &stamp, sizeof(stamp));
    memcpy(dst + sizeof(stamp), val, q->sizeq - sizeof(stamp));
    q->wridx = (q->wridx + 1) % q->maxq;
    if (q->count == q->maxq) q->rdidx = (q->rdidx + 1) % q->maxq;
    else q->count++;
    mutex_unlock(&q->mutexq);
    return 0;
}''')
            for fn in ['fhp_open', 'fhp_ioctl', 'fhp_input_ioctl']:
                count_fn(fn, 'long' if 'ioctl' in fn else 'int')
            sub_fn('fhp_poll', '    poll_wait(', '    if (fts_c56_stopping()) return POLLERR | POLLHUP;\n    poll_wait(')
            def frame(t):
                t = t.replace('fhp->state == FHP_STATE_FRAME_READY', 'fts_c56_stopping() || fhp->state == FHP_STATE_FRAME_READY')
                return t.replace('    ret = fhpq_dequeue_userspace', '    if (fts_c56_stopping()) return -ESHUTDOWN;\n    ret = fhpq_dequeue_userspace')
            modify_fn('fhp_ioctl_get_frame', frame)
            for marker in ['static const struct file_operations fhp_fops = {', 'static const struct file_operations fhp_input_fops = {']:
                a = src.index(marker) + len(marker)
                changes.append((a, a, '\n    .owner = THIS_MODULE,\n#line ' + str(src.count('\n', 0, a) + 1) + ' "' + canonical + '"\n'))
            # Keep partial optional misc registration state alive until real removal.
            at = src.index('struct fhp_data *fts_fhp_data;') + len('struct fhp_data *fts_fhp_data;')
            changes.append((at, at, '\nstatic bool c56_fhp_main_registered;\nstatic bool c56_fhp_input_registered;\n#line ' + str(src.count('\n', 0, at) + 1) + ' "' + canonical + '"\n'))
            replace_fn('fts_fhp_init', '''int fts_fhp_init(struct fts_ts_data *ts_data)
{
    struct fhp_data *fhp;
    int ret;
    fhp = kzalloc(sizeof(*fhp), GFP_KERNEL);
    if (!fhp) return -ENOMEM;
    fhp->ts_data = ts_data;
    fhp->state = FHP_STATE_IDLE;
    init_waitqueue_head(&fhp->frame_waitq);
    mutex_init(&fhp->q.mutexq);
    fts_fhp_data = fhp;
    ret = fhp_miscdev_init(fhp);
    if (ret) {
        FTS_ERROR("C56-H3 FHP misc registration failed:%d", ret);
        fts_fhp_data = NULL;
        kfree(fhp);
        return ret;
    }
    c56_fhp_main_registered = true;
    ret = fhp_input_miscdev_init(fhp);
    if (ret) {
        FTS_ERROR("C56-H3 FHP input registration failed:%d", ret);
        /* An open first endpoint owns a module reference: retain its state.
         * Probe treats FHP as optional; real removal closes this registration. */
        return ret;
    }
    c56_fhp_input_registered = true;
    return 0;
}''')
            t = '''void fts_c56_fhp_wake(void)\n{\n    if (fts_fhp_data) wake_up_all(&fts_fhp_data->frame_waitq);\n}\n\nint fts_fhp_exit(struct fts_ts_data *ts_data)\n{\n    struct fhp_data *fhp = fts_fhp_data;\n    if (!fhp) return 0;\n    if (c56_fhp_input_registered) misc_deregister(&fhp->fhp_input_dev);\n    if (c56_fhp_main_registered) misc_deregister(&fhp->fhp_dev);\n    c56_fhp_input_registered = false;\n    c56_fhp_main_registered = false;\n    fhpq_close(&fhp->q);\n    fts_fhp_data = NULL;\n    kfree(fhp);\n    return 0;\n}'''
            replace_fn('fts_fhp_exit', t)
        if name == 'focaltech_fwdbg.c':
            def debug_init(t):
                block = '\tif (ts_data->ts_workqueue)\n\t\tINIT_DELAYED_WORK(&ts_data->fwdbg_work, fts_fwdbg_work_func);\n'
                assert t.count(block) == 1
                t = t.replace(block, '')
                at = t.index('\tdbg->ts_data = ts_data;')
                return t[:at] + block + t[at:]
            modify_fn('fts_fwdbg_init', debug_init)
            count_fn('fts_fwdbg_handle_reset', 'void')
            def read(t):
                t = t.replace('dbg->wait_state == 2', 'fts_c56_stopping() || dbg->wait_state == 2')
                t = t.replace('dbg->wait_state == 2);', 'dbg->wait_state == 2);\n            if (fts_c56_stopping()) return -ESHUTDOWN;')
                return counted(t, 'fts_fwdbg_read', 'ssize_t')
            modify_fn('fts_fwdbg_read', read)
            count_fn('fts_fwdbg_open')
            a, b, z = function_span(src, 'fts_fwdbg_exit')
            t = src[a:z]
            # Remove the old too-late cancellation, insert it before the first free.
            t = t.replace('\t\tcancel_delayed_work_sync(&ts_data->fwdbg_work);', '')
            t = t.replace('\tif (dbg) {', '\tif (ts_data && ts_data->fwdbg_work.work.func)\n\t\tcancel_delayed_work_sync(&ts_data->fwdbg_work);\n\tif (dbg) {', 1)
            wake = 'void fts_c56_fwdbg_wake(void)\n{\n    if (fts_fwdbg_data) wake_up_all(&fts_fwdbg_data->frame_waitq);\n}\n'
            replace_fn('fts_fwdbg_exit', wake + t)
        if name == 'focaltech_ex_fun.c':
            replace_fn('fts_ta_open', '''static int fts_ta_open(struct inode *inode, struct file *file)
{
    struct fts_ts_data *ts_data = pde_data(inode);
    int ret = 0;
    if (!fts_c56_get()) return -ESHUTDOWN;
    mutex_lock(&fts_c56_life.ta_lock);
    if (fts_c56_stopping()) { ret = -ESHUTDOWN; goto out; }
    if (ts_data->touch_analysis_support && !ts_data->ta_buf) {
        ts_data->ta_buf = kzalloc(FTS_MAX_TOUCH_BUF, GFP_KERNEL);
        if (!ts_data->ta_buf) { ret = -ENOMEM; goto out; }
    }
    fts_c56_life.ta_open_count++;
out:
    mutex_unlock(&fts_c56_life.ta_lock);
    fts_c56_put();
    return ret;
}''')
            replace_fn('fts_ta_release', '''static int fts_ta_release(struct inode *inode, struct file *file)
{
    struct fts_ts_data *ts_data = pde_data(inode);
    int ret = 0;
    mutex_lock(&fts_c56_life.ta_lock);
    if (!fts_c56_life.ta_open_count) {
        FTS_ERROR("C56-H3 unbalanced TA release");
        ret = -EINVAL;
    } else if (!--fts_c56_life.ta_open_count) {
        WRITE_ONCE(ts_data->ta_flag, 0);
        wake_up_all(&ts_data->ts_waitqueue);
    }
    /* Device-owned buffer: keep it stable across IRQ/read/close; full removal
     * frees it after IRQ synchronization and proc_remove drains all users. */
    mutex_unlock(&fts_c56_life.ta_lock);
    return ret;
}''')
            # TA reads can otherwise wait forever while proc_remove drains active calls.
            a = src.index('wait_event_interruptible(ts_data->ts_waitqueue, !ts_data->ta_flag);')
            text = 'wait_event_interruptible(ts_data->ts_waitqueue, fts_c56_stopping() || !ts_data->ta_flag);\n        if (fts_c56_stopping()) return -ESHUTDOWN;\n#line ' + str(src.count('\n', 0, a) + 1) + ' "' + canonical + '"\n'
            changes.append((a, a + len('wait_event_interruptible(ts_data->ts_waitqueue, !ts_data->ta_flag);'), text))
        # Enforce nonoverlapping edits, including source-specific point insertions.
        changes.sort(key=lambda e: (e[0], e[1]))
        for x, y in zip(changes, changes[1:]):
            assert x[1] <= y[0], (name, x[:2], y[:2])
        for a, b, text in reversed(changes):
            src = src[:a] + text + src[b:]
        result[name] = src
    return original, result


def main():
    original, result = generate()
    OUT.mkdir(parents=True, exist_ok=True)
    receipt = {}
    for name, text in result.items():
        (OUT / name).write_text(text)
        receipt[name] = {'original_sha256': hashlib.sha256(original[name].encode()).hexdigest(),
                         'modified_sha256': hashlib.sha256(text.encode()).hexdigest(),
                         'changed': original[name] != text}
    (OUT.parents[4] / 'source-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('Private H3 source:', OUT)
    print('Changed files:', [n for n, r in receipt.items() if r['changed']])


if __name__ == '__main__':
    main()

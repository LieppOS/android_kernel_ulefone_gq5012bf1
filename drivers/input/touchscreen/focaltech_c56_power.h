/* SPDX-License-Identifier: GPL-2.0-only */
/* C56 FT3680 resource accounting; included at the old core power helpers. */
#ifndef FOCALTECH_C56_POWER_H
#define FOCALTECH_C56_POWER_H

static int fts_power_source_exit(struct fts_ts_data *ts);

static int fts_power_source_ctrl(struct fts_ts_data *ts, int enable)
{
	int ret = 0, err;

	if (enable) {
		if (fts_c56_stopping())
			return -ESHUTDOWN;
		if (!ts->power_disabled)
			return 0;
		if (IS_ERR_OR_NULL(ts->vdd))
			return -EINVAL;
		ret = gpio_direction_output(ts->pdata->reset_gpio, 0);
		if (ret)
			return ret;
		msleep(1);
		ret = regulator_enable(ts->vdd);
		if (ret)
			goto fail;
		fts_c56_life.vdd_enabled = true;
		if (!IS_ERR_OR_NULL(ts->vcc_i2c)) {
			ret = regulator_enable(ts->vcc_i2c);
			if (ret)
				goto fail;
			fts_c56_life.iovcc_enabled = true;
		}
		if (fts_c56_life.dvdd_requested) {
			ret = gpio_direction_output(ts->pdata->dvdd_gpio, 1);
			if (ret)
				goto fail;
			fts_c56_life.dvdd_high = true;
		}
		ts->power_disabled = false;
		return 0;
	fail:
		FTS_ERROR("C56-H3 enable failed:%d", ret);
		err = fts_power_source_ctrl(ts, 0);
		if (err)
			FTS_ERROR("C56-H3 enable rollback failed:%d", err);
		return ret;
	}

	/* Continue safe cleanup after an error, but preserve the FIRST error. */
	err = gpio_direction_output(ts->pdata->reset_gpio, 0);
	if (err)
		ret = err;
	msleep(1);
	if (fts_c56_life.vdd_enabled) {
		err = regulator_disable(ts->vdd);
		if (!err)
			fts_c56_life.vdd_enabled = false;
		else {
			FTS_ERROR("C56-H3 VDD disable:%d", err);
			if (!ret)
				ret = err;
		}
	}
	if (fts_c56_life.iovcc_enabled) {
		err = regulator_disable(ts->vcc_i2c);
		if (!err)
			fts_c56_life.iovcc_enabled = false;
		else {
			FTS_ERROR("C56-H3 IOVCC disable:%d", err);
			if (!ret)
				ret = err;
		}
	}
	if (fts_c56_life.dvdd_requested) {
		err = gpio_direction_output(ts->pdata->dvdd_gpio, 0);
		if (!err)
			fts_c56_life.dvdd_high = false;
		else {
			FTS_ERROR("C56-H3 VDDI low:%d", err);
			if (!ret)
				ret = err;
		}
	}
	ts->power_disabled =
		!(fts_c56_life.vdd_enabled || fts_c56_life.iovcc_enabled ||
		  fts_c56_life.dvdd_high);
	return ret;
}

static int fts_power_source_init(struct fts_ts_data *ts)
{
	int ret, err;

	ts->power_disabled = true;
	ts->vdd = regulator_get(ts->dev, "vdd");
	if (IS_ERR_OR_NULL(ts->vdd)) {
		ret = ts->vdd ? PTR_ERR(ts->vdd) : -ENODEV;
		ts->vdd = NULL;
		return ret;
	}
	if (regulator_count_voltages(ts->vdd) > 0) {
		ret = regulator_set_voltage(ts->vdd, FTS_VTG_MIN_UV,
					    FTS_VTG_MAX_UV);
		if (ret)
			goto fail;
	}
	ts->vcc_i2c = regulator_get(ts->dev, "vcc_i2c");
	/* Keep the existing optional-supply contract; defer must not be swallowed. */
	if (IS_ERR(ts->vcc_i2c)) {
		ret = PTR_ERR(ts->vcc_i2c);
		ts->vcc_i2c = NULL;
		if (ret != -ENODEV && ret != -ENOENT)
			goto fail;
	} else if (ts->vcc_i2c && regulator_count_voltages(ts->vcc_i2c) > 0) {
		ret = regulator_set_voltage(ts->vcc_i2c, FTS_I2C_VTG_MIN_UV,
					    FTS_I2C_VTG_MAX_UV);
		if (ret)
			goto fail;
	}
	if (gpio_is_valid(ts->pdata->dvdd_gpio)) {
		ret = gpio_request(ts->pdata->dvdd_gpio, "fts_dvdd_gpio");
		if (ret)
			goto fail;
		fts_c56_life.dvdd_requested = true;
	}
#if FTS_PINCTRL_EN
	ret = fts_pinctrl_init(ts);
	if (ret)
		goto fail;
	ret = fts_pinctrl_select_normal(ts);
	if (ret)
		goto fail;
#endif
	ret = fts_power_source_ctrl(ts, 1);
	if (!ret)
		return 0;
fail:
	FTS_ERROR("C56-H3 power init:%d", ret);
	err = fts_power_source_exit(ts);
	if (err)
		FTS_ERROR("C56-H3 power-init unwind:%d", err);
	return ret;
}

static int fts_power_source_exit(struct fts_ts_data *ts)
{
	int ret = 0, err;

#if FTS_PINCTRL_EN
	fts_pinctrl_select_release(ts);
#endif
	if (fts_c56_life.vdd_enabled || fts_c56_life.iovcc_enabled ||
	    fts_c56_life.dvdd_requested)
		ret = fts_power_source_ctrl(ts, 0);
	if (!IS_ERR_OR_NULL(ts->vdd) && !fts_c56_life.vdd_enabled) {
		if (regulator_count_voltages(ts->vdd) > 0) {
			err = regulator_set_voltage(ts->vdd, 0, FTS_VTG_MAX_UV);
			if (err && !ret)
				ret = err;
		}
		regulator_put(ts->vdd);
		ts->vdd = NULL;
	}
	if (!IS_ERR_OR_NULL(ts->vcc_i2c) && !fts_c56_life.iovcc_enabled) {
		if (regulator_count_voltages(ts->vcc_i2c) > 0) {
			err = regulator_set_voltage(ts->vcc_i2c, 0,
						    FTS_I2C_VTG_MAX_UV);
			if (err && !ret)
				ret = err;
		}
		regulator_put(ts->vcc_i2c);
		ts->vcc_i2c = NULL;
	}
	/* Never put an enabled supply or pretend a failed disable succeeded. */
	return ret;
}
#endif

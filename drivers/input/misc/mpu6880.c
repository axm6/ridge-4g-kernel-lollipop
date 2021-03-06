/*
 * MPU6880 6-axis gyroscope + accelerometer driver
 *
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/of_gpio.h>
#include <mach/gpiomux.h>
#include <linux/sensors.h>
#include "linux/input/mpu6880.h"
#include <linux/miscdevice.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/uaccess.h>
#define DEBUG_NODE

/*VDD 2.375V-3.46V VLOGIC 1.8V +-5%*/
#define MPU6880_VDD_MIN_UV		1710000
#define MPU6880_VDD_MAX_UV	3600000
#define MPU6880_VLOGIC_MIN_UV	1800000
#define MPU6880_VLOGIC_MAX_UV	1800000
#define MPU6880_VIO_MIN_UV	1710000
#define MPU6880_VIO_MAX_UV	3600000

#define MPU6880_ACCEL_MIN_VALUE	-32768
#define MPU6880_ACCEL_MAX_VALUE	32767
#define MPU6880_GYRO_MIN_VALUE	-32768
#define MPU6880_GYRO_MAX_VALUE	32767

//LINE<JIRA_ID><DATE20141201><modify delay time>zenghaihui
#define MPU6880_ACCEL_MIN_POLL_INTERVAL_MS	5
#define MPU6880_ACCEL_MAX_POLL_INTERVAL_MS	5000
#define MPU6880_ACCEL_DEFAULT_POLL_INTERVAL_MS	200

//LINE<JIRA_ID><DATE20141201><modify delay time>zenghaihui
#define MPU6880_GYRO_MIN_POLL_INTERVAL_MS	5
#define MPU6880_GYRO_MAX_POLL_INTERVAL_MS	5000
#define MPU6880_GYRO_DEFAULT_POLL_INTERVAL_MS	200

#define MPU6880_RAW_ACCEL_DATA_LEN	6
#define MPU6880_RAW_GYRO_DATA_LEN	6

/* Sensitivity Scale Factor */
#define MPU6880_ACCEL_SCALE_SHIFT_2G	4
#define MPU6880_GYRO_SCALE_SHIFT_FS0	3

#define MPU6880_DEV_NAME_ACCEL	"accelerometer"
#define MPU6880_DEV_NAME_GYRO		"gyroscope"
#define GYRO_RAW_DATA_FOR_CALI	_IOW('c', 3, int *)
#define GS_RAW_DATA_FOR_CALI	_IOW('c', 2, int *)
#define MPU6880_AXIS_X          0
#define MPU6880_AXIS_Y          1
#define MPU6880_AXIS_Z          2

#define MPU6880_AXES_NUM        3
/*#define MPU6880_FULL_RES			0x08*/
#define MPU6880_RANGE_2G			(0x00 << 3)
#define MPU6880_RANGE_4G			(0x01 << 3)
#define MPU6880_RANGE_8G			(0x02 << 3)
#define MPU6880_RANGE_16G			(0x03 << 3)
/*#define MPU6880_SELF_TEST         0x80*/
#define MPU6880_REG_DATA_FORMAT		0x1C

#define MPU6880_RANGE_PN250dps			(0x00 << 3)
#define MPU6880_RANGE_PN500dps			(0x01 << 3)
#define MPU6880_RANGE_PN1000dps			(0x02 << 3)
#define MPU6880_RANGE_PN2000dps			(0x03 << 3)
#define MPU6880_REG_Gyro_DATA_FORMAT		0x1B

/* Gyro Offset Max Value (dps) */
#define DEF_GYRO_OFFSET_MAX             120
#define DEF_ST_PRECISION                1000
#define DEF_SELFTEST_GYRO_SENS_250          (32768 / 250)
#define DEF_SELFTEST_GYRO_SENS_500          (32768 / 500)
#define DEF_SELFTEST_GYRO_SENS_1000          (32768 / 1000)
#define DEF_SELFTEST_GYRO_SENS_2000          (32768 / 2000)

enum mpu6880_place {
	MPU6880_PLACE_PU = 0,
	MPU6880_PLACE_PR = 1,
	MPU6880_PLACE_LD = 2,
	MPU6880_PLACE_LL = 3,
	MPU6880_PLACE_PU_BACK = 4,
	MPU6880_PLACE_PR_BACK = 5,
	MPU6880_PLACE_LD_BACK = 6,
	MPU6880_PLACE_LL_BACK = 7,
	MPU6880_PLACE_UNKNOWN = 8,
	MPU6880_AXIS_REMAP_TAB_SZ = 8
};

struct mpu6880_place_name {
	char name[32];
	enum mpu6880_place place;
};

struct cali_data {
		int x;
		int y;
		int z;
};

struct axis_data {
	s16 x;
	s16 y;
	s16 z;
	s16 rx;
	s16 ry;
	s16 rz;
};

/**
 *  struct mpu6880_sensor - Cached chip configuration data.
 *  @client:		I2C client.
 *  @dev:		device structure.
 *  @accel_dev:		accelerometer input device structure.
 *  @gyro_dev:		gyroscope input device structure.
 *  @accel_cdev:		sensor class device structure for accelerometer.
 *  @gyro_cdev:		sensor class device structure for gyroscope.
 *  @pdata:	device platform dependent data.
 *  @accel_poll_work:	accelerometer delay work structure
 *  @gyro_poll_work:	gyroscope delay work structure.
 *  @vlogic:	regulator data for Vlogic and I2C bus pullup.
 *  @vdd:		regulator data for Vdd.
 *  @reg:		notable slave registers.
 *  @cfg:		cached chip configuration data.
 *  @axis:	axis data reading.
 *  @gyro_poll_ms:		gyroscope polling delay.
 *  @accel_poll_ms:	accelerometer polling delay.
 *  @enable_gpio:	enable GPIO.
 *  @use_poll:		use interrupt mode instead of polling data.
 */
struct mpu6880_sensor {
	struct i2c_client *client;
	struct device *dev;
	struct input_dev *accel_dev;
	struct input_dev *gyro_dev;
	struct sensors_classdev accel_cdev;
	struct sensors_classdev gyro_cdev;
	struct mpu6880_platform_data *pdata;
	struct mutex op_lock;
	enum inv_devices chip_type;
	struct delayed_work accel_poll_work;
	struct delayed_work gyro_poll_work;
	struct regulator *vlogic;
	struct regulator *vdd;
	struct regulator *vio;
	struct mpu_reg_map reg;
	struct mpu_chip_config cfg;
	struct axis_data axis;
	u32 gyro_poll_ms;
	u32 accel_poll_ms;
	int enable_gpio;
	bool use_poll;
	bool power_enabled;
};
struct mpu6880_sensor *mpu_info;
/* Accelerometer information read by HAL */
static struct sensors_classdev mpu6880_acc_cdev = {
	.name = "MPU6880-accel",
	.vendor = "Invensense",
	.version = 1,
	.handle = SENSORS_ACCELERATION_HANDLE,
	.type = SENSOR_TYPE_ACCELEROMETER,
	.max_range = "156.8",	/* m/s^2 */
	.resolution = "0.000598144",	/* m/s^2 */
	.sensor_power = "0.5",	/* 0.5 mA */
	.min_delay = MPU6880_ACCEL_MIN_POLL_INTERVAL_MS * 1000,
	.delay_msec = MPU6880_ACCEL_DEFAULT_POLL_INTERVAL_MS,
	.fifo_reserved_event_count = 0,
	.fifo_max_event_count = 0,
	.enabled = 0,
	.sensors_enable = NULL,
	.sensors_poll_delay = NULL,
};

/* gyroscope information read by HAL */
static struct sensors_classdev mpu6880_gyro_cdev = {
	.name = "MPU6880-gyro",
	.vendor = "Invensense",
	.version = 1,
	.handle = SENSORS_GYROSCOPE_HANDLE,
	.type = SENSOR_TYPE_GYROSCOPE,
	.max_range = "34.906586",	/* rad/s */
	.resolution = "0.0010681152",	/* rad/s */
	.sensor_power = "3.6",	/* 3.6 mA */
	.min_delay = MPU6880_GYRO_MIN_POLL_INTERVAL_MS * 1000,
	.delay_msec = MPU6880_ACCEL_DEFAULT_POLL_INTERVAL_MS,
	.fifo_reserved_event_count = 0,
	.fifo_max_event_count = 0,
	.enabled = 0,
	.sensors_enable = NULL,
	.sensors_poll_delay = NULL,
};

static char selftestRes[8] = {0};

struct sensor_axis_remap {
	/* src means which source will be mapped to target x, y, z axis */
	/* if an target OS axis is remapped from (-)x,
	 * src is 0, sign_* is (-)1 */
	/* if an target OS axis is remapped from (-)y,
	 * src is 1, sign_* is (-)1 */
	/* if an target OS axis is remapped from (-)z,
	 * src is 2, sign_* is (-)1 */
	int src_x:3;
	int src_y:3;
	int src_z:3;

	int sign_x:2;
	int sign_y:2;
	int sign_z:2;
};

static const struct sensor_axis_remap
mpu6880_accel_axis_remap_tab[MPU6880_AXIS_REMAP_TAB_SZ] = {
	/* src_x src_y src_z  sign_x  sign_y  sign_z */
	{  0,    1,    2,     1,      1,      1 }, /* P0 */
	{  1,    0,    2,     1,     -1,      1 }, /* P1 */
	{  0,    1,    2,    -1,     -1,      1 }, /* P2 */
	{  1,    0,    2,    -1,      1,      1 }, /* P3 */

	{  0,    1,    2,    -1,      1,     -1 }, /* P4 */
	{  1,    0,    2,    -1,     -1,     -1 }, /* P5 */
	{  0,    1,    2,     1,     -1,     -1 }, /* P6 */
	{  1,    0,    2,     1,      1,     -1 }, /* P7 */
};

static const struct sensor_axis_remap
mpu6880_gyro_axis_remap_tab[MPU6880_AXIS_REMAP_TAB_SZ] = {
	/* src_x src_y src_z  sign_x  sign_y  sign_z */
	{  0,    1,    2,    -1,      1,     -1 }, /* P0 */
	{  1,    0,    2,    -1,     -1,     -1 }, /* P1*/
	{  0,    1,    2,     1,     -1,     -1 }, /* P2 */
	{  1,    0,    2,     1,      1,     -1 }, /* P3 */

	{  0,    1,    2,     1,      1,      1 }, /* P4 */
	{  1,    0,    2,     1,     -1,      1 }, /* P5 */
	{  0,    1,    2,    -1,     -1,      1 }, /* P6 */
	{  1,    0,    2,    -1,      1,      1 }, /* P7 */
};

static const struct mpu6880_place_name
mpu6880_place_name2num[MPU6880_AXIS_REMAP_TAB_SZ] = {
	{"Portrait Up", MPU6880_PLACE_PU},
	{"Landscape Right", MPU6880_PLACE_PR},
	{"Portrait Down", MPU6880_PLACE_LD},
	{"Landscape Left", MPU6880_PLACE_LL},
	{"Portrait Up Back Side", MPU6880_PLACE_PU_BACK},
	{"Landscape Right Back Side", MPU6880_PLACE_PR_BACK},
	{"Portrait Down Back Side", MPU6880_PLACE_LD_BACK},
	{"Landscape Left Back Side", MPU6880_PLACE_LL_BACK},
};

static void setup_mpu6880_reg(struct mpu_reg_map *reg);
static int mpu6880_init_config(struct mpu6880_sensor *sensor);

static int mpu6880_power_ctl(struct mpu6880_sensor *sensor, bool on)
{
	int rc = 0;

	if (on && (!sensor->power_enabled)) {
		rc = regulator_enable(sensor->vdd);
		if (rc) {
			dev_err(&sensor->client->dev,
				"Regulator vdd enable failed rc=%d\n", rc);
			return rc;
		}

		rc = regulator_enable(sensor->vio);
		if (rc) {
			dev_err(&sensor->client->dev,
				"Regulator vio enable failed rc=%d\n", rc);
			regulator_disable(sensor->vdd);
			return rc;
		}

		if (gpio_is_valid(sensor->enable_gpio)) {
			udelay(POWER_EN_DELAY_US);
			gpio_set_value(sensor->enable_gpio, 1);
		}
		msleep(POWER_UP_TIME_MS);

		sensor->power_enabled = true;
	} else if (!on && (sensor->power_enabled)) {
		if (gpio_is_valid(sensor->enable_gpio)) {
			udelay(POWER_EN_DELAY_US);
			gpio_set_value(sensor->enable_gpio, 0);
			udelay(POWER_EN_DELAY_US);
		}


		rc = regulator_disable(sensor->vdd);
		if (rc) {
			dev_err(&sensor->client->dev,
				"Regulator vdd disable failed rc=%d\n", rc);
			return rc;
		}

		rc = regulator_disable(sensor->vio);
		if (rc) {
			dev_err(&sensor->client->dev,
				"Regulator vio disable failed rc=%d\n", rc);
			if (regulator_enable(sensor->vdd) ||
					regulator_enable(sensor->vio))
				return -EIO;
		}

		sensor->power_enabled = false;
	} else {
		dev_warn(&sensor->client->dev,
				"Ignore power status change from %d to %d\n",
				on, sensor->power_enabled);
	}
	return rc;
}

static int mpu6880_power_init(struct mpu6880_sensor *sensor)
{
	int ret = 0;

	sensor->vdd = regulator_get(&sensor->client->dev, "vdd");
	if (IS_ERR(sensor->vdd)) {
		ret = PTR_ERR(sensor->vdd);
		dev_err(&sensor->client->dev,
			"Regulator get failed vdd ret=%d\n", ret);
		return ret;
	}

	if (regulator_count_voltages(sensor->vdd) > 0) {
		ret = regulator_set_voltage(sensor->vdd, MPU6880_VDD_MIN_UV,
					   MPU6880_VDD_MAX_UV);
		if (ret) {
			dev_err(&sensor->client->dev,
				"Regulator set_vtg failed vdd ret=%d\n", ret);
			goto reg_vdd_put;
		}
	}

	sensor->vio = regulator_get(&sensor->client->dev, "vio");
	if (IS_ERR(sensor->vio)) {
		ret = PTR_ERR(sensor->vio);
		dev_err(&sensor->client->dev,
			"Regulator get failed vio ret=%d\n", ret);
		goto reg_vdd_set_vtg;
	}

	if (regulator_count_voltages(sensor->vio) > 0) {
		ret = regulator_set_voltage(sensor->vio,
				MPU6880_VIO_MIN_UV,
				MPU6880_VIO_MAX_UV);
		if (ret) {
			dev_err(&sensor->client->dev,
			"Regulator set_vtg failed vio ret=%d\n", ret);
			goto reg_vio_put;
		}
	}

	return 0;

reg_vio_put:
	regulator_put(sensor->vio);
reg_vdd_set_vtg:
	if (regulator_count_voltages(sensor->vdd) > 0)
		regulator_set_voltage(sensor->vdd, 0, MPU6880_VDD_MAX_UV);
reg_vdd_put:
	regulator_put(sensor->vdd);
	return ret;
}

static int mpu6880_power_deinit(struct mpu6880_sensor *sensor)
{
	int ret = 0;
	if (regulator_count_voltages(sensor->vdd) > 0)
		regulator_set_voltage(sensor->vdd, 0, MPU6880_VDD_MAX_UV);
	regulator_put(sensor->vdd);
	return ret;
}

/**
 * mpu6880_read_reg - read multiple register data
 * @start_addr: register address read from
 * @buffer: provide register addr and get register
 * @length: length of register
 *
 * Reads the register values in one transaction or returns a negative
 * error code on failure.
 */
static int mpu6880_read_reg(struct i2c_client *client, u8 start_addr,
			       u8 *buffer, int length)
{
	/*
	 * Annoying we can't make this const because the i2c layer doesn't
	 * declare input buffers const.
	 */
	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 1,
			.buf = &start_addr,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = length,
			.buf = buffer,
		},
	};

	return i2c_transfer(client->adapter, msg, 2);
}

/* I2C Write */
static int8_t I2C_Write(uint8_t *txData, uint8_t length)
{
	int8_t index;

	struct mpu6880_sensor *self_info = mpu_info;
	struct i2c_msg data[] = {
		{
			.addr = self_info->client->addr,
			.flags = 0,
			.len = length,
			.buf = txData,
		},
	};

	for (index = 0; index < 5; index++) {
		if (i2c_transfer(self_info->client->adapter, data, 1) > 0)
			break;
		usleep(10000);
	}

	if (index >= 5) {
		pr_alert("%s I2C Write Fail !!!!\n", __func__);
		return -EIO;
	}

	return 0;
}

static int mpu6880_write_reg(struct i2c_client *client, u8 start_addr,
			       u8 data, int length)
{
	int ret = 0;
	u8 buf[2];

	buf[0] = start_addr;
	buf[1] = data;

	ret = I2C_Write(buf, 2);
	if (ret < 0) {
		dev_err(&client->dev, "%s | 0x%02X", __func__, buf[0]);
		return -EIO;
	}

	return 0;
}

/**
 * mpu6880_read_accel_data - get accelerometer data from device
 * @sensor: sensor device instance
 * @data: axis data to update
 *
 * Return the converted X Y and Z data from the sensor device
 */
static void mpu6880_read_accel_data(struct mpu6880_sensor *sensor,
			     struct axis_data *data)
{
	u16 buffer[3];

	mpu6880_read_reg(sensor->client, sensor->reg.raw_accel,
		(u8 *)buffer, MPU6880_RAW_ACCEL_DATA_LEN);
	data->x = be16_to_cpu(buffer[0]);
	data->y = be16_to_cpu(buffer[1]);
	data->z = be16_to_cpu(buffer[2]);
}

/**
 * mpu6880_read_gyro_data - get gyro data from device
 * @sensor: sensor device instance
 * @data: axis data to update
 *
 * Return the converted RX RY and RZ data from the sensor device
 */
static void mpu6880_read_gyro_data(struct mpu6880_sensor *sensor,
			     struct axis_data *data)
{
	u16 buffer[3];

	mpu6880_read_reg(sensor->client, sensor->reg.raw_gyro,
		(u8 *)buffer, MPU6880_RAW_GYRO_DATA_LEN);
	data->rx = be16_to_cpu(buffer[0]);
	data->ry = be16_to_cpu(buffer[1]);
	data->rz = be16_to_cpu(buffer[2]);
}

/**
 * mpu6880_remap_accel_data - remap accelerometer raw data to axis data
 * @data: data needs remap
 * @place: sensor position
 */
static void mpu6880_remap_accel_data(struct axis_data *data, int place)
{
	const struct sensor_axis_remap *remap;
	s16 tmp[3];
	/* sensor with place 0 needs not to be remapped */
        //LINE<JIRA_ID><DATE20141202><mpu6880 axis remap>zenghaihui
	//if ((place <= 0) || (place >= MPU6880_AXIS_REMAP_TAB_SZ))
	if ((place < 0) || (place >= MPU6880_AXIS_REMAP_TAB_SZ))
		return;

	remap = &mpu6880_accel_axis_remap_tab[place];

	tmp[0] = data->x;
	tmp[1] = data->y;
	tmp[2] = data->z;
	data->x = tmp[remap->src_x] * remap->sign_x;
	data->y = tmp[remap->src_y] * remap->sign_y;
	data->z = tmp[remap->src_z] * remap->sign_z;
}

/**
 * mpu6880_remap_gyro_data - remap gyroscope raw data to axis data
 * @data: data needs remap
 * @place: sensor position
 */
static void mpu6880_remap_gyro_data(struct axis_data *data, int place)
{
	const struct sensor_axis_remap *remap;
	s16 tmp[3];
	/* sensor with place 0 needs not to be remapped */
        //LINE<JIRA_ID><DATE20141202><mpu6880 axis remap>zenghaihui
	//if ((place <= 0) || (place >= MPU6880_AXIS_REMAP_TAB_SZ))
	if ((place < 0) || (place >= MPU6880_AXIS_REMAP_TAB_SZ))
		return;

	remap = &mpu6880_gyro_axis_remap_tab[place];
	tmp[0] = data->rx;
	tmp[1] = data->ry;
	tmp[2] = data->rz;
	data->rx = tmp[remap->src_x] * remap->sign_x;
	data->ry = tmp[remap->src_y] * remap->sign_y;
	data->rz = tmp[remap->src_z] * remap->sign_z;
}

/**
 * mpu6880_interrupt_thread - handle an IRQ
 * @irq: interrupt numner
 * @data: the sensor
 *
 * Called by the kernel single threaded after an interrupt occurs. Read
 * the sensor data and generate an input event for it.
 */
static irqreturn_t mpu6880_interrupt_thread(int irq, void *data)
{
	struct mpu6880_sensor *sensor = data;

	mpu6880_read_accel_data(sensor, &sensor->axis);
	mpu6880_read_gyro_data(sensor, &sensor->axis);

	input_report_abs(sensor->accel_dev, ABS_X, sensor->axis.x);
	input_report_abs(sensor->accel_dev, ABS_Y, sensor->axis.y);
	input_report_abs(sensor->accel_dev, ABS_Z, sensor->axis.z);
	input_sync(sensor->accel_dev);

	input_report_abs(sensor->gyro_dev, ABS_RX, sensor->axis.rx);
	input_report_abs(sensor->gyro_dev, ABS_RY, sensor->axis.ry);
	input_report_abs(sensor->gyro_dev, ABS_RZ, sensor->axis.rz);
	input_sync(sensor->gyro_dev);

	return IRQ_HANDLED;
}

/**
 * mpu6880_accel_work_fn - polling accelerometer data
 * @work: the work struct
 *
 * Called by the work queue; read sensor data and generate an input
 * event
 */
static void mpu6880_accel_work_fn(struct work_struct *work)
{
	struct mpu6880_sensor *sensor;

	sensor = container_of((struct delayed_work *)work,
				struct mpu6880_sensor, accel_poll_work);

	mpu6880_read_accel_data(sensor, &sensor->axis);
	mpu6880_remap_accel_data(&sensor->axis, sensor->pdata->place);

//LINE<JIRA_ID><DATE20150611><gsensor for android L>zenghaihui
#if 0
	input_report_abs(sensor->accel_dev, ABS_X,
		(sensor->axis.x >> MPU6880_ACCEL_SCALE_SHIFT_2G));
	input_report_abs(sensor->accel_dev, ABS_Y,
		(sensor->axis.y >> MPU6880_ACCEL_SCALE_SHIFT_2G));
	input_report_abs(sensor->accel_dev, ABS_Z,
		(sensor->axis.z >> MPU6880_ACCEL_SCALE_SHIFT_2G));
#else
	input_report_abs(sensor->accel_dev, ABS_X,
		(sensor->axis.x ));
	input_report_abs(sensor->accel_dev, ABS_Y,
		(sensor->axis.y ));
	input_report_abs(sensor->accel_dev, ABS_Z,
		(sensor->axis.z ));
#endif

	input_sync(sensor->accel_dev);

	if (sensor->use_poll)
		schedule_delayed_work(&sensor->accel_poll_work,
			msecs_to_jiffies(sensor->accel_poll_ms));
}

/**
 * mpu6880_gyro_work_fn - polling gyro data
 * @work: the work struct
 *
 * Called by the work queue; read sensor data and generate an input
 * event
 */
static void mpu6880_gyro_work_fn(struct work_struct *work)
{
	struct mpu6880_sensor *sensor;

	sensor = container_of((struct delayed_work *)work,
				struct mpu6880_sensor, gyro_poll_work);

	mpu6880_read_gyro_data(sensor, &sensor->axis);
	mpu6880_remap_gyro_data(&sensor->axis, sensor->pdata->place);

	input_report_abs(sensor->gyro_dev, ABS_RX,
		(sensor->axis.rx));
	input_report_abs(sensor->gyro_dev, ABS_RY,
		(sensor->axis.ry));
	input_report_abs(sensor->gyro_dev, ABS_RZ,
		(sensor->axis.rz));
	input_sync(sensor->gyro_dev);

	if (sensor->use_poll)
		schedule_delayed_work(&sensor->gyro_poll_work,
			msecs_to_jiffies(sensor->gyro_poll_ms));
}

/**
 *  mpu6880_set_lpa_freq() - set low power wakeup frequency.
 */
static int mpu6880_set_lpa_freq(struct mpu6880_sensor *sensor, int lpa_freq)
{
	int ret;
	u8 data;

	/* only for MPU6880 with fixed rate, need expend */
	if (INV_MPU6880 == sensor->chip_type) {
		ret = i2c_smbus_read_byte_data(sensor->client,
				sensor->reg.pwr_mgmt_2);
		if (ret < 0)
			return ret;

		data = (u8)ret;
		data &= ~BIT_LPA_FREQ_MASK;
		data |= MPU6880_LPA_5HZ;
		ret = i2c_smbus_write_byte_data(sensor->client,
				sensor->reg.pwr_mgmt_2, data);
		if (ret < 0)
			return ret;
	}
	sensor->cfg.lpa_freq = lpa_freq;

	return 0;
}

static int mpu6880_switch_engine(struct mpu6880_sensor *sensor,
				bool en, u32 mask)
{
	struct mpu_reg_map *reg;
	u8 data, mgmt_1;
	int ret;

	reg = &sensor->reg;
	/*
	 * switch clock needs to be careful. Only when gyro is on, can
	 * clock source be switched to gyro. Otherwise, it must be set to
	 * internal clock
	 */
	mgmt_1 = MPU_CLK_INTERNAL;
	if (BIT_PWR_GYRO_STBY_MASK == mask) {
		ret = i2c_smbus_read_byte_data(sensor->client,
			reg->pwr_mgmt_1);
		if (ret < 0)
			goto error;
		mgmt_1 = (u8)ret;
		mgmt_1 &= ~BIT_CLK_MASK;
	}

	if ((BIT_PWR_GYRO_STBY_MASK == mask) && (!en)) {
		/*
		 * turning off gyro requires switch to internal clock first.
		 * Then turn off gyro engine
		 */
		mgmt_1 |= MPU_CLK_INTERNAL;
		ret = i2c_smbus_write_byte_data(sensor->client,
			reg->pwr_mgmt_1, mgmt_1);
		if (ret < 0)
			goto error;
	}

	ret = i2c_smbus_read_byte_data(sensor->client,
			reg->pwr_mgmt_2);
	if (ret < 0)
		goto error;
	data = (u8)ret;
	if (en)
		data &= (~mask);
	else
		data |= mask;
	ret = i2c_smbus_write_byte_data(sensor->client,
			reg->pwr_mgmt_2, data);
	if (ret < 0)
		goto error;

	if ((BIT_PWR_GYRO_STBY_MASK == mask) && en) {
		/* wait gyro stable */
		msleep(SENSOR_UP_TIME_MS);
		/* after gyro is on & stable, switch internal clock to PLL */
		mgmt_1 |= MPU_CLK_PLL_X;
		ret = i2c_smbus_write_byte_data(sensor->client,
				reg->pwr_mgmt_1, mgmt_1);
		if (ret < 0)
			goto error;
	}

	return 0;

error:
	dev_err(&sensor->client->dev, "Fail to switch MPU engine\n");
	return ret;
}

static int mpu6880_init_engine(struct mpu6880_sensor *sensor)
{
	int ret;

	ret = mpu6880_switch_engine(sensor, false, BIT_PWR_GYRO_STBY_MASK);
	if (ret)
		return ret;

	ret = mpu6880_switch_engine(sensor, false, BIT_PWR_ACCEL_STBY_MASK);
	if (ret)
		return ret;

	return 0;
}

/**
 * mpu6880_set_power_mode - set the power mode
 * @sensor: sensor data structure
 * @power_on: value to switch on/off of power, 1: normal power,
 *    0: low power
 *
 * Put device to normal-power mode or low-power mode.
 */
static int mpu6880_set_power_mode(struct mpu6880_sensor *sensor,
					bool power_on)
{
	struct i2c_client *client = sensor->client;
	s32 ret;
	u8 val;

	ret = i2c_smbus_read_byte_data(client, sensor->reg.pwr_mgmt_1);
	if (ret < 0) {
		dev_err(&client->dev,
				"Fail to read power mode, ret=%d\n", ret);
		return ret;
	}

	if (power_on)
		val = (u8)ret & ~BIT_SLEEP;
	else
		val = (u8)ret | BIT_SLEEP;
	ret = i2c_smbus_write_byte_data(client, sensor->reg.pwr_mgmt_1, val);
	if (ret < 0) {
		dev_err(&client->dev,
				"Fail to write power mode, ret=%d\n", ret);
	}
		return ret;

	return 0;
}

static int mpu6880_gyro_enable(struct mpu6880_sensor *sensor, bool on)
{
	int ret;
	u8 data;

	if (sensor->cfg.is_asleep) {
		dev_err(&sensor->client->dev,
			"Fail to set gyro state, device is asleep.\n");
		return -EINVAL;
	}

	ret = i2c_smbus_read_byte_data(sensor->client,
				sensor->reg.pwr_mgmt_1);

	if (ret < 0) {
		dev_err(&sensor->client->dev,
			"Fail to get sensor power state ret=%d\n", ret);
		return ret;
	}

	data = (u8)ret;
	if (on) {
		ret = mpu6880_switch_engine(sensor, true,
			BIT_PWR_GYRO_STBY_MASK);

		if (ret)
			return ret;
		sensor->cfg.gyro_enable = 1;

		data &= ~BIT_SLEEP;
		ret = i2c_smbus_write_byte_data(sensor->client,
				sensor->reg.pwr_mgmt_1, data);

		if (ret < 0) {
			dev_err(&sensor->client->dev,
				"Fail to set sensor power state ret=%d\n", ret);
			return ret;
		}

		sensor->cfg.enable = 1;
	} else {
		ret = mpu6880_switch_engine(sensor, false,
			BIT_PWR_GYRO_STBY_MASK);
		if (ret)
			return ret;
		sensor->cfg.gyro_enable = 0;
		if (!sensor->cfg.accel_enable) {
			data |=  BIT_SLEEP;
			ret = i2c_smbus_write_byte_data(sensor->client,
					sensor->reg.pwr_mgmt_1, data);
			if (ret < 0) {
				dev_err(&sensor->client->dev,
					"Fail to set sensor power state ret=%d\n",
					ret);
				return ret;
			}
			sensor->cfg.enable = 0;
		}
	}
	return 0;
}

/**
 * mpu6880_restore_context - update the sensor register context
 */

static int mpu6880_restore_context(struct mpu6880_sensor *sensor)
{
	struct mpu_reg_map *reg;
	struct i2c_client *client;
	int ret;
	u8 data;

	client = sensor->client;
	reg = &sensor->reg;

	ret = i2c_smbus_write_byte_data(client, reg->gyro_config,
			sensor->cfg.fsr << GYRO_CONFIG_FSR_SHIFT);
	if (ret < 0) {
		dev_err(&client->dev, "update fsr failed.\n");
		goto exit;
	}

	ret = i2c_smbus_write_byte_data(client, reg->lpf, sensor->cfg.lpf);
	if (ret < 0) {
		dev_err(&client->dev, "update lpf failed.\n");
		goto exit;
	}

	ret = i2c_smbus_write_byte_data(client, reg->accel_config,
			(ACCEL_FS_02G << ACCL_CONFIG_FSR_SHIFT));
	if (ret < 0) {
		dev_err(&client->dev, "update accel_fs failed.\n");
		goto exit;
	}

	ret = i2c_smbus_read_byte_data(client, reg->fifo_en);
	if (ret < 0) {
		dev_err(&client->dev, "read fifo_en failed.\n");
		goto exit;
	}

	data = (u8)ret;

	if (sensor->cfg.accel_fifo_enable) {
		ret = i2c_smbus_write_byte_data(client, reg->fifo_en,
				data |= BIT_ACCEL_FIFO);
		if (ret < 0) {
			dev_err(&client->dev, "write accel_fifo_enabled failed.\n");
			goto exit;
		}
	}

	if (sensor->cfg.gyro_fifo_enable) {
		ret = i2c_smbus_write_byte_data(client, reg->fifo_en,
				data |= BIT_GYRO_FIFO);
		if (ret < 0) {
			dev_err(&client->dev, "write accel_fifo_enabled failed.\n");
			goto exit;
		}
	}

	ret = mpu6880_set_lpa_freq(sensor, sensor->cfg.lpa_freq);
	if (ret < 0) {
		dev_err(&client->dev, "set lpa_freq failed.\n");
		goto exit;
	}

	ret = i2c_smbus_write_byte_data(client, reg->sample_rate_div,
			ODR_DLPF_ENA / INIT_FIFO_RATE - 1);
	if (ret < 0) {
		dev_err(&client->dev, "set lpa_freq failed.\n");
		goto exit;
	}

	dev_dbg(&client->dev, "restore context finished\n");

exit:
	return ret;
}

/**
 * mpu6880_reset_chip - reset chip to default state
 */
static void mpu6880_reset_chip(struct mpu6880_sensor *sensor)
{
	struct i2c_client *client;
	int ret, i;

	client = sensor->client;

	ret = i2c_smbus_write_byte_data(client, sensor->reg.pwr_mgmt_1,
			BIT_RESET_ALL);
	if (ret < 0) {
		dev_err(&client->dev, "Reset chip fail!\n");
		goto exit;
	}

	for (i = 0; i < MPU6880_RESET_RETRY_CNT; i++) {
		ret = i2c_smbus_read_byte_data(sensor->client,
					sensor->reg.pwr_mgmt_1);
		if (ret < 0) {
			dev_err(&sensor->client->dev,
				"Fail to get reset state ret=%d\n", ret);
			goto exit;
		}

		if ((ret & BIT_H_RESET) == 0) {
			dev_dbg(&sensor->client->dev,
				"Chip reset success! i=%d\n", i);
			break;
		}

		msleep(MPU6880_RESET_WAIT_MS);
	}

exit:
	return;
}

static int mpu6880_gyro_set_enable(struct mpu6880_sensor *sensor, bool enable)
{
	int ret = 0;
	mutex_lock(&sensor->op_lock);

	if (enable) {
		if (!sensor->cfg.enable) {
			ret = mpu6880_power_ctl(sensor, true);
			if (ret < 0) {
				dev_err(&sensor->client->dev,
						"Failed to power up mpu6880\n");
				goto exit;
			}
			ret = mpu6880_set_power_mode(sensor, true);
			ret = mpu6880_restore_context(sensor);
			if (ret < 0) {
				dev_err(&sensor->client->dev,
						"Failed to restore context\n");
				goto exit;
			}
		}

		ret = mpu6880_gyro_enable(sensor, true);
		if (ret) {
			dev_err(&sensor->client->dev,
				"Fail to enable gyro engine ret=%d\n", ret);
			ret = -EBUSY;
			goto exit;
		}

		if (sensor->use_poll)
			schedule_delayed_work(&sensor->gyro_poll_work,
				msecs_to_jiffies(sensor->gyro_poll_ms));
		else
			enable_irq(sensor->client->irq);
	} else {
		msleep(500);//wangyanhui 
		ret = mpu6880_gyro_enable(sensor, false);
		if (ret) {
			dev_err(&sensor->client->dev,
				"Fail to disable gyro engine ret=%d\n", ret);
			ret = -EBUSY;
			goto exit;
		}
		if (sensor->use_poll)
			cancel_delayed_work_sync(&sensor->gyro_poll_work);
		else
			disable_irq(sensor->client->irq);
	}
	msleep(100);//wangyanhui add 
exit:
	mutex_unlock(&sensor->op_lock);
	return ret;
}

static int mpu6880_gyro_set_poll_delay(struct mpu6880_sensor *sensor,
					unsigned long delay)
{
	mutex_lock(&sensor->op_lock);
	if (delay < MPU6880_GYRO_MIN_POLL_INTERVAL_MS)
		delay = MPU6880_GYRO_MIN_POLL_INTERVAL_MS;
	if (delay > MPU6880_GYRO_MAX_POLL_INTERVAL_MS)
		delay = MPU6880_GYRO_MAX_POLL_INTERVAL_MS;

	sensor->gyro_poll_ms = delay;
	if (sensor->use_poll) {
		cancel_delayed_work_sync(&sensor->gyro_poll_work);
		schedule_delayed_work(&sensor->gyro_poll_work,
				msecs_to_jiffies(sensor->gyro_poll_ms));
	}
	mutex_unlock(&sensor->op_lock);
	return 0;
}

static int mpu6880_gyro_cdev_enable(struct sensors_classdev *sensors_cdev,
			unsigned int enable)
{
	struct mpu6880_sensor *sensor = container_of(sensors_cdev,
			struct mpu6880_sensor, gyro_cdev);

	return mpu6880_gyro_set_enable(sensor, enable);
}

static int mpu6880_gyro_cdev_poll_delay(struct sensors_classdev *sensors_cdev,
			unsigned int delay_ms)
{
	struct mpu6880_sensor *sensor = container_of(sensors_cdev,
			struct mpu6880_sensor, gyro_cdev);

	return mpu6880_gyro_set_poll_delay(sensor, delay_ms);
}

/**
 * mpu6880_gyro_attr_get_polling_delay - get the sampling rate
 */
static ssize_t mpu6880_gyro_attr_get_polling_delay(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	int val;
	struct mpu6880_sensor *sensor = dev_get_drvdata(dev);

	val = sensor ? sensor->gyro_poll_ms : 0;
	return snprintf(buf, 8, "%d\n", val);
}

/**
 * mpu6880_gyro_attr_set_polling_delay - set the sampling rate
 */
static ssize_t mpu6880_gyro_attr_set_polling_delay(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct mpu6880_sensor *sensor = dev_get_drvdata(dev);
	unsigned long interval_ms;
	int ret;

	if (kstrtoul(buf, 10, &interval_ms))
		return -EINVAL;

	ret = mpu6880_gyro_set_poll_delay(sensor, interval_ms);

	return ret ? -EBUSY : size;
}

static int MPU6880_SetPowerMode(struct i2c_client *iclient)
{
	int i = 0;
	u8  temp_data;
	struct mpu6880_sensor *self_info = mpu_info;

	for (; i <= 107; i++) {
		mpu6880_read_reg(self_info->client, i, &temp_data, 1);
		/*pr_info("wlg_set_self_test----read 0x%d,%X\n",i,temp_data);*/
	}

	mpu6880_write_reg(self_info->client, 0X38, 0X00, 1);
	mpu6880_write_reg(self_info->client, 0X23, 0X00, 1);
	mpu6880_write_reg(self_info->client, 0X6A, 0X00, 1);
	mpu6880_write_reg(self_info->client, 0X6A, 0X04, 1);
	mpu6880_write_reg(self_info->client, 0X1A, 0X02, 1);
	mpu6880_write_reg(self_info->client, 0X1D, 0X02, 1);
	mpu6880_write_reg(self_info->client, 0X19, 0X00, 1);
	mpu6880_write_reg(self_info->client, 0X1B, 0X00, 1);
	mpu6880_write_reg(self_info->client, 0X1C, 0X00, 1);
	mpu6880_write_reg(self_info->client, 0X6A, 0X40, 1);
	mpu6880_write_reg(self_info->client, 0X6B, 0X0, 1);
	mpu6880_write_reg(self_info->client, 0X6C, 0X0, 1);
	mpu6880_write_reg(self_info->client, 0X23, 0X78, 1);
	mpu6880_write_reg(self_info->client, 0X23, 0X00, 1);

	return 0;
}

static int MPU6880_JudgeTestResult(struct i2c_client *client,
s32 prv[MPU6880_AXES_NUM], s32 nxt[MPU6880_AXES_NUM])
{
	struct criteria {
		int min;
		int max;
	};

	struct criteria gyro_offset[4][3] = {
	     /*x y z*/
	{{ 1000, (DEF_GYRO_OFFSET_MAX * DEF_SELFTEST_GYRO_SENS_250)},
	{ 1000, (DEF_GYRO_OFFSET_MAX * DEF_SELFTEST_GYRO_SENS_250)},
	{ 1000, (DEF_GYRO_OFFSET_MAX * DEF_SELFTEST_GYRO_SENS_250)} },

	{{ 1000, (DEF_GYRO_OFFSET_MAX * DEF_SELFTEST_GYRO_SENS_500)},
	{ 1000, (DEF_GYRO_OFFSET_MAX * DEF_SELFTEST_GYRO_SENS_500)},
	{ 1000, (DEF_GYRO_OFFSET_MAX * DEF_SELFTEST_GYRO_SENS_500)} },

	{{ 1000, (DEF_GYRO_OFFSET_MAX * DEF_SELFTEST_GYRO_SENS_1000)},
	{ 1000, (DEF_GYRO_OFFSET_MAX * DEF_SELFTEST_GYRO_SENS_1000)},
	{ 1000, (DEF_GYRO_OFFSET_MAX * DEF_SELFTEST_GYRO_SENS_1000)} },

	{{ 1000, (DEF_GYRO_OFFSET_MAX * DEF_SELFTEST_GYRO_SENS_2000)},
	{ 1000, (DEF_GYRO_OFFSET_MAX * DEF_SELFTEST_GYRO_SENS_2000)},
	{ 1000, (DEF_GYRO_OFFSET_MAX * DEF_SELFTEST_GYRO_SENS_2000)} },
	};

	struct criteria (*ptr)[3] = NULL;
	u8 format;
	int res;

	/*mpu6880_read_reg(sensor->client,
	sensor->reg.raw_gyro,(u8 *)buffer,
	MPU6880_RAW_GYRO_DATA_LEN);*/

	res = mpu6880_read_reg(client, MPU6880_REG_Gyro_DATA_FORMAT,
	&format, 1);
	if (res < 0)
		return res;
	else
		res = 0;

	format = format & MPU6880_RANGE_PN2000dps;

	switch (format) {
	case MPU6880_RANGE_PN250dps:
		pr_info("format use gyro_offset[0]\n");
		ptr = &gyro_offset[0];
		break;

	case MPU6880_RANGE_PN500dps:
		pr_info("format use gyro_offset[1]\n");
		ptr = &gyro_offset[1];
		break;

	case MPU6880_RANGE_PN1000dps:
		pr_info("format use gyro_offset[2]\n");
		ptr = &gyro_offset[2];
		break;

	case MPU6880_RANGE_PN2000dps:
		pr_info("format use gyro_offset[3]\n");
		ptr = &gyro_offset[3];
		break;

	default:
		pr_info("format unknow use\n");
		break;
	}

	if (!ptr) {
		pr_info("null pointer\n");
		return -EINVAL;
	}

	pr_info("format=0x%x\n", format);
	pr_info("X diff is %ld\n",
		abs(nxt[MPU6880_AXIS_X] - prv[MPU6880_AXIS_X]));
	pr_info("Y diff is %ld\n",
		abs(nxt[MPU6880_AXIS_Y] - prv[MPU6880_AXIS_Y]));
	pr_info("Z diff is %ld\n",
		abs(nxt[MPU6880_AXIS_Z] - prv[MPU6880_AXIS_Z]));

	if (abs(prv[MPU6880_AXIS_X]) > (*ptr)[MPU6880_AXIS_X].max) {
		pr_info("gyro X offset[%X] is over range\n",
			prv[MPU6880_AXIS_X]);
		res = -EINVAL;
	}

	if (abs(prv[MPU6880_AXIS_Y]) > (*ptr)[MPU6880_AXIS_Y].max) {
		pr_info("gyro Y offset[%X] is over range\n",
			prv[MPU6880_AXIS_Y]);
		res = -EINVAL;
	}

	if (abs(prv[MPU6880_AXIS_Z]) > (*ptr)[MPU6880_AXIS_Z].max) {
		pr_info("gyro Z offset[%X] is over range\n",
			prv[MPU6880_AXIS_Z]);
		res = -EINVAL;
	}

	if ((abs(nxt[MPU6880_AXIS_X] - prv[MPU6880_AXIS_X])
		< (*ptr)[MPU6880_AXIS_X].min)) {
		pr_info("X is out of work\n");
		res = -EINVAL;
	}

	if ((abs(nxt[MPU6880_AXIS_Y] - prv[MPU6880_AXIS_Y])
		< (*ptr)[MPU6880_AXIS_Y].min)) {
		pr_info("Y is out of work\n");
		res = -EINVAL;
	}

	if ((abs(nxt[MPU6880_AXIS_Z] - prv[MPU6880_AXIS_Z])
		< (*ptr)[MPU6880_AXIS_Z].min)) {
		pr_info("Z is out of work\n");
		res = -EINVAL;
	}

	return res;
}

static ssize_t show_self_value(struct device *dev,
struct device_attribute *attr, char *buf)
{
	struct mpu6880_sensor *self_info = mpu_info;

	if (NULL == self_info) {
		pr_info("show_self_value is null!!\n");
		return 0;
	}

	return snprintf(buf, 8, "%s\n", selftestRes);
}

/**
 * mpu6880_gyro_attr_set_enable -
 *    Set/get enable function is just needed by sensor HAL.
 */
static ssize_t store_self_value(struct device *dev,
struct device_attribute *attr, const char *buf, size_t count)
{
	u8 temp_data;
	struct axis_data *self_data;
	struct axis_data *self_data2;
	struct mpu6880_sensor *self_info = mpu_info;
	int idx, num;
	int ret = 0;
	s32 avg_prv[MPU6880_AXES_NUM] = {0, 0, 0};
	s32 avg_nxt[MPU6880_AXES_NUM] = {0, 0, 0};

	if (1 != sscanf(buf, "%d", &num)) {
		pr_info("parse number fail\n");
		return count;
	} else if (num == 0) {
		pr_info("invalid data count\n");
		return count;
	}
	self_data = kzalloc(sizeof(*self_data) * num, GFP_KERNEL);
	self_data2 = kzalloc(sizeof(*self_data2) * num, GFP_KERNEL);
	if (!self_data || !self_data2)
		goto exit;

	pr_info("NORMAL:\n");
	MPU6880_SetPowerMode(self_info->client);
	msleep(50);

	for (idx = 0; idx < num; idx++) {
		/*mpu6880_read_accel_data(self_info, self_data);*/
		/*wlg test gyro self test*/
		mpu6880_read_gyro_data(self_info, self_data);

		pr_info("x = %d,y = %d,z= %d\n", self_data->rx,
			self_data->ry, self_data->rz);
		avg_prv[MPU6880_AXIS_X] += self_data->rx;
		avg_prv[MPU6880_AXIS_Y] += self_data->ry;
		avg_prv[MPU6880_AXIS_Z] += self_data->rz;
		pr_info("[%5d %5d %5d]\n", self_data->rx,
			self_data->ry, self_data->rz);
	}

	avg_prv[MPU6880_AXIS_X] /= num;
	avg_prv[MPU6880_AXIS_Y] /= num;
	avg_prv[MPU6880_AXIS_Z] /= num;

	/*initial setting for self test*/
	pr_info("SELFTEST:\n");
	/*wlg test gyro self test [write 0E to 1B enable self test]*/
	mpu6880_read_reg(self_info->client, 0X1B, &temp_data, 1);
	pr_info("wlg_set_self_test----read 0x1B    %d\n", temp_data);
	temp_data |= 0xE0;
	/*mpu6880_write_reg(self_info->client, 0X1B, temp_data, 1);*/
	ret = i2c_smbus_write_byte_data(self_info->client,
		0X1B, temp_data);
	if (ret < 0)
		return ret;

	udelay(POWER_EN_DELAY_US+40);

	for (idx = 0; idx < num; idx++) {
		mpu6880_read_gyro_data(self_info, self_data2);

		pr_info("xx = %d,yy = %d,zz= %d\n", self_data2->rx,
			self_data2->ry, self_data2->rz);
		avg_nxt[MPU6880_AXIS_X] += self_data2->rx;
		avg_nxt[MPU6880_AXIS_Y] += self_data2->ry;
		avg_nxt[MPU6880_AXIS_Z] += self_data2->rz;
		pr_info("[%5d %5d %5d]\n", self_data2->rx,
			self_data2->ry, self_data2->rz);
	}

	avg_nxt[MPU6880_AXIS_X] /= num;
	avg_nxt[MPU6880_AXIS_Y] /= num;
	avg_nxt[MPU6880_AXIS_Z] /= num;

	pr_info("X: %5d - %5d = %5d\n", avg_nxt[MPU6880_AXIS_X],
		avg_prv[MPU6880_AXIS_X],
		avg_nxt[MPU6880_AXIS_X] - avg_prv[MPU6880_AXIS_X]);
	pr_info("Y: %5d - %5d = %5d\n", avg_nxt[MPU6880_AXIS_Y],
		avg_prv[MPU6880_AXIS_Y],
		avg_nxt[MPU6880_AXIS_Y] - avg_prv[MPU6880_AXIS_Y]);
	pr_info("Z: %5d - %5d = %5d\n", avg_nxt[MPU6880_AXIS_Z],
		avg_prv[MPU6880_AXIS_Z],
	avg_nxt[MPU6880_AXIS_Z] - avg_prv[MPU6880_AXIS_Z]);
/*
#if 1
	if (!MPU6880_JudgeTestResult(self_info->client,
		avg_prv, avg_nxt))
#else
	if (1)
#endif
*/
	if (!MPU6880_JudgeTestResult(self_info->client,
		avg_prv, avg_nxt)) {
		pr_info("SELFTEST : PASS\n");
		strlcpy(selftestRes, "y", sizeof(selftestRes));
	} else {
		pr_info("SELFTEST : FAIL\n");
		strlcpy(selftestRes, "n", sizeof(selftestRes));
	}
exit:
	/*pr_info("EXIT SELFTEST:\n");*/
	/*wlg test gyro self test [write 0E to 1B enable self test]*/
	mpu6880_read_reg(self_info->client, 0X1B, &temp_data, 1);
	/*pr_info("wlg_exit_selftest----read 0x1B    %d\n",temp_data);*/
	temp_data &= 0x1F;
	/*mpu6880_write_reg(self_info->client, 0X1B, temp_data, 1);*/
	ret = i2c_smbus_write_byte_data(self_info->client,
		0X1B, temp_data);
	if (ret < 0)
		return ret;

	/*restore the setting*/
	/*mpu6880_init_client(client, 0);*/
	kfree(self_data);
	kfree(self_data2);

	return count;
}

static ssize_t mpu6880_gyro_attr_get_enable(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct mpu6880_sensor *sensor = dev_get_drvdata(dev);

	return snprintf(buf, 4, "%d\n", sensor->cfg.gyro_enable);
}

/**
 * mpu6880_gyro_attr_set_enable -
 *    Set/get enable function is just needed by sensor HAL.
 */
static ssize_t mpu6880_gyro_attr_set_enable(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct mpu6880_sensor *sensor = dev_get_drvdata(dev);
	unsigned long enable;
	int ret;

	if (kstrtoul(buf, 10, &enable))
		return -EINVAL;

	if (enable)
		ret = mpu6880_gyro_set_enable(sensor, true);
	else
		ret = mpu6880_gyro_set_enable(sensor, false);

	return ret ? -EBUSY : count;
}

static struct device_attribute gyro_attr[] = {
	__ATTR(poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
	mpu6880_gyro_attr_get_polling_delay,
	mpu6880_gyro_attr_set_polling_delay),
	__ATTR(selftest, S_IRUGO | S_IWUSR,
	show_self_value, store_self_value),
	__ATTR(enable, S_IRUGO | S_IWUSR,
	mpu6880_gyro_attr_get_enable,
	mpu6880_gyro_attr_set_enable),
};

static int create_gyro_sysfs_interfaces(struct device *dev)
{
	int i;
	int err;
	for (i = 0; i < ARRAY_SIZE(gyro_attr); i++) {
		err = device_create_file(dev, gyro_attr + i);
		if (err)
			goto error;
	}
	return 0;

error:
	for (; i >= 0; i--)
		device_remove_file(dev, gyro_attr + i);
	dev_err(dev, "Unable to create interface\n");
	return err;
}

static int remove_gyro_sysfs_interfaces(struct device *dev)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(gyro_attr); i++)
		device_remove_file(dev, gyro_attr + i);
	return 0;
}

static int mpu6880_accel_enable(struct mpu6880_sensor *sensor, bool on)
{
	int ret;
	u8 data;

	if (sensor->cfg.is_asleep)
		return -EINVAL;

	ret = i2c_smbus_read_byte_data(sensor->client,
				sensor->reg.pwr_mgmt_1);
	if (ret < 0) {
		dev_err(&sensor->client->dev,
			"Fail to get sensor power state ret=%d\n", ret);
		return ret;
	}

	data = (u8)ret;
	if (on) {
		ret = mpu6880_switch_engine(sensor, true,
			BIT_PWR_ACCEL_STBY_MASK);
		if (ret)
			return ret;
		sensor->cfg.accel_enable = 1;

		data &= ~BIT_SLEEP;
		ret = i2c_smbus_write_byte_data(sensor->client,
				sensor->reg.pwr_mgmt_1, data);
		if (ret < 0) {
			dev_err(&sensor->client->dev,
				"Fail to set sensor power state ret=%d\n", ret);
			return ret;
		}

		sensor->cfg.enable = 1;
	} else {
		ret = mpu6880_switch_engine(sensor, false,
			BIT_PWR_ACCEL_STBY_MASK);
		if (ret)
			return ret;
		sensor->cfg.accel_enable = 0;

		if (!sensor->cfg.gyro_enable) {
			data |=  BIT_SLEEP;
			ret = i2c_smbus_write_byte_data(sensor->client,
					sensor->reg.pwr_mgmt_1, data);
			if (ret < 0) {
				dev_err(&sensor->client->dev,
					"Fail to set sensor power state ret=%d\n",
					ret);
				return ret;
			}

			sensor->cfg.enable = 0;
		}
	}
	return 0;
}

static int mpu6880_accel_set_enable(struct mpu6880_sensor *sensor, bool enable)
{
	int ret = 0;

	mutex_lock(&sensor->op_lock);

	if (enable) {
		if (!sensor->cfg.enable) {
			ret = mpu6880_power_ctl(sensor, true);
			if (ret < 0) {
				dev_err(&sensor->client->dev,
					"Failed to set power up mpu6880");
				goto exit;
			}

			ret = mpu6880_set_power_mode(sensor, true);
			ret = mpu6880_restore_context(sensor);
			if (ret < 0) {
				dev_err(&sensor->client->dev,
					"Failed to restore context");
				goto exit;
			}
		}

		ret = mpu6880_accel_enable(sensor, true);
		if (ret) {
			dev_err(&sensor->client->dev,
				"Fail to enable accel engine ret=%d\n", ret);
			ret = -EBUSY;
			goto exit;
		}

		if (sensor->use_poll)
			schedule_delayed_work(&sensor->accel_poll_work,
				msecs_to_jiffies(sensor->accel_poll_ms));
		else
			enable_irq(sensor->client->irq);
	} else {
		if (sensor->use_poll)
			cancel_delayed_work_sync(&sensor->accel_poll_work);
		else
			disable_irq(sensor->client->irq);

		ret = mpu6880_accel_enable(sensor, false);
		if (ret) {
			dev_err(&sensor->client->dev,
				"Fail to disable accel engine ret=%d\n", ret);
			ret = -EBUSY;
			goto exit;
		}
	}

exit:
	mutex_unlock(&sensor->op_lock);
	return ret;
}

static int mpu6880_accel_set_poll_delay(struct mpu6880_sensor *sensor,
					unsigned long delay)
{
	u8 divider;
	int ret;

	mutex_lock(&sensor->op_lock);
	if (delay < MPU6880_ACCEL_MIN_POLL_INTERVAL_MS)
		delay = MPU6880_ACCEL_MIN_POLL_INTERVAL_MS;
	if (delay > MPU6880_ACCEL_MAX_POLL_INTERVAL_MS)
		delay = MPU6880_ACCEL_MAX_POLL_INTERVAL_MS;

	if (sensor->accel_poll_ms != delay) {
		/* Output frequency divider. and set timer delay */
		divider = ODR_DLPF_ENA / INIT_FIFO_RATE - 1;
		ret = i2c_smbus_write_byte_data(sensor->client,
				sensor->reg.sample_rate_div, divider);
		if (ret == 0)
			sensor->accel_poll_ms = delay;
	}
	if (sensor->use_poll) {
		cancel_delayed_work_sync(&sensor->accel_poll_work);
		schedule_delayed_work(&sensor->accel_poll_work,
				msecs_to_jiffies(sensor->accel_poll_ms));
	}
	mutex_unlock(&sensor->op_lock);
	return 0;
}

static int mpu6880_accel_cdev_enable(struct sensors_classdev *sensors_cdev,
			unsigned int enable)
{
	struct mpu6880_sensor *sensor = container_of(sensors_cdev,
			struct mpu6880_sensor, accel_cdev);

	return mpu6880_accel_set_enable(sensor, enable);
}

static int mpu6880_accel_cdev_poll_delay(struct sensors_classdev *sensors_cdev,
			unsigned int delay_ms)
{
	struct mpu6880_sensor *sensor = container_of(sensors_cdev,
			struct mpu6880_sensor, accel_cdev);

	return mpu6880_accel_set_poll_delay(sensor, delay_ms);
}

/**
 * mpu6880_accel_attr_get_polling_delay - get the sampling rate
 */
static ssize_t mpu6880_accel_attr_get_polling_delay(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	int val;
	struct mpu6880_sensor *sensor = dev_get_drvdata(dev);

	val = sensor ? sensor->accel_poll_ms : 0;
	return snprintf(buf, 8, "%d\n", val);
}

/**
 * mpu6880_accel_attr_set_polling_delay - set the sampling rate
 */
static ssize_t mpu6880_accel_attr_set_polling_delay(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct mpu6880_sensor *sensor = dev_get_drvdata(dev);
	unsigned long interval_ms;
	int ret;

	if (kstrtoul(buf, 10, &interval_ms))
		return -EINVAL;

	ret = mpu6880_accel_set_poll_delay(sensor, interval_ms);

	return ret ? -EBUSY : size;
}

static ssize_t mpu6880_accel_attr_get_enable(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct mpu6880_sensor *sensor = dev_get_drvdata(dev);

	return snprintf(buf, 4, "%d\n", sensor->cfg.accel_enable);
}

/**
 * mpu6880_accel_attr_set_enable -
 *    Set/get enable function is just needed by sensor HAL.
 */

static ssize_t mpu6880_accel_attr_set_enable(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct mpu6880_sensor *sensor = dev_get_drvdata(dev);
	unsigned long enable;
	int ret;

	if (kstrtoul(buf, 10, &enable))
		return -EINVAL;

	if (enable)
		ret = mpu6880_accel_set_enable(sensor, true);
	else
		ret = mpu6880_accel_set_enable(sensor, false);

	return ret ? -EBUSY : count;
}

#ifdef DEBUG_NODE
u8 mpu6880_address;
u8 mpu6880_data;

static ssize_t mpu6880_accel_attr_get_reg_addr(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	return snprintf(buf, 8, "%d\n", mpu6880_address);
}

static ssize_t mpu6880_accel_attr_set_reg_addr(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	unsigned long addr;

	if (kstrtoul(buf, 10, &addr))
		return -EINVAL;
	if ((addr < 0) || (addr > 255))
		return -EINVAL;

	mpu6880_address = addr;
	dev_info(dev, "mpu6880_address =%d\n", mpu6880_address);

	return size;
}

static ssize_t mpu6880_accel_attr_get_data(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct mpu6880_sensor *sensor = dev_get_drvdata(dev);
	int ret;

	ret = i2c_smbus_read_byte_data(sensor->client, mpu6880_address);
	dev_info(dev, "read addr(0x%x)=0x%x\n", mpu6880_address, ret);
	if (ret >= 0 && ret <= 255)
		mpu6880_data = ret;
	return snprintf(buf, 8, "0x%x\n", ret);
}

static ssize_t mpu6880_accel_attr_set_data(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	unsigned long reg_data;

	if (kstrtoul(buf, 10, &reg_data))
		return -EINVAL;
	if ((reg_data < 0) || (reg_data > 255))
		return -EINVAL;

	mpu6880_data = reg_data;
	dev_info(dev, "set mpu6880_data =0x%x\n", mpu6880_data);

	return size;
}
static ssize_t mpu6880_accel_attr_reg_write(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct mpu6880_sensor *sensor = dev_get_drvdata(dev);
	int ret;

	ret = i2c_smbus_write_byte_data(sensor->client,
		mpu6880_address, mpu6880_data);
	dev_info(dev, "write addr(0x%x)<-0x%x ret=%d\n",
		mpu6880_address, mpu6880_data, ret);

	return size;
}

#endif

static struct device_attribute accel_attr[] = {__ATTR(poll_delay,
S_IRUGO | S_IWUSR | S_IWGRP,
mpu6880_accel_attr_get_polling_delay,
mpu6880_accel_attr_set_polling_delay),
__ATTR(enable, S_IRUGO | S_IWUSR,
mpu6880_accel_attr_get_enable,
mpu6880_accel_attr_set_enable),
#ifdef DEBUG_NODE
	__ATTR(addr, S_IRUSR | S_IWUSR,
		mpu6880_accel_attr_get_reg_addr,
		mpu6880_accel_attr_set_reg_addr),
	__ATTR(reg, S_IRUSR | S_IWUSR,
		mpu6880_accel_attr_get_data,
		mpu6880_accel_attr_set_data),
	__ATTR(write, S_IWUSR,
		NULL,
		mpu6880_accel_attr_reg_write),
#endif
};

static int create_accel_sysfs_interfaces(struct device *dev)
{
	int i;
	int err;
	for (i = 0; i < ARRAY_SIZE(accel_attr); i++) {
		err = device_create_file(dev, accel_attr + i);
		if (err)
			goto error;
	}
	return 0;

error:
	for (; i >= 0; i--)
		device_remove_file(dev, accel_attr + i);
	dev_err(dev, "Unable to create interface\n");
	return err;
}

static int remove_accel_sysfs_interfaces(struct device *dev)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(accel_attr); i++)
		device_remove_file(dev, accel_attr + i);
	return 0;
}

static void setup_mpu6880_reg(struct mpu_reg_map *reg)
{
	reg->sample_rate_div	= REG_SAMPLE_RATE_DIV;
	reg->lpf		= REG_CONFIG;
	reg->fifo_en		= REG_FIFO_EN;
	reg->gyro_config	= REG_GYRO_CONFIG;
	reg->accel_config	= REG_ACCEL_CONFIG;
	reg->fifo_count_h	= REG_FIFO_COUNT_H;
	reg->fifo_r_w		= REG_FIFO_R_W;
	reg->raw_gyro		= REG_RAW_GYRO;
	reg->raw_accel		= REG_RAW_ACCEL;
	reg->temperature	= REG_TEMPERATURE;
	reg->int_enable		= REG_INT_ENABLE;
	reg->int_status		= REG_INT_STATUS;
	reg->pwr_mgmt_1		= REG_PWR_MGMT_1;
	reg->pwr_mgmt_2		= REG_PWR_MGMT_2;
};

/**
 * mpu_check_chip_type() - check and setup chip type.
 */
static int mpu_check_chip_type(struct mpu6880_sensor *sensor,
		const struct i2c_device_id *id)
{
	struct mpu_reg_map *reg;
	s32 ret;
	struct i2c_client *client = sensor->client;

	if (!strcmp(id->name, "mpu6880"))
			sensor->chip_type = INV_MPU6880;
	else if (!strcmp(id->name, "mpu6500"))
			sensor->chip_type = INV_MPU6500;
	else if (!strcmp(id->name, "mpu6xxx"))
			sensor->chip_type = INV_MPU6880;
	else
		return -EPERM;
	reg = &sensor->reg;
	setup_mpu6880_reg(reg);

	/* turn off and turn on power to ensure gyro engine is on */

	ret = mpu6880_set_power_mode(sensor, false);
	if (ret)
		return ret;
	ret = mpu6880_set_power_mode(sensor, true);
	if (ret)
		return ret;

	if (!strcmp(id->name, "mpu6xxx")) {
		ret = i2c_smbus_read_byte_data(client,
				REG_WHOAMI);
		if (ret < 0)
			return ret;

		if (ret == MPU6500_ID) {
			sensor->chip_type = INV_MPU6500;
		} else if (ret == MPU6880_ID) {
			sensor->chip_type = INV_MPU6880;
		} else {
			dev_err(&client->dev,
				"Invalid chip ID %d\n", ret);
			return -ENODEV;
		}
	}

	return 0;
}

/**
 *  mpu6880_init_config() - Initialize hardware, disable FIFO.
 *  @indio_dev:	Device driver instance.
 *  Initial configuration:
 *  FSR: +/- 2000DPS
 *  DLPF: 42Hz
 *  FIFO rate: 50Hz
 *  AFS: 2G
 */
static int mpu6880_init_config(struct mpu6880_sensor *sensor)
{
	struct mpu_reg_map *reg;
	struct i2c_client *client;
	s32 ret;

	if (sensor->cfg.is_asleep)
		return -EINVAL;

	reg = &sensor->reg;
	client = sensor->client;

	/* reset device*/
	ret = i2c_smbus_write_byte_data(client,
		reg->pwr_mgmt_1, BIT_H_RESET);
	if (ret < 0)
		return ret;
	do {
		usleep(10);
		/* check reset complete */
		ret = i2c_smbus_read_byte_data(client,
			reg->pwr_mgmt_1);
		if (ret < 0) {
			dev_err(&client->dev,
				"Failed to read reset status ret =%d\n",
				ret);
			return ret;
		}
	} while (ret & BIT_H_RESET);
	memset(&sensor->cfg, 0, sizeof(struct mpu_chip_config));

	/* Gyro full scale range configure */
	ret = i2c_smbus_write_byte_data(client, reg->gyro_config,
		MPU_FSR_2000DPS << GYRO_CONFIG_FSR_SHIFT);
	if (ret < 0)
		return ret;
	sensor->cfg.fsr = MPU_FSR_2000DPS;

	ret = i2c_smbus_write_byte_data(client, reg->lpf, MPU_DLPF_42HZ);
	if (ret < 0)
		return ret;
	sensor->cfg.lpf = MPU_DLPF_42HZ;

	ret = i2c_smbus_write_byte_data(client, reg->sample_rate_div,
					ODR_DLPF_ENA / INIT_FIFO_RATE - 1);
	if (ret < 0)
		return ret;
	sensor->cfg.fifo_rate = INIT_FIFO_RATE;

	ret = i2c_smbus_write_byte_data(client, reg->accel_config,
		(ACCEL_FS_02G << ACCL_CONFIG_FSR_SHIFT));
	if (ret < 0)
		return ret;
	sensor->cfg.accel_fs = ACCEL_FS_02G;

	sensor->cfg.gyro_enable = 0;
	sensor->cfg.gyro_fifo_enable = 0;
	sensor->cfg.accel_enable = 0;
	sensor->cfg.accel_fifo_enable = 0;

	return 0;
}

#ifdef CONFIG_OF
static int mpu6880_dt_get_place(struct device *dev,
			struct mpu6880_platform_data *pdata)
{
	const char *place_name;
	int rc;
	int i;

	rc = of_property_read_string(dev->of_node, "invn,place", &place_name);
	if (rc) {
		dev_err(dev, "Cannot get place configuration!\n");
		return -EINVAL;
	}

	for (i = 0; i < MPU6880_AXIS_REMAP_TAB_SZ; i++) {
		if (!strcmp(place_name, mpu6880_place_name2num[i].name)) {
			pdata->place = mpu6880_place_name2num[i].place;
			break;
		}
	}
	if (i >= MPU6880_AXIS_REMAP_TAB_SZ) {
		dev_warn(dev, "Invalid place parameter, use default value 0\n");
		pdata->place = 0;
	}

	return 0;
}

static int mpu6880_parse_dt(struct device *dev,
			struct mpu6880_platform_data *pdata)
{
	int rc;

	rc = mpu6880_dt_get_place(dev, pdata);
	if (rc)
		return rc;

	/* check gpio_int later, use polling if gpio_int is invalid. */
	pdata->gpio_int = of_get_named_gpio_flags(dev->of_node,
				"invn,gpio-int", 0, &pdata->int_flags);

	pdata->gpio_en = of_get_named_gpio_flags(dev->of_node,
				"invn,gpio-en", 0, NULL);

	pdata->use_int = of_property_read_bool(dev->of_node,
				"invn,use-interrupt");

	pdata->accel_poll_ms = of_get_named_gpio_flags(dev->of_node,
				"invn,accel_poll_ms", 0, NULL);

	pdata->gyro_poll_ms = of_get_named_gpio_flags(dev->of_node,
				"invn,gyro_poll_ms", 0, NULL);

	return 0;
}
#else
static int mpu6880_parse_dt(struct device *dev,
			struct mpu6880_platform_data *pdata)
{
	return -EINVAL;
}
#endif

/**
 * mpu6880_probe - device detection callback
 * @client: i2c client of found device
 * @id: id match information
 *
 * The I2C layer calls us when it believes a sensor is present at this
 * address. Probe to see if this is correct and to validate the device.
 *
 * If present install the relevant sysfs interfaces and input device.
 */

/* GS open fops */
ssize_t gs_open(struct inode *inode, struct file *file)
{
	file->private_data = mpu_info;
	return nonseekable_open(inode, file);
}

/* GS release fops */
ssize_t gs_release(struct inode *inode, struct file *file)
{
	return 0;
}

/* GS IOCTL */
static long gs_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int rc = 0;
	void __user *argp = (void __user *)arg;
	struct mpu6880_sensor *sensor = file->private_data;
	struct cali_data rawdata;

	switch (cmd) {
	case GS_RAW_DATA_FOR_CALI:
			mpu6880_read_accel_data(sensor, &sensor->axis);
			mpu6880_remap_accel_data(&sensor->axis,
				sensor->pdata->place);

			rawdata.x = sensor->axis.x;
			rawdata.y = sensor->axis.y;
			rawdata.z = sensor->axis.z;
			if (copy_to_user(argp, &rawdata, sizeof(rawdata))) {
				dev_err(&sensor->client->dev, "copy_to_user failed.");
				return -EFAULT;
			}
			break;
	default:
			pr_err("%s: INVALID COMMAND %d\n",
				__func__, _IOC_NR(cmd));
			rc = -EINVAL;
	}

	return rc;
}

static const struct file_operations gs_fops = {
	.owner = THIS_MODULE,
	.open = gs_open,
	.release = gs_release,
	.unlocked_ioctl = gs_ioctl
};

struct miscdevice gs_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "gsensor",
	.fops = &gs_fops
};

ssize_t gyro_open(struct inode *inode, struct file *file)
{

	file->private_data = mpu_info;
	return nonseekable_open(inode, file);
}

/* GS release fops */
ssize_t gyro_release(struct inode *inode, struct file *file)
{
	return 0;
}

/* GS IOCTL */
static long gyro_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int rc = 0;
	void __user *argp = (void __user *)arg;
	struct mpu6880_sensor *sensor = file->private_data;
	struct cali_data gyrorawdata;

	switch (cmd) {
	case GYRO_RAW_DATA_FOR_CALI:
			mpu6880_read_gyro_data(sensor, &sensor->axis);
			mpu6880_remap_gyro_data(&sensor->axis,
				sensor->pdata->place);
			gyrorawdata.x = sensor->axis.rx;
			gyrorawdata.y = sensor->axis.ry;
			gyrorawdata.z = sensor->axis.rz;

			if (copy_to_user(argp, &gyrorawdata,
				sizeof(gyrorawdata))) {
				dev_err(&sensor->client->dev, "copy_to_user failed.");
				return -EFAULT;
			}
			break;
	default:
			pr_err("%s: INVALID COMMAND %d\n",
				__func__, _IOC_NR(cmd));
			rc = -EINVAL;
	}

	return rc;
}

static const struct file_operations gyro_fops = {
	.owner = THIS_MODULE,
	.open = gyro_open,
	.release = gyro_release,
	.unlocked_ioctl = gyro_ioctl
};

struct miscdevice gyro_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "gyro",
	.fops = &gyro_fops
};

static int mpu6880_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	struct mpu6880_sensor *sensor;
	struct mpu6880_platform_data *pdata;
	int ret;

	ret = i2c_check_functionality(client->adapter,
					 I2C_FUNC_SMBUS_BYTE |
					 I2C_FUNC_SMBUS_BYTE_DATA |
					 I2C_FUNC_I2C);
	if (!ret) {
		dev_err(&client->dev,
			"Required I2C funcationality does not supported\n");
		return -ENODEV;
	}
	sensor = devm_kzalloc(&client->dev, sizeof(struct mpu6880_sensor),
			GFP_KERNEL);
	if (!sensor) {
		dev_err(&client->dev, "Failed to allocate driver data\n");
		return -ENOMEM;
	}

	sensor->client = client;
	sensor->dev = &client->dev;
	i2c_set_clientdata(client, sensor);

	if (client->dev.of_node) {
		pdata = devm_kzalloc(&client->dev,
			sizeof(struct mpu6880_platform_data), GFP_KERNEL);
		if (!pdata) {
			dev_err(&client->dev, "Failed to allcated memory\n");
			ret = -ENOMEM;
			goto err_free_devmem;
		}
		ret = mpu6880_parse_dt(&client->dev, pdata);
		if (ret) {
			dev_err(&client->dev, "Failed to parse device tree\n");
			ret = -EINVAL;
			goto err_free_devmem;
		}
	} else {
		pdata = client->dev.platform_data;
	}

	if (!pdata) {
		dev_err(&client->dev, "Cannot get device platform data\n");
		ret = -EINVAL;
		goto err_free_devmem;
	}
	mpu_info = sensor;
	mutex_init(&sensor->op_lock);
	sensor->pdata = pdata;
	sensor->enable_gpio = sensor->pdata->gpio_en;
	if (gpio_is_valid(sensor->enable_gpio)) {
		ret = gpio_request(sensor->enable_gpio, "MPU_EN_PM");
		gpio_direction_output(sensor->enable_gpio, 0);
	}
	ret = mpu6880_power_init(sensor);
	if (ret) {
		dev_err(&client->dev, "Failed to init regulator\n");
		goto err_free_enable_gpio;
	}
	sensor->power_enabled = false;
	ret = mpu6880_power_ctl(sensor, true);
	if (ret) {
		dev_err(&client->dev, "Failed to power on device\n");
		goto err_deinit_regulator;
	}

	ret = mpu_check_chip_type(sensor, id);
	if (ret) {
		dev_err(&client->dev, "Cannot get invalid chip type\n");
		goto err_power_off_device;
	}

	ret = mpu6880_init_engine(sensor);
	if (ret) {
		dev_err(&client->dev, "Failed to init chip engine\n");
		goto err_power_off_device;
	}

	ret = mpu6880_set_lpa_freq(sensor, MPU6880_LPA_5HZ);
	if (ret) {
		dev_err(&client->dev, "Failed to set lpa frequency\n");
		goto err_power_off_device;
	}

	sensor->cfg.is_asleep = false;
	ret = mpu6880_init_config(sensor);
	if (ret) {
		dev_err(&client->dev, "Failed to set default config\n");
		goto err_power_off_device;
	}

	sensor->accel_dev = input_allocate_device();
	if (!sensor->accel_dev) {
		dev_err(&client->dev,
			"Failed to allocate accelerometer input device\n");
		ret = -ENOMEM;
		goto err_power_off_device;
	}

	sensor->gyro_dev = input_allocate_device();
	if (!sensor->gyro_dev) {
		dev_err(&client->dev,
			"Failed to allocate gyroscope input device\n");
		ret = -ENOMEM;
		goto err_free_input_accel;
	}
	
	sensor->accel_dev->name = MPU6880_DEV_NAME_ACCEL;
	sensor->gyro_dev->name = MPU6880_DEV_NAME_GYRO;
	sensor->accel_dev->id.bustype = BUS_I2C;
	sensor->gyro_dev->id.bustype = BUS_I2C;
	sensor->gyro_poll_ms = pdata->gyro_poll_ms;
	sensor->accel_poll_ms = pdata->accel_poll_ms;

	input_set_capability(sensor->accel_dev, EV_ABS, ABS_MISC);
	input_set_capability(sensor->gyro_dev, EV_ABS, ABS_MISC);
	input_set_abs_params(sensor->accel_dev, ABS_X,
			MPU6880_ACCEL_MIN_VALUE, MPU6880_ACCEL_MAX_VALUE,
			0, 0);
	input_set_abs_params(sensor->accel_dev, ABS_Y,
			MPU6880_ACCEL_MIN_VALUE, MPU6880_ACCEL_MAX_VALUE,
			0, 0);
	input_set_abs_params(sensor->accel_dev, ABS_Z,
			MPU6880_ACCEL_MIN_VALUE, MPU6880_ACCEL_MAX_VALUE,
			0, 0);
	input_set_abs_params(sensor->gyro_dev, ABS_RX,
			     MPU6880_GYRO_MIN_VALUE, MPU6880_GYRO_MAX_VALUE,
			     0, 0);
	input_set_abs_params(sensor->gyro_dev, ABS_RY,
			     MPU6880_GYRO_MIN_VALUE, MPU6880_GYRO_MAX_VALUE,
			     0, 0);
	input_set_abs_params(sensor->gyro_dev, ABS_RZ,
			     MPU6880_GYRO_MIN_VALUE, MPU6880_GYRO_MAX_VALUE,
			     0, 0);
	sensor->accel_dev->dev.parent = &client->dev;
	sensor->gyro_dev->dev.parent = &client->dev;
	input_set_drvdata(sensor->accel_dev, sensor);
	input_set_drvdata(sensor->gyro_dev, sensor);

	if ((sensor->pdata->use_int) &&
		gpio_is_valid(sensor->pdata->gpio_int)) {
		sensor->use_poll = 0;
		/* configure interrupt gpio */
		ret = gpio_request(sensor->pdata->gpio_int,
							"mpu_gpio_int");
		if (ret) {
			dev_err(&client->dev,
				"Unable to request interrupt gpio %d\n",
				sensor->pdata->gpio_int);
			goto err_free_input_gyro;
		}

		ret = gpio_direction_input(sensor->pdata->gpio_int);
		if (ret) {
			dev_err(&client->dev,
				"Unable to set direction for gpio %d\n",
				sensor->pdata->gpio_int);
			goto err_free_gpio;
		}
		client->irq = gpio_to_irq(sensor->pdata->gpio_int);

		ret = request_threaded_irq(client->irq,
				     NULL, mpu6880_interrupt_thread,
				     sensor->pdata->int_flags | IRQF_ONESHOT,
				     "mpu6880", sensor);
		if (ret) {
			dev_err(&client->dev,
				"Can't get IRQ %d, error %d\n",
				client->irq, ret);
			client->irq = 0;
			goto err_free_gpio;
		}
		disable_irq(client->irq);
	} else {
		sensor->use_poll = 1;
			INIT_DELAYED_WORK(&sensor->gyro_poll_work,
			mpu6880_gyro_work_fn);
		INIT_DELAYED_WORK(&sensor->accel_poll_work,
			mpu6880_accel_work_fn);
		dev_dbg(&client->dev,
			"Polling mode is enabled. use_int=%d gpio_int=%d",
			sensor->pdata->use_int, sensor->pdata->gpio_int);
	}

	ret = input_register_device(sensor->accel_dev);
	if (ret) {
		dev_err(&client->dev, "Failed to register input device\n");
		goto err_free_irq;
	}
	ret = input_register_device(sensor->gyro_dev);
	if (ret) {
		dev_err(&client->dev, "Failed to register input device\n");
		goto err_unregister_accel;
	}

	ret = misc_register(&gs_misc);
	if (ret < 0)
		return ret;

	ret = misc_register(&gyro_misc);
	if (ret < 0)
		return ret;

	ret = create_accel_sysfs_interfaces(&sensor->accel_dev->dev);
	if (ret < 0) {
		dev_err(&client->dev, "failed to create sysfs for accel\n");
		goto err_unregister_gyro;
	}
	ret = create_gyro_sysfs_interfaces(&sensor->gyro_dev->dev);
	if (ret < 0) {
		dev_err(&client->dev, "failed to create sysfs for gyro\n");
		goto err_remove_accel_sysfs;
	}

	sensor->gyro_cdev = mpu6880_gyro_cdev;
	sensor->gyro_cdev.delay_msec = sensor->gyro_poll_ms;
	sensor->gyro_cdev.sensors_enable = mpu6880_gyro_cdev_enable;
	sensor->gyro_cdev.sensors_poll_delay = mpu6880_gyro_cdev_poll_delay;
	ret = sensors_classdev_register(&client->dev, &sensor->gyro_cdev);
	if (ret) {
		dev_err(&client->dev,
			"create accel class device file failed!\n");
		ret = -EINVAL;
		goto err_remove_gyro_sysfs;
	}

	sensor->accel_cdev = mpu6880_acc_cdev;
	sensor->accel_cdev.delay_msec = sensor->accel_poll_ms;
	sensor->accel_cdev.sensors_enable = mpu6880_accel_cdev_enable;
	sensor->accel_cdev.sensors_poll_delay = mpu6880_accel_cdev_poll_delay;
	ret = sensors_classdev_register(&client->dev, &sensor->accel_cdev);
	if (ret) {
		dev_err(&client->dev,
			"create accel class device file failed!\n");
		ret = -EINVAL;
		goto err_remove_gyro_cdev;
	}

	ret = mpu6880_power_ctl(sensor, false);
	if (ret) {
		dev_err(&client->dev,
				"Power off mpu6880 failed\n");
		goto err_remove_accel_cdev;
	}
	return 0;
err_remove_accel_cdev:
	 sensors_classdev_unregister(&sensor->accel_cdev);
err_remove_gyro_cdev:
	sensors_classdev_unregister(&sensor->gyro_cdev);
err_remove_gyro_sysfs:
	remove_accel_sysfs_interfaces(&sensor->gyro_dev->dev);
err_remove_accel_sysfs:
	remove_accel_sysfs_interfaces(&sensor->accel_dev->dev);
err_unregister_gyro:
	input_unregister_device(sensor->gyro_dev);
err_unregister_accel:
	input_unregister_device(sensor->accel_dev);
err_free_irq:
	if (client->irq > 0)
		free_irq(client->irq, sensor);
err_free_gpio:
	if ((sensor->pdata->use_int) &&
		(gpio_is_valid(sensor->pdata->gpio_int)))
		gpio_free(sensor->pdata->gpio_int);
err_free_input_gyro:
	input_free_device(sensor->gyro_dev);
err_free_input_accel:
	input_free_device(sensor->accel_dev);
err_power_off_device:
	mpu6880_power_ctl(sensor, false);
err_deinit_regulator:
	mpu6880_power_deinit(sensor);
err_free_enable_gpio:
	if (gpio_is_valid(sensor->enable_gpio))
		gpio_free(sensor->enable_gpio);
err_free_devmem:
	devm_kfree(&client->dev, sensor);
	dev_err(&client->dev, "Probe device return error%d\n", ret);
	return ret;
}

/**
 * mpu6880_remove - remove a sensor
 * @client: i2c client of sensor being removed
 *
 * Our sensor is going away, clean up the resources.
 */
static int mpu6880_remove(struct i2c_client *client)
{
	struct mpu6880_sensor *sensor = i2c_get_clientdata(client);

	sensors_classdev_unregister(&sensor->accel_cdev);
	sensors_classdev_unregister(&sensor->gyro_cdev);
	remove_gyro_sysfs_interfaces(&sensor->gyro_dev->dev);
	remove_accel_sysfs_interfaces(&sensor->accel_dev->dev);
	input_unregister_device(sensor->gyro_dev);
	input_unregister_device(sensor->accel_dev);
	if (client->irq > 0)
		free_irq(client->irq, sensor);
	if ((sensor->pdata->use_int) &&
		(gpio_is_valid(sensor->pdata->gpio_int)))
		gpio_free(sensor->pdata->gpio_int);
	input_free_device(sensor->gyro_dev);
	input_free_device(sensor->accel_dev);
	mpu6880_power_ctl(sensor, false);
	mpu6880_power_deinit(sensor);
	if (gpio_is_valid(sensor->enable_gpio))
		gpio_free(sensor->enable_gpio);
	devm_kfree(&client->dev, sensor);

	return 0;
}

#ifdef CONFIG_PM
/**
 * mpu6880_suspend - called on device suspend
 * @dev: device being suspended
 *
 * Put the device into sleep mode before we suspend the machine.
 */
static int mpu6880_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mpu6880_sensor *sensor = i2c_get_clientdata(client);
	int ret = 0;

	mutex_lock(&sensor->op_lock);

	if (!sensor->use_poll)
		disable_irq(client->irq);
	else {
		if (sensor->cfg.gyro_enable)
			cancel_delayed_work_sync(&sensor->gyro_poll_work);

		if (sensor->cfg.accel_enable)
			cancel_delayed_work_sync(&sensor->accel_poll_work);
	}

	mpu6880_set_power_mode(sensor, false);
	ret = mpu6880_power_ctl(sensor, false);
	if (ret < 0) {
		dev_err(&client->dev, "Power off mpu6050 failed\n");
		goto exit;
	}

	dev_dbg(&client->dev, "suspended\n");

exit:
	mutex_unlock(&sensor->op_lock);

	return ret;
}

/**
 * mpu6880_resume - called on device resume
 * @dev: device being resumed
 *
 * Put the device into powered mode on resume.
 */
static int mpu6880_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mpu6880_sensor *sensor = i2c_get_clientdata(client);
	int ret = 0;

	/* Keep sensor power on to prevent  */
	ret = mpu6880_power_ctl(sensor, true);
	if (ret < 0) {
		dev_err(&client->dev, "Power on mpu6880 failed\n");
		goto exit;
	}
	/* Reset sensor to recovery from unexpect state */
	mpu6880_reset_chip(sensor);

	if (sensor->cfg.enable) {
		ret = mpu6880_restore_context(sensor);
		if (ret < 0) {
			dev_err(&client->dev, "Failed to restore context\n");
			goto exit;
		}
		mpu6880_set_power_mode(sensor, true);
	} else {
		mpu6880_set_power_mode(sensor, false);
	}

	if (sensor->cfg.gyro_enable) {
		ret = mpu6880_gyro_enable(sensor, true);
		if (ret < 0) {
			dev_err(&client->dev, "Failed to enable gyro\n");
			goto exit;
		}

		if (sensor->use_poll) {
			schedule_delayed_work(&sensor->gyro_poll_work,
				msecs_to_jiffies(sensor->gyro_poll_ms));
		}
	}

	if (sensor->cfg.accel_enable) {
		ret = mpu6880_accel_enable(sensor, true);
		if (ret < 0) {
			dev_err(&client->dev, "Failed to enable accel\n");
			goto exit;
		}

		if (sensor->use_poll) {
			schedule_delayed_work(&sensor->accel_poll_work,
				msecs_to_jiffies(sensor->accel_poll_ms));
		}
	}

	if (!sensor->use_poll)
		enable_irq(client->irq);

	dev_dbg(&client->dev, "resumed\n");

exit:
	return ret;
}
#endif

static UNIVERSAL_DEV_PM_OPS(mpu6880_pm, mpu6880_suspend, mpu6880_resume, NULL);

static const struct i2c_device_id mpu6880_ids[] = {
	{ "mpu6880", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mpu6880_ids);

static const struct of_device_id mpu6880_of_match[] = {
	{ .compatible = "invn,mpu6880", },
	{ },
};
MODULE_DEVICE_TABLE(of, mpu6880_of_match);

static struct i2c_driver mpu6880_i2c_driver = {
	.driver	= {
		.name	= "mpu6880",
		.owner	= THIS_MODULE,
		.pm	= &mpu6880_pm,
		.of_match_table = mpu6880_of_match,
	},
	.probe		= mpu6880_probe,
	.remove		= mpu6880_remove,
	.id_table	= mpu6880_ids,
};

module_i2c_driver(mpu6880_i2c_driver);

MODULE_DESCRIPTION("MPU6880 Tri-axis gyroscope driver");
MODULE_LICENSE("GPL v2");

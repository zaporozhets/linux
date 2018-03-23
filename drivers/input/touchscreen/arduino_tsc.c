/*
 * Arduino touch screen controller driver for I2C
 * Based on ar1021 driver
 * Author: Taras Zaporozhets <taras.zaporozhets@gmail.com>
 *
 * License: GPLv2 as published by the FSF.
 */

#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/interrupt.h>

#define ARDUINO_TSC_TOCUH_PKG_SIZE	5

#define ARDUINO_TSC_MAX_X	1024
#define ARDUINO_TSC_MAX_Y	1024

struct arduino_tsc {
	struct i2c_client *client;
	struct input_dev *input;
	u8 data[ARDUINO_TSC_TOCUH_PKG_SIZE];
};

static irqreturn_t arduino_tsc_irq(int irq, void *dev_id)
{
	struct arduino_tsc *arduino_tsc = dev_id;
	struct input_dev *input = arduino_tsc->input;
	u8 *data = arduino_tsc->data;
	unsigned int x, y, button;
	int retval;

	retval = i2c_master_recv(arduino_tsc->client,
				 arduino_tsc->data, sizeof(arduino_tsc->data));
	if (retval != sizeof(arduino_tsc->data))
		goto out;

	/* sync bit set ? */
	if (!(data[0] & BIT(7)))
		goto out;

	button = data[0] & BIT(0);
	x = ((data[2] << 8) | data[1]);
	y = ((data[4] << 8) | data[3]);

	input_report_abs(input, ABS_X, x);
	input_report_abs(input, ABS_Y, y);
	input_report_key(input, BTN_TOUCH, button);
	input_sync(input);

out:
	return IRQ_HANDLED;
}

static int arduino_tsc_open(struct input_dev *dev)
{
	struct arduino_tsc *arduino_tsc = input_get_drvdata(dev);
	struct i2c_client *client = arduino_tsc->client;

	enable_irq(client->irq);

	return 0;
}

static void arduino_tsc_close(struct input_dev *dev)
{
	struct arduino_tsc *arduino_tsc = input_get_drvdata(dev);
	struct i2c_client *client = arduino_tsc->client;

	disable_irq(client->irq);
}

static int arduino_tsc_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct arduino_tsc *arduino_tsc;
	struct input_dev *input;
	int error;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "i2c_check_functionality error\n");
		return -ENXIO;
	}

	arduino_tsc = devm_kzalloc(&client->dev, sizeof(*arduino_tsc), GFP_KERNEL);
	if (!arduino_tsc)
		return -ENOMEM;

	input = devm_input_allocate_device(&client->dev);
	if (!input)
		return -ENOMEM;

	arduino_tsc->client = client;
	arduino_tsc->input = input;

	input->name = "Arduino TSC I2C Touchscreen";
	input->id.bustype = BUS_I2C;
	input->dev.parent = &client->dev;
	input->open = arduino_tsc_open;
	input->close = arduino_tsc_close;

	__set_bit(INPUT_PROP_DIRECT, input->propbit);
	input_set_capability(input, EV_KEY, BTN_TOUCH);
	input_set_abs_params(input, ABS_X, 0, ARDUINO_TSC_MAX_X, 0, 0);
	input_set_abs_params(input, ABS_Y, 0, ARDUINO_TSC_MAX_Y, 0, 0);

	input_set_drvdata(input, arduino_tsc);

	error = devm_request_threaded_irq(&client->dev, client->irq,
					  NULL, arduino_tsc_irq,
					  IRQF_ONESHOT,
					  "arduino_tsc", arduino_tsc);
	if (error) {
		dev_err(&client->dev,
			"Failed to enable IRQ, error: %d\n", error);
		return error;
	}

	/* Disable the IRQ, we'll enable it in arduino_tsc_open() */
	disable_irq(client->irq);

	error = input_register_device(arduino_tsc->input);
	if (error) {
		dev_err(&client->dev,
			"Failed to register input device, error: %d\n", error);
		return error;
	}

	return 0;
}

static int __maybe_unused arduino_tsc_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	disable_irq(client->irq);

	return 0;
}

static int __maybe_unused arduino_tsc_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	enable_irq(client->irq);

	return 0;
}

static SIMPLE_DEV_PM_OPS(arduino_tsc_pm, arduino_tsc_suspend, arduino_tsc_resume);

static const struct i2c_device_id arduino_tsc_id[] = {
	{ "arduino_tsc", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, arduino_tsc_id);

static const struct of_device_id arduino_tsc_of_match[] = {
	{ .compatible = "arduino_tsc", },
	{ }
};
MODULE_DEVICE_TABLE(of, arduino_tsc_of_match);

static struct i2c_driver arduino_tsc_driver = {
	.driver	= {
		.name	= "arduino_tsc",
		.pm	= &arduino_tsc_pm,
		.of_match_table = arduino_tsc_of_match,
	},

	.probe		= arduino_tsc_probe,
	.id_table	= arduino_tsc_id,
};
module_i2c_driver(arduino_tsc_driver);

MODULE_AUTHOR("Taras Zaporozhets <taras.zaporozhets@gmail.com>");
MODULE_DESCRIPTION("Arduino based touchscreen controller I2C Driver");
MODULE_LICENSE("GPL");

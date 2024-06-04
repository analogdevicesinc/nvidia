/*
 * nv_cam_v1.c - generic sensor driver
 *
 * Copyright (c) 2016-2022, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/slab.h>
#include <linux/module.h>

#include <linux/seq_file.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>

#include <media/tegra_v4l2_camera.h>
#include <media/tegra-v4l2-camera.h>
#include <media/camera_common.h>

#define CAM_DEFAULT_DATAFMT	MEDIA_BUS_FMT_UYVY8_1X16
#define CAM_DEFAULT_WIDTH	1920
#define CAM_DEFAULT_HEIGHT	1536

struct cam {
	int	numctrls;
	struct v4l2_ctrl_handler	ctrl_handler;
	struct i2c_client	*i2c_client;
	struct v4l2_subdev	*subdev;
	struct media_pad	pad;
	struct regmap	*regmap;
	struct camera_common_data	*s_data;
	struct camera_common_pdata	*pdata;
	struct v4l2_mbus_framefmt *fmt;
	struct v4l2_ctrl		*ctrls[];
};

static uint32_t cam_mbus_codes[] = {
	MEDIA_BUS_FMT_SRGGB12_1X12,
	MEDIA_BUS_FMT_SGRBG12_1X12,
	MEDIA_BUS_FMT_SGBRG12_1X12,
	MEDIA_BUS_FMT_SBGGR12_1X12,
	MEDIA_BUS_FMT_SRGGB10_1X10,
	MEDIA_BUS_FMT_SGRBG10_1X10,
	MEDIA_BUS_FMT_SGBRG10_1X10,
	MEDIA_BUS_FMT_SBGGR10_1X10,
	MEDIA_BUS_FMT_SRGGB8_1X8,
	MEDIA_BUS_FMT_YUYV8_1X16,
	MEDIA_BUS_FMT_YVYU8_1X16,
	MEDIA_BUS_FMT_UYVY8_1X16,
	MEDIA_BUS_FMT_VYUY8_1X16,
	MEDIA_BUS_FMT_RGB888_1X24,
	MEDIA_BUS_FMT_YUYV8_2X8,
	MEDIA_BUS_FMT_YVYU8_2X8,
	MEDIA_BUS_FMT_UYVY8_2X8,
	MEDIA_BUS_FMT_VYUY8_2X8,
};

static struct {
	unsigned int width;
	unsigned int height;
} cam_res[] = {
	{640,	480},
	{1024,	768},
	{1920,	1080},
	{1920,	1280},
	{1920,	1536},
	{2880,	1860},
};

static const struct regmap_config sensor_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
	.use_single_read = true,
	.use_single_write = true,
};

static int cam_s_ctrl(struct v4l2_ctrl *ctrl);

static const struct v4l2_ctrl_ops cam_ctrl_ops = {
	.s_ctrl = cam_s_ctrl,
};

static struct v4l2_ctrl_config ctrl_config_list[] = {
/* Do not change the name field for the controls! */
	{
		.ops = &cam_ctrl_ops,
		.id = TEGRA_CAMERA_CID_FRAME_RATE,
		.name = "Frame Rate",
		.type = V4L2_CTRL_TYPE_INTEGER64,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.min = 1,
		.max = 90,
		.def = 30,
		.step = 1,
	},
};

static int cam_power_on(struct camera_common_data *s_data)
{
	return 0;
}

static int cam_power_off(struct camera_common_data *s_data)
{
	return 0;
}

static int cam_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct device *dev = &client->dev;

	dev_dbg(dev, "%s++ enable %d\n", __func__, enable);

	return 0;
}

static int cam_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct device *dev = &client->dev;

	dev_dbg(dev, "%s: enable = %d\n", __func__, on);

	return 0;
}

static struct v4l2_subdev_video_ops cam_subdev_video_ops = {
	.s_stream	= cam_s_stream,
};

static struct v4l2_subdev_core_ops cam_subdev_core_ops = {
	.s_power	= cam_s_power,
};

static int cam_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_format *format)
{
	if (format->pad)
		return -EINVAL;

	return 0;
}

static int cam_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;

	if (format->pad)
		return -EINVAL;

	mf->width		= CAM_DEFAULT_WIDTH;
	mf->height		= CAM_DEFAULT_HEIGHT;
	mf->code		= CAM_DEFAULT_DATAFMT;
	mf->colorspace		= V4L2_COLORSPACE_SRGB;
	mf->field		= V4L2_FIELD_NONE;
	mf->quantization	= V4L2_QUANTIZATION_FULL_RANGE;
	mf->xfer_func		= V4L2_XFER_FUNC_NONE;

	return 0;
}

static int cam_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad)
		return -EINVAL;

	if (code->index >= ARRAY_SIZE(cam_mbus_codes) - 1)
		return -EINVAL;

	code->code = cam_mbus_codes[code->index];

	return 0;
}

static int cam_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(cam_res))
		return -EINVAL;

	fse->max_width = cam_res[fse->index].width;
	fse->max_height = cam_res[fse->index].height;

	return 0;
}

static struct v4l2_subdev_pad_ops cam_subdev_pad_ops = {
	.set_fmt = cam_set_fmt,
	.get_fmt = cam_get_fmt,
	.enum_mbus_code = cam_enum_mbus_code,
	.enum_frame_size	= cam_enum_frame_size,
	.get_mbus_config	= camera_common_get_mbus_config,
};

static struct v4l2_subdev_ops cam_subdev_ops = {
	.core	= &cam_subdev_core_ops,
	.video	= &cam_subdev_video_ops,
	.pad = &cam_subdev_pad_ops,
};

const static struct of_device_id cam_of_match[] = {
	{ .compatible = "adi,cam_v1",},
	{ },
};

static struct camera_common_sensor_ops cam_common_ops = {
	.power_on = cam_power_on,
	.power_off = cam_power_off,
};

static int cam_set_frame_rate(struct cam *priv, s64 val)
{
	return 0;
}

static int cam_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct cam *priv =
		container_of(ctrl->handler, struct cam, ctrl_handler);
	struct device *dev = &priv->i2c_client->dev;
	int err = 0;

	switch (ctrl->id) {
	case TEGRA_CAMERA_CID_FRAME_RATE:
		err = cam_set_frame_rate(priv, *ctrl->p_new.p_s64);
		break;
	default:
		dev_err(dev, "%s: unknown ctrl id.\n", __func__);
		return -EINVAL;
	}

	return err;
}

static int cam_ctrls_init(struct cam *priv)
{
	struct i2c_client *client = priv->i2c_client;
	struct v4l2_ctrl *ctrl;
	int num_ctrls;
	int err = 0;
	int i;

	dev_dbg(&client->dev, "%s++\n", __func__);

	num_ctrls = ARRAY_SIZE(ctrl_config_list);
	v4l2_ctrl_handler_init(&priv->ctrl_handler, num_ctrls);

	for (i = 0; i < num_ctrls; i++) {
		ctrl = v4l2_ctrl_new_custom(&priv->ctrl_handler,
			&ctrl_config_list[i], NULL);
		if (ctrl == NULL) {
			dev_err(&client->dev, "Failed to init %s ctrl\n",
				ctrl_config_list[i].name);
			continue;
		}

		if (ctrl_config_list[i].type == V4L2_CTRL_TYPE_STRING &&
			ctrl_config_list[i].flags & V4L2_CTRL_FLAG_READ_ONLY) {
			ctrl->p_new.p_char = devm_kzalloc(&client->dev,
				ctrl_config_list[i].max + 1, GFP_KERNEL);
		}
		priv->ctrls[i] = ctrl;
	}

	priv->numctrls = num_ctrls;
	priv->subdev->ctrl_handler = &priv->ctrl_handler;
	if (priv->ctrl_handler.error) {
		dev_err(&client->dev, "Error %d adding controls\n",
			priv->ctrl_handler.error);
		err = priv->ctrl_handler.error;
		goto error;
	}

	err = v4l2_ctrl_handler_setup(&priv->ctrl_handler);
	if (err) {
		dev_err(&client->dev,
			"Error %d setting default controls\n", err);
		goto error;
	}

	return 0;

error:
	v4l2_ctrl_handler_free(&priv->ctrl_handler);
	return err;
}

MODULE_DEVICE_TABLE(of, cam_of_match);

static struct camera_common_pdata *cam_parse_dt(struct i2c_client *client,
				struct camera_common_data *s_data)
{
	struct device_node *np = client->dev.of_node;
	struct camera_common_pdata *board_priv_pdata;
	const struct of_device_id *match;
	int err = 0;

	if (!np)
		return NULL;

	match = of_match_device(cam_of_match, &client->dev);
	if (!match) {
		dev_err(&client->dev, "Failed to find matching dt id\n");
		return NULL;
	}

	board_priv_pdata = devm_kzalloc(&client->dev,
			   sizeof(*board_priv_pdata), GFP_KERNEL);

	err = of_property_read_string(np, "mclk",
				      &board_priv_pdata->mclk_name);
	if (err)
		dev_err(&client->dev, "mclk not in DT\n");

	return board_priv_pdata;
}

static int cam_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	dev_dbg(&client->dev, "%s:\n", __func__);

	return 0;
}

static const struct v4l2_subdev_internal_ops cam_subdev_internal_ops = {
	.open = cam_open,
};

static const struct media_entity_operations cam_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int cam_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct camera_common_data *common_data;
	struct cam *priv;
	int err = 0;

	dev_info(&client->dev, "probing cam_v1 v4l2 sensor\n");

	if (!IS_ENABLED(CONFIG_OF) || !client->dev.of_node)
		return -EINVAL;

	common_data = devm_kzalloc(&client->dev,
			    sizeof(struct camera_common_data), GFP_KERNEL);

	priv = devm_kzalloc(&client->dev,
			    sizeof(struct cam) + sizeof(struct v4l2_ctrl *) *
			    ARRAY_SIZE(ctrl_config_list),
			    GFP_KERNEL);
	if (!priv) {
		dev_err(&client->dev, "unable to allocate memory!\n");
		return -ENOMEM;
	}

	priv->regmap = devm_regmap_init_i2c(client, &sensor_regmap_config);
	if (IS_ERR(priv->regmap)) {
		dev_err(&client->dev,
			"regmap init failed: %ld\n", PTR_ERR(priv->regmap));
		return -ENODEV;
	}

	if (client->dev.of_node)
		priv->pdata = cam_parse_dt(client, common_data);
	if (!priv->pdata) {
		dev_err(&client->dev, "unable to get platform data\n");
		return -EFAULT;
	}

	common_data->ops = &cam_common_ops;
	common_data->ctrl_handler = &priv->ctrl_handler;
	common_data->dev = &client->dev;
	common_data->ctrls = priv->ctrls;
	common_data->priv = (void *)priv;
	common_data->numctrls = ARRAY_SIZE(ctrl_config_list);
	common_data->numfmts = 0;
	common_data->def_clk_freq = 37125000;
	common_data->use_sensor_mode_id = false;

	priv->i2c_client = client;
	priv->s_data = common_data;
	priv->subdev = &common_data->subdev;
	priv->subdev->dev = &client->dev;
	priv->s_data->dev = &client->dev;

	err = camera_common_initialize(common_data, "cam_v1");
	if (err) {
		dev_err(&client->dev, "Failed to initialize cam_v1.\n");
		return err;
	}

	v4l2_i2c_subdev_init(priv->subdev, client, &cam_subdev_ops);

	err = cam_ctrls_init(priv);
	if (err)
		return err;

	priv->subdev->internal_ops = &cam_subdev_internal_ops;
	priv->subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;

#if defined(CONFIG_MEDIA_CONTROLLER)
	priv->pad.flags = MEDIA_PAD_FL_SOURCE;
	priv->subdev->entity.ops = &cam_media_ops;
	err = tegra_media_entity_init(&priv->subdev->entity, 1,
				&priv->pad, true, true);
	if (err < 0) {
		dev_err(&client->dev, "unable to init media entity\n");
		return err;
	}
#endif

	err = v4l2_async_register_subdev(priv->subdev);
	if (err)
		return err;

	dev_info(&client->dev, "Detected generic sensor\n");

	return 0;
}

static int cam_remove(struct i2c_client *client)
{
	struct camera_common_data *s_data = to_camera_common_data(&client->dev);
	struct cam *priv = (struct cam *)s_data->priv;

	v4l2_async_unregister_subdev(priv->subdev);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&priv->subdev->entity);
#endif

	v4l2_ctrl_handler_free(&priv->ctrl_handler);
	camera_common_cleanup(s_data);
	return 0;
}

static const struct i2c_device_id cam_id[] = {
	{ "cam_v1", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, cam_id);

static struct i2c_driver cam_i2c_driver = {
	.driver = {
		.name = "cam_v1",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(cam_of_match),
	},
	.probe = cam_probe,
	.remove = cam_remove,
	.id_table = cam_id,
};

module_i2c_driver(cam_i2c_driver);

MODULE_DESCRIPTION("Media Controller driver generic camera");
MODULE_AUTHOR("ADI");
MODULE_LICENSE("GPL v2");

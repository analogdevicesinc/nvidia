// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2018-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * tegracam_v4l2 - tegra camera framework for v4l2 support
 */

#include <nvidia/conftest.h>

#include <linux/types.h>
#include <media/tegra-v4l2-camera.h>
#include <media/tegracam_core.h>
#include <media/tegracam_utils.h>
#include <media/mipi-csi2.h>

static int v4l2sd_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_common_data *s_data = to_camera_common_data(&client->dev);
	struct camera_common_sensor_ops *sensor_ops;
	struct tegracam_device *tc_dev;
	struct tegracam_sensor_data *sensor_data;
	struct sensor_blob *ctrl_blob;
	struct sensor_blob *mode_blob;
	int err = 0;

	dev_dbg(&client->dev, "%s++ enable %d\n", __func__, enable);

	if (!s_data)
		return -EINVAL;

	sensor_ops = s_data->ops;
	tc_dev = to_tegracam_device(s_data);
	sensor_data = &s_data->tegracam_ctrl_hdl->sensor_data;
	ctrl_blob = &sensor_data->ctrls_blob;
	mode_blob = &sensor_data->mode_blob;

	/* reset control packet at start/stop streaming */
	memset(ctrl_blob, 0, sizeof(struct sensor_blob));
	memset(mode_blob, 0, sizeof(struct sensor_blob));
	if (enable) {
		/* increase ref count so module can't be unloaded */
		if (!try_module_get(s_data->owner))
			return -ENODEV;

		err = sensor_ops->set_mode(tc_dev);
		if (err) {
			dev_err(&client->dev, "Error writing mode\n");
			goto error;
		}

		/* update control ranges based on mode settings*/
		err = tegracam_init_ctrl_ranges_by_mode(
			s_data->tegracam_ctrl_hdl, (u32) s_data->mode);
		if (err) {
			dev_err(&client->dev, "Error updating control ranges\n");
			goto error;
		}

		if (s_data->override_enable) {
			err = tegracam_ctrl_set_overrides(
					s_data->tegracam_ctrl_hdl);
			if (err) {
				dev_err(&client->dev,
					"overrides cannot be set\n");
				goto error;
			}
		}
		err = tegracam_ctrl_synchronize_ctrls(s_data->tegracam_ctrl_hdl);
		if (err) {
			dev_err(&client->dev, "Error synchronizing controls during stream start\n");
			goto error;
		}
		err = sensor_ops->start_streaming(tc_dev);
		if (err) {
			dev_err(&client->dev, "Error turning on streaming\n");
			goto error;
		}

		/* add done command for blobs */
		prepare_done_cmd(mode_blob);
		prepare_done_cmd(ctrl_blob);
		tc_dev->is_streaming = true;
	} else {
		err = sensor_ops->stop_streaming(tc_dev);
		if (err) {
			dev_err(&client->dev, "Error turning off streaming\n");
			goto error;
		}

		/* add done command for blob */
		prepare_done_cmd(ctrl_blob);
		tc_dev->is_streaming = false;

		module_put(s_data->owner);
	}

	return 0;

error:
	module_put(s_data->owner);
	return err;
}

static int v4l2sd_enable_streams(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 u32 pad, u64 streams_mask)
{
	return v4l2sd_stream(sd, 1);
}

static int v4l2sd_disable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  u32 pad, u64 streams_mask)
{
	return v4l2sd_stream(sd, 0);
}

static int v4l2sd_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_common_data *s_data = to_camera_common_data(&client->dev);
	struct camera_common_power_rail *pw;

	if (!s_data)
		return -EINVAL;

	pw = s_data->power;
	*status = pw->state == SWITCH_ON;
	return 0;
}

static int cam_g_frame_interval(struct v4l2_subdev *sd,
#if defined(NV_V4L2_SUBDEV_PAD_OPS_STRUCT_HAS_GET_SET_FRAME_INTERVAL)
				struct v4l2_subdev_state *sd_state,
#endif
				struct v4l2_subdev_frame_interval *ival)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_common_data *s_data = to_camera_common_data(&client->dev);

	if (!s_data)
		return -EINVAL;

	ival->interval.denominator = s_data->frmfmt[s_data->mode_prop_idx].framerates[0];
	ival->interval.numerator = 1;
	return 0;
}

static struct v4l2_subdev_video_ops v4l2sd_video_ops = {
	.s_stream	= v4l2_subdev_s_stream_helper,
	.g_input_status = v4l2sd_g_input_status,
#if !defined(NV_V4L2_SUBDEV_PAD_OPS_STRUCT_HAS_GET_SET_FRAME_INTERVAL)
	.g_frame_interval = cam_g_frame_interval,
	.s_frame_interval = cam_g_frame_interval,
#endif
};

static struct v4l2_subdev_core_ops v4l2sd_core_ops = {
	.s_power	= camera_common_s_power,
};

static bool tegracam_has_embedded_metadata(struct camera_common_data *s_data)
{
	const struct sensor_properties *props = &s_data->sensor_props;
	int i;

	for (i = 0; i < props->num_modes; i++) {
		if (props->sensor_modes[i].image_properties.embedded_metadata_height)
			return true;
	}
	return false;
}

static int cam_init_state(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *state)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_common_data *s_data = to_camera_common_data(&client->dev);
	bool has_emb = tegracam_has_embedded_metadata(s_data);
	struct v4l2_subdev_route routes[2];
	struct v4l2_subdev_krouting routing;
	struct v4l2_mbus_framefmt fmt = { };
	int num_routes = 0;
	int ret;

	/* Stream 0: image data */
	memset(routes, 0, sizeof(routes));
	routes[0].source_pad = 0;
	routes[0].source_stream = 0;
	routes[0].sink_pad = 0;
	routes[0].sink_stream = 0;
	routes[0].flags = V4L2_SUBDEV_ROUTE_FL_ACTIVE |
			  V4L2_SUBDEV_ROUTE_FL_IMMUTABLE;
	num_routes = 1;

	/* Stream 1: embedded metadata (if any mode supports it) */
	if (has_emb) {
		routes[1].source_pad = 0;
		routes[1].source_stream = 1;
		routes[1].sink_pad = 0;
		routes[1].sink_stream = 1;
		routes[1].flags = V4L2_SUBDEV_ROUTE_FL_ACTIVE |
				  V4L2_SUBDEV_ROUTE_FL_IMMUTABLE;
		num_routes = 2;
	}

	routing.len_routes = num_routes;
	routing.num_routes = num_routes;
	routing.routes = routes;

	/* Set default format for image stream */
	fmt.width = s_data->def_width;
	fmt.height = s_data->def_height;
	fmt.code = s_data->colorfmt->code;
	fmt.field = V4L2_FIELD_NONE;
	fmt.colorspace = V4L2_COLORSPACE_SRGB;

	ret = v4l2_subdev_set_routing_with_fmt(sd, state, &routing, &fmt);
	if (ret)
		return ret;

	/* Override format for embedded metadata stream */
	if (has_emb) {
		const struct sensor_properties *props = &s_data->sensor_props;
		struct sensor_image_properties *image =
			&props->sensor_modes[s_data->mode_prop_idx].image_properties;
		struct v4l2_mbus_framefmt *emb_fmt;

		emb_fmt = v4l2_subdev_state_get_format(state, 0, 1);
		if (emb_fmt) {
			emb_fmt->width = image->width;
			emb_fmt->height = image->embedded_metadata_height;
			emb_fmt->code = MEDIA_BUS_FMT_META_8;
			emb_fmt->field = V4L2_FIELD_NONE;
			emb_fmt->colorspace = 0;
		}
	}

	return 0;
}

static int cam_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_common_data *s_data = to_camera_common_data(&client->dev);
	struct tegracam_device *tc_dev = to_tegracam_device(s_data);

	/* Call through to sensor-specific open if provided */
	if (tc_dev->v4l2sd_internal_ops && tc_dev->v4l2sd_internal_ops->open)
		return tc_dev->v4l2sd_internal_ops->open(sd, fh);

	return 0;
}

static const struct v4l2_subdev_internal_ops tegracam_internal_ops = {
	.init_state = cam_init_state,
	.open = cam_open,
};

static int cam_set_routing(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *state,
			   enum v4l2_subdev_format_whence which,
			   struct v4l2_subdev_krouting *routing)
{
	if (routing->num_routes > V4L2_FRAME_DESC_ENTRY_MAX)
		return -E2BIG;

	return v4l2_subdev_set_routing(sd, state, routing);
}

static int v4l2sd_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_state *state,
		struct v4l2_subdev_format *format)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_common_data *s_data = to_camera_common_data(&client->dev);
	int ret;

	if (!s_data)
		return -EINVAL;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		ret = camera_common_try_fmt(sd, &format->format);
	else {
		ret = camera_common_s_fmt(sd, &format->format);

		if (ret == 0) {
			/* update control ranges based on mode settings*/
			ret = tegracam_init_ctrl_ranges_by_mode(
				s_data->tegracam_ctrl_hdl, (u32) s_data->mode);
			if (ret) {
				dev_err(&client->dev, "Error updating control ranges %d\n", ret);
				return ret;
			}
		}
	}

	/* TODO: Add set mode for blob collection */

	return ret;
}

static struct v4l2_subdev_pad_ops v4l2sd_pad_ops = {
#if defined(NV_V4L2_SUBDEV_PAD_OPS_STRUCT_HAS_GET_SET_FRAME_INTERVAL)
	.get_frame_interval = cam_g_frame_interval,
	.set_frame_interval = cam_g_frame_interval,
#endif
	.set_fmt = v4l2sd_set_fmt,
	.get_fmt = v4l2_subdev_get_fmt,
	.enum_mbus_code = camera_common_enum_mbus_code,
	.enum_frame_size	= camera_common_enum_framesizes,
	.enum_frame_interval	= camera_common_enum_frameintervals,
	.get_mbus_config	= camera_common_get_mbus_config,
	.get_frame_desc		= camera_common_get_frame_desc,
	.set_routing		= cam_set_routing,
	.enable_streams		= v4l2sd_enable_streams,
	.disable_streams	= v4l2sd_disable_streams,
};

static struct v4l2_subdev_ops v4l2sd_ops = {
	.core	= &v4l2sd_core_ops,
	.video	= &v4l2sd_video_ops,
	.pad = &v4l2sd_pad_ops,
};

static const struct media_entity_operations media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

int tegracam_v4l2subdev_register(struct tegracam_device *tc_dev,
				bool is_sensor)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct tegracam_ctrl_handler *ctrl_hdl;
	struct v4l2_subdev *sd = NULL;
	struct device *dev = tc_dev->dev;
	int err = 0;

	if (!s_data)
		return -EINVAL;

	ctrl_hdl = s_data->tegracam_ctrl_hdl;

	/* init v4l2 subdevice for registration */
	sd = &s_data->subdev;
	if (!sd || !tc_dev->client) {
		dev_err(dev, "Invalid subdev context\n");
		return -ENODEV;
	}

	v4l2_i2c_subdev_init(sd, tc_dev->client, &v4l2sd_ops);

	ctrl_hdl->ctrl_ops = tc_dev->tcctrl_ops;
	err = tegracam_ctrl_handler_init(ctrl_hdl);
	if (err) {
		dev_err(dev, "Failed to init ctrls %s\n", tc_dev->name);
		return err;
	}
	if (ctrl_hdl->ctrl_ops != NULL)
		tc_dev->numctrls = ctrl_hdl->ctrl_ops->numctrls;
	else
		tc_dev->numctrls = 0;
	s_data->numctrls = tc_dev->numctrls;
	sd->ctrl_handler = s_data->ctrl_handler = &ctrl_hdl->ctrl_handler;
	s_data->ctrls = ctrl_hdl->ctrls;
	sd->internal_ops = &tegracam_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			V4L2_SUBDEV_FL_HAS_EVENTS |
			V4L2_SUBDEV_FL_STREAMS;
	s_data->owner = sd->owner;
	/* Set owner to NULL so we can unload the driver module */
	sd->owner = NULL;

#if IS_ENABLED(CONFIG_MEDIA_CONTROLLER)
	tc_dev->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.ops = &media_ops;
	err = tegra_media_entity_init(&sd->entity,
			1, &tc_dev->pad, true, is_sensor);
	if (err < 0) {
		dev_err(dev, "unable to init media entity\n");
		return err;
	}
#endif

	err = v4l2_subdev_init_finalize(sd);
	if (err) {
		dev_err(dev, "subdev init finalize failed\n");
		goto err_entity_cleanup;
	}

#if defined(CONFIG_V4L2_ASYNC)
	err = v4l2_async_register_subdev(sd);
	if (err) {
		dev_err(dev, "async register subdev failed\n");
		goto err_subdev_cleanup;
	}
	return 0;

err_subdev_cleanup:
	v4l2_subdev_cleanup(sd);
#else
	err = -ENOTSUPP;
	v4l2_subdev_cleanup(sd);
#endif
err_entity_cleanup:
#if IS_ENABLED(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	return err;
}
EXPORT_SYMBOL_GPL(tegracam_v4l2subdev_register);

void tegracam_v4l2subdev_unregister(struct tegracam_device *tc_dev)
{
	struct camera_common_data *s_data = tc_dev->s_data;
	struct v4l2_subdev *sd;

	if (!s_data)
		return;

	sd = &s_data->subdev;

	v4l2_subdev_cleanup(sd);
	v4l2_ctrl_handler_free(s_data->ctrl_handler);
#if defined(CONFIG_V4L2_ASYNC)
	v4l2_async_unregister_subdev(sd);
#endif
#if IS_ENABLED(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
}
EXPORT_SYMBOL_GPL(tegracam_v4l2subdev_unregister);

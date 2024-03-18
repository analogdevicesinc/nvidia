# GMSL drivers for NVIDIA Jetson

## Table of contents
1. [Applying patches to the Jetson Linux tree](#applying-patches-to-the-jetson-linux-tree)
2. [Releases](#releases)
3. [Source Code](#source-code)
4. [Device tree](#device-tree)
5. [Configurations](#configurations)
6. [Hardware configuration](#hardware-configuration)
7. [Testing](#testing)
8. [Debugging](#debugging)
9. [Software configuration](#software-configuration)

## Applying patches to the Jetson Linux tree

To be able to apply the patches provided in this repo, you have to clone it in a location of your choice by running the following command.

`git clone https://github.com/analogdevicesinc/nvidia --single-branch -b gmsl/main nvidia-gmsl`

Further instructions can be found in the [Main documentation](https://github.com/analogdevicesinc/nvidia).

## Releases

| Release Tag   	| Patches Directory       	|
|---------------	|-------------------------	|
| jetson_35.3.1 	| ./patches-jetson_35.3.1 	|

## Boards, configurations and device trees

| Board                          	| Release Tag   	| Configuration                                                            	| Device tree                                           	|
|--------------------------------	|---------------	| -------------------------------------------------------------------------	|-------------------------------------------------------	|
| Jetson Orin Nano Developer Kit 	| jetson_35.3.1 	| [MAX96724 + 2xMAX9295A + 2xMAX96717 + 4xOX03A on CAM0 at 2 lanes][cfg-0] 	| [tegra234-p3767-0003-p3768-0000-a0-gmsl-0.dtb][dts-0] 	|
| Jetson Orin Nano Developer Kit 	| jetson_35.3.1 	| [MAX96724 + 4xMAX96717 + 4xOX03A on CAM0 at 2 lanes][cfg-1]              	| [tegra234-p3767-0003-p3768-0000-a0-gmsl-1.dtb][dts-1] 	|
| Jetson Orin Nano Developer Kit 	| jetson_35.3.1 	| [MAX96724 + 2xMAX9295A + 2xMAX96717 + 4xOX03A on CAM1 at 2 lanes][cfg-2] 	| [tegra234-p3767-0003-p3768-0000-a0-gmsl-2.dtb][dts-2] 	|
| Jetson Orin Nano Developer Kit 	| jetson_35.3.1 	| [MAX96724 + 4xMAX96717 + 4xOX03A on CAM1 at 2 lanes][cfg-3]              	| [tegra234-p3767-0003-p3768-0000-a0-gmsl-3.dtb][dts-3] 	|
| Jetson Orin Nano Developer Kit 	| jetson_35.3.1 	| [MAX96724 + 2xMAX9295A + 2xMAX96717 + 4xOX03A on CAM1 at 4 lanes][cfg-4] 	| [tegra234-p3767-0003-p3768-0000-a0-gmsl-4.dtb][dts-4] 	|
| Jetson Orin Nano Developer Kit 	| jetson_35.3.1 	| [MAX96724 + 4xMAX96717 + 4xOX03A on CAM1 at 4 lanes][cfg-5]              	| [tegra234-p3767-0003-p3768-0000-a0-gmsl-5.dtb][dts-5] 	|
| Jetson Orin Nano Developer Kit 	| jetson_35.3.1 	| [MAX96724 + 4xMAX96717 + 4xIMX219 on CAM0 at 2 lanes][cfg-6]             	| [tegra234-p3767-0003-p3768-0000-a0-gmsl-6.dtb][dts-6] 	|
| Jetson Orin Nano Developer Kit 	| jetson_35.3.1 	| [MAX96724 + 4xMAX96717 + 4xIMX219 on CAM1 at 4 lanes][cfg-7]             	| [tegra234-p3767-0003-p3768-0000-a0-gmsl-7.dtb][dts-7] 	|
| Jetson Orin Nano Developer Kit 	| jetson_35.3.1 	| [MAX96714 + 1xMAX9295A + 1xOX03A on CAM1 at 4 lanes][cfg-8]              	| [tegra234-p3767-0003-p3768-0000-a0-gmsl-8.dtb][dts-8] 	|

[cfg-0]: #max96724--2xmax9295a--2xmax96717--4xox03a-on-cam0-at-2-lanes
[cfg-1]: #max96724--4xmax96717--4xox03a-on-cam0-at-2-lanes
[cfg-2]: #max96724--2xmax9295a--2xmax96717--4xox03a-on-cam1-at-2-lanes
[cfg-3]: #max96724--4xmax96717--4xox03a-on-cam1-at-2-lanes
[cfg-4]: #max96724--2xmax9295a--2xmax96717--4xox03a-on-cam1-at-4-lanes
[cfg-5]: #max96724--4xmax96717--4xox03a-on-cam1-at-4-lanes
[cfg-6]: #max96724--4xmax96717--4ximx219-on-cam0-at-2-lanes
[cfg-7]: #max96724--4xmax96717--4ximx219-on-cam1-at-4-lanes
[cfg-8]: #max96714--1xmax9295a--1xox03a-on-cam1-at-4-lanes

[dts-0]: https://github.com/analogdevicesinc/nvidia/blob/gmsl/jetson_35.3.1/hardware_nvidia_platform_t23x_p3768_kernel-dts/tegra234-p3767-0003-p3768-0000-a0-gmsl-0.dts
[dts-1]: https://github.com/analogdevicesinc/nvidia/blob/gmsl/jetson_35.3.1/hardware_nvidia_platform_t23x_p3768_kernel-dts/tegra234-p3767-0003-p3768-0000-a0-gmsl-1.dts
[dts-2]: https://github.com/analogdevicesinc/nvidia/blob/gmsl/jetson_35.3.1/hardware_nvidia_platform_t23x_p3768_kernel-dts/tegra234-p3767-0003-p3768-0000-a0-gmsl-2.dts
[dts-3]: https://github.com/analogdevicesinc/nvidia/blob/gmsl/jetson_35.3.1/hardware_nvidia_platform_t23x_p3768_kernel-dts/tegra234-p3767-0003-p3768-0000-a0-gmsl-3.dts
[dts-4]: https://github.com/analogdevicesinc/nvidia/blob/gmsl/jetson_35.3.1/hardware_nvidia_platform_t23x_p3768_kernel-dts/tegra234-p3767-0003-p3768-0000-a0-gmsl-4.dts
[dts-5]: https://github.com/analogdevicesinc/nvidia/blob/gmsl/jetson_35.3.1/hardware_nvidia_platform_t23x_p3768_kernel-dts/tegra234-p3767-0003-p3768-0000-a0-gmsl-5.dts
[dts-6]: https://github.com/analogdevicesinc/nvidia/blob/gmsl/jetson_35.3.1/hardware_nvidia_platform_t23x_p3768_kernel-dts/tegra234-p3767-0003-p3768-0000-a0-gmsl-6.dts
[dts-7]: https://github.com/analogdevicesinc/nvidia/blob/gmsl/jetson_35.3.1/hardware_nvidia_platform_t23x_p3768_kernel-dts/tegra234-p3767-0003-p3768-0000-a0-gmsl-7.dts
[dts-8]: https://github.com/analogdevicesinc/nvidia/blob/gmsl/jetson_35.3.1/hardware_nvidia_platform_t23x_p3768_kernel-dts/tegra234-p3767-0003-p3768-0000-a0-gmsl-8.dts

## Source code

| Release Tag   	| File                          	|
|---------------	| ------------------------------	|
| jetson_35.3.1 	| [MAX96717/MAX9295A][source-0] 	|
| jetson_35.3.1 	| [MAX96724][source-1]          	|
| jetson_35.3.1 	| [MAX9296A][source-2]          	|
| jetson_35.3.1 	| [Serializer][source-3]        	|
| jetson_35.3.1 	| [Deserializer][source-4]      	|
| jetson_35.3.1 	| [Aggregator][source-5]        	|

## Device tree

| Release Tag   	| File           	|
|---------------	| ---------------	|
| jetson_35.3.1 	| [Main][dtss-0] 	|

[dtss-0]: https://github.com/analogdevicesinc/nvidia/blob/gmsl/jetson_35.3.1/kernel_kernel-5.10/arch/arm64/boot/dts/gmsl/gmsl.dtsi

## Device tree documentation

| Release Tag   	| File                       	|
|---------------	| ---------------------------	|
| jetson_35.3.1 	| [MAX96717/MAX9295A][doc-0] 	|
| jetson_35.3.1 	| [MAX96724][doc-1]          	|
| jetson_35.3.1 	| [MAX9296A][doc-2]          	|
| jetson_35.3.1 	| [Serializer][doc-3]        	|
| jetson_35.3.1 	| [Deserializer][doc-4]      	|

[source-0]: https://github.com/analogdevicesinc/nvidia/blob/gmsl/jetson_35.3.1/kernel_kernel-5.10/drivers/media/i2c/maxim-serdes/max96717.c
[source-1]: https://github.com/analogdevicesinc/nvidia/blob/gmsl/jetson_35.3.1/kernel_kernel-5.10/drivers/media/i2c/maxim-serdes/max96724.c
[source-2]: https://github.com/analogdevicesinc/nvidia/blob/gmsl/jetson_35.3.1/kernel_kernel-5.10/drivers/media/i2c/maxim-serdes/max9296a.c
[source-3]: https://github.com/analogdevicesinc/nvidia/blob/gmsl/jetson_35.3.1/kernel_kernel-5.10/drivers/media/i2c/maxim-serdes/max_ser.c
[source-4]: https://github.com/analogdevicesinc/nvidia/blob/gmsl/jetson_35.3.1/kernel_kernel-5.10/drivers/media/i2c/maxim-serdes/max_des.c
[source-5]: https://github.com/analogdevicesinc/nvidia/blob/gmsl/jetson_35.3.1/kernel_kernel-5.10/drivers/media/i2c/maxim-serdes/max_aggregator.c

[doc-0]: https://github.com/analogdevicesinc/nvidia/blob/gmsl/jetson_35.3.1/kernel_kernel-5.10/Documentation/devicetree/bindings/media/i2c/maxim%2Cmax96717.yaml
[doc-1]: https://github.com/analogdevicesinc/nvidia/blob/gmsl/jetson_35.3.1/kernel_kernel-5.10/Documentation/devicetree/bindings/media/i2c/maxim%2Cmax96724.yaml
[doc-2]: https://github.com/analogdevicesinc/nvidia/blob/gmsl/jetson_35.3.1/kernel_kernel-5.10/Documentation/devicetree/bindings/media/i2c/maxim%2Cmax9296a.yaml
[doc-3]: https://github.com/analogdevicesinc/nvidia/blob/gmsl/jetson_35.3.1/kernel_kernel-5.10/Documentation/devicetree/bindings/media/i2c/maxim-serializer.yaml
[doc-4]: https://github.com/analogdevicesinc/nvidia/blob/gmsl/jetson_35.3.1/kernel_kernel-5.10/Documentation/devicetree/bindings/media/i2c/maxim-deserializer.yaml

## Configurations

### MAX96724 + 2xMAX9295A + 2xMAX96717 + 4xOX03A on CAM0 at 2 lanes

[Software configuration](#sw-cfg-0).

### MAX96724 + 4xMAX96717 + 4xOX03A on CAM0 at 2 lanes

[Software configuration](#sw-cfg-0).

### MAX96724 + 2xMAX9295A + 2xMAX96717 + 4xOX03A on CAM1 at 2 lanes

[Software configuration](#sw-cfg-0).

### MAX96724 + 4xMAX96717 + 4xOX03A on CAM1 at 2 lanes

[Software configuration](#sw-cfg-0).

### MAX96724 + 2xMAX9295A + 2xMAX96717 + 4xOX03A on CAM1 at 4 lanes

[Software configuration](#sw-cfg-0).

### MAX96724 + 4xMAX96717 + 4xOX03A on CAM1 at 4 lanes

[Software configuration](#sw-cfg-0).

### MAX96724 + 4xMAX96717 + 4xIMX219 on CAM0 at 2 lanes

[Software configuration](#sw-cfg-1).

### MAX96724 + 4xMAX96717 + 4xIMX219 on CAM1 at 4 lanes

[Software configuration](#sw-cfg-1).

### MAX96714 + 1xMAX9295A + 1xOX03A on CAM1 at 4 lanes

[Software configuration](#sw-cfg-0).

## Hardware configuration

### CFG Pin Levels

EVKITs need to have their CFG Pin Levels configured using the [GMSL SerDes GUI Software](gui-0).

To do this, open the software, navigate to the `Tools` tab, and then press on the `Set CFG Pin Levels` entry under the `Other Config` section.

Use the `Serializer` tab to configure serializers, and the `Deserializer` tab to configure deserializers.

You'll have to connect your serializers one by one to configure them since the GUI can't switch between multiple connected serializers.

[gui-0]: https://www.analog.com/en/design-center/evaluation-hardware-and-software/software/software-download.html?swpart=SFW0019760F

#### MAX96724

Configure it to use device address 0x4e (0x27), COAX, GMSL2 and 6Gbps.

This should mean `CFG0` at pin level `0` and `CFG1` at pin level `1`.

#### MAX9295A, MAX96717

Configure it to use device address 0x80 (0x40), COAX, and either Tunnel or Pixel mode. The default device tree configuration will switch it to pixel mode.

This should mean `CFG0` at pin level `0` and `CFG1` at pin level `5` or `7`.

### CSI I2C

When using the MAX96724 deserializer, to accept I2C communcation over the CSI bus, you will have to flip the `SW5` switches to the `ON` position.

## Testing

To test GMSL, there are multiple options.

If you run these commands via SSH, don't forget to export the `DISPLAY` variable by running the following command.

`export DISPLAY=:0`

Depending on your configuration, you might have to replace `0` with another number.

You can find this number by connecting a monitor, keyboard, and mouse, to your board, opening the Terminal app and running the following command.

`echo $DISPLAY`

### Probing

To test that the cameras probed correctly, you can use the following command.

`media-ctl -p`

This command will print the V4L2 hierarchy.

An example output can be seen below.

<details>
<summary>Example output</summary>

```
Media controller API version 5.10.104

Media device information
------------------------
driver          tegra-camrtc-ca
model           NVIDIA Tegra Video Input Device
serial
bus info
hw revision     0x3
driver version  5.10.104

Device topology
- entity 1: nvcsi0 (2 pads, 2 links)
            type V4L2 subdev subtype Unknown flags 0
            device node name /dev/v4l-subdev0
	pad0: Sink
		<- "des_ch_0":0 [ENABLED]
	pad1: Source
		-> "vi-output, cam_0":0 [ENABLED]

- entity 4: nvcsi1 (2 pads, 2 links)
            type V4L2 subdev subtype Unknown flags 0
            device node name /dev/v4l-subdev1
	pad0: Sink
		<- "des_ch_1":0 [ENABLED]
	pad1: Source
		-> "vi-output, cam_1":0 [ENABLED]

- entity 7: nvcsi2 (2 pads, 2 links)
            type V4L2 subdev subtype Unknown flags 0
            device node name /dev/v4l-subdev2
	pad0: Sink
		<- "des_ch_2":0 [ENABLED]
	pad1: Source
		-> "vi-output, cam_2":0 [ENABLED]

- entity 10: nvcsi3 (2 pads, 2 links)
             type V4L2 subdev subtype Unknown flags 0
             device node name /dev/v4l-subdev3
	pad0: Sink
		<- "des_ch_3":0 [ENABLED]
	pad1: Source
		-> "vi-output, cam_3":0 [ENABLED]

- entity 13: cam_0 (1 pad, 1 link)
             type V4L2 subdev subtype Sensor flags 0
             device node name /dev/v4l-subdev4
	pad0: Source
		[fmt:SBGGR12_1X12/1920x1280 field:none colorspace:srgb]
		-> "ser_0_ch_0":1 [ENABLED]

- entity 15: ser_0_ch_0 (2 pads, 2 links)
             type V4L2 subdev subtype Unknown flags 0
             device node name /dev/v4l-subdev5
	pad0: Source
		[fmt:SBGGR12_1X12/0x0]
		-> "des_ch_0":1 [ENABLED]
	pad1: Sink
		<- "cam_0":0 [ENABLED]

- entity 18: cam_1 (1 pad, 1 link)
             type V4L2 subdev subtype Sensor flags 0
             device node name /dev/v4l-subdev6
	pad0: Source
		[fmt:SBGGR12_1X12/1920x1280 field:none colorspace:srgb]
		-> "ser_1_ch_0":1 [ENABLED]

- entity 20: ser_1_ch_0 (2 pads, 2 links)
             type V4L2 subdev subtype Unknown flags 0
             device node name /dev/v4l-subdev7
	pad0: Source
		[fmt:SBGGR12_1X12/0x0]
		-> "des_ch_1":1 [ENABLED]
	pad1: Sink
		<- "cam_1":0 [ENABLED]

- entity 23: cam_2 (1 pad, 1 link)
             type V4L2 subdev subtype Sensor flags 0
             device node name /dev/v4l-subdev8
	pad0: Source
		[fmt:SBGGR12_1X12/1920x1280 field:none colorspace:srgb]
		-> "ser_2_ch_0":1 [ENABLED]

- entity 25: ser_2_ch_0 (2 pads, 2 links)
             type V4L2 subdev subtype Unknown flags 0
             device node name /dev/v4l-subdev9
	pad0: Source
		[fmt:SBGGR12_1X12/0x0]
		-> "des_ch_2":1 [ENABLED]
	pad1: Sink
		<- "cam_2":0 [ENABLED]

- entity 28: cam_3 (1 pad, 1 link)
             type V4L2 subdev subtype Sensor flags 0
             device node name /dev/v4l-subdev10
	pad0: Source
		[fmt:SBGGR12_1X12/1920x1280 field:none colorspace:srgb]
		-> "ser_3_ch_0":1 [ENABLED]

- entity 30: ser_3_ch_0 (2 pads, 2 links)
             type V4L2 subdev subtype Unknown flags 0
             device node name /dev/v4l-subdev11
	pad0: Source
		[fmt:SBGGR12_1X12/0x0]
		-> "des_ch_3":1 [ENABLED]
	pad1: Sink
		<- "cam_3":0 [ENABLED]

- entity 33: des_ch_0 (2 pads, 2 links)
             type V4L2 subdev subtype Unknown flags 0
             device node name /dev/v4l-subdev12
	pad0: Source
		[fmt:SBGGR12_1X12/0x0]
		-> "nvcsi0":0 [ENABLED]
	pad1: Sink
		<- "ser_0_ch_0":0 [ENABLED]

- entity 36: vi-output, cam_0 (1 pad, 1 link)
             type Node subtype V4L flags 0
             device node name /dev/video0
	pad0: Sink
		<- "nvcsi0":1 [ENABLED]

- entity 74: des_ch_1 (2 pads, 2 links)
             type V4L2 subdev subtype Unknown flags 0
             device node name /dev/v4l-subdev13
	pad0: Source
		[fmt:SBGGR12_1X12/0x0]
		-> "nvcsi1":0 [ENABLED]
	pad1: Sink
		<- "ser_1_ch_0":0 [ENABLED]

- entity 77: vi-output, cam_1 (1 pad, 1 link)
             type Node subtype V4L flags 0
             device node name /dev/video1
	pad0: Sink
		<- "nvcsi1":1 [ENABLED]

- entity 91: des_ch_2 (2 pads, 2 links)
             type V4L2 subdev subtype Unknown flags 0
             device node name /dev/v4l-subdev14
	pad0: Source
		[fmt:SBGGR12_1X12/0x0]
		-> "nvcsi2":0 [ENABLED]
	pad1: Sink
		<- "ser_2_ch_0":0 [ENABLED]

- entity 94: vi-output, cam_2 (1 pad, 1 link)
             type Node subtype V4L flags 0
             device node name /dev/video2
	pad0: Sink
		<- "nvcsi2":1 [ENABLED]

- entity 108: des_ch_3 (2 pads, 2 links)
              type V4L2 subdev subtype Unknown flags 0
              device node name /dev/v4l-subdev15
	pad0: Source
		[fmt:SBGGR12_1X12/0x0]
		-> "nvcsi3":0 [ENABLED]
	pad1: Sink
		<- "ser_3_ch_0":0 [ENABLED]

- entity 111: vi-output, cam_3 (1 pad, 1 link)
              type Node subtype V4L flags 0
              device node name /dev/video3
	pad0: Sink
		<- "nvcsi3":1 [ENABLED]
```
</details>

To use this command, you need to install the `v4l-utils` package using the following command.

`sudo apt install v4l-utils`

### Debugging

To read and write registers, you can interact with the devices through the V4L2 API.

The following commands will read and write register `0x8d3` from subdev `12`, which is the MAX96724 in the example above.

`v4l2-dbg -d 0 -c subdev12 -g 0x8d3`

`v4l2-dbg -d 0 -c subdev12 -s 0x8d3 0x00`

These commands can only be run as root.

Below are registers that might help you when trying to figure out where the problem is.

#### MAX96724

```
MIPI_PHY25 (0x8D0)
MIPI_PHY26 (0x8D1)
MIPI_PHY27 (0x8D2)
MIPI_PHY28 (0x8D3)
VPRBS (0x1DC, 0x1FC, 0x21C, 0x23C)
```

#### MAX96717

```
VIDEO_TX2 (0x112)
EXT21 (0x38D)
EXT22 (0x38E)
EXT23 (0x38F)
EXT24 (0x390)
```

#### MAX9295A

```
VIDEO_TX2 (0x102)
```

### Software configuration

Because of the way the V4L2 Media Entity framework works in, the serializers and deserializers will need to have their format and resolution configured at run-time.

The device names need to match the name of the serializers and deserializer, which which you can find out using the command in the [Probing](#Probing) section.

The format needs to match the format of the connected camera's active mode, which you can find out using the command in the [Probing](#Probing) section.

Although the resolution is not currently used, it is a necessary parameter.

#### Software configuration for MAX96724 + 4xMAX9295A/MAX96717 + 4xOX03A <a id="sw-cfg-0"></a>

```
media-ctl -d /dev/media0 --set-v4l2 '"ser_0_ch_0":1[fmt:SBGGR12_1X12/1920x1280]'
media-ctl -d /dev/media0 --set-v4l2 '"ser_1_ch_0":1[fmt:SBGGR12_1X12/1920x1280]'
media-ctl -d /dev/media0 --set-v4l2 '"ser_2_ch_0":1[fmt:SBGGR12_1X12/1920x1280]'
media-ctl -d /dev/media0 --set-v4l2 '"ser_3_ch_0":1[fmt:SBGGR12_1X12/1920x1280]'
media-ctl -d /dev/media0 --set-v4l2 '"des_ch_0":0[fmt:SBGGR12_1X12/1920x1280]'
media-ctl -d /dev/media0 --set-v4l2 '"des_ch_1":0[fmt:SBGGR12_1X12/1920x1280]'
media-ctl -d /dev/media0 --set-v4l2 '"des_ch_2":0[fmt:SBGGR12_1X12/1920x1280]'
media-ctl -d /dev/media0 --set-v4l2 '"des_ch_3":0[fmt:SBGGR12_1X12/1920x1280]'
```

#### Software configuration for MAX96724 + 4xMAX9295A/MAX96717 + 4xIMX219 <a id="sw-cfg-1"></a>

```
media-ctl -d /dev/media0 --set-v4l2 '"ser_0_ch_0":1[fmt:SRGGB10_1X10/1920x1080]'
media-ctl -d /dev/media0 --set-v4l2 '"ser_1_ch_0":1[fmt:SRGGB10_1X10/1920x1080]'
media-ctl -d /dev/media0 --set-v4l2 '"ser_2_ch_0":1[fmt:SRGGB10_1X10/1920x1080]'
media-ctl -d /dev/media0 --set-v4l2 '"ser_3_ch_0":1[fmt:SRGGB10_1X10/1920x1080]'
media-ctl -d /dev/media0 --set-v4l2 '"des_ch_0":0[fmt:SRGGB10_1X10/1920x1080]'
media-ctl -d /dev/media0 --set-v4l2 '"des_ch_1":0[fmt:SRGGB10_1X10/1920x1080]'
media-ctl -d /dev/media0 --set-v4l2 '"des_ch_2":0[fmt:SRGGB10_1X10/1920x1080]'
media-ctl -d /dev/media0 --set-v4l2 '"des_ch_3":0[fmt:SRGGB10_1X10/1920x1080]'
```

### nvargus-daemon

The nvargus-daemon prevents [QV4L2](#qv4l2) from working, but is needed for the [Argus](#argus) sample.

To stop it use the following command.

`sudo systemctl stop nvargus-daemon.service`

You can also disable it permanently by using the following command.

`sudo systemctl disable nvargus-daemon.service`

To enable it again, use the following command.

`sudo systemctl enable nvargus-daemon.service`

And to start it again, use the following command.

`sudo systemctl start nvargus-daemon.service`

### QV4L2

To test the cameras using qv4l2, you need to install the `qv4l2` package using the following command.

`sudo apt install qv4l2`

To open the camera `0` in the `qv4l2` app, run the following command.

`qv4l2 -d 0`

To open 4 cameras at the same time, run the following command.

`qv4l2 -d 0 & qv4l2 -d 1 & qv4l2 -d 2 & qv4l2 -d 3 &`

The same applies for other cameras.

### Argus

To test the cameras using Argus, you need to clone and build the `jetson_multimedia_api` project.

First, install the necessary dependencies.

`sudo apt install build-essential libgtk-3-dev`

Make sure that you have included the CUDA Toolkit when flashing the board, and that the [`nvargus-daemon`](#nvargus-daemon) is turned on.

Then, clone and build the project using the following commands.

Here is a table with the branch you need to clone depending on your release tag.

| Release Tag   	| Branch                                                      	|
|---------------	| ------------------------------------------------------------	|
| jetson_35.3.1 	| [gmsl-userspace/jetson_35.3.1/jetson_multimedia_api][jma-0] 	|

[jma-0]: https://github.com/analogdevicesinc/nvidia/tree/gmsl-userspace/jetson_35.3.1/jetson_multimedia_api

```
git clone https://github.com/analogdevicesinc/nvidia --single-branch -b gmsl-userspace/jetson_35.3.1/jetson_multimedia_api jetson_multimedia_api
cd ./jetson_multimedia_api
cd ./argus
cmake .
cd ./samples/cudaBayerDemosaic
make
```

To run the Argus sample using 4 cameras, on a window of 1920x1080 size, run the following command.

`./argus_cudabayerdemosaic -n 4 -r 0,0,1920,1080`

### CSI Camera (Argus + OpenCV)

To test the cameras using CSI Camera, you need to clone the `csi-camera` project.

Here is a table with the branch you need to clone depending on your release tag.

| Release Tag   	| Branch                                                      	|
|---------------	| ------------------------------------------------------------	|
| jetson_35.3.1 	| [gmsl-userspace/jetson_35.3.1/csi-camera][cc-0] 	|

[cc-0]: https://github.com/analogdevicesinc/nvidia/tree/gmsl-userspace/jetson_35.3.1/csi-camera

Then, move into the `csi-camera` directory.

`cd csi-camera`

To run the CSI Camera app using camera 0, run the following command.

`python simple-camera.py 0`

#!/usr/bin/env bash

media-ctl -d /dev/media0 --set-v4l2 '"max96717:0 10-0040":1[fmt:SRGGB10_1X10/1920x1280]'
media-ctl -d /dev/media0 --set-v4l2 '"max96717:0 12-0040":1[fmt:SRGGB10_1X10/1920x1280]'
media-ctl -d /dev/media0 --set-v4l2 '"max96717:0 14-0040":1[fmt:SRGGB10_1X10/1920x1280]'
media-ctl -d /dev/media0 --set-v4l2 '"max96717:0 16-0040":1[fmt:SRGGB10_1X10/1920x1280]'

media-ctl -d /dev/media0 --set-v4l2 '"max96724:0 9-0027":0[fmt:SRGGB10_1X10/1920x1280]'
media-ctl -d /dev/media0 --set-v4l2 '"max96724:1 9-0027":0[fmt:SRGGB10_1X10/1920x1280]'
media-ctl -d /dev/media0 --set-v4l2 '"max96724:2 9-0027":0[fmt:SRGGB10_1X10/1920x1280]'
media-ctl -d /dev/media0 --set-v4l2 '"max96724:3 9-0027":0[fmt:SRGGB10_1X10/1920x1280]'

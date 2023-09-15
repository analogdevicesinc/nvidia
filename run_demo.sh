#!/usr/bin/env bash

~/CSI-Camera/set_cfg_imx219_cam0.sh

python ~/CSI-Camera/simple_camera.py 0 &
python ~/CSI-Camera/simple_camera.py 1 &
python ~/CSI-Camera/simple_camera.py 2 &
python ~/CSI-Camera/simple_camera.py 3 &

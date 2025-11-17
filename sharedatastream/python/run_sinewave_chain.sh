#!/bin/bash
set -e

# 启动正弦波生产者
python3.12 test_sinewave_output.py &
PID_OUTPUT=$!
echo "Started test_sinewave_output.py (PID $PID_OUTPUT)"

# 启动第一个OpenCV可视化（原始波形）
sleep 1
python3.12 test_sinewave_opencv.py &
PID_OPENCV1=$!
echo "Started test_sinewave_opencv.py (PID $PID_OPENCV1)"

# 启动幅度减半处理
sleep 1
python3.12 test_sinewave_halfamp.py &
PID_HALFAMP=$!
echo "Started test_sinewave_halfamp.py (PID $PID_HALFAMP)"

# 启动第二个OpenCV可视化（幅度减半后波形）
sleep 1
python3.12 test_sinewave_opencv.py RingQueueSineWaveHalfAmp 1024 12800 1 50 &
PID_OPENCV2=$!
echo "Started test_sinewave_opencv.py (HalfAmp) (PID $PID_OPENCV2)"

wait 
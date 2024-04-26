#!/bin/bash

echo "start remove uImage from android folder..."
rm ../android/device/nufront/nusmart7npsc/uImage
rm ../android/device/nufront/nusmart7npsc/uImage_recovery

echo "copy uImaqge to android folder..."
cp arch/arm/boot/uImage ../android/device/nufront/nusmart7npsc/uImage
cp arch/arm/boot/uImage ../android/device/nufront/nusmart7npsc/uImage_recovery

echo "end..."

#!/bin/bash

if [ -e zImage ]; then
	rm zImage
fi

rm compile.log

# Set Default Path
TOP_DIR=$PWD
KERNEL_PATH="/home/sarthak/Documents/xxKernelICS"

# Set toolchain and root filesystem path
#TOOLCHAIN="/media/Main_Storage/android_toolchains/arm-eabi-linaro-4.6.2/bin/arm-eabi-"
# ROOTFS_PATH="/home/sarthak/Documents/xxICSInitramfs"

export KERNELDIR=$KERNEL_PATH

export USE_SEC_FIPS_MODE=true

make -j80
./strip.sh

# Copying kernel modules
find -name '*.ko' -exec cp -av {} $ROOTFS_PATH/lib/modules/ \;
make -j80

# Copy Kernel Image
rm -f $KERNEL_PATH/releasetools/zip/$KBUILD_BUILD_VERSION.zip
cp -f $KERNEL_PATH/arch/arm/boot/zImage .
cp -f $KERNEL_PATH/arch/arm/boot/zImage $KERNEL_PATH/releasetools/zip

cd arch/arm/boot
tar cf $KERNEL_PATH/arch/arm/boot/$KBUILD_BUILD_VERSION.tar ../../../zImage && ls -lh $KBUILD_BUILD_VERSION.tar

cd ../../..
cd releasetools/zip
zip -r $KBUILD_BUILD_VERSION.zip *

cp $KERNEL_PATH/arch/arm/boot/$KBUILD_BUILD_VERSION.tar $KERNEL_PATH/releasetools/tar/$KBUILD_BUILD_VERSION.tar
rm $KERNEL_PATH/arch/arm/boot/$KBUILD_BUILD_VERSION.tar
rm $KERNEL_PATH/releasetools/zip/zImage


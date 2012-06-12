#Change to corresponding path to the binary of your toolchain!

echo " ---------  CRANIUM KERNEL MODULE STRIPPER FOR ARM-EABI-4.4.3 ---------- "

for i in $(find . | grep .ko | grep './')
do
        echo $i
/media/Main_Storage/android_toolchains/arm-eabi-linaro-4.6.2/bin/arm-eabi-strip --strip-unneeded $i

done

echo " --------  MODULES STRIPPED! --------- "




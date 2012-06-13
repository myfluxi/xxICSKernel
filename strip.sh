#Change to corresponding path to the binary of your toolchain!

echo " ---------  SHEDDING BLOAT FROM MODULES...NOW ---------- "

for i in $(find . | grep .ko | grep './')
do
        echo $i
/media/Main_Storage/android_toolchains/arm-eabi-linaro-4.6.2/bin/arm-eabi-strip --strip-unneeded $i

done

echo " --------  MODULES STRIPPED! --------- "




#!/bin/bash

KERNEL_ROOT=$(cd $(dirname $0);pwd)      #kernel code的根目录
CONFIG_FILE=""                           #kernel配置文件
DTB_FILE=""                           #dtb file
MAKE_TARGET=""                           #编译的平台
#KERNEL_OUT=${KERNEL_ROOT}/../../out/
KERNEL_OUT=${KERNEL_ROOT}/../out/

F_CLEAN=0
F_TARGET=0

function show_help() {
	echo "
usage :
	build.sh [-c] [-i] [-t <target>] [-h]
	-h:		show help.
	-c:		clear project.
	-i:		initialize code use repo/git tools.
	-t <target>:	target type:
			full_nusmart7tl_phone        --->tl7790_phone_test_defconfig
"
}

function LOGE() {
	echo "[ERROR]: $1"
	exit 1
}

function LOGI() {
	echo "[INFO]: $1"
}

function select_platform() {
	ARG=$1

	local android
	[[ "$SDK_ANDROID_VERSION" = "4.4" ]] && android="_android4.4"

    #full_nusmart7npsc is for project npsc01 android, full_npsc_printer is for project npsc01 linux
    if [ $1 = "full_nusmart7npsc" -o $1 = "full_npsc_printer" ]; then
        MAKE_TARGET=$ARG
        CONFIG_FILE=nufront_npsc01_vm3_defconfig
        if [[ "$BOARDTYPE" = "vm3" ]]; then
            DTB_FILE=nufront-npsc01-vm3
        fi
    else
        LOGE "compile kernel fail, not found '$1' configure."
    fi
}

function select_kernel_version() {
	echo
}

function INIT_CODE_CMD() {
	#初始化code.
	repo init -u ssh://zhengxing@192.168.2.17:29418/platform/manifest.git -b  linux_kernel.ns115_pm
	repo sync
	repo start --all linux_kernel.ns115_pm
}

function check_kernel_out_dir() {
	if [[ ! -d $1 ]]; then
		mkdir -p $1
	fi
}

function compile_kernel() {
	LOGI "======================================================="
	LOGI "compile kernel......"
	LOGI "======================================================="
	LOGI "target is $MAKE_TARGET ......"
	LOGI "config is $CONFIG_FILE ......"
	make_uImage
	KERNEL_OUT=${KERNEL_ROOT}/../out/$MAKE_TARGET
	check_kernel_out_dir ${KERNEL_OUT}
	LOGI "cp ${KERNEL_ROOT}/arch/arm/boot/uImage ${KERNEL_OUT}"
	cp ${KERNEL_ROOT}/arch/arm/boot/uImage ${KERNEL_OUT}
	if [[ $? != 0 ]]; then
		LOGE "copy uImage fail..."
	fi
	KERNEL_OUT_OBJ=$KERNEL_OUT/obj/
	LOGI "cp ${KERNEL_ROOT}/vmlinux ${KERNEL_OUT_OBJ}"
	cp ${KERNEL_ROOT}/vmlinux ${KERNEL_OUT_OBJ}
	if [[ $? != 0 ]]; then
		LOGE "copy vmlinux fail..."
	fi

	check_kernel_out_dir ${KERNEL_OUT}/modules

	for ko in $(find ${KERNEL_ROOT} -name "*.ko")
	do
		LOGI "cp -f $ko ${KERNEL_OUT}/modules/"
		cp -f $ko ${KERNEL_OUT}/modules/
	done

	# use the previous uImage as recovery uImage
	LOGI "use the previous uImage as recovery uImage on npsc platform"
	LOGI "cp ${KERNEL_ROOT}/arch/arm/boot/uImage ${KERNEL_OUT}/uImage_recovery"
	cp ${KERNEL_ROOT}/arch/arm/boot/uImage ${KERNEL_OUT}/uImage_recovery

	if [[ $? != 0 ]]; then
		LOGE "copy uImage_recovery fail..."
	fi
}

function make_main() {
	check_kernel_out_dir ${KERNEL_OUT}
	if [ $F_CLEAN -eq 1 -a $F_TARGET -ne 1 ]; then
		echo "make clean kernel......"
		P1=$(pwd)
		cd $KERNEL_ROOT
		make clean
		cd $P1
		rm -fr ${KERNEL_ROOT}/.config
	elif [ $F_TARGET -eq 1 -a $F_CLEAN -ne 1 ]; then
		compile_kernel
	elif [ $F_TARGET -eq 1 -a $F_CLEAN -eq 1 ]; then
		make_clean
	fi
}

function make_uImage() {
	P1=`pwd`
	cd ${KERNEL_ROOT}
	if [[ $CONFIG_FILE = "nufront_npsc01_vm3_defconfig" ]]; then
		echo "========================================================="
		echo " compile the root uImage		"
		echo "========================================================="
		make ${CONFIG_FILE} || LOGE "make uImage config fail..."
		echo CONFIG_NUFRONT_ROOT=y >> .config
		make -j4 uImage DTB=${DTB_FILE}     || LOGE "make uImage fail..."
		make -j8 modules    || LOGE "make modules fail..."
		make dtbs	    || LOGE "make dtbs fail..."
		cp arch/arm/boot/uImage $KERNEL_OUT/$MAKE_TARGET/uImage_root

		echo "========================================================="
		echo "compile the  uImage		"
		echo "========================================================="
		make ${CONFIG_FILE} || LOGE "make uImage config fail..."
		make -j4 uImage DTB=${DTB_FILE}     || LOGE "make uImage fail..."
		make -j8 modules    || LOGE "make modules fail..."
		make dtbs
		cp arch/arm/boot/uImage $KERNEL_OUT/$MAKE_TARGET/uImage
	else
		make ${CONFIG_FILE} || LOGE "make uImage config fail..."
		make -j4 uImage     || LOGE "make uImage fail..."
		make -j8 modules    || LOGE "make modules fail..."
		make dtbs
	fi
	cd $P1
}

function make_uImage_recovery() {
	p1=`pwd`
	cd ${KERNEL_ROOT}
	make ${CONFIG_FILE}_recovery || LOGE "make recovery uImage config fail..."
	make -j4 uImage              || LOGE "make recovery uImage fail..."
	cd $P1
}

function make_clean() {
	KERNEL_OUT=${KERNEL_ROOT}/../out/$MAKE_TARGET
	if [[ ! -d $KERNEL_OUT ]]; then
		echo "$KERNEL_OUT is empty......"
	else
		echo "clean ${KERNEL_OUT}/uImage......"
		rm -fr ${KERNEL_OUT}/uImage

		echo "clean ${KERNEL_OUT}/uImage_recovery"
		rm -fr ${KERNEL_OUT}/uImage_recovery

		echo "clean ${KERNEL_OUT}/modules/*.ko......"
		rm -fr ${KERNEL_OUT}/modules/*

		echo "clean ${KERNEL_OUT}/obj/vmlinux......"
		rm -fr ${KERNEL_OUT}/obj/vmlinux
	fi
	P1=$(pwd)
	cd $KERNEL_ROOT
	echo "make clean kernel......"
	make clean
	cd $P1
	rm -fr ${KERNEL_ROOT}/.config
	make distclean
}

function custom_modules() {
    make distclean
    modules_out="${KERNEL_ROOT}/../out/${MAKE_TARGET}/modules"
	uImage_out="${KERNEL_ROOT}/../out/${MAKE_TARGET}/"
    if [ ! -d $modules_out ]; then
        mkdir -p $modules_out
    else
        rm -f $modules_out/*
    fi
    # make config
    make ${CONFIG_FILE} || exit 1
    # modify config
    custom_y2m
    # build module
    make -j16 uImage || exit 1
    make modules || exit 1

    # copy ko files to modules
    for ko in $(find ${KERNEL_ROOT} -name "*.ko")
	do
		LOGI "cp -f $ko $modules_out"
		cp -f $ko $modules_out
	done
		
	LOGI "cp -f ${KERNEL_ROOT}/arch/arm/boot/uImage $uImage_out"
	cp -f ${KERNEL_ROOT}/arch/arm/boot/uImage $uImage_out

	LOGI "cp -f ${KERNEL_ROOT}/arch/arm/boot/uImage $uImage_out/uImage_recovery"
	cp -f ${KERNEL_ROOT}/arch/arm/boot/uImage $uImage_out/uImage_recovery

	# delete source code, Kconfig, Makefile, CONFIGs.
	custom_delete

	# copy config file back
	make savedefconfig
	cp -f defconfig ./arch/arm/configs/${CONFIG_FILE}
	rm defconfig
}

function custom_y2m() {
    sed -i -e 's/^\(CONFIG\(_[^_]\+\)*_VPU_ENC\)=y/\1=m/g'     \
           -e 's/^\(CONFIG\(_[^_]\+\)*_VPU_DEC\)=y/\1=m/g'     \
           -e 's/^\(CONFIG\(_[^_]\+\)*_MEMALLOC\)=y/\1=m/g'    \
           -e 's/^\(CONFIG\(_[^_]\+\)*_VIVANTE_GAL\)=y/\1=m/g'       \
           -e 's/^\(CONFIG_MALI400\)=y/\1=m/g'     \
           -e 's/^\(CONFIG_UMP\)=y/\1=m/g' .config
}

function custom_delete() {
    ## on2
    #remove on2 source file
    rm ./drivers/char/hx170dec*
    rm ./drivers/char/memalloc*
    rm ./drivers/char/hx280enc*
    #delete on2 Kconfig
    sed -i -e '/^config \([^_]\+_\)*VPU_ENC/,+3d'  \
           -e '/^config \([^_]\+_\)*VPU_DEC/,+3d'  \
           -e '/^config \([^_]\+_\)*MEMALLOC/,+3d' ./drivers/char/Kconfig
    #delete on2 makefile
    sed -i -e '/CONFIG\(_[^_]\+\)*_VPU_ENC/d'  \
           -e '/CONFIG\(_[^_]\+\)*_VPU_DEC/d'  \
           -e '/CONFIG\(_[^_]\+\)*_MEMALLOC/d' ./drivers/char/Makefile

    ## mali400
    #remove mali400 source file
    rm -rf ./drivers/gpu/arm
    #delete mali400 Kconfig
    sed -i '/gpu\/arm/d' ./drivers/video/Kconfig
    #delete mali400 makefile
    sed -i 's/arm\///g' ./drivers/gpu/Makefile

    ## gal
    #remove gal source file
    rm -rf ./drivers/gpu/gal
    #delete gal Kconfig
    sed -i '/gal/d' ./drivers/video/Kconfig
    #delete gal makefile
    sed -i 's/gal\///g' ./drivers/gpu/Makefile

    sed -i -e '/CONFIG\(_[^_]\+\)*_VPU_ENC/d'     \
           -e '/CONFIG\(_[^_]\+\)*_VPU_DEC/d'     \
           -e '/CONFIG\(_[^_]\+\)*_MEMALLOC/d'    \
           -e '/CONFIG\(_[^_]\+\)*_VIVANTE_GAL/d'       \
           -e '/CONFIG_MALI/d'     \
           -e '/CONFIG_UMP/d' \
           -e '/CONFIG_UMP_DEBUG/d'               .config

}

###################################################

if [[ $# == 0 ]]; then
	show_help;
	exit 0;
fi

while getopts kt:b:chiM OPT;
do
	case $OPT in
	b)
		BOARDTYPE=$OPTARG
		;;
	t)
		select_platform $OPTARG
		F_TARGET=1
		;;
	k)
		select_kernel_version  $OPTARG
		;;
	c)
		F_CLEAN=1
		;;
	h)
		show_help
		exit 0
		;;
	i)
		#INIT_CODE_CMD
		exit 0
		;;
	M)
	    custom_modules || exit 1
	    exit 0
	    ;;
	*)
		show_help
		exit 1
		;;
	esac
done

make_main

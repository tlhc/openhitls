#!/bin/bash

# This file is part of the openHiTLS project.
#
# openHiTLS is licensed under the Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#
#     http://license.coscl.org.cn/MulanPSL2
#
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
set -e
cd ../../
HITLS_ROOT_DIR=`pwd`

hitls_compile_option=()

paramList=$@
paramNum=$#
add_options=""
del_options=""
get_arch=`arch`

LIB_TYPE="static"
enable_sctp="--enable-sctp"
BITS=64

clean()
{
    rm -rf ${HITLS_ROOT_DIR}/build
    mkdir ${HITLS_ROOT_DIR}/build
}

down_depend_code()
{
    if [ ! -d "${HITLS_ROOT_DIR}/platform" ]; then
        cd ${HITLS_ROOT_DIR}
        mkdir platform
    fi

    if [ ! -d "${HITLS_ROOT_DIR}/platform/Secure_C" ]; then
        cd ${HITLS_ROOT_DIR}/platform
        git clone https://gitee.com/openeuler/libboundscheck.git  Secure_C
    fi
}

build_depend_code()
{
    if [ ! -d "${HITLS_ROOT_DIR}/platform/Secure_C/lib" ]; then
        mkdir -p ${HITLS_ROOT_DIR}/platform/Secure_C/lib
        cd ${HITLS_ROOT_DIR}/platform/Secure_C
        make -j
    fi
}

build_hitls_code()
{
    bsl_features="err hash init list log sal sal_mem sal_thread sal_lock sal_time sal_file sal_net sal_str tlv \
                  uio_plt uio_buffer uio_sctp uio_tcp usrdata asn1 obj base64 pem"

    # Compile openHiTLS
    cd ${HITLS_ROOT_DIR}/build
    add_options="${add_options} -DHITLS_EAL_INIT_OPTS=1 -DHITLS_CRYPTO_ASM_CHECK" # Get CPU capability
    python3 ../configure.py --enable ${bsl_features} hitls_crypto hitls_tls hitls_x509 --bits=$BITS --system=linux ${enable_sctp}
    if [[ $get_arch = "x86_64" ]]; then
        echo "Compile: env=x86_64, c, little endian, 64bits"
        python3 ../configure.py --lib_type ${LIB_TYPE} --asm_type x8664 --add_options="$add_options" --del_options="$del_options" ${enable_sctp}
    elif [[ $get_arch = "armv8_be" ]]; then
        echo "Compile: env=armv8, asm + c, big endian, 64bits"
        python3 ../configure.py --lib_type ${LIB_TYPE} --endian big --asm_type armv8 --add_options="$add_options" --del_options="$del_options" ${enable_sctp}
    elif [[ $get_arch = "armv8_le" ]]; then
        echo "Compile: env=armv8, asm + c, little endian, 64bits"
        python3 ../configure.py --lib_type ${LIB_TYPE} --asm_type armv8 --add_options="$add_options" --del_options="$del_options" ${enable_sctp}
    else
        echo "Compile: env=$get_arch, c, little endian, 64bits"
        python3 ../configure.py --lib_type ${LIB_TYPE} --add_options="$add_options" --del_options="$del_options" ${enable_sctp}
    fi
    cmake ..
    make -j
}

parse_option()
{
    for i in $paramList
    do
        key=${i%%=*}
        value=${i#*=}
        case "${key}" in
            "gcov")
                add_options="${add_options} -fno-omit-frame-pointer -fprofile-arcs -ftest-coverage -fdump-rtl-expand"
                ;;
            "debug")
                add_options="${add_options} -O0 -g3 -gdwarf-2"
                del_options="${del_options} -O2 -D_FORTIFY_SOURCE=2"
                ;;
            "asan")
                add_options="${add_options} -fsanitize=address -fsanitize-address-use-after-scope -O0 -g3 -fno-stack-protector -fno-omit-frame-pointer -fgnu89-inline"
                del_options="${del_options} -fstack-protector-strong -fomit-frame-pointer -O2 -D_FORTIFY_SOURCE=2"
                ;;
            "armv8_be")
                get_arch="armv8_be"
                ;;
            "armv8_le")
                get_arch="armv8_le"
                ;;
            "pure_c")
                get_arch="C"
                ;;
            "no_sctp")
                enable_sctp=""
                ;;
            "bits")
                BITS="$value"
                ;;
            "shared")
                LIB_TYPE="shared"
                ;;
            "libfuzzer")
                add_options="${add_options} -fsanitize=fuzzer-no-link -fsanitize=signed-integer-overflow -fsanitize-coverage=trace-cmp"
                del_options="${del_options} -Wtrampolines -O2 -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fomit-frame-pointer -fdump-rtl-expand"
                export ASAN_OPTIONS=detect_stack_use_after_return=1:strict_string_checks=1:detect_leaks=1:log_path=asan.log
                export CC=clang
                ;;
            "help")
                printf "%-50s %-30s\n" "Build openHiTLS Code"                      "sh build_hitls.sh"
                printf "%-50s %-30s\n" "Build openHiTLS Code With Gcov"            "sh build_hitls.sh gcov"
                printf "%-50s %-30s\n" "Build openHiTLS Code With Debug"           "sh build_hitls.sh debug"
                printf "%-50s %-30s\n" "Build openHiTLS Code With Asan"            "sh build_hitls.sh asan"
                exit 0
                ;;
            *)
                echo "${i} option is not recognized, Please run <sh build_hitls.sh> get supported options."
                exit -1
                ;;
        esac
    done
}

clean
parse_option
down_depend_code
build_depend_code
build_hitls_code

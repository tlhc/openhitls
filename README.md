[简体中文](./README-zh.md) | English

# openHiTLS
Welcome to visit the openHiTLS Code Repository, which is under the openHiTLS community: <https://openhitls.net>. openHiTLS aims to provide highly efficient and agile open-source SDKs for Cryptography and Transport Layer Security in all scenarios. openHiTLS is developing and supports some common standard cryptographic algorithms, (D)TLS, TLCP protocols currently. More features are to be planned.

## Overview

The architecture of openHiTLS is highly modular, and openHiTLS can be configured in modules and features. The RAM/ROM footprint depends on the features selected. It provides the optimal performance optimization for cryptographic algorithms. Currently, 4 components and cryptographic algorithms are configured, and the performance optimization of ShangMi cryptographic algorithms on ARM, x86 is ready. More architectures and features are to be planned.

## Feature Introduction

1. Functional feature: TLS1.2, TLS1.3, DTLS1.2. TLCP; AES, SM4, Chacha20, RSA, ECDSA, ECDH, SM2, DRBG, HKDF, SCRYPT, PBKDF2, SHA2, SHA3, MD5, SM3, HMAC; PKI
2. DFX feature: highly modular with features configured, performance optimization on ARM, x86 maintainability and testability with logs and error stacks.

## Component Introduction

openHiTLS include 4 components currently. The BSL component will be used with other components.
- The bsl is short for Base Support Layer, which provides the base C standand enhanced functions and OS adapter. It will be used with other modules
- The crypto is short for cryptographic algorithms, which provides the full cryptographic functions with high performance. It will be used by tls, and can also be used with bsl
- The tls is short for Transport Layer Security, which provides all tls protocol versions up to tls1.3. It will be used with crypto and bsl or other third-party crypto and pki libraries
- The pki component currently provides basic functions that can support TLS to work, and will gradually improve the functions in the future

## Development

### Dependency Preparation

openHiTLS depends on Secure C which should be downloaded to ${openHiTLS_dir}/platform/Secure_C. One of the official git repositories of Secure C is located at <https://gitee.com/openeuler/libboundscheck>.

* Download the security library

```bash
# Method 1: Pull it with the openHiTLS code repository
git clone --recurse-submodules https://gitcode.com/openhitls/openhitls.git

# Method 2: Pull the security library separately
git clone https://gitcode.com/openhitls/openhitls.git
cd ${openHiTLS_dir} 
git clone https://gitee.com/openeuler/libboundscheck platform/Secure_C
```

* Build security library
```bash
cd ${openHiTLS_dir}/platform/Secure_C
make -j
```

### For Application Developers

Source code mirroring of the official releases is pending for planning.


The official source code repository is located at <https://gitcode.com/openhitls>. A local copy of the git repository can be obtained by cloning it using:
```
git clone https://gitcode.com/openhitls/openhitls.git
```
If you are going to contribute, you need to fork the openhitls repository on gitee and clone your public fork instead:
```
git clone https://gitcode.com/"your gitcode name"/openhitls.git
```

## Document
This document is designed to improve the learning efficiency of developers and contributors on openHiTLS. Refer to the [docs](docs/index/index.md).

## Build and Installation
The major steps in Linux are as follows. Refer to [build & install](docs/en/4_User%20Guide/1_Build%20and%20Installation%20Guide.md)
The major steps in Linux:

Step 1 (Prepare the build directory):
```
cd openHiTLS && mkdir -p ./build && cd ./build
```
Step 2 (Generate configurations):
```
python3 ../configure.py ["option"]
```

* C Full build:
```
python3 ../configure.py --enable hitls_bsl hitls_crypto hitls_tls hitls_pki --lib_type static --bits=64 --system=linux
```

* x8664 Optimize the full build：
```
python3 ../configure.py --enable hitls_bsl hitls_crypto hitls_tls hitls_pki --lib_type static --bits=64 --system=linux --asm_type x8664
```
The options are described in [Build Installation Guide](docs/en/4_User%20Guide/1_Build%20and%20Installation%20Guide.md)

Step 3 (Generate the build script):
```
cmake ..
```
Step 4 (Build and install):
```
make && make install
```

## Contribution

If you plan to contribute to the openHiTLS community, please visit the link [CLA Signing](https://cla.openhitls.net)  to complete CLA signing.

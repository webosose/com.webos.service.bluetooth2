# Copyright (c) 2014-2024 LG Electronics, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0


cmake_minimum_required(VERSION 2.8.7)

project(webos-bluetooth-service CXX)

include(webOS/webOS)
webos_modules_init(1 0 0 QUALIFIER RC7)
webos_component(1 0 0)

# Enable C++11 support (still gcc 4.6 so can't use -std=c++11)
webos_add_compiler_flags(ALL -std=c++11 -Wall)

include(FindPkgConfig)

pkg_check_modules(GLIB2 REQUIRED glib-2.0)
include_directories(${GLIB2_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${GLIB2_CFLAGS})

pkg_check_modules(GIO2 REQUIRED gio-2.0)
include_directories(${GIO2_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${GIO2_CFLAGS})

pkg_check_modules(GIO-UNIX REQUIRED gio-unix-2.0)
include_directories(${GIO-UNIX_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${GIO-UNIX_CFLAGS_OTHER})

pkg_check_modules(LUNASERVICE2 REQUIRED luna-service2)
include_directories(${LUNASERVICE2_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${LUNASERVICE2_CFLAGS})

pkg_check_modules(PBNJSON_CXX REQUIRED pbnjson_cpp)
include_directories(${PBNJSON_CXX_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${PBNJSON_CXX_CFLAGS_OTHER})

pkg_check_modules(PMLOG REQUIRED PmLogLib)
include_directories(${PMLOG_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${PMLOG_CFLAGS_OTHER})

if(MULTI_SESSION_SUPPORT)
    add_definitions(-DMULTI_SESSION_SUPPORT)
endif()

set(WEBOS_BLUETOOTH_SIL "mock" CACHE STRING "Bluetooth SIL implementation to use")
set(WEBOS_BLUETOOTH_SIL_BASE_PATH "${WEBOS_INSTALL_LIBDIR}/bluetooth-sils" CACHE STRING "Base path for SIL modules")

set(WEBOS_BLUETOOTH_ENABLED_SERVICE_CLASSES "" CACHE STRING "Bluetooth service classes for which to enable support")
set(WEBOS_BLUETOOTH_PAIRING_IO_CAPABILITY "NoInputNoOutput" CACHE STRING "Bluetooth device IO capability")
if(NOT WEBOS_BLUETOOTH_PAIRING_IO_CAPABILITY MATCHES "^(NoInputNoOutput|DisplayYesNo|DisplayOnly|KeyboardDisplay|KeyboardOnly)$")
   message(FATAL_ERROR "Unrecognized value of WEBOS_BLUETOOTH_PAIRING_IO_CAPABILITY: ${WEBOS_BLUETOOTH_PAIRING_IO_CAPABILITY}")
endif()

set(WEBOS_BLUETOOTH_DEVICE_NAME "LG RASPBERRYPI WEBOS3" CACHE STRING "Bluetooth friendly name")
set(BTMNGR_COMPATIBLE false)

add_definitions(-DWBS_LOCAL_SERVICE)

file(GLOB SOURCES src/*.cpp)

webos_configure_source_files(SOURCES src/config.h)
webos_add_compiler_flags(ALL -I${CMAKE_CURRENT_BINARY_DIR}/Configured/src)

webos_add_linker_options(ALL --no-undefined)

add_executable(webos-bluetooth-service ${SOURCES})

target_link_libraries(webos-bluetooth-service
    ${GLIB2_LDFLAGS} ${LUNASERVICE2_LDFLAGS} ${PBNJSON_CXX_LDFLAGS}
    ${GIO2_LDFLAGS} ${GIO-UNIX_LDFLAGS} ${PMLOG_LDFLAGS}
    rt pthread dl luna-service2++ ${EXT_LIBS})

webos_build_daemon()
webos_build_system_bus_files()
webos_build_db8_files()
webos_build_configured_file(files/conf/pmlog/webos-bluetooth-service.conf SYSCONFDIR pmlog.d)

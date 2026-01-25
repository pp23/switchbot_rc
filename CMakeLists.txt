# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

set(REQUIRED_ENV_VARS
			"WIFI_PASSWORD"
			"WIFI_SSID"
	)

set(_missing "")
message(NOTICE "Required environment variables:")
foreach(var IN LISTS REQUIRED_ENV_VARS)
	message(NOTICE " - ${var}")
	if(NOT DEFINED ENV{${var}} OR "$ENV{${var}}" STREQUAL "")
		list(APPEND _missing ${var})
	endif()
endforeach()

if(_missing)
		message(FATAL_ERROR
			"The following environment variables are missing or empty:\n"
			" ${_missing}\n\n"
			)
endif()


add_compile_definitions(CONFIG_WIFI_SSID=\"$ENV{WIFI_SSID}\")
add_compile_definitions(CONFIG_WIFI_PASSWORD=\"$ENV{WIFI_PASSWORD}\")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(switchbot_over_web)

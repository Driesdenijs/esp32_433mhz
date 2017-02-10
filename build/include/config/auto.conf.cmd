deps_config := \
	/home/dries/esp/esp-idf/components/bt/Kconfig \
	/home/dries/esp/esp-idf/components/esp32/Kconfig \
	/home/dries/esp/esp-idf/components/ethernet/Kconfig \
	/home/dries/esp/esp-idf/components/freertos/Kconfig \
	/home/dries/esp/esp-idf/components/log/Kconfig \
	/home/dries/esp/esp-idf/components/lwip/Kconfig \
	/home/dries/esp/esp-idf/components/mbedtls/Kconfig \
	/home/dries/esp/esp-idf/components/spi_flash/Kconfig \
	/home/dries/esp/esp-idf/components/bootloader/Kconfig.projbuild \
	/home/dries/esp/esp-idf/components/esptool_py/Kconfig.projbuild \
	/home/dries/esp/esp-idf/components/partition_table/Kconfig.projbuild \
	/home/dries/esp/esp-idf/Kconfig

include/config/auto.conf: \
	$(deps_config)


$(deps_config): ;

#!/bin/sh
esptool.py --chip esp32s3 ${PORT:+-p "$PORT"} -b 460800 --before default_reset --after hard_reset write_flash 0x0 "$(dirname "$0")/praan_aq_node_merged.bin"

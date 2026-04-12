#!/usr/bin/env python3
"""
Patch loragw_hal.c to handle missing temperature sensor gracefully.
Instead of failing with error, uses a fixed 25C default temperature.
"""

filepath = "/home/gabi/sx1302_hal/libloragw/src/loragw_hal.c"

with open(filepath, "r") as f:
    content = f.read()

# Patch 1: fatal error when no temp sensor found -> warning + continue with ts_fd = -1
old1 = (
    '        if (i == sizeof I2C_PORT_TEMP_SENSOR) {\n'
    '            printf("ERROR: no temperature sensor found.\\n");\n'
    '            return LGW_HAL_ERROR;\n'
    '        }'
)
new1 = (
    '        if (i == sizeof I2C_PORT_TEMP_SENSOR) {\n'
    '            printf("WARNING: no temperature sensor found, using default 25C.\\n");\n'
    '            ts_fd = -1;\n'
    '        }'
)

# Patch 2: lgw_get_temperature - return fixed 25C when ts_fd == -1 (no sensor)
old2 = (
    '        case LGW_COM_SPI:\n'
    '            err = stts751_get_temperature(ts_fd, ts_addr, temperature);\n'
    '            break;'
)
new2 = (
    '        case LGW_COM_SPI:\n'
    '            if (ts_fd >= 0) {\n'
    '                err = stts751_get_temperature(ts_fd, ts_addr, temperature);\n'
    '            } else {\n'
    '                *temperature = 25.0;\n'
    '                err = LGW_HAL_SUCCESS;\n'
    '            }\n'
    '            break;'
)

if old1 in content:
    content = content.replace(old1, new1)
    print("Patch 1 OK: temperature sensor fatal error -> warning")
else:
    print("ERROR: Patch 1 not found in file")

if old2 in content:
    content = content.replace(old2, new2)
    print("Patch 2 OK: lgw_get_temperature default 25C")
else:
    print("ERROR: Patch 2 not found in file")

with open(filepath, "w") as f:
    f.write(content)

print("File written.")

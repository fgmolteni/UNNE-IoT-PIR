#!/usr/bin/env python3
"""
Patch 3: guard ts_fd >= 0 before calling i2c_linuxdev_close(ts_fd)
Avoids error when no temperature sensor was found (ts_fd == -1).
"""

filepath = "/home/gabi/sx1302_hal/libloragw/src/loragw_hal.c"

with open(filepath, "r") as f:
    content = f.read()

old = (
    '        DEBUG_MSG("INFO: Closing I2C for temperature sensor\\n");\n'
    '        x = i2c_linuxdev_close(ts_fd);\n'
    '        if (x != 0) {\n'
    '            printf("ERROR: failed to close I2C temperature sensor device (err=%i)\\n", x);\n'
    '            err = LGW_HAL_ERROR;\n'
    '        }\n'
)
new = (
    '        if (ts_fd >= 0) {\n'
    '            DEBUG_MSG("INFO: Closing I2C for temperature sensor\\n");\n'
    '            x = i2c_linuxdev_close(ts_fd);\n'
    '            if (x != 0) {\n'
    '                printf("ERROR: failed to close I2C temperature sensor device (err=%i)\\n", x);\n'
    '                err = LGW_HAL_ERROR;\n'
    '            }\n'
    '        }\n'
)

if old in content:
    content = content.replace(old, new)
    print("Patch 3 OK: guard ts_fd >= 0 on close")
else:
    print("ERROR: Patch 3 not found in file")

with open(filepath, "w") as f:
    f.write(content)

print("Done")

#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

// The board's single I2C bus (SDA 16, SCL 15). Shared: the touch controller
// (0x38) uses it, and the ES8311 codec (0x18) sits on it too. The codec is
// not used in this project, so no device is ever added for it.
//
// Safe to call more than once; the bus is created on the first call.
esp_err_t i2c_bus_init(void);

// NULL until i2c_bus_init() succeeded
i2c_master_bus_handle_t i2c_bus_get(void);

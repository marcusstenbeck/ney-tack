// *****************************************************************************
// Module: https://learn.adafruit.com/adafruit-ltr-329-ltr-303/pinouts
// Datasheet: https://cdn-shop.adafruit.com/product-files/5610/LTR-303ALS-01-Lite-On-datasheet-140480318.pdf
// Adafruit Arduino driver: https://github.com/adafruit/Adafruit_LTR329_LTR303/blob/main/Adafruit_LTR329_LTR303.cpp
// Pico i2c examples: https://github.com/raspberrypi/pico-examples/blob/master/i2c/mcp9808_i2c/mcp9808_i2c.c
// Pico SDK PDF: https://datasheets.raspberrypi.com/pico/raspberry-pi-pico-c-sdk.pdf
// Shawn Hymel Pico Series: https://www.youtube.com/playlist?list=PLEBQazB0HUyQO6rJxKr2umPCgmfAU-cqR
// Shawn Hymel I2C Tutorial: https://www.youtube.com/watch?v=jS4q9VljmGQ
// *****************************************************************************
#ifndef LTR303_I2C_H
#define LTR303_I2C_H

#include <stdio.h>
#include "pico/stdlib.h"
#include "my_i2c.h"
#include "hardware/i2c.h"

// *****************************************************************************
// Definitions
// *****************************************************************************

#define I2C_PORT i2c1

#define LTR303_I2CADDR_DEFAULT 0x29    // I2C address
#define LTR303_REG_PART_ID 0x86        // Part id/revision register
#define LTR303_REG_MANU_ID 0x87        // Manufacturer ID register
#define LTR303_ALS_CTRL 0x80           // ALS control register
#define LTR303_STATUS 0x8C             // Status register
#define LTR303_CH1DATA 0x88            // Data for channel 1 (read all 4 bytes!)
#define LTR303_MEAS_RATE 0x85          // Integration time and data rate
#define LTR303_REG_INTERRUPT 0x8F      // Register to enable/configure int output
#define LTR303_REG_THRESHHIGH_LSB 0x97 // ALS 'high' threshold limit
#define LTR303_REG_THRESHLOW_LSB 0x99  // ALS 'low' threshold limit
#define LTR303_REG_INTPERSIST 0x9E     // Register for setting the IRQ persistance

#define LTR303_DEVICE_ID 0xA0
#define LTR303_MANUFACTURER_ID 0x05

#define LTR_ALS_CTRL_ALS_MODE_ACTIVE 0b00000001

// *****************************************************************************
// Global variables
// *****************************************************************************

// *****************************************************************************
// Function declarations
// *****************************************************************************

int ltr303_i2c_init();
int ltr303_i2c_reset();
int ltr303_i2c_enable();
int ltr303_i2c_has_new_data();
int ltr303_i2c_read_both_channels(uint16_t *ch0_value, uint16_t *ch1_value);

// *****************************************************************************
// Function definitions
// *****************************************************************************

int ltr303_i2c_init()
{
  i2c_init(I2C_PORT, 400 * 1000);

  gpio_set_function(26, GPIO_FUNC_I2C);
  gpio_set_function(27, GPIO_FUNC_I2C);
  gpio_pull_up(26);
  gpio_pull_up(27);

  // Buffer to store raw reads
  uint8_t data[4];

  // Read part ID to make sure we can communicate with the LTR303
  reg_read(I2C_PORT, LTR303_I2CADDR_DEFAULT, LTR303_REG_PART_ID, data, 1);

  if (data[0] != LTR303_DEVICE_ID)
  {
    return -1;
  }

  // Read manufacturer ID to make sure we can communicate with the LTR303
  reg_read(I2C_PORT, LTR303_I2CADDR_DEFAULT, LTR303_REG_MANU_ID, data, 1);

  if (data[0] != LTR303_MANUFACTURER_ID)
  {
    return -1;
  }

  // The data sheet says that the device will initially be in a 'powered down' state.
  // We need to set the device to active mode.
  if (ltr303_i2c_reset())
  {
    printf("Failed to reset LTR303\n");
    return -1;
  }

  if (ltr303_i2c_enable())
  {
    printf("Failed to enable LTR303\n");
    return -1;
  }

  return 0;
}

int ltr303_i2c_reset()
{
  uint8_t data[1];

  reg_read(I2C_PORT, LTR303_I2CADDR_DEFAULT, LTR303_ALS_CTRL, data, 1);

  data[0] |= (1 << 1); // reset

  reg_write(I2C_PORT, LTR303_I2CADDR_DEFAULT, LTR303_ALS_CTRL, &data[0], 1);

  // datasheet tells us to sleep for 10ms after reset
  sleep_ms(10);

  reg_read(I2C_PORT, LTR303_I2CADDR_DEFAULT, LTR303_ALS_CTRL, data, 1);

  if (data[0] != 0x00)
  {
    return -1;
  }

  return 0;
}

int ltr303_i2c_enable()
{
  uint8_t data[1];

  reg_read(I2C_PORT, LTR303_I2CADDR_DEFAULT, LTR303_ALS_CTRL, data, 1);

  data[0] |= (1 << 0); // active mode

  reg_write(I2C_PORT, LTR303_I2CADDR_DEFAULT, LTR303_ALS_CTRL, &data[0], 1);

  reg_read(I2C_PORT, LTR303_I2CADDR_DEFAULT, LTR303_ALS_CTRL, data, 1);

  if (!(data[0] & LTR_ALS_CTRL_ALS_MODE_ACTIVE))
  {
    return -1;
  }

  return 0;
}

// ch0 is Visible + IR
// ch1 is IR only
// get visible by subtracting ch1 from ch0
int ltr303_i2c_read_both_channels(uint16_t *ch0_value, uint16_t *ch1_value)
{
  uint8_t data[4];

  reg_read(I2C_PORT, LTR303_I2CADDR_DEFAULT, LTR303_CH1DATA, data, 4);
  *ch1_value = (data[1] << 8) | data[0];
  *ch0_value = (data[3] << 8) | data[2];

  reg_read(I2C_PORT, LTR303_I2CADDR_DEFAULT, LTR303_STATUS, data, 1);

  if (data[0] & 0x80)
  {
    return 1;
  }

  return 0;
}

int ltr303_i2c_has_new_data()
{
  uint8_t data[1];

  reg_read(I2C_PORT, LTR303_I2CADDR_DEFAULT, LTR303_STATUS, data, 1);

  if (data[0] & 0x04)
  {
    return 1;
  }

  return 0;
}

#endif

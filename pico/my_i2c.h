#ifndef MY_I2C_H
#define MY_I2C_H

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

int reg_write(
    i2c_inst_t *i2c,
    const uint addr,
    const uint8_t reg,
    uint8_t *buf,
    const uint8_t nbytes)
{
  int num_bytes_read = 0;

  // Q: why is the length of msg `nbytes + 1`?
  // A: because we need to start with the register address and
  //    then append the data to write to the register
  uint8_t msg[nbytes + 1];

  if (nbytes < 1)
  {
    // Technically, this is not an error, but we should
    // be writing one or mote bytes to a register.
    // We return 0 to indicate that no bytes were written.
    return 0;
  }

  // append register address to front of data packet
  msg[0] = reg;
  for (int i = 0; i < nbytes; i++)
  {
    msg[i + 1] = buf[i];
  }

  // Q: can we write to more than one register?
  // write data to register(s) over i2c
  i2c_write_blocking(i2c, addr, msg, nbytes + 1, false);

  return num_bytes_read;
}

int reg_read(
    i2c_inst_t *i2c,
    const uint addr,
    const uint8_t reg,
    uint8_t *buf,
    const uint8_t nbytes)
{
  int num_bytes_read = 0;

  if (nbytes < 1)
  {
    // If no bytes should be read we return 0
    // to indicate that no bytes were read
    return 0;
  }

  // Q: can we read more than one register?

  // prepping the device to read from a register
  i2c_write_blocking(i2c, addr, &reg, 1, true);
  // Read data from register over i2c
  num_bytes_read = i2c_read_blocking(i2c, addr, buf, nbytes, false);

  return num_bytes_read;
}

#endif
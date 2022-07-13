#include "RealTimeClock.h"

// Public methods
bool RealTimeClock::Initialize(device_t device_type, TwoWire &bus)
{
  wire = &bus;

  return configure(device_type);
}

bool RealTimeClock::Initialize(TwoWire &bus)
{
  wire = &bus;
  if (configure(device_t::DS13xx))
    return true;

  if (configure(device_t::MCP7941x))
    return true;

  if (configure(device_t::PCF85263))
    return true;

  return false;
}

void RealTimeClock::SetTime(time_t *time)
{
  tm time_tm = localtime(time);
  SetTime(time_tm);
}

void RealTimeClock::SetTime(tm *time)
{
  uint8_t clockHalt = 0;
  uint8_t osconEtc = 0;

  switch (device)
  {
  case device_t::PCF85263:
    uint8_t settings[3] = {0x01, 0xA4, 0x00};
    writeBlock(0x2E, 3, settings);
    // 0x2e Stop the clock
    // 0x2f STOP
    // Clear hundredths of seconds
    break;

  case device_t::DS13xx:
    stopClock();
    clockHalt = 0x80; // Clock halt to be kept enabled for now
    break;

  case device_t::MCP7941x:
    stopClock();
    osconEtc = read((uint8_t)0x03) & 0x38;
    break;

  default:
    return;
  }

  uint8_t reg = getRegister(timeFunc_t::TIME);
  uint8_t values[7];

  values[0] = uint2bcd(time->tm_sec) | clockHalt;
  values[1] = uint2bcd(time->tm_min);
  values[2] = uint2bcd(time->tm_hour);
  values[3] = uint2bcd(time->tm_mday);
  values[4] = device == device_t::PCF85263 ? uint2bcd(time->tm_wday) : uint2bcd(time->tm_wday + 1);
  values[5] = uint2bcd(time->tm_mon + 1);
  values[6] = uint2bcd(time->tm_year % 100);

  writeBlock(reg, 7, values);

  startClock();
}

void RealTimeClock::GetTime(time_t *time)
{
  tm time_tm = tm();
  GetTime(&time_tm);
  *time = mktime(&time_tm);
}

void RealTimeClock::GetTime(tm *time)
{
  uint8_t values[7] = {};

  // Find which register to read from
  uint8_t reg = getRegister(timeFunc_t::TIME);
  read(reg, 7, values);

  time->tm_sec = bcd2uint(values[0] & 0x7f);
  time->tm_min = bcd2uint(values[1] & 0x7f);

  if (values[2] & 0x40)
  {
    // Twelve hour mode
    time->tm_hour = bcd2uint(values[2] & 0x1f);
    if (values[2] & 0x20)
      time->tm_hour += 12; // Seems notation for AM/PM is user-defined
  }
  else
    time->tm_hour = bcd2uint(values[2] & 0x3f);

  if (device == device_t::PCF85263)
  {
    // Day of month is before day of week!
    time->tm_mday = bcd2uint(values[3] & 0x3f);
    time->tm_wday = (values[4] & 0x07); // Clock uses [0..6]
  }
  else
  {
    time->tm_wday = (values[3] & 0x07) - 1; // Clock uses [1..7]
    time->tm_mday = bcd2uint(values[4] & 0x3f);
  }

  time->tm_mon = bcd2uint(values[5] & 0x1f) - 1; // Clock uses [1..12]

  time->tm_year = bcd2uint(values[6]) + 100; // Assume 21st century
  time->tm_yday = -1;
}

bool RealTimeClock::GetClock(tm *time, timeFunc_t func)
{

  if (device != device_t::MCP7941x && func != timeFunc_t::TIME)
    return false; // not supported

  // Find which register to read from
  uint8_t reg = getRegister(func);

  if (func == timeFunc_t::TIME)
    GetTime(time);

  else if (device == MCP7941x &&
           (func == TIME_POWER_FAILED || func == TIME_POWER_RESTORED))
    readMCPTimeSaver(time, reg);

  else if (device == MCP7941x &&
           (func == ALARM0 || func == ALARM1))
    return false; // TODO: Find out what should be done here

  return true;
}

// Private methods

bool RealTimeClock::configure(device_t device_type)
{
  if (!checkDevice(device_type))
    return false;

  switch (device)
  {
  case device_t::PCF85263:
    uint8_t values[8] = {0x00, 0x00, 0x12, 0x00, 0x00, 0x07, 0x00, 0x00};
    writeBlock(0x23, 8, values);
    break;

  case device_t::MCP7941x:
    enableBatteryBackup();
    write(0x08, 0x00);
    break;

  case device_t::DS13xx:
    // do nothing
    break;
  default:
    return;
  }
  startClock();

  return true;
}

bool RealTimeClock::resetClock()
{
  if (device == device_t::PCF85263)
    write(0x2F, 0x2C);
}

void RealTimeClock::startClock() const
{
  switch (device)
  {
  case device_t::PCF85263:
    write(address, 0x00);
    return;

  case device_t::MCP7941x:
    uint8_t data = read(address, 0x00);
    if ((data & 0x80) == 0)
      write(address, data | 0x80); // Enable start bit
    break;

  case device_t::DS13xx:
    uint8_t data = read(address, 0x00);
    if (data & 0x80)
      write(address, data & 0x7F); // Clear clock halt
    break;

  default:
    break;
  }
}

void RealTimeClock::stopClock() const
{
  switch (device)
  {
  case device_t::PCF85263:
    write(0x2E, 0x01);
    break;

  case device_t::DS13xx:
    uint8_t data = read(address, 0x00);
    write(address, data | 0x80);
    break;

  case device_t::MCP7941x:
    uint8_t data = read(address, 0x00);
    write(address, data & 0x7F);
    break;

  default:
    break;
  }
}

void RealTimeClock::enableBatteryBackup(bool enable) const
{
  if (device != device_t::MCP7941x)
    return;

  uint8_t data = read(0x03);
  if (bool(data & 0x08) == enable)
    return;

  stopClock();

  data = enable ? d | 0x08 : d & 0xF7;
  write(0x03, data);
  startClock();
}

uint8_t RealTimeClock::getRegister(timeFunc_t func) const
{
  const uint8_t regTable[3][5] = {
      {0, 0xff, 0xff, 0xff, 0xff}, // DS1307
      {0, 0x0a, 0x11, 0x18, 0x1C}, // MCP7941x
      {1, 0x08, 0x0d, 0xff, 0xff}, // PCF85263

  };
  return regTable[device][func];
}

void RealTimeClock::readMCPTimeSaver(struct tm *time, uint8_t reg, uint8_t sz) const
{
  if (sz != 4)
    return;

  uint8_t values[4] = {};
  read(reg, sz, values);

  time->tm_sec = 0;
  time->tm_min = bcd2uint(values[0] & 0x7f);
  time->tm_hour = bcd2uint(values[1] & 0x3f);
  time->tm_wday = 0;
  time->tm_mday = bcd2uint(values[2] & 0x3f);
  uint8_t wdayMonth = values[3];
  time->tm_mon = bcd2uint(wdayMonth & 0x1f) - 1; // Clock uses [1..12]
  time->tm_wday = (wdayMonth >> 5) - 1;          // Clock uses [1..7]
  time->tm_year = (YEAR_EPOCH - 1900);           // not stored
  time->tm_yday = -1;
}

bool RealTimeClock::checkDevice(device_t device_type)
{
  if (device_type == device_t::NONE)
    return false;

  device = device_type;
  address = deviceAddresses[(uint8_t)device_type];

  wire->beginTransmission(address);
  wire->write(uint8_t(0));
  wire->endTransmission();
  wire->requestFrom(address, (uint8_t)1);
  return wire->available();
}

void RealTimeClock::read(uint8_t _register, uint8_t length, uint8_t *values)
{
  wire->beginTransmission(address);
  wire->write(_register);
  wire->endTransmission();
  wire->requestFrom(address, length);
  for (uint8_t i = 0; i < length; i++)
    values[i] = wire->read();
}

uint8_t RealTimeClock::read(uint8_t _register)
{
  wire->beginTransmission(address);
  wire->write(_register);
  wire->endTransmission();
  wire->requestFrom(address, (uint8_t)1);
  return wire->read();
}

void RealTimeClock::write(uint8_t _register, uint8_t value)
{
  wire->beginTransmission(address);
  wire->write((uint8_t)_register);
  wire->write((uint8_t)value);
  wire->endTransmission();
}

void RealTimeClock::writeBlock(uint8_t _register, uint8_t length, uint8_t *value)
{
  wire->beginTransmission(address);
  wire->write((uint8_t)_register);
  for (size_t i = 0; i < length; i++)
    wire->write((uint8_t)value[i]);

  wire->endTransmission();
}

void RealTimeClock::write(uint8_t _register, uint8_t &value, uint8_t &mask)
{
  uint8_t val = (read(_register) & mask) | value;
  wire->beginTransmission(address);
  wire->write((uint8_t)_register);
  wire->write((uint8_t)val);
  wire->endTransmission();
}

uint8_t RealTimeClock::uint2bcd(uint8_t uint_val)
{
  return uint_val + 6 * (uint_val / 10);
}

uint8_t RealTimeClock::bcd2uint(int8_t bcd_val)
{
  return bcd_val - 6 * (bcd_val >> 4);
}

uint8_t RealTimeClock::bcd2uint24Hour(uint8_t bcdHour)
{
  if (bcdHour & 0x40)
    return (bcdHour & 0x20) ? bcd2uint(bcdHour & 0x1f) + 12 : bcd2uint(bcdHour & 0x1f);

  return bcd2uint(bcdHour);
}

#pragma once
#include <Arduino.h>
#include <Wire.h>

#ifndef YEAR_EPOCH
#define YEAR_EPOCH 1970
#endif

class RealTimeClock
{
public:
	typedef enum
	{
		DS13xx = 0, // DS1307, DS1337, DS1338
		MCP7941x = 1,
		PCF85263 = 2,
		NONE = 127,
	} device_t;

	typedef enum
	{
		TIME = 0,
		ALARM0 = 1,
		ALARM1 = 2,
		TIME_POWER_FAILED = 3,
		TIME_POWER_RESTORED = 4,
	} timeFunc_t;

	typedef enum
	{
		freq1Hz = 0,
		freq4096Hz = 1,
		freq8192Hz = 2,
		freq32768Hz = 3,
		freqCalibration = 4, // device-specific calibration for MCP7941x

		// Additional values for PCF85263
		freq1024Hz = 5,
		freq2048Hz = 6,
		freq16384Hz = 7,
		freqOutputLow = 8,
	} freq_t;

	bool Initialize(TwoWire &bus = Wire);
	bool Initialize(device_t device, TwoWire &bus = Wire);

	void GetTime(tm *time);
	void GetTime(time_t *time);
	void SetTime(tm *time);
	void SetTime(time_t *time);
	void SetSQW(freq_t f) const;
	bool GetClock(tm *time, timeFunc_t func); // TODO: Make it better

private:
	const uint8_t deviceAddresses[3] = {0x68, 0x6F, 0x51};

	uint8_t address;
	device_t device;
	TwoWire *wire;

	bool configure(device_t device_type);
	bool checkDevice(device_t dev);
	uint8_t getRegister(timeFunc_t func) const;
	void readMCPTimeSaver(struct tm *tm, uint8_t reg) const;
	void resetClock() const;
	void stopClock() const;
	void startClock() const;
	void enableBatteryBackup(bool enable) const;

	void read(uint8_t _register, uint8_t length, uint8_t *values);
	uint8_t read(uint8_t _register);
	void write(uint8_t _register, uint8_t value);
	void write(uint8_t _register, uint8_t &value, uint8_t &mask);
	void writeBlock(uint8_t _register, uint8_t length, uint8_t *value);

	uint8_t uint2bcd(uint8_t int_val);
	uint8_t bcd2uint(int8_t bcd_val);
	uint8_t bcd2uint24Hour(uint8_t bcdHour);
};
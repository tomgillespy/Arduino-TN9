/*
Arduino-TN9: abn Arduino library to read TN9 series temperature sensors
Copyright 2016, Victor Tseng <palatis@gmail.com>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived
   from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef TH901_H
#define TH901_H

#include <Arduino.h>
#include <inttypes.h>

#ifdef __AVR_ARCH__
#include <avr/wdt.h>
typedef uint32_t time_t;
#endif

// max frame length is 20ms according to datasheet, using 25 to give it
// some buffer.
#define TN901_FRAME_TIMEOUT 25 //ms

#define TN901_OTADDRESS 0x4c
#define TN901_ETADDRESS 0x66
#define TN901_ENDADDRESS 0x0d

#define MODE_OT 0x01
#define MODE_ET 0x02

class TN901
{
private:
	uint8_t _dataPin;
	uint8_t _clkPin;
	uint8_t _ackPin;

	mutable float _tempEnvironment;
	mutable float _tempObject;
	mutable uint8_t _data[5];
	
	mutable uint8_t _idx;
	mutable time_t _conversionStartMillis;
	mutable bool _environmentUpdated;
	mutable bool _objectUpdated;

	void init()
	{
		pinMode(_dataPin, INPUT);
		pinMode(_clkPin, INPUT);
		pinMode(_ackPin, OUTPUT);
		digitalWrite(_ackPin, HIGH);
	}
	
	uint8_t updateTemperature() const
	{
		uint8_t crc = _data[0] + _data[1] + _data[2];

		#ifdef TN901_DEBUG
		Serial.print("TN901: data: ");
		Serial.print(_data[0], HEX);
		Serial.print(":");
		Serial.print(_data[1], HEX);
		Serial.print(":");
		Serial.print(_data[2], HEX);
		Serial.print(":");
		Serial.print(_data[3], HEX);
		Serial.print(":");
		Serial.print(_data[4], HEX);

		Serial.print(", crc = ");
		Serial.print(crc, HEX);
		if (crc == _data[3]) {
			Serial.println(" (GOOD)");
		} else {
			Serial.print(" (BAD (assume ");
			Serial.print(crc, HEX);
			Serial.println("))");
		}
		#endif

		if (_data[4] == TN901_ENDADDRESS && ((_data[0] + _data[1] + _data[2]) & 0xff) == _data[3])
		{
			if (_data[0] == TN901_OTADDRESS)
			{
				_tempObject = ((_data[1] * 256.0 + _data[2]) / 16.0) - 273.15;
				#ifdef TN901_DEBUG
				Serial.print("TN901: Got object temp = ");
				Serial.println(_tempObject);
				#endif
				_objectUpdated = true;
				return MODE_OT;
			}
			else if (_data[0] == TN901_ETADDRESS)
			{
				_tempEnvironment = ((_data[1] * 256.0 + _data[2]) / 16.0) - 273.15;
				#ifdef TN901_DEBUG
				Serial.print("TN901: Got environment temp = ");
				Serial.println(_tempObject);
				#endif
				_environmentUpdated = true;
				return MODE_ET;
			}
		}

		return 0x00;
	}

public:
	TN901() {}

	TN901(uint8_t pinData, uint8_t pinClk, uint8_t pinAck):
		_dataPin(pinData), _clkPin(pinClk), _ackPin(pinAck)
	{
		init();
	}
	
	void init(uint8_t pinData, uint8_t pinClk, uint8_t pinAck)
	{
		_dataPin = pinData;
		_clkPin = pinClk;
		_ackPin = pinAck;

		init();
	}

	void read(uint8_t mode = MODE_ET | MODE_OT) const
	{
		mode &= MODE_ET | MODE_OT;
		uint8_t flag = 0x00;

		digitalWrite(_ackPin, LOW); // start conversion

		uint8_t k;
		for(k = 0; k < 10; ++k) // try 10 frames
		{
			for(uint8_t j = 0; j < 5; ++j) // 5 byte per frame
			{
				for(uint8_t i = 0; i < 8; ++i) // 8 bits per byte
				{
					time_t timeoutMillis;
					// wait for clkPin go LOW
					while(digitalRead(_clkPin))
						wdt_reset();

					_data[j] <<= 1;
					_data[j] |= ((digitalRead(_dataPin) == HIGH) ? 0x01 : 0x00);

					// wait for clkPin go HIGH
					while (!digitalRead(_clkPin))
						wdt_reset();
				}
			}

			flag |= updateTemperature();

			#ifdef TN901_DEBUG
			Serial.print("TN901: mode = ");
			Serial.print(mode, HEX);
			Serial.print(", flag = ");
			Serial.println(flag, HEX);
			#endif
			if ((mode & flag) == mode)
			{
				#ifdef TN901_DEBUG
				Serial.println("TN901: got all temp we want, return...");
				#endif
				break;
			}
		}

		digitalWrite(_ackPin, HIGH); // end conversion

		#ifdef TN901_DEBUG
		Serial.print("TN901: conversions = ");
		Serial.println(k);
		#endif

		return;
	}

	float getObjectTemperature() const
	{
		_objectUpdated = false;
		return _tempObject;
	}
	
	float getEnvironmentTemperature() const {
		_environmentUpdated = false;
		return _tempEnvironment;
	}
	
	bool startConversion(void (*isrFunc)())
	{
		int8_t interrupt = digitalPinToInterrupt(_clkPin);
		
		// if no interrupt support, return false.
		if (interrupt == NOT_AN_INTERRUPT)
			return false;
		
		// prepare interrupt
		_idx = 0;
		attachInterrupt(interrupt, isrFunc, FALLING);
		
		// start conversion
		digitalWrite(_ackPin, LOW);
		
		return true;
	}
	
	void endConversion() {
		int8_t interrupt = digitalPinToInterrupt(_clkPin);
		
		// if no interrupt support, return false.
		if (interrupt == NOT_AN_INTERRUPT)
			return;
		
		digitalWrite(_ackPin, HIGH);
		detachInterrupt(interrupt);
	}
	
	void processIsr() const
	{
		time_t timeoutMillis = millis() - _conversionStartMillis;
		if (_idx >= 40 || timeoutMillis > TN901_FRAME_TIMEOUT)
		{
			#ifdef TN901_DEBUG
			if (timeoutMillis > TN901_FRAME_TIMEOUT && _idx < 40)
				Serial.println("TN901: timed out");
			#endif

			_idx = 0; // required when timed out
			_conversionStartMillis = millis();
			_data[0] = _data[1] = _data[2] = _data[3] = _data[4] = 0x00;
		}

		uint8_t nByte = _idx / 8;

		_data[nByte] <<= 1;
		_data[nByte] |= (digitalRead(_dataPin) == HIGH) ? 0x01 : 0x00;

		++_idx;
		if (_idx >= 40) // read 40 bits...
			updateTemperature();
	}
	
	bool isEnvironmentTemperatureUpdated() const { return _environmentUpdated; }
	bool isObjectTemperatureUpdated() const { return _objectUpdated; }
};

#endif
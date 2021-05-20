// Shimmer3GSRPlus.cpp
// author: Andreas Seiderer <seiderer@hcm-lab.de>
// created: 9/3/2015
// Copyright (C) University of Augsburg, Lab for Human Centered Multimedia
//
// *************************************************************************************************
//
// This file is part of Social Signal Interpretation (SSI) developed at the 
// Lab for Human Centered Multimedia of the University of Augsburg
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public
// License as published by the Free Software Foundation; either
// version 3 of the License, or any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
//
//*************************************************************************************************

#include "Shimmer3GSRplus.h"
#include "Serial.h"
#include <iostream>

#ifdef USE_SSI_LEAK_DETECTOR
	#include "SSI_LeakWatcher.h"
	#ifdef _DEBUG
		#define new DEBUG_NEW
		#undef THIS_FILE
		static char THIS_FILE[] = __FILE__;
	#endif
#endif


namespace ssi {

static char ssi_log_name[] = "shimmer3gsr+";



//https://stackoverflow.com/questions/1861294/how-to-calculate-execution-time-of-a-code-snippet-in-c
_int64 GetTimeMicros64()
{
	FILETIME ft;
	LARGE_INTEGER li;

	GetSystemTimeAsFileTime(&ft);
	li.LowPart = ft.dwLowDateTime;
	li.HighPart = ft.dwHighDateTime;

	unsigned _int64 ret = li.QuadPart;
	ret -= 116444736000000000LL; /* Convert from file time to UNIX epoch time. */
	ret /= 10; /* From 100 nano seconds (10^-7) to 1 �sec intervals */

	return ret;
}


Shimmer3GSRPlus::Shimmer3GSRPlus (const ssi_char_t *file) 
	: m_ppgraw_provider (0),
	_file (0),
	m_ppgraw_channel (0),
	ssi_log_level (SSI_LOG_LEVEL_DEFAULT) {
	
	getOptions()->setStartCMD(0);
	getOptions()->setStopCMD(0);
	getOptions()->setDeviceInstanceId(0);

	if (file) {
		if (!OptionList::LoadXML(file, &_options)) {
			OptionList::SaveXML(file, &_options);
		}
		_file = ssi_strcpy (file);
	}
}

Shimmer3GSRPlus::~Shimmer3GSRPlus () {

	if (_file) {
		OptionList::SaveXML(_file, &_options);
		delete[] _file;
	}

	delete m_ppgraw_channel;
}

bool Shimmer3GSRPlus::setProvider (const ssi_char_t *name, IProvider *provider) {

	if (strcmp (name, SSI_SHIMMER3_PPGRAW_PROVIDER_NAME) == 0) {
		setPPGRawProvider (provider);
		return true;
	}

	ssi_wrn ("unkown provider name '%s'", name);

	return false;
}

void Shimmer3GSRPlus::setPPGRawProvider (IProvider *provider) {

	if (m_ppgraw_provider) {
		ssi_wrn ("gsr raw was already set");
	}

	m_ppgraw_provider = provider;
	if (m_ppgraw_provider) {		
		m_ppgraw_provider->init (getChannel (0));
		ssi_msg (SSI_LOG_LEVEL_DETAIL, "gsr raw provider set");
	}
}

bool Shimmer3GSRPlus::connect () {

	_device = std::make_unique<Shimmer3LogAndStreamDevice>(_options.port, _options.baud);

	if (!_device->connect()) {
		return false;
	}

	// set thread name
	Thread::setName (getName ());

	return true;
}

void Shimmer3GSRPlus::run () {
	if (_device && _device->isConnected() && !_device->isStreaming()) {
		std::cout << "Start streaming!" << std::endl;
		_device->startStreaming();
	}

	auto packet = _device->readNextPacket();

	if (packet) {
		processPPGValue(*packet.get());
	}
	else {
		ssi_wrn("Did not get a packet from the Shimmer Device");
	}
}

void Shimmer3GSRPlus::processPPGValue(const Shimmer3LogAndStreamDevice::DataPacket& packet) {
	try
	{
		float rawPPGValue = packet.get(Shimmer3LogAndStreamDevice::SENSORID::INTERNAL_ADC_A13) * 1.0f;

		m_ppgraw_provider->provide(ssi_pcast(char, &rawPPGValue), 1);
	}
	catch (const std::exception&)
	{
		ssi_wrn("The shimmer seems to be not configured to provide ppg values! Please make sure you are using the correct shimmer board and configuration!");
	}
}

bool Shimmer3GSRPlus::disconnect() {

	if (!_device) {
		ssi_wrn("sensor not connected");
		return false;
	}

	if (!_device->isConnected()) {
		ssi_wrn("sensor not connected");
		_device.reset(nullptr);
		return false;
	}

	ssi_msg(SSI_LOG_LEVEL_BASIC, "sending stop message ...");
	_device->stopStreaming();

	ssi_msg(SSI_LOG_LEVEL_BASIC, "try to disconnect sensor ...");

	_device.reset(nullptr);

	ssi_msg (SSI_LOG_LEVEL_DETAIL, "sensor disconnected");

	return true;
}

void Shimmer3GSRPlus::TODO_meaningfulgsrvalue() {
	/* C# implementation
		int iGSR = getSignalIndex(Shimmer3Configuration.SignalNames.GSR);
		int newGSRRange = -1; // initialized to -1 so it will only come into play if mGSRRange = 4  
		double datatemp = newPacket[iGSR];
		double gsrResistanceKOhms = -1;
		//double p1 = 0, p2 = 0;
		if (GSRRange == 4)
		{
			newGSRRange = (49152 & (int)datatemp) >> 14;
		}
		datatemp = (double)((int)datatemp & 4095);
		if (GSRRange == 0 || newGSRRange == 0)
		{
			//Note that from FW 1.0 onwards the MSB of the GSR data contains the range
			// the polynomial function used for calibration has been deprecated, it is replaced with a linear function
			//p1 = 0.0363;
			//p2 = -24.8617;

			//Changed to new GSR algorithm using non inverting amp
			//p1 = 0.0373;
			//p2 = -24.9915;
			gsrResistanceKOhms = CalibrateGsrDataToResistanceFromAmplifierEq(datatemp, 0);
		}
		else if (GSRRange == 1 || newGSRRange == 1)
		{
			//p1 = 0.0051;
			//p2 = -3.8357;
			//Changed to new GSR algorithm using non inverting amp
			//p1 = 0.0054;
			//p2 = -3.5194;
			gsrResistanceKOhms = CalibrateGsrDataToResistanceFromAmplifierEq(datatemp, 1);
		}
		else if (GSRRange == 2 || newGSRRange == 2)
		{
			//p1 = 0.0015;
			//p2 = -1.0067;
			//Changed to new GSR algorithm using non inverting amp
			//p1 = 0.0015;
			//p2 = -1.0163;
			gsrResistanceKOhms = CalibrateGsrDataToResistanceFromAmplifierEq(datatemp, 2);
		}
		else if (GSRRange == 3 || newGSRRange == 3)
		{
			//p1 = 4.4513e-04;
			//p2 = -0.3193;
			//Changed to new GSR algorithm using non inverting amp
			//p1 = 4.5580e-04;
			//p2 = -0.3014;
			if (datatemp < GSR_UNCAL_LIMIT_RANGE3)
			{
				datatemp = GSR_UNCAL_LIMIT_RANGE3;
			}
			gsrResistanceKOhms = CalibrateGsrDataToResistanceFromAmplifierEq(datatemp, 3);
		}
		//Changed to new GSR algorithm using non inverting amp
		//datatemp = CalibrateGsrData(datatemp, p1, p2);
		gsrResistanceKOhms = NudgeGsrResistance(gsrResistanceKOhms, GSRRange);
		double gsrConductanceUSiemens = (1.0 / gsrResistanceKOhms) * 1000;
		objectCluster.Add(Shimmer3Configuration.SignalNames.GSR, ShimmerConfiguration.SignalFormats.RAW, ShimmerConfiguration.SignalUnits.NoUnits, newPacket[iGSR]);
		objectCluster.Add(Shimmer3Configuration.SignalNames.GSR, ShimmerConfiguration.SignalFormats.CAL, ShimmerConfiguration.SignalUnits.KiloOhms, gsrResistanceKOhms);
		objectCluster.Add(Shimmer3Configuration.SignalNames.GSR_CONDUCTANCE, ShimmerConfiguration.SignalFormats.CAL, ShimmerConfiguration.SignalUnits.MicroSiemens, gsrConductanceUSiemens);
	*/
}

}

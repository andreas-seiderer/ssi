// pupiltracker.cpp
// author: Fabian Wildgrube
// created: 2021/02/11
// Copyright (C) University of Augsburg, Lab for Human Centered Multimedia
//
// *************************************************************************************************
//
// This file is part of Social Signal Interpretation (SSI) developed at the 
// Lab for Human Centered Multimedia of the University of Augsburg
//
// This library is free software; you can redistribute itand/or
// modify it under the terms of the GNU General Public
// License as published by the Free Software Foundation; either
// version 3 of the License, or any laterversion.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FORA PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public
// License along withthis library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
//
//*************************************************************************************************

#include "ioput/file/FileTools.h"
#include "base/ITheFramework.h"
#include "base/Factory.h"

#include <ssiocv.h>
#include "../include/pupiltracker.h"

namespace ssi {

	PupilTracker::PupilTracker(const ssi_char_t *file)
		: _stride_in(0),
		_stride_out(0),
		_listener(0),
		_file(0),
		_ts(0),
		_serverSocket(_ioContext)
	{

		if (file) {
			if (!OptionList::LoadXML(file, &_options)) {
				OptionList::SaveXML(file, &_options);
			}
			_file = ssi_strcpy(file);
		}

		ssi_event_init(_event, SSI_ETYPE_MAP);
	}

	bool PupilTracker::setEventListener(IEventListener *listener) {

		_listener = listener;

		if(_options.address[0] != '\0') {

			_event_address.setAddress(_options.address);
			_event.sender_id = Factory::AddString(_event_address.getSender(0));
			_event.event_id = Factory::AddString(_event_address.getEvent(0));

		}
		else {

			ssi_wrn("use of deprecated option 'sname' and 'ename', use 'address' instead")

			_event.sender_id = Factory::AddString(_options.sname);
			if (_event.sender_id == SSI_FACTORY_STRINGS_INVALID_ID) {
				return false;
			}
			_event.event_id = Factory::AddString(_options.ename);
			if (_event.event_id == SSI_FACTORY_STRINGS_INVALID_ID) {
				return false;
			}

			_event_address.setSender(_options.sname);
			_event_address.setEvents(_options.ename);
		}

		return true;

	}

	PupilTracker::~PupilTracker() {

		if (_file) {
			OptionList::SaveXML(_file, &_options);
			delete[] _file;
		}

		ssi_event_destroy(_event);

	}

	void PupilTracker::setFormatIn(ssi_video_params_t format) {
		_format_in = format;
		_stride_in = ssi_video_stride(_format_in);
		_frameWidthIn = _format_in.widthInPixels;
		_frameHeightIn = _format_in.heightInPixels;
		_bytesPerPixelIn = (_format_in.depthInBitsPerChannel / 8) * _format_in.numOfChannels;
		_bytesPerFrameIn = _frameWidthIn * _frameHeightIn * _bytesPerPixelIn;

		if (_bytesPerPixelIn != 3) {
			ssi_err("invalid bit depth or channel size. Can only work with 8bit/channel and 3 channels (RGB)");
			return;
		}
	}

	void PupilTracker::setFormatOut(ssi_video_params_t format) {
		_format_out = format;
		_stride_out = ssi_video_stride(_format_out);

	}

	void PupilTracker::setFormat(ssi_video_params_t format_in) {
		PupilTracker::setFormatIn(format_in);
		PupilTracker::setFormatOut(format_in);
	}

	void PupilTracker::transform_enter(ssi_stream_t &stream_in,
		ssi_stream_t &stream_out,
		ssi_size_t xtra_stream_in_num,
		ssi_stream_t xtra_stream_in[]) {

		//set up connection to pupiltracking server
		_serverSocket.connect(tcp::endpoint(boost::asio::ip::address::from_string("127.0.0.1"), 9876));

		// send metadata (frame size, fps, etc.)
		// all as doubles because fps is a double
		double fps = stream_in.sr;

		double* metaData = new double[4];
		metaData[0] = _frameWidthIn * 1.0;
		metaData[1] = _frameHeightIn * 1.0;
		metaData[2] = _bytesPerPixelIn * 1.0;
		metaData[3] = fps;

		boost::asio::write(_serverSocket, boost::asio::buffer(reinterpret_cast<const char*>(metaData), 4 * sizeof(double)));

		//create buffer for sending frames
		_sendFrameBuffer = new unsigned char[_bytesPerFrameIn];
	}

	void PupilTracker::transform(ITransformer::info info,
		ssi_stream_t &stream_in,
		ssi_stream_t &stream_out,
		ssi_size_t xtra_stream_in_num,
		ssi_stream_t xtra_stream_in[]) {
		
		cv::Mat frame;


		// copy input frame into buffer for sending to server
		frame = cv::Mat(_frameHeightIn, _frameWidthIn, CV_8UC3, stream_in.ptr, _stride_in);
		unsigned char* rawFrameData = frame.data;
		memcpy(_sendFrameBuffer, rawFrameData, _bytesPerFrameIn);

		// send frame to server for processing
		boost::asio::write(_serverSocket, boost::asio::buffer(_sendFrameBuffer, _bytesPerFrameIn));		

		// receive pupil data
		auto bytes_transferred = boost::asio::read(_serverSocket, _response_buffer, boost::asio::transfer_exactly(_sampleDimensionsOut * sizeof(float)));
		const char* pupilDataTrackingFrameBuffer = boost::asio::buffer_cast<const char*>(_response_buffer.data());
		float leftPupilDiameter = static_cast<float>(*reinterpret_cast<const float*>(pupilDataTrackingFrameBuffer));
		float leftPupilDiameterRelative = static_cast<float>(*reinterpret_cast<const float*>(pupilDataTrackingFrameBuffer + 1 * sizeof(float)));
		float leftPupilConfidence = static_cast<float>(*reinterpret_cast<const float*>(pupilDataTrackingFrameBuffer + 2 * sizeof(float)));
		float rightPupilDiameter = static_cast<float>(*reinterpret_cast<const float*>(pupilDataTrackingFrameBuffer + 3 * sizeof(float)));
		float rightPupilDiameterRelative = static_cast<float>(*reinterpret_cast<const float*>(pupilDataTrackingFrameBuffer + 4 * sizeof(float)));
		float rightPupilConfidence = static_cast<float>(*reinterpret_cast<const float*>(pupilDataTrackingFrameBuffer + 5 * sizeof(float)));

		//std::cout << "  Left: " << leftPupilDiameter << " (c: " << leftPupilConfidence << "), Right: " << rightPupilDiameter << " (c: " << rightPupilConfidence << ")\n";
		_response_buffer.consume(bytes_transferred);

		// Send pupil data to output stream
		ssi_real_t* outptr = ssi_pcast(ssi_real_t, stream_out.ptr);
		*(outptr++) = leftPupilDiameter;
		*(outptr++) = leftPupilDiameterRelative;
		*(outptr++) = leftPupilConfidence;
		*(outptr++) = rightPupilDiameter;
		*(outptr++) = rightPupilDiameterRelative;
		*(outptr++) = rightPupilConfidence;
		
		// Release
		frame.release();
	}

	void PupilTracker::transform_flush(ssi_stream_t &stream_in,
		ssi_stream_t &stream_out,
		ssi_size_t xtra_stream_in_num,
		ssi_stream_t xtra_stream_in[]) {

		delete[] _sendFrameBuffer;
	}

}
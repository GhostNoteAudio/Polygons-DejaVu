
#pragma once

#include "Constants.h"
#include "ParameterDejaVu.h"
#include "Utils.h"
//#include "Z4Rev.h"
#include "blocks/DelayBlockExternal.h"
#include "FlashRecording.h"

using namespace Polygons;

namespace DejaVu
{
	//DelayBlockExternal<2000000, BUFFER_SIZE> Delay(1000);
	FlashRecording rec;

	class ControllerDejaVu
	{
	private:
		int samplerate;
		float inGain;
		float outGain;
		uint16_t parameters[Parameter::COUNT];
		int loopLength;
		int mode; // playback or record

	public:
		bool isRecordingBaseLoop;
		bool isRecordingOverdub;
		bool isPlaybackEnabled;
		
		ControllerDejaVu(int samplerate)
		{
			this->samplerate = samplerate;
			inGain = 1.0;
			outGain = 1.0;
			isRecordingBaseLoop = false;
			isRecordingOverdub = false;
			isPlaybackEnabled = false;
			loopLength = 0;
			rec.Init();
		}

		void TriggerRecord()
		{
			if (isRecordingBaseLoop)
			{
				// turning off recording
				rec.FlushEndBufferAsync();
				loopLength = rec.GetProcessedSamples();
				rec.SetMode(RecordingMode::Playback);
				isRecordingBaseLoop =  false;
				isPlaybackEnabled = true;
			}
			else
			{
				// turning on recording
				rec.SetMode(RecordingMode::Recording);
				isRecordingBaseLoop = true;
				isPlaybackEnabled = false;
			}
			rec.ResetPtr();
		}

		void TriggerStartStop()
		{
			if (isRecordingBaseLoop)
			{
				// stopping playback and recording
				rec.FlushEndBufferAsync();
				loopLength = rec.GetProcessedSamples();
				rec.SetMode(RecordingMode::Stopped);
				isRecordingBaseLoop =  false;
				isPlaybackEnabled = false;
			}
			else if (isRecordingOverdub)
			{
				rec.SetMode(RecordingMode::Stopped);
				isRecordingOverdub = false;
				isPlaybackEnabled = false;
			}
			else
			{
				isPlaybackEnabled = !isPlaybackEnabled;
				rec.SetMode(isPlaybackEnabled ? RecordingMode::Playback : RecordingMode::Stopped);
			}
			
			rec.ResetPtr();
		}

		void TriggerOverdub()
		{
			// overdub disabled when recording base loop
			if (isRecordingBaseLoop)
				return;

			if (isPlaybackEnabled)
			{
				isRecordingOverdub = !isRecordingOverdub;
				rec.SetMode(isRecordingOverdub ? RecordingMode::Overdub : RecordingMode::Playback);
			}
			else // playback was stopped
			{
				isPlaybackEnabled = true;
				isRecordingOverdub = true;
				rec.SetMode(RecordingMode::Overdub);
				rec.ResetPtr();
			}
		}

		int GetSamplerate()
		{
			return samplerate;
		}

		uint16_t* GetAllParameters()
		{
			return parameters;
		}

		double GetScaledParameter(int param)
		{
			switch (param)
			{
				case Parameter::InGain:		return (int)(P(param) * 40) / 2.0; // 0.5db increments
				case Parameter::OutGain:	return -20 + P(param) * 40;
				case Parameter::Mode:		return P(param, 4) > 0.5 ? 1 : 0;
			}
			return parameters[param];
		}

		void SetParameter(int param, uint16_t value)
		{
			parameters[param] = value;
			auto scaled = GetScaledParameter(param);

			if (param == Parameter::InGain)
				inGain = DB2gain(scaled);
			else if (param == Parameter::OutGain)
				outGain = DB2gain(scaled);
			else if (param == Parameter::Mode)
				mode = scaled;
		}

		void Process(float** inputs, float** outputs, int bufferSize)
		{
			auto b1 = Buffers::Request();
			auto b2 = Buffers::Request();
			auto bufPlayback = b1.Ptr;
			auto bufOverdubbed = b2.Ptr;
			ZeroBuffer(bufPlayback, bufferSize);
			ZeroBuffer(bufOverdubbed, bufferSize);
			ZeroBuffer(outputs[0], bufferSize);

			rec.Process(inputs[0], outputs[0], bufferSize);
			Mix(outputs[0], inputs[0], 1.0, bufferSize);
			Copy(outputs[1], outputs[0], bufferSize);

			
			// reset at end of loop
			if (isPlaybackEnabled && rec.GetProcessedSamples() >= loopLength)
			{
				rec.FlushEndBufferAsync();
				rec.ResetPtr();
				Serial.println("Resetting loop");
			}
		}
		
	private:
		double P(int para, int maxVal=1023)
		{
			auto idx = (int)para;
			return idx >= 0 && idx < Parameter::COUNT ? (parameters[idx] / (double)maxVal) : 0.0;
		}
	};
}

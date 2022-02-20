
#pragma once

#include "Constants.h"
#include "ParameterDejaVu.h"
#include "Utils.h"
//#include "Z4Rev.h"
#include "blocks/DelayBlockExternal.h"

using namespace Polygons;

namespace DejaVu
{
	DelayBlockExternal<2000000, BUFFER_SIZE> Delay(1000);

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
			Delay.init();
		}

		void TriggerRecord()
		{
			if (isRecordingBaseLoop)
			{
				// turning off recording
				isRecordingBaseLoop =  false;
				loopLength = Delay.getPtr();
				isPlaybackEnabled = true;
			}
			else
			{
				// turning on recording
				isRecordingBaseLoop = true;
				isPlaybackEnabled = false;
			}
			Delay.setPtr(0);
		}

		void TriggerStartStop()
		{
			if (isRecordingBaseLoop)
			{
				// stopping playback and recording
				isRecordingBaseLoop =  false;
				loopLength = Delay.getPtr();
				isPlaybackEnabled = false;
			}
			else if (isRecordingOverdub)
			{
				isRecordingOverdub = false;
				isPlaybackEnabled = false;
			}
			else
			{
				if (isPlaybackEnabled)
					isPlaybackEnabled = false;
				else
					isPlaybackEnabled = true;
			}
			
			Delay.setPtr(0);
		}

		void TriggerOverdub()
		{
			// overdub disabled when recording base loop
			if (isRecordingBaseLoop)
				return;

			if (isPlaybackEnabled)
			{
				isRecordingOverdub = !isRecordingOverdub;
			}
			else // playback was stopped
			{
				isPlaybackEnabled = true;
				isRecordingOverdub = true;
				Delay.setPtr(0);
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

			// reset at end of loop
			if (isPlaybackEnabled && Delay.getPtr() >= loopLength)
			{
				Delay.setPtr(0);
				Serial.println("Resetting loop");
			}
			
			if (isPlaybackEnabled)
				Delay.read(bufPlayback, 0, bufferSize);

			if (isRecordingBaseLoop)
			{
				Delay.write(inputs[0], bufferSize);
			}
			else if (isRecordingOverdub)
			{
				Mix(bufOverdubbed, bufPlayback, 1.0, bufferSize);
				Mix(bufOverdubbed, inputs[0], 1.0, bufferSize);
				Delay.write(bufOverdubbed, bufferSize);
			}

			ZeroBuffer(outputs[0], bufferSize);
			Copy(outputs[0], inputs[0], bufferSize);
			if (isPlaybackEnabled)
				Mix(outputs[0], bufPlayback, 1.0, bufferSize);

			Copy(outputs[1], outputs[0], bufferSize);
			Delay.updatePtr(bufferSize);
		}
		
	private:
		double P(int para, int maxVal=1023)
		{
			auto idx = (int)para;
			return idx >= 0 && idx < Parameter::COUNT ? (parameters[idx] / (double)maxVal) : 0.0;
		}
	};
}

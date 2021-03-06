
#pragma once

#include "Constants.h"
#include "ParameterDejaVu.h"
#include "Utils.h"
//#include "Z4Rev.h"
#include "blocks/DelayBlockExternal.h"
#include "FlashReaderWriter.h"

using namespace Polygons;

namespace DejaVu
{
	class ControllerDejaVu
	{
	private:
		
		int samplerate;
		float outGain;
		uint16_t parameters[Parameter::COUNT];
		int loopLength;

	public:
		FlashReaderWriter recl, recr;
		
		ControllerDejaVu(int samplerate) : recl(".L"), recr(".R")
		{
			this->samplerate = samplerate;
			outGain = 1.0;
			loopLength = 0;
		}

		void Init()
		{
			recl.Init();
			recr.Init();
		}

		void TriggerRecord()
		{
			if (recl.GetMode() == RecordingMode::Recording)
			{
				// turning off recording
				recl.SetTotalLength(loopLength);
				recr.SetTotalLength(loopLength);
				recl.AdvanceWrite();
				recr.AdvanceWrite();
				recl.SetMode(RecordingMode::Playback);
				recr.SetMode(RecordingMode::Playback);
				
			}
			else
			{
				// turning on recording
				loopLength = 0;
				recl.SetTotalLength(0);
				recr.SetTotalLength(0);
				recl.SetMode(RecordingMode::Recording);
				recr.SetMode(RecordingMode::Recording);
			}
			recl.PreparePlay();
			recr.PreparePlay();
		}

		void TriggerStartStop()
		{
			if (recl.GetMode() == RecordingMode::Recording)
			{
				// stopping playback and recording
				recl.SetTotalLength(loopLength);
				recr.SetTotalLength(loopLength);
				recl.AdvanceWrite();
				recr.AdvanceWrite();
				recl.SetMode(RecordingMode::Stopped);
				recr.SetMode(RecordingMode::Stopped);
			}
			else if (recl.GetMode() == RecordingMode::Overdub)
			{
				recl.SetMode(RecordingMode::Stopped);
				recr.SetMode(RecordingMode::Stopped);
			}
			else
			{
				auto playState = recl.GetMode() == RecordingMode::Playback ? RecordingMode::Stopped : RecordingMode::Playback;
				recl.SetMode(playState);
				recr.SetMode(playState);
			}
			
			recl.PreparePlay();
			recr.PreparePlay();
		}

		void TriggerOverdub()
		{
			// overdub disabled when recording base loop
			if (recl.GetMode() == RecordingMode::Recording)
				return;

			else if (recl.GetMode() == RecordingMode::Playback)
			{
				recl.SetMode(RecordingMode::Overdub);
				recr.SetMode(RecordingMode::Overdub);
			}
			else if (recl.GetMode() == RecordingMode::Overdub)
			{
				recl.SetMode(RecordingMode::Playback);
				recr.SetMode(RecordingMode::Playback);
			}
			else // playback was stopped
			{
				recl.SetMode(RecordingMode::Overdub);
				recr.SetMode(RecordingMode::Overdub);
				recl.PreparePlay();
				recr.PreparePlay();
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

		double GetSetLenValue()
		{
			double val = P(Parameter::SetLength);
			if (GetScaledParameter(Parameter::SetLengthMode) == 0) // seconds
			{
				if (val < 0.5)
					return (int)(3 + val * 57 * 10) / 10.0; // seconds, up to 30 sec, 100ms increments
				else
					return 30 + (int)((val - 0.5) * 180); // seconds, 30-120 sec, 1 sec increment
			}
			else if (GetScaledParameter(Parameter::SetLengthMode) == 1) // beats
			{
				return (int)(1 + val * 127);
			}
			else
			{
				return (int)(1 + val * 15);
			}
		}

		int GetSetLenValueSamples()
		{
			int bpm = GetScaledParameter(Parameter::Bpm);
			double val = GetSetLenValue();
			if (GetScaledParameter(Parameter::SetLengthMode) == 0) // seconds
				return val * samplerate;
			else if (GetScaledParameter(Parameter::SetLengthMode) == 1) // beats
				return (val / bpm) * 60 * samplerate;
			else // bars, 4x4 assumed
				return (val / bpm) * 60 * 4 * samplerate;
		}

		double GetScaledParameter(int param)
		{
			switch (param)
			{
				case Parameter::InGain:			return (int)(P(param) * 40) / 2.0; // 0.5db increments
				case Parameter::OutGain:		return -20 + P(param) * 40;
				case Parameter::LoadSlot:		return 1+(int)(P(param) * 29.999);
				case Parameter::SaveSlot:		return 1+(int)(P(param) * 29.999);
				case Parameter::SetLength:		return GetSetLenValue();
				case Parameter::SetLengthMode:	return (int)(P(param) * 2.999);
				case Parameter::Bpm:			return (int)10 + (int)(P(param) * 290);
			}
			return parameters[param];
		}		

		void SetParameter(int param, uint16_t value)
		{
			parameters[param] = value;
			auto scaled = GetScaledParameter(param);
			if (param == Parameter::OutGain)
				outGain = DB2gain(scaled);
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
			ZeroBuffer(outputs[1], bufferSize);

			recl.Process(inputs[0], outputs[0], bufferSize);
			recr.Process(inputs[1], outputs[1], bufferSize);
			Mix(outputs[0], inputs[0], 1.0, bufferSize);
			Mix(outputs[1], inputs[1], 1.0, bufferSize);
			Gain(outputs[0], outGain, bufferSize);
			Gain(outputs[1], outGain, bufferSize);
			loopLength += bufferSize;
		}
		
	private:
		double P(int para, int maxVal=1023)
		{
			auto idx = (int)para;
			return idx >= 0 && idx < Parameter::COUNT ? (parameters[idx] / (double)maxVal) : 0.0;
		}
	};
}

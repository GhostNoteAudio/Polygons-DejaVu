#pragma once

#include "Polygons.h"
#include "Constants.h"
#include "ParameterDejaVu.h"
#include "ControllerDejaVu.h"
#include "Utils.h"
#include "EffectBase.h"

namespace DejaVu
{
    class DejaVuEffect : public Polygons::EffectBase
    {
    public:
        const char* ParameterNames[Parameter::COUNT];
        float BufferInL[BUFFER_SIZE];
        float BufferInR[BUFFER_SIZE];
        float BufferOutL[BUFFER_SIZE];
        float BufferOutR[BUFFER_SIZE];
        int InputClip, OutputClip = 0;
        ControllerDejaVu controller;

        DejaVuEffect() : controller(SAMPLERATE)
        {
        }

        void SetNames()
        {
            ParameterNames[Parameter::InGain] = "In Gain";
            ParameterNames[Parameter::OutGain] = "Out Gain";
            ParameterNames[Parameter::LoadSlot] = "Load";
            ParameterNames[Parameter::SaveSlot] = "Save";
            ParameterNames[Parameter::SetLength] = "Set Len";
            ParameterNames[Parameter::SetLengthMode] = "Len Type";
            ParameterNames[Parameter::Bpm] = "BPM";
        }

        inline void SetIOConfig()
        {
            int gainIn = (int8_t)(controller.GetScaledParameter(Parameter::InGain) * 2.0 + 0.0001);        
            Polygons::codec.analogInGain(gainIn, gainIn);
        }

        // --------------------- EffectBase implementation ---------------------

        virtual void RegisterParams() override
        {
            os.Register(Parameter::InGain,         1023, Polygons::ControlMode::Encoded, 0, 4);
            os.Register(Parameter::OutGain,        1023, Polygons::ControlMode::Encoded, 1, 2);
            os.Register(Parameter::LoadSlot,       1023, Polygons::ControlMode::Encoded, 2, 2);
            os.Register(Parameter::SaveSlot,       1023, Polygons::ControlMode::Encoded, 3, 2);
            os.Register(Parameter::SetLength,      1023, Polygons::ControlMode::Encoded, 4, 1);
            os.Register(Parameter::SetLengthMode,  1023, Polygons::ControlMode::Encoded, 5, 8);
            os.Register(Parameter::Bpm,            1023, Polygons::ControlMode::Encoded, 6, 1);
        }

        virtual void GetPageName(int page, char* dest) override
        {
            if (page == 4 && InputClip)
                strcpy(dest, " !!IN CLIP!!");
            else if (page == 7 && OutputClip)
                strcpy(dest, " !!OUT CLIP!!");
            else if (page == 2 || page == 3 || page == 4)
                strcpy(dest, "<Click>");
            else
                strcpy(dest, "");
        }

        virtual void GetParameterName(int paramId, char* dest) override
        {
            if (paramId >= 0)
                strcpy(dest, ParameterNames[paramId]);
            else
                strcpy(dest, "");
        }

        virtual void GetParameterDisplay(int paramId, char* dest) override
        {
            double val = controller.GetScaledParameter(paramId);
            if (paramId == Parameter::InGain || paramId == Parameter::OutGain)
            {
                sprintf(dest, "%.1fdB", val);
            }
            else if (paramId == Parameter::LoadSlot || paramId == Parameter::SaveSlot || paramId == Parameter::Bpm)
            {
                sprintf(dest, "%d", (int)val);
            }
            else if (paramId == Parameter::SetLengthMode)
            {
                if (val == 0)
                    strcpy(dest, "Seconds");
                else if (val == 1)
                    strcpy(dest, "Beats");
                else if (val == 2)
                    strcpy(dest, "Bars");
                else
                    strcpy(dest, "---");
            }
            else
            {
                sprintf(dest, "%.2f", val);
            }
        }

        virtual void SetParameter(uint8_t paramId, uint16_t value) override
        {
            Serial.print("Setting param ");
            Serial.print(paramId);
            Serial.print(" to value ");
            Serial.println(value);
            controller.SetParameter(paramId, value);
            if (paramId == Parameter::InGain)
                SetIOConfig();
        }

        virtual void SetLeds()
        {
            Polygons::pushDigital(2, controller.recl.GetMode() == RecordingMode::Recording? 1 : 0);
            Polygons::pushDigital(5, controller.recl.GetMode() == RecordingMode::Overdub ? 1 : 0);
            Polygons::pushDigital(8, controller.recl.GetMode() != RecordingMode::Stopped ? 1 : 0);
        }

        virtual bool HandleUpdate(Polygons::ParameterUpdate* update) 
        {
            if (update->Type == MessageType::Digital && update->Index == 2 && update->Value > 0)
            {
                os.menu.setMessage("Loading loop...");
                os.redrawDisplay();
                Polygons::pushDisplayFull();
                int slot = controller.GetScaledParameter(Parameter::LoadSlot);
                controller.recl.LoadRecording(slot);
                controller.recr.LoadRecording(slot);
                os.menu.setMessage("Loaded!", 1000);
                return true;
            }
            if (update->Type == MessageType::Digital && update->Index == 3 && update->Value > 0)
            {
                os.menu.setMessage("Storing loop...");
                os.redrawDisplay();
                Polygons::pushDisplayFull();
                int slot = controller.GetScaledParameter(Parameter::SaveSlot);
                controller.recl.SaveRecording(slot);
                controller.recr.SaveRecording(slot);
                os.menu.setMessage("Stored!", 1000);
                return true;
            }

            if (update->Type == MessageType::Digital && update->Index == 8 && update->Value > 0)
            {
                controller.TriggerRecord();
                SetLeds();
                return true;
            }
            if (update->Type == MessageType::Digital && update->Index == 9 && update->Value > 0)
            {
                controller.TriggerOverdub();
                SetLeds();
                return true;
            }
            else if (update->Type == MessageType::Digital && update->Index == 10 && update->Value > 0)
            {
                controller.TriggerStartStop();
                SetLeds();
                return true;
            }
            
            return false;
        }

        virtual void CustomDrawCallback()
        {
            auto canvas = Polygons::getCanvas();
            canvas->fillRect(0, 0, 64, 10, 0); // remove the selected page highlighting
            /*canvas->setTextColor(1);

            canvas->setCursor(140, 19);
            canvas->println("Load");
            canvas->setCursor(140, 28);
            canvas->println("<Click>");

            canvas->setCursor(194, 19);
            canvas->println("Save");
            canvas->setCursor(194, 28);
            canvas->println("<Click>");*/
        }

        virtual void AudioCallback(int32_t** inputs, int32_t** outputs, int bufferSize) override
        {
            IntBuffer2Float(BufferInL, inputs[0], bufferSize);
            IntBuffer2Float(BufferInR, inputs[1], bufferSize);
            float maxInL = MaxAbsF(BufferInL, bufferSize);
            float maxInR = MaxAbsF(BufferInR, bufferSize);
            
            if (maxInL >= 0.88 || maxInR >= 0.88)
                InputClip = 2000;
            else
                InputClip = InputClip > 0 ? InputClip - 1 : 0;
            
            float* ins[2] = {BufferInL, BufferInR};
            float* outs[2] = {BufferOutL, BufferOutR};
            controller.Process(ins, outs, bufferSize);
            
            FloatBuffer2Int(outputs[0], BufferOutL, bufferSize);
            FloatBuffer2Int(outputs[1], BufferOutR, bufferSize);
            float maxOutL = MaxAbsF(BufferOutL, bufferSize);
            float maxOutR = MaxAbsF(BufferOutR, bufferSize);

            if (maxOutL >= 0.98 || maxOutR >= 0.98)
                OutputClip = 2000;
            else
                OutputClip = OutputClip > 0 ? OutputClip - 1 : 0;
        }

    public:

        virtual void Start() override
        {
            LogInfo("initialising controller...")
            controller.Init();
            LogInfo("initialising controller complete!")

            LogInfo("Starting up - waiting for controller signal...")
            os.waitForControllerSignal();
            SetNames();
            os.PageCount = 1;
            RegisterEffect();
            
        }
    };
}

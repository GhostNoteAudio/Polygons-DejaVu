#pragma once

namespace DejaVu
{
    class Parameter
    {
    public:
        static const int InGain = 0;
        static const int OutGain = 1;
        
        static const int LoadSlot = 2;
        static const int SaveSlot = 3;

        static const int SetLength = 4;
        static const int SetLengthMode = 5;
        static const int Bpm = 6;

        static const int COUNT = 7;
    };
}

#pragma once
#include <SdFat.h>
#include <stdint.h>
#include "Polygons.h"
#include "Utils.h"

using namespace Polygons;

enum class RecordingMode
{
    Stopped = 0,
    Recording = 1,
    Overdub = 2,
    Playback = 3
};

const char* BaseFilePath = "DejaVu/RecordingBuffer.dat";

void ts()
{
    auto time = micros();
    auto seconds = (int)(time / 1000000);
    auto millis = (int)(time / 1000) - seconds * 1000;
    auto micro = time - seconds * 1000000 - millis * 1000;
    Serial.printf("[%03d:%03d.%03d] : ", seconds, millis, micro);
    /*Serial.print(seconds);
    Serial.print(":");
    Serial.print(millis );
    Serial.print(".");
    Serial.print(micro);
    Serial.print(": ");*/
}

class FlashReaderWriter
{
    SdFat sd;
    SdFile file;

    const static int StorageBufferSize = 4096; // must be multiple of the BUFFER_SIZE

    struct FlashOperation
    {
        int FlashIdx = 0;
        bool Pending = false;
        float Data[StorageBufferSize] = {0};
    };

     // circular buffer for operations processed async
    FlashOperation WriteOps[2];
    FlashOperation ReadOps[2];
    int ReadOpsHead = 0;
    int ReadOpsTail = 0;
    int WriteOpsHead = 0;
    int WriteOpsTail = 0;

    char BufferFileName[64];

    // These buffers store the data from the first block in flash.
    // We do this because when the loop comes around, we need this data very quickly, and we don't have time to load it from flash
    float BufLoopStart[StorageBufferSize] = {0};

    float BufWrite[StorageBufferSize] = {0};
    float BufRead[StorageBufferSize] = {0};
    float BufReadNext[StorageBufferSize] = {0};

    int BufIdx = 0;
    int BufIdxTotal = 0;
    int FlashIdxRead = 0;
    int FlashIdxWrite = 0;
    int TotalLength = 0;
    int TotalStorageArea = 0;
    RecordingMode Mode = RecordingMode::Stopped;

public:

    inline FlashReaderWriter(const char* fileSuffix = "")
    {
        strcpy(BufferFileName, BaseFilePath);
        strcat(BufferFileName, fileSuffix);
        Serial.print("Buffer file: ");
        Serial.println(BufferFileName);
    }

    inline void Init()
    {
        Serial.println("-------------------------------------------");
        Serial.println(BufferFileName);

        if (!sd.begin(P_SPI_SD_CS, SPI_FULL_SPEED))
        {
            sd.initErrorHalt(&Serial);
            Serial.println("Bad stuff");
        }
        Serial.println("SD Card initialization done.");
        
        bool initFile = true;
        sd.mkdir("DejaVu");

        if (!file.open(BufferFileName, O_RDWR | O_CREAT | O_TRUNC)) 
            Serial.println("Failed to open file");
        else
            Serial.println("File created");

        if (initFile)
        {
            Serial.println("About to allocate...");
            bool allocResult = file.preAllocate(17280000);
            file.seek(0);
            auto size = file.size();
            Serial.print("Allocation result: ");
            Serial.print(allocResult);
            Serial.print(" -- new file size: ");
            Serial.println(size);
        }
        Serial.println("Flash buffer ready");

        WriteOps[0].Pending = false;
        WriteOps[1].Pending = false;
        ReadOps[0].Pending = false;
        ReadOps[1].Pending = false;
    }

    inline void SetMode(RecordingMode mode)
    {
        Mode = mode;
    }

    inline RecordingMode GetMode()
    {
        return Mode;
    }

    inline void SetTotalLength(int len)
    {
        TotalLength = len;
        
        // figure out how many storage buffers are needed to store TotalLength samples
        TotalStorageArea = (TotalLength / StorageBufferSize) * StorageBufferSize;
        if (TotalLength % StorageBufferSize > 0)
            TotalStorageArea += StorageBufferSize;

        ts();
        Serial.print("Setting TotalLength to: ");
        Serial.print(TotalLength);
        Serial.print(" :: ");
        Serial.println(TotalStorageArea);
    }

    inline void PreparePlay()
    {
        ts();
        Serial.println("Preparing play...");
        // We load the BufLoopStart into the queued buffer, and then invoke AdvanceRead which copies it into the main buffer
        Copy(BufReadNext, BufLoopStart, StorageBufferSize);
        FlashIdxRead = StorageBufferSize;
        FlashIdxWrite = 0;
        BufIdx = 0;
        BufIdxTotal = 0;
        AdvanceRead();
    }

    inline void AdvanceRead()
    {
        ts();
        Serial.print("Advance Read with ");
        Serial.print(BufIdx);
        Serial.println(" samples");

        Copy(BufRead, BufReadNext, StorageBufferSize);
        ZeroBuffer(BufReadNext, StorageBufferSize);

        if (ReadOps[ReadOpsHead].Pending)
            Serial.println("While trying to read - operations have not completed!");
        ReadOps[ReadOpsHead].FlashIdx = FlashIdxRead;
        ReadOps[ReadOpsHead].Pending = true;
        ReadOpsHead = (ReadOpsHead + 1) % 2;
        FlashIdxRead += StorageBufferSize;
        if (FlashIdxRead >= TotalStorageArea)
            FlashIdxRead = 0;

        shouldReadCurrentBuffer = false;
    }

    inline void AdvanceWrite()
    {
        ts();
        Serial.print("Advance Write with ");
        Serial.print(BufIdx);
        Serial.println(" samples");

        if (WriteOps[WriteOpsHead].Pending)
            Serial.println("While trying to write - operations have not completed!");
        WriteOps[WriteOpsHead].FlashIdx = FlashIdxWrite;
        WriteOps[WriteOpsHead].Pending = true;
        if (Mode == RecordingMode::Overdub) // when overdubbing, mix existing data into the recording buffer
            Mix(BufWrite, BufRead, 1.0, StorageBufferSize);
        Copy(WriteOps[WriteOpsHead].Data, BufWrite, StorageBufferSize);
        ZeroBuffer(BufWrite, StorageBufferSize);
        WriteOpsHead = (WriteOpsHead + 1) % 2;
        FlashIdxWrite += StorageBufferSize;
        if (FlashIdxWrite >= TotalStorageArea && TotalStorageArea != 0)
            FlashIdxWrite = 0;

        shouldWriteCurrentBuffer = false;
    }

    bool shouldReadCurrentBuffer = false;
    bool shouldWriteCurrentBuffer = false;

    inline void Process(float* input, float* output, int bufSize)
    {
        auto shouldReadNow = Mode == RecordingMode::Playback || Mode == RecordingMode::Overdub;
        auto shouldWriteNow = Mode == RecordingMode::Recording || Mode == RecordingMode::Overdub;
        shouldReadCurrentBuffer = shouldReadCurrentBuffer || shouldReadNow;
        shouldWriteCurrentBuffer = shouldWriteCurrentBuffer || shouldWriteNow;

        if (BufIdx >= StorageBufferSize || (BufIdxTotal >= TotalLength && TotalLength != 0))
        {
            if (shouldReadCurrentBuffer)
                AdvanceRead();
            if (shouldWriteCurrentBuffer)
                AdvanceWrite();

            if (BufIdxTotal >= TotalLength && TotalLength != 0)
            {
                BufIdxTotal = 0;
                ts();
                Serial.println("Resetting BufIdxTotal");
            }
            BufIdx = 0;
        }

        if (shouldReadNow)
            Copy(output, &BufRead[BufIdx], bufSize);

        if (shouldWriteNow)
            Copy(&BufWrite[BufIdx], input, bufSize);
        else
            ZeroBuffer(&BufWrite[BufIdx], bufSize);

        BufIdx += bufSize;
        BufIdxTotal += bufSize;
    }

    inline void ProcessReadOperation(FlashOperation* op)
    {
        ts();
        Serial.print("Read op pending. Reading from flashIdx ");
        Serial.println(op->FlashIdx);

        if (op->FlashIdx >= TotalStorageArea && TotalStorageArea != 0)
        {
            ts();
            Serial.print("Trying to read out of bound flash data at Index ");
            Serial.print(op->FlashIdx);
            Serial.println(" -- SHOULD NOT HAPPEN aborting read");
            op->Pending = false;
            return;
        }

        if (op->FlashIdx == 0)
        {
            ts();
            Serial.println("Reading FlashIdx 0 from RAM");
            // Cheat and read the data at index 0 from ram, not flash
            Copy(BufReadNext, BufLoopStart, StorageBufferSize);
        }
        else
        {
            file.seek(op->FlashIdx * 4);
            file.read((int8_t*)BufReadNext, StorageBufferSize * 4);
        }
        op->Pending = false;
    }

    inline void ProcessWriteOperation(FlashOperation* op)
    {
        ts();
        Serial.print("Write op pending. Writing to flashIdx ");
        Serial.println(op->FlashIdx);

        // store the very first buffer in RAM for fast access
        if (op->FlashIdx == 0)
        {
            ts();
            Serial.println("Storing LoopStart");
            Copy(BufLoopStart, op->Data, StorageBufferSize);
        }
        
        file.seek(op->FlashIdx * 4);
        file.write((int8_t*)op->Data, StorageBufferSize * 4);
        op->Pending = false;
    }

    inline void ProcessFlashOperations()
    {
        while (WriteOpsHead != WriteOpsTail)
        {
            auto op = &WriteOps[WriteOpsTail];
            if (op->Pending)
                ProcessWriteOperation(op);
            
            WriteOpsTail = (WriteOpsTail + 1) % 2;
        }

        while (ReadOpsHead != ReadOpsTail)
        {
            auto op = &ReadOps[ReadOpsTail];
            if (op->Pending)
                ProcessReadOperation(op);
            
            ReadOpsTail = (ReadOpsTail + 1) % 2;
        }
    }
};

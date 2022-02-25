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

class FlashRecording
{
    SdFat sd;
    SdFile file;

    const static int StorageBufferSize = 4096; // must be multiple of the BUFFER_SIZE
    const static int OperationsBufferSize = 4;

    struct FlashOperation
    {
        int Buffer01 = 0;
        int FlashIdx = 0;
        bool Write = false; // false= read, true=write
        bool Pending = false;
        float Data[StorageBufferSize] = {0};
    };

    // circular buffer for operations processed async
    FlashOperation Operations[OperationsBufferSize];
    int OpsReadIdx = 0;
    int OpsWriteIdx = 0;

    char BufferFileName[64];

    // These buffers store the data from the first block in flash.
    // We do this because when the loop comes around, we need this data very quickly, and we don't have time to load it from flash
    float BufLoopStart0[StorageBufferSize] = {0};
    float BufLoopStart1[StorageBufferSize] = {0};

    float BufRec0[StorageBufferSize] = {0};
    float BufRec1[StorageBufferSize] = {0};
    float BufPlay0[StorageBufferSize] = {0};
    float BufPlay1[StorageBufferSize] = {0};
    //int PlayFlashIdx0 = 0; // the index of the data in bufPlay0 in flash memory
    //int PlayFlashIdx1 = 0; // the index of the data in bufPlay1 in flash memory
    int BufIdx = 0;
    int FlashIdxRead = 0;
    int FlashIdxWrite = 0;
    int ActiveBuf = 0;
    int ProcessedSamples = 0;
    int MaxAllowedFlashReadIdx = 0;
    RecordingMode Mode = RecordingMode::Stopped;

public:
    inline FlashRecording(const char* fileSuffix = "")
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
            /*
            Serial.println("Initialising flash buffer...");
            for (size_t i = 0; i < 1728; i++) // 6 minute buffer
            {
                file.write(zeros, 10000);
            }
            file.seek(0);*/
        }
        Serial.println("Flash buffer ready");

        Operations[0].Pending = false;
        Operations[1].Pending = false;
        Operations[2].Pending = false;
        Operations[3].Pending = false;
    }

    inline int GetProcessedSamples()
    {
        return ProcessedSamples;
    }

    inline void SetMode(RecordingMode mode)
    {
        Mode = mode;
    }

    inline RecordingMode GetMode()
    {
        return Mode;
    }

    inline void PreloadStartData()
    {
        // This basically just skips reading the first 2 buffers from flash, and reads it from RAM instead for quick loading
        Copy(BufPlay0, BufLoopStart0, StorageBufferSize);
        Copy(BufPlay1, BufLoopStart1, StorageBufferSize);
        FlashIdxRead = 2 * StorageBufferSize;
        Serial.println("Setting FlashIdxWrite to 0");
        FlashIdxWrite = 0; // we want recorded data to sync up with what's being played
        //PlayFlashIdx0 = 0;
        //PlayFlashIdx1 = StorageBufferSize;
    }

    inline void ResetPtr()
    {
        Serial.println("Resetting pointers to zero");
        BufIdx = 0;
        FlashIdxRead = 0;
        Serial.println("Setting FlashIdxWrite to 0");
        FlashIdxWrite = 0;
        ActiveBuf = 0;
        ProcessedSamples = 0;
        PreloadStartData();
    }

    void FlushEndBufferAsync()
    {
        // In case we have a half-filled buffer when we stop recording, we must store this data to flash.
        // We also set the maximum buffer index readable from flash, and any read ops trying to access
        // data beyond this boundary will be discarded. This is so that things behave correctly at the
        // boundary when loading the loopStart samples. Since these operations are async we could overwrite
        // the start values with out-of-bounds data from flash.
        bool shouldWrite = Mode == RecordingMode::Recording || Mode == RecordingMode::Overdub;
        if (!shouldWrite)
            return;

        //int writeIdx = FlashIdxWrite;
        // when overdubbing, we make sure that we're writing the recorded data into the same buffer that was
        // playing, to make the recordings line up. otherwise overdub will lag behind.
        //if (Mode == RecordingMode::Overdub)
        //    writeIdx = ActiveBuf == 0 ? PlayFlashIdx0 : PlayFlashIdx1;

        Operations[OpsWriteIdx].Buffer01 = ActiveBuf;
        Operations[OpsWriteIdx].FlashIdx = FlashIdxWrite;
        Operations[OpsWriteIdx].Write = true;
        Operations[OpsWriteIdx].Pending = true;
        Copy(Operations[OpsWriteIdx].Data, ActiveBuf == 0 ? BufRec0 : BufRec1, StorageBufferSize);
        OpsWriteIdx = (OpsWriteIdx + 1) % OperationsBufferSize;

        Serial.println("Flushing end buffer");

        if (Mode == RecordingMode::Recording)
        {
            Serial.print("Setting max ReadIdx to ");
            Serial.println(FlashIdxWrite);
            MaxAllowedFlashReadIdx = FlashIdxWrite;
        }
    }

    inline void Process(float* input, float* output, int bufSize)
    {
        bool shouldRead = Mode == RecordingMode::Playback || Mode == RecordingMode::Overdub;
        bool shouldWrite = Mode == RecordingMode::Recording || Mode == RecordingMode::Overdub;

        if (ActiveBuf == 0)
        {
            if (shouldRead)
                Copy(output, &BufPlay0[BufIdx], bufSize);

            if (shouldWrite)
                Copy(&BufRec0[BufIdx], input, bufSize);
            else
                ZeroBuffer(&BufRec0[BufIdx], bufSize);
        }
        else
        {
            if (shouldRead)
                Copy(output, &BufPlay1[BufIdx], bufSize);

            if (shouldWrite)
                Copy(&BufRec1[BufIdx], input, bufSize);
            else
                ZeroBuffer(&BufRec1[BufIdx], bufSize);
        }

        BufIdx += bufSize;
        ProcessedSamples += bufSize;
        if (BufIdx >= StorageBufferSize)
        {
            if (shouldRead)
            {
                if (Operations[OpsWriteIdx].Pending)
                    Serial.println("While trying to read - operations have not completed!");
                Operations[OpsWriteIdx].Buffer01 = ActiveBuf;
                Operations[OpsWriteIdx].FlashIdx = FlashIdxRead;
                Operations[OpsWriteIdx].Write = false;
                Operations[OpsWriteIdx].Pending = true;
                OpsWriteIdx = (OpsWriteIdx + 1) % OperationsBufferSize;
            }
            if (shouldWrite)
            {
                //int writeIdx = FlashIdxWrite;
                // when overdubbing, we make sure that we're writing the recorded data into the same buffer that was
                // playing, to make the recordings line up. otherwise overdub will lag behind.
                //if (Mode == RecordingMode::Overdub)
                //    writeIdx = ActiveBuf == 0 ? PlayFlashIdx0 : PlayFlashIdx1;
                Serial.print("Creating write operation at index ");
                Serial.println(FlashIdxWrite);

                if (Operations[OpsWriteIdx].Pending)
                    Serial.println("While trying to write - operations have not completed!");
                Operations[OpsWriteIdx].Buffer01 = ActiveBuf;
                Operations[OpsWriteIdx].FlashIdx = FlashIdxWrite;
                Operations[OpsWriteIdx].Write = true;
                Operations[OpsWriteIdx].Pending = shouldWrite;
                if (Mode == RecordingMode::Overdub) // when overdubbing, mix existing data into the recording buffer
                    Mix(ActiveBuf == 0 ? BufRec0 : BufRec1, ActiveBuf == 0 ? BufPlay0 : BufPlay1, 1.0, StorageBufferSize);
                Copy(Operations[OpsWriteIdx].Data, ActiveBuf == 0 ? BufRec0 : BufRec1, StorageBufferSize);
                OpsWriteIdx = (OpsWriteIdx + 1) % OperationsBufferSize;
            }

            ActiveBuf = ActiveBuf == 0 ? 1 : 0;
            BufIdx = 0;
        }
    }

    inline void ProcessReadOperation(FlashOperation* op)
    {
        Serial.print("Read op pending. Reading from flashIdx ");
        Serial.print(op->FlashIdx);
        Serial.print(" into buffer ");
        Serial.print(op->Buffer01);
        Serial.print(" -- current read buffer: ");
        Serial.println(ActiveBuf);

        if (op->FlashIdx > MaxAllowedFlashReadIdx)
        {
            Serial.print("Trying to read out of bound flash data at Index ");
            Serial.print(op->FlashIdx);
            Serial.println(" -- aborting read");
            op->Pending = false;
            return;
        }

        file.seek(op->FlashIdx * 4);
        if (op->Buffer01 == 0)
        {
            file.read((int8_t*)BufPlay0, StorageBufferSize * 4);
            //PlayFlashIdx0 = op->FlashIdx;
        }
        else
        {
            file.read((int8_t*)BufPlay1, StorageBufferSize * 4);
            //PlayFlashIdx1 = op->FlashIdx;
        }
        op->Pending = false;
        FlashIdxRead += StorageBufferSize;
        //Serial.println("Done with Read Op");
    }

    inline void ProcessWriteOperation(FlashOperation* op)
    {
        Serial.print("Write op pending. Writing to flashIdx ");
        Serial.print(op->FlashIdx);
        Serial.print(" from buffer ");
        Serial.println(op->Buffer01);

        // store the very first 2 buffers in RAM for fast access
        if (op->FlashIdx == 0)
        {
            Serial.println("Storing LoopStart");
            Copy(BufLoopStart, op->Data, StorageBufferSize);
        }
        
        file.seek(op->FlashIdx * 4);
        file.write((int8_t*)op->Data, StorageBufferSize * 4);
        op->Pending = false;
        FlashIdxWrite += StorageBufferSize;
        //Serial.println("Done with Write Op");
    }

    inline void ProcessFlashOperations()
    {
        while (OpsReadIdx != OpsWriteIdx)
        {
            auto op = &Operations[OpsReadIdx];
            if (op->Pending)
            {
                if (op->Write)
                    ProcessWriteOperation(op);
                else
                    ProcessReadOperation(op);
            }
            
            OpsReadIdx = (OpsReadIdx + 1) % OperationsBufferSize;
        }
    }

};

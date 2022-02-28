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

class FlashReaderWriter
{
    SdFat sd;
    SdFile file;

    const static int StorageBufferSize = 4096; // must be multiple of the BUFFER_SIZE

    struct FlashReadOp
    {
        int FlashIdx = 0;
        bool Pending = false;
        int OperationId = 0;
    };

    struct FlashWriteOp
    {
        int FlashIdx = 0;
        bool Pending = false;
        int OperationId = 0;
        float Data[StorageBufferSize] = {0};
    };

     // circular buffer for operations processed async
    const static int OpBufferSize = 3;
    FlashWriteOp WriteOps[OpBufferSize];
    FlashReadOp ReadOps[OpBufferSize];
    int ReadOpsHead = 0;
    int ReadOpsTail = 0;
    int WriteOpsHead = 0;
    int WriteOpsTail = 0;
    int OperationId = 0;

    char BufferFileName[64];
    char SaveFileName[70];

    // These buffers store the data from the first block in flash.
    // We do this because when the loop comes around, we need this data very quickly, and we don't have time to load it from flash
    float BufLoopStart0[StorageBufferSize] = {0};
    float BufLoopStart1[StorageBufferSize] = {0};

    float BufWrite[StorageBufferSize] = {0};
    float BufRead[StorageBufferSize] = {0};
    float BufReadNext[StorageBufferSize] = {0};
    float BufReadNextNext[StorageBufferSize] = {0};
    
    int BufReadIdx = 0;
    int BufReadNextIdx = 0;
    int BufReadNextNextIdx = 0;

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
        LogInfof("Buffer file: %s", BufferFileName)
    }

    inline void SetRecordingFile(int slot)
    {
        strcpy(SaveFileName, BufferFileName);
        sprintf(&SaveFileName[strlen(SaveFileName)], ".%d", slot);
        LogInfof("Loading/Storing to file: %s", SaveFileName);
    }

    inline void SaveRecording(int slot)
    {
        AudioDisable();
        SetRecordingFile(slot);
        SdFile saveFile;
        if (!saveFile.open(SaveFileName, O_RDWR | O_CREAT | O_TRUNC))
            LogError("Failed to open save file")
        else
            LogInfo("SaveFile created")

        saveFile.write((uint8_t*)&TotalLength, sizeof(int));
        saveFile.write((uint8_t*)&TotalStorageArea, sizeof(int));
        LogInfof("Written length and storage info: %d :: %d", TotalLength, TotalStorageArea)
        float buf[StorageBufferSize];

        int i = 0;
        int chunkCount = TotalStorageArea / StorageBufferSize;
        file.seek(0);
        while(i < chunkCount)
        {
            int result = file.read((uint8_t*)buf, StorageBufferSize * 4);
            if (result <= 0)
            {
                LogInfo("Exiting file save operation")
                break;
            }
            saveFile.write((uint8_t*)buf, StorageBufferSize * 4);
            i++;
            LogInfof("Saved chunk %d of %d", i, chunkCount)
        }

        saveFile.close();

        if (i == chunkCount)
            LogInfof("Successfully saved content in slot %d", slot)

        AudioEnable();
    }

    inline int LoadRecording(int slot)
    {
        AudioDisable();
        SetRecordingFile(slot);
        SdFile saveFile;
        if (!sd.exists(SaveFileName))
            return 1;

        if (!saveFile.open(SaveFileName, O_RDONLY))
        {
            LogError("Failed to open save file")
            return 2;
        }
        else
            LogInfo("SaveFile opened")

        int readTotalLen, readTotalStorageArea;
        saveFile.read((uint8_t*)&readTotalLen, sizeof(int));
        saveFile.read((uint8_t*)&readTotalStorageArea, sizeof(int));
        LogInfof("Read length and storage info: %d :: %d", readTotalLen, readTotalStorageArea)
        float buf[StorageBufferSize];

        int i = 0;
        int chunkCount = readTotalStorageArea / StorageBufferSize;
        file.seek(0);
        while(i < chunkCount)
        {
            int result = saveFile.read((uint8_t*)buf, StorageBufferSize * 4);
            if (result <= 0)
            {
                LogInfo("Exiting file load operation")
                return 2;
            }
            file.write((uint8_t*)buf, StorageBufferSize * 4);
            i++;
            LogInfof("Loaded chunk %d of %d", i, chunkCount)
        }

        file.seek(0);
        file.read((uint8_t*)BufLoopStart0, StorageBufferSize * 4);
        file.read((uint8_t*)BufLoopStart1, StorageBufferSize * 4);
        file.seek(0);

        saveFile.close();

        if (i == chunkCount)
            LogInfof("Successfully loaded content from slot %d", slot)

        TotalLength = readTotalLen;
        TotalStorageArea = readTotalStorageArea;
        PreparePlay();
        AudioEnable();
        return 0;
    }

    void SetFixedLength(int sampleCount)
    {
        LogInfof("Setting fixed length of %d samples", sampleCount)
        AudioDisable();
        SetTotalLength(sampleCount);
        float buf[StorageBufferSize];
        ZeroBuffer(buf, StorageBufferSize);

        int i = 0;
        int chunkCount = TotalStorageArea / StorageBufferSize;
        file.seek(0);
        while(i < chunkCount)
        {
            file.write((uint8_t*)buf, StorageBufferSize * 4);
            i++;
            LogInfof("Zeroed chunk %d of %d", i, chunkCount)
        }

        file.seek(0);
        ZeroBuffer(BufLoopStart0, StorageBufferSize);
        ZeroBuffer(BufLoopStart1, StorageBufferSize);
        PreparePlay();
        AudioEnable();
    }

    inline void Init()
    {
        LogInfo("-------------------------------------------")
        LogInfo(BufferFileName)

        if (!sd.begin(P_SPI_SD_CS, 60000000))
        {
            sd.initErrorHalt(&Serial);
            LogError("Unable to initialise SD Card!")
        }
        LogInfo("SD Card initialization done.")
        
        bool initFile = true;
        sd.mkdir("DejaVu");

        if (!file.open(BufferFileName, O_RDWR | O_CREAT | O_TRUNC))
            LogError("Failed to open buffer file")
        else
            LogInfo("Bufferfile created")

        if (initFile)
        {
            LogInfo("About to allocate...")
            bool allocResult = file.preAllocate(17280000);
            file.seek(0);
            auto size = file.size();
            LogInfof("Allocation result: %s -- new file size: %d", allocResult ? "true" : "false", size)
        }
        LogInfo("Flash buffer ready")

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

        LogDebugf("Setting TotalLength to: %d :: %d", TotalLength, TotalStorageArea)
    }

    inline void PreparePlay()
    {
        LogDebug("Preparing play...")
        // We load the BufLoopStart into the queued buffer, and then invoke AdvanceRead which copies it into the main buffer
        Copy(BufReadNext, BufLoopStart0, StorageBufferSize);
        Copy(BufReadNextNext, BufLoopStart1, StorageBufferSize);
        BufReadNextIdx = 0;
        BufReadNextNextIdx = StorageBufferSize;
        FlashIdxRead = 2 * StorageBufferSize;
        FlashIdxWrite = 0;
        BufIdx = 0;
        BufIdxTotal = 0;
        AdvanceRead();
    }

    inline void AdvanceRead()
    {
        LogDebugf("Advance Read with %d samples. OpId %d", BufIdx, OperationId)

        Copy(BufRead, BufReadNext, StorageBufferSize);
        Copy(BufReadNext, BufReadNextNext, StorageBufferSize);
        ZeroBuffer(BufReadNextNext, StorageBufferSize);
        BufReadIdx = BufReadNextIdx;
        BufReadNextIdx = BufReadNextNextIdx;

        if (ReadOps[ReadOpsHead].Pending)
            LogWarn("While trying to read - operations have not completed!")
        ReadOps[ReadOpsHead].FlashIdx = FlashIdxRead;
        ReadOps[ReadOpsHead].OperationId = OperationId;
        ReadOps[ReadOpsHead].Pending = true;
        ReadOpsHead = (ReadOpsHead + 1) % OpBufferSize;
        FlashIdxRead += StorageBufferSize;
        if (FlashIdxRead >= TotalStorageArea)
            FlashIdxRead = 0;

        shouldReadCurrentBuffer = false;
        OperationId++;
    }

    inline void AdvanceWrite()
    {
        LogDebugf("Advance Write with %d samples. OpId %d", BufIdx, OperationId)

        if (WriteOps[WriteOpsHead].Pending)
            LogWarn("While trying to write - operations have not completed!")
        WriteOps[WriteOpsHead].FlashIdx = FlashIdxWrite;
        WriteOps[WriteOpsHead].OperationId = OperationId;
        WriteOps[WriteOpsHead].Pending = true;
        if (shouldForceOverdub)
        {
            Mix(BufWrite, BufRead, 1.0, StorageBufferSize);
            WriteOps[WriteOpsHead].FlashIdx = BufReadIdx;
            LogDebugf("Overdubbing, using BufReadIdx as flash Index: %d", BufReadIdx)
        }
        Copy(WriteOps[WriteOpsHead].Data, BufWrite, StorageBufferSize);
        ZeroBuffer(BufWrite, StorageBufferSize);
        WriteOpsHead = (WriteOpsHead + 1) % OpBufferSize;
        FlashIdxWrite += StorageBufferSize;
        if (FlashIdxWrite >= TotalStorageArea && TotalStorageArea != 0)
            FlashIdxWrite = 0;

        shouldWriteCurrentBuffer = false;
        shouldForceOverdub = false;
        OperationId++;
    }

    bool shouldReadCurrentBuffer = false;
    bool shouldWriteCurrentBuffer = false;
    bool shouldForceOverdub = false;

    inline void Process(float* input, float* output, int bufSize)
    {
        auto shouldReadNow = Mode == RecordingMode::Playback || Mode == RecordingMode::Overdub;
        auto shouldWriteNow = Mode == RecordingMode::Recording || Mode == RecordingMode::Overdub;
        shouldReadCurrentBuffer = shouldReadCurrentBuffer || shouldReadNow;
        shouldWriteCurrentBuffer = shouldWriteCurrentBuffer || shouldWriteNow;
        shouldForceOverdub = shouldForceOverdub || Mode == RecordingMode::Overdub;

        if (BufIdx >= StorageBufferSize || (BufIdxTotal >= TotalLength && TotalLength != 0))
        {
            if (shouldWriteCurrentBuffer)
                AdvanceWrite();

            if (shouldReadCurrentBuffer)
                AdvanceRead();

            if (BufIdxTotal >= TotalLength && TotalLength != 0)
            {
                BufIdxTotal = 0;
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

    inline void ProcessReadOperation(FlashReadOp* op)
    {
        auto t1 = micros();
        LogDebugf("Processing Read operation %d. Reading from flashIdx %d", op->OperationId, op->FlashIdx)

        if (op->FlashIdx >= TotalStorageArea && TotalStorageArea != 0)
        {
            LogWarnf("Trying to read out of bound flash data at Index %d - Aborting Read", op->FlashIdx)
            op->Pending = false;
            return;
        }

        if (op->FlashIdx == 0)
        {
            LogDebug("Reading FlashIdx 0 from RAM")
            // Cheat and read the data at index 0 from ram, not flash
            Copy(BufReadNextNext, BufLoopStart0, StorageBufferSize);
        }
        else if (op->FlashIdx == StorageBufferSize)
        {
            LogDebug("Reading FlashIdx +1buffer from RAM")
            // Cheat and read the data at index StorageBufferSize from ram, not flash
            Copy(BufReadNextNext, BufLoopStart1, StorageBufferSize);
        }
        else
        {
            file.seek(op->FlashIdx * 4);
            file.read((int8_t*)BufReadNextNext, StorageBufferSize * 4);
        }
        BufReadNextNextIdx = op->FlashIdx;
        op->Pending = false;
        auto t2 = micros();
        LogDebugf("ProcessReadOp time: %d", (t2-t1))
    }

    inline void ProcessWriteOperation(FlashWriteOp* op)
    {
        auto t1 = micros();
        LogDebugf("Processing Write operation %d. Writing to flashIdx %d", op->OperationId, op->FlashIdx)

        // store the first buffers in RAM for fast access
        if (op->FlashIdx == 0)
        {
            LogDebug("Storing LoopStart0")
            Copy(BufLoopStart0, op->Data, StorageBufferSize);
        }
        if (op->FlashIdx == StorageBufferSize)
        {
            LogDebug("Storing LoopStart1")
            Copy(BufLoopStart1, op->Data, StorageBufferSize);
        }
        
        file.seek(op->FlashIdx * 4);
        file.write((int8_t*)op->Data, StorageBufferSize * 4);
        op->Pending = false;
        auto t2 = micros();
        LogDebugf("ProcessWriteOp time: %d", (t2-t1))
    }

    inline void ProcessFlashOperations()
    {
        while (ReadOpsHead != ReadOpsTail)
        {
            auto op = &ReadOps[ReadOpsTail];
            if (op->Pending)
                ProcessReadOperation(op);
            
            ReadOpsTail = (ReadOpsTail + 1) % OpBufferSize;
        }

        while (WriteOpsHead != WriteOpsTail)
        {
            auto op = &WriteOps[WriteOpsTail];
            if (op->Pending)
                ProcessWriteOperation(op);
            
            WriteOpsTail = (WriteOpsTail + 1) % OpBufferSize;
        }
    }
};

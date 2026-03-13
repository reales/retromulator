#include "akaiIsoReader.h"
#include <algorithm>
#include <cstring>

namespace akaiLib
{

// ══════════════════════════════════════════════════════════════════════════════
// RawCDSectorInputStream — transparently reads user data from raw CD sectors
// ══════════════════════════════════════════════════════════════════════════════

class RawCDSectorInputStream : public juce::InputStream
{
public:
    static int detectSectorSize(juce::InputStream& stream)
    {
        stream.setPosition(0);
        uint8_t header[16];
        if (stream.read(header, 16) != 16)
            return 0;

        static const uint8_t syncPattern[12] = {
            0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0x00
        };
        if (memcmp(header, syncPattern, 12) != 0)
            return 0;

        uint8_t mode = header[15];
        if (mode != 1 && mode != 2)
            return 0;

        stream.setPosition(2352);
        uint8_t header2[12];
        if (stream.read(header2, 12) != 12)
            return 0;
        if (memcmp(header2, syncPattern, 12) != 0)
            return 0;

        return 2352;
    }

    RawCDSectorInputStream(juce::InputStream* source, bool ownsSource, int rawSectorSize, int mode)
        : sourceStream(source), ownsStream(ownsSource), sectorSize(rawSectorSize),
          userDataOffset(mode == 2 ? 24 : 16),
          userDataSize(2048),
          logicalSize((source->getTotalLength() / rawSectorSize) * 2048),
          logicalPosition(0)
    {
    }

    ~RawCDSectorInputStream() override
    {
        if (ownsStream)
            delete sourceStream;
    }

    juce::int64 getTotalLength() override { return logicalSize; }
    bool isExhausted() override { return logicalPosition >= logicalSize; }
    juce::int64 getPosition() override { return logicalPosition; }

    bool setPosition(juce::int64 newPosition) override
    {
        logicalPosition = juce::jlimit((juce::int64)0, logicalSize, newPosition);
        return true;
    }

    int read(void* destBuffer, int maxBytesToRead) override
    {
        if (logicalPosition >= logicalSize || maxBytesToRead <= 0)
            return 0;

        uint8_t* dest = static_cast<uint8_t*>(destBuffer);
        int totalRead = 0;

        while (totalRead < maxBytesToRead && logicalPosition < logicalSize)
        {
            juce::int64 sectorIndex = logicalPosition / userDataSize;
            int offsetInSector = (int)(logicalPosition % userDataSize);
            juce::int64 physicalPos = sectorIndex * sectorSize + userDataOffset + offsetInSector;
            sourceStream->setPosition(physicalPos);

            int bytesLeftInSector = userDataSize - offsetInSector;
            int bytesToRead = juce::jmin(maxBytesToRead - totalRead, bytesLeftInSector);
            int bytesRead = sourceStream->read(dest + totalRead, bytesToRead);
            if (bytesRead <= 0)
                break;

            totalRead += bytesRead;
            logicalPosition += bytesRead;
        }

        return totalRead;
    }

private:
    juce::InputStream* sourceStream;
    bool ownsStream;
    int sectorSize;
    int userDataOffset;
    int userDataSize;
    juce::int64 logicalSize;
    juce::int64 logicalPosition;
};

// ══════════════════════════════════════════════════════════════════════════════
// Public API
// ══════════════════════════════════════════════════════════════════════════════

bool AkaiIsoReader::canReadFile(const juce::File& file)
{
    if (!file.hasFileExtension("iso") && !file.hasFileExtension("img")
        && !file.hasFileExtension("akai") && !file.hasFileExtension("bin")
        && !file.hasFileExtension("cue"))
        return false;

    // For .cue files, find the associated .bin
    juce::File dataFile = file;
    if (file.hasFileExtension("cue"))
    {
        dataFile = file.withFileExtension("bin");
        if (!dataFile.existsAsFile())
            return false;
    }

    juce::FileInputStream stream(dataFile);
    if (stream.failedToOpen())
        return false;

    AkaiIsoReader reader;

    int sectorSize = RawCDSectorInputStream::detectSectorSize(stream);
    if (sectorSize > 0)
    {
        auto* rawStream = new juce::FileInputStream(dataFile);
        if (rawStream->failedToOpen())
        {
            delete rawStream;
            return false;
        }
        uint8_t mode = 1;
        rawStream->setPosition(15);
        rawStream->read(&mode, 1);
        rawStream->setPosition(0);
        RawCDSectorInputStream wrappedStream(rawStream, true, sectorSize, mode);
        return reader.isValidAkaiIso(wrappedStream);
    }

    stream.setPosition(0);
    return reader.isValidAkaiIso(stream);
}

bool AkaiIsoReader::open(const juce::File& file)
{
    m_partitions.clear();
    m_programRefs.clear();

    // For .cue files, find the associated .bin
    m_isoFile = file;
    juce::File dataFile = file;
    if (file.hasFileExtension("cue"))
    {
        dataFile = file.withFileExtension("bin");
        if (!dataFile.existsAsFile())
            return false;
        m_isoFile = file; // keep the .cue as the display name
    }

    auto* fileStream = new juce::FileInputStream(dataFile);
    if (fileStream->failedToOpen())
    {
        delete fileStream;
        return false;
    }

    std::unique_ptr<juce::InputStream> streamPtr;
    int sectorSize = RawCDSectorInputStream::detectSectorSize(*fileStream);
    if (sectorSize > 0)
    {
        uint8_t mode = 1;
        fileStream->setPosition(15);
        fileStream->read(&mode, 1);
        fileStream->setPosition(0);
        streamPtr.reset(new RawCDSectorInputStream(fileStream, true, sectorSize, mode));
    }
    else
    {
        fileStream->setPosition(0);
        streamPtr.reset(fileStream);
    }

    auto& stream = *streamPtr;

    if (!isValidAkaiIso(stream))
        return false;

    if (!scanPartitions(stream, m_partitions))
        return false;

    // Read volumes and file entries for each partition
    for (auto& partition : m_partitions)
    {
        if (!readFAT(stream, partition))
            continue;
        if (!readVolumes(stream, partition))
            continue;

        for (auto& volume : partition.volumes)
        {
            if (!volume.isActive())
                continue;
            readFileEntries(stream, partition, volume);
        }
    }

    // Collect all program references with partition labels A, B, C, D, E...
    for (int p = 0; p < m_partitions.size(); ++p)
    {
        const char partLabel = static_cast<char>('A' + p);

        for (int v = 0; v < m_partitions[p].volumes.size(); ++v)
        {
            const auto& volume = m_partitions[p].volumes[v];
            if (!volume.isActive())
                continue;

            for (int f = 0; f < volume.files.size(); ++f)
            {
                if (volume.files[f].isProgram())
                {
                    AkaiProgramRef ref;
                    ref.partitionIndex = p;
                    ref.volumeIndex = v;
                    ref.fileIndex = f;
                    ref.partitionLabel = juce::String::charToString(partLabel);
                    ref.volumeName = volume.name;
                    ref.programName = volume.files[f].name;
                    m_programRefs.add(ref);
                }
            }
        }
    }

    return !m_programRefs.isEmpty();
}

juce::String AkaiIsoReader::loadProgram(int programIndex,
                                         std::vector<std::pair<juce::String, juce::MemoryBlock>>& wavFiles)
{
    wavFiles.clear();

    if (programIndex < 0 || programIndex >= m_programRefs.size())
        return {};

    // Re-open the data file
    juce::File dataFile = m_isoFile;
    if (m_isoFile.hasFileExtension("cue"))
        dataFile = m_isoFile.withFileExtension("bin");

    auto* fileStream = new juce::FileInputStream(dataFile);
    if (fileStream->failedToOpen())
    {
        delete fileStream;
        return {};
    }

    std::unique_ptr<juce::InputStream> streamPtr;
    int sectorSize = RawCDSectorInputStream::detectSectorSize(*fileStream);
    if (sectorSize > 0)
    {
        uint8_t mode = 1;
        fileStream->setPosition(15);
        fileStream->read(&mode, 1);
        fileStream->setPosition(0);
        streamPtr.reset(new RawCDSectorInputStream(fileStream, true, sectorSize, mode));
    }
    else
    {
        fileStream->setPosition(0);
        streamPtr.reset(fileStream);
    }

    auto& stream = *streamPtr;

    const auto& ref = m_programRefs[programIndex];
    const auto& partition = m_partitions[ref.partitionIndex];
    const auto& volume = partition.volumes[ref.volumeIndex];
    const auto& fileEntry = volume.files[ref.fileIndex];

    juce::MemoryBlock programData = readFileData(stream, partition, fileEntry);
    if (programData.getSize() == 0)
        return {};

    AkaiProgram akaiProgram;
    if (!parseProgram(programData, akaiProgram))
        return {};

    // Build SFZ text and extract WAV files for each keygroup/zone
    juce::String sfzText;
    int sampleIndex = 0;

    for (const auto& keygroup : akaiProgram.keygroups)
    {
        bool zoneConsumed[4] = { false, false, false, false };

        for (int z = 0; z < keygroup.numZones; ++z)
        {
            if (zoneConsumed[z])
                continue;

            const auto& velZone = keygroup.zones[z];
            if (velZone.sampleName.isEmpty())
                continue;

            juce::String trimmedName = velZone.sampleName.trim();
            juce::String wavFileName;
            juce::MemoryBlock wavData;
            AkaiSampleInfo sampleInfo;
            int rootKey = keygroup.keyLow;

            // Detect stereo L/R pairs
            int leftIdx = -1, rightIdx = -1;
            if (trimmedName.endsWithIgnoreCase("-L"))
            {
                juce::String baseName = trimmedName.dropLastCharacters(2);
                for (int rz = 0; rz < keygroup.numZones; ++rz)
                {
                    if (rz == z || zoneConsumed[rz]) continue;
                    juce::String rName = keygroup.zones[rz].sampleName.trim();
                    if (rName.endsWithIgnoreCase("-R") && rName.dropLastCharacters(2) == baseName)
                    { leftIdx = z; rightIdx = rz; break; }
                }
            }
            else if (trimmedName.endsWithIgnoreCase("-R"))
            {
                juce::String baseName = trimmedName.dropLastCharacters(2);
                for (int lz = 0; lz < keygroup.numZones; ++lz)
                {
                    if (lz == z || zoneConsumed[lz]) continue;
                    juce::String lName = keygroup.zones[lz].sampleName.trim();
                    if (lName.endsWithIgnoreCase("-L") && lName.dropLastCharacters(2) == baseName)
                    { leftIdx = lz; rightIdx = z; break; }
                }
            }

            if (leftIdx >= 0 && rightIdx >= 0)
            {
                // Stereo pair
                const auto& leftZone = keygroup.zones[leftIdx];
                const auto& rightZone = keygroup.zones[rightIdx];
                const AkaiFileEntry* leftEntry = findSampleInVolume(volume, leftZone.sampleName);
                const AkaiFileEntry* rightEntry = findSampleInVolume(volume, rightZone.sampleName);

                if (leftEntry && rightEntry)
                {
                    juce::MemoryBlock leftData = readFileData(stream, partition, *leftEntry);
                    juce::MemoryBlock rightData = readFileData(stream, partition, *rightEntry);

                    if (leftData.getSize() > 0 && rightData.getSize() > 0)
                    {
                        AkaiSampleInfo leftInfo, rightInfo;
                        wavData = parseStereoSamplesToWav(leftData, rightData, leftInfo, rightInfo);
                        sampleInfo = leftInfo;
                        rootKey = juce::jlimit(0, 127, (int)leftInfo.originalPitch);
                    }
                }

                wavFileName = leftZone.sampleName.trim().dropLastCharacters(2)
                            + "_" + juce::String(sampleIndex) + ".wav";
                zoneConsumed[leftIdx] = true;
                zoneConsumed[rightIdx] = true;
            }
            else
            {
                // Mono sample
                const AkaiFileEntry* sampleEntry = findSampleInVolume(volume, velZone.sampleName);
                if (!sampleEntry)
                    continue;

                juce::MemoryBlock sampleData = readFileData(stream, partition, *sampleEntry);
                if (sampleData.getSize() == 0)
                    continue;

                wavData = parseSampleToWav(sampleData, sampleInfo);
                rootKey = juce::jlimit(0, 127, (int)sampleInfo.originalPitch);

                wavFileName = trimmedName + "_" + juce::String(sampleIndex) + ".wav";
            }

            if (wavData.getSize() == 0)
                continue;

            wavFiles.push_back({ wavFileName, std::move(wavData) });

            // Build SFZ region
            const int loKey = juce::jlimit(0, 127, keygroup.keyLow);
            const int hiKey = juce::jlimit(0, 127, keygroup.keyHigh);
            const int loVel = juce::jlimit(0, 127, velZone.velLow);
            const int hiVel = juce::jlimit(0, 127, velZone.velHigh);
            const int tune = (keygroup.tuneSemitones + velZone.tuneSemitones) * 100
                           + keygroup.tuneCents + velZone.tuneCents;

            sfzText += "<region> sample=" + wavFileName
                     + " lokey=" + juce::String(loKey)
                     + " hikey=" + juce::String(hiKey)
                     + " pitch_keycenter=" + juce::String(rootKey)
                     + " lovel=" + juce::String(loVel)
                     + " hivel=" + juce::String(hiVel);

            if (tune != 0)
                sfzText += " tune=" + juce::String(tune);

            if (velZone.loudnessOffset != 0)
                sfzText += " volume=" + juce::String(velZone.loudnessOffset);

            if (velZone.panOffset != 0)
                sfzText += " pan=" + juce::String(velZone.panOffset);

            // Loop points
            if (sampleInfo.loopStart >= 0 && sampleInfo.loopEnd > sampleInfo.loopStart)
            {
                sfzText += " loop_mode=loop_continuous"
                         " loop_start=" + juce::String(sampleInfo.loopStart)
                         + " loop_end=" + juce::String(sampleInfo.loopEnd);
            }

            sfzText += "\n";
            sampleIndex++;
        }
    }

    if (sfzText.isEmpty())
        return {};

    return "<group>\n" + sfzText;
}

// ══════════════════════════════════════════════════════════════════════════════
// ISO validation & partition scanning
// ══════════════════════════════════════════════════════════════════════════════

bool AkaiIsoReader::isValidAkaiIso(juce::InputStream& stream) const
{
    stream.setPosition(0);

    if (stream.getTotalLength() < (int64_t)(AKAI_BLOCK_SIZE * 4))
        return false;

    uint8_t buf[2];
    if (stream.read(buf, 2) != 2)
        return false;

    uint16_t partSize = readU16(buf);
    if (partSize == 0 || (partSize & AKAI_PARTITION_END_MARK))
        return false;
    if (partSize > 65535)
        return false;

    int64_t partBytes = (int64_t)partSize * AKAI_BLOCK_SIZE;
    if (partBytes > stream.getTotalLength())
        return false;

    stream.setPosition(AKAI_DIR_ENTRY_OFFSET);
    uint8_t volBuf[AKAI_DIR_ENTRY_SIZE];
    if (stream.read(volBuf, (int)AKAI_DIR_ENTRY_SIZE) != (int)AKAI_DIR_ENTRY_SIZE)
        return false;

    uint16_t volType = readU16(volBuf + 12);
    if (volType != 0 && volType != 1 && volType != 3)
        return false;

    if (volType != 0)
    {
        uint16_t volStart = readU16(volBuf + 14);
        if (volStart >= partSize)
            return false;
    }

    return true;
}

bool AkaiIsoReader::scanPartitions(juce::InputStream& stream, juce::Array<AkaiPartition>& partitions) const
{
    stream.setPosition(0);
    int64_t offset = 0;
    int64_t fileSize = stream.getTotalLength();

    while (offset < fileSize)
    {
        stream.setPosition(offset);
        uint8_t buf[2];
        if (stream.read(buf, 2) != 2)
            break;

        uint16_t sizeInBlocks = readU16(buf);
        if (sizeInBlocks == 0 || (sizeInBlocks & AKAI_PARTITION_END_MARK))
            break;

        int64_t partSizeBytes = (int64_t)sizeInBlocks * AKAI_BLOCK_SIZE;

        if (offset + partSizeBytes > fileSize)
        {
            AkaiPartition partition;
            partition.offset = offset;
            partition.sizeInBlocks = sizeInBlocks;
            partitions.add(partition);
            break;
        }

        AkaiPartition partition;
        partition.offset = offset;
        partition.sizeInBlocks = sizeInBlocks;
        partitions.add(partition);
        offset += partSizeBytes;
    }

    if (partitions.isEmpty() && fileSize >= AKAI_BLOCK_SIZE * 4)
    {
        AkaiPartition partition;
        partition.offset = 0;
        partition.sizeInBlocks = (uint32_t)(fileSize / AKAI_BLOCK_SIZE);
        partitions.add(partition);
    }

    return !partitions.isEmpty();
}

bool AkaiIsoReader::readFAT(juce::InputStream& stream, AkaiPartition& partition) const
{
    int64_t fatOffset = partition.offset + AKAI_FAT_OFFSET;
    stream.setPosition(fatOffset);

    int maxFatEntries = juce::jmin((int)partition.sizeInBlocks, 11387);
    partition.fat.clearQuick();
    partition.fat.ensureStorageAllocated(maxFatEntries);

    for (int i = 0; i < maxFatEntries; ++i)
    {
        uint8_t buf[2];
        if (stream.read(buf, 2) != 2)
            break;
        partition.fat.add(readU16(buf));
    }

    return partition.fat.size() > 0;
}

bool AkaiIsoReader::readVolumes(juce::InputStream& stream, AkaiPartition& partition) const
{
    int64_t volOffset = partition.offset + AKAI_DIR_ENTRY_OFFSET;
    stream.setPosition(volOffset);

    for (int i = 0; i < AKAI_MAX_VOLUMES; ++i)
    {
        uint8_t buf[AKAI_DIR_ENTRY_SIZE];
        if (stream.read(buf, (int)AKAI_DIR_ENTRY_SIZE) != (int)AKAI_DIR_ENTRY_SIZE)
            break;

        AkaiVolume volume;
        volume.name = akaiNameToString(buf, 12);
        volume.type = readU16(buf + 12);
        volume.startBlock = readU16(buf + 14);

        if (volume.isActive())
            partition.volumes.add(volume);
    }

    return !partition.volumes.isEmpty();
}

bool AkaiIsoReader::readFileEntries(juce::InputStream& stream, const AkaiPartition& partition, AkaiVolume& volume) const
{
    if (volume.startBlock >= partition.sizeInBlocks)
        return false;

    int64_t blockOffset = partition.offset + (int64_t)volume.startBlock * AKAI_BLOCK_SIZE;
    stream.setPosition(blockOffset);

    int maxEntries = volume.isS3000() ? AKAI_MAX_FILE_ENTRIES_S3000 : AKAI_MAX_FILE_ENTRIES_S1000;
    int blocksToRead = volume.isS3000() ? 2 : 1;
    int totalBytes = blocksToRead * AKAI_BLOCK_SIZE;

    juce::MemoryBlock entryData((size_t)totalBytes);
    int bytesRead = stream.read(entryData.getData(), totalBytes);
    if (bytesRead < (int)AKAI_BLOCK_SIZE)
        return false;

    const uint8_t* data = static_cast<const uint8_t*>(entryData.getData());
    int availableEntries = bytesRead / AKAI_FILE_ENTRY_SIZE;
    maxEntries = juce::jmin(maxEntries, availableEntries);

    for (int i = 0; i < maxEntries; ++i)
    {
        const uint8_t* entry = data + (i * AKAI_FILE_ENTRY_SIZE);

        bool isEmpty = true;
        for (int j = 0; j < (int)AKAI_FILE_ENTRY_SIZE; ++j)
        {
            if (entry[j] != 0) { isEmpty = false; break; }
        }
        if (isEmpty)
            continue;

        bool validName = true;
        for (int j = 0; j < 12; ++j)
        {
            if (entry[j] > 40) { validName = false; break; }
        }
        if (!validName)
            continue;

        AkaiFileEntry fileEntry;
        fileEntry.name = akaiNameToString(entry, 12);
        fileEntry.type = entry[0x10];
        fileEntry.size = readU24(entry + 0x11);
        fileEntry.startBlock = readU16(entry + 0x14);

        if (fileEntry.type == 0 || fileEntry.name.isEmpty())
            continue;

        if (fileEntry.isProgram() || fileEntry.isSample())
            volume.files.add(fileEntry);
    }

    return true;
}

juce::MemoryBlock AkaiIsoReader::readFileData(juce::InputStream& stream, const AkaiPartition& partition, const AkaiFileEntry& entry) const
{
    juce::MemoryBlock result;

    if (entry.startBlock >= partition.sizeInBlocks || entry.size == 0)
        return result;

    result.ensureSize(entry.size);
    result.setSize(0);

    uint16_t currentBlock = entry.startBlock;
    size_t bytesRemaining = entry.size;
    int maxChainLength = 100000;

    while (bytesRemaining > 0 && currentBlock > 0 && maxChainLength-- > 0)
    {
        int64_t blockOffset = partition.offset + (int64_t)currentBlock * AKAI_BLOCK_SIZE;
        stream.setPosition(blockOffset);

        size_t toRead = juce::jmin(bytesRemaining, (size_t)AKAI_BLOCK_SIZE);
        uint8_t blockBuf[AKAI_BLOCK_SIZE];
        int readBytes = stream.read(blockBuf, (int)toRead);
        if (readBytes <= 0)
            break;

        result.append(blockBuf, (size_t)readBytes);
        bytesRemaining -= (size_t)readBytes;

        if (currentBlock < (uint16_t)partition.fat.size())
        {
            uint16_t nextBlock = partition.fat[currentBlock];
            if (nextBlock == 0 || (nextBlock & AKAI_FAT_SPECIAL_MASK) != 0)
                break;
            currentBlock = nextBlock;
        }
        else
            break;
    }

    if (result.getSize() > entry.size)
        result.setSize(entry.size);

    return result;
}

// ══════════════════════════════════════════════════════════════════════════════
// Program parsing
// ══════════════════════════════════════════════════════════════════════════════

bool AkaiIsoReader::parseProgram(const juce::MemoryBlock& data, AkaiProgram& program) const
{
    const uint8_t* d = static_cast<const uint8_t*>(data.getData());
    size_t dataSize = data.getSize();

    if (dataSize < 192)
        return false;

    uint8_t headerId = d[0x00];
    if (headerId != 1 && headerId != 3)
        return false;

    program.name = akaiNameToString(d + 0x03, 12);
    program.midiProgram = d[0x0F];
    program.polyphony = d[0x11];
    program.keyLow = d[0x13];
    program.keyHigh = d[0x14];
    program.loudness = d[0x19];

    int numKeygroups = d[0x2A];
    if (numKeygroups == 0 || numKeygroups > 99)
        return false;

    size_t kgOffset = 150;
    static constexpr size_t KEYGROUP_SIZE = 150;

    for (int kg = 0; kg < numKeygroups; ++kg)
    {
        if (kgOffset + KEYGROUP_SIZE > dataSize)
            break;

        const uint8_t* kgData = d + kgOffset;

        AkaiKeygroup keygroup;
        keygroup.keyLow = kgData[0x03];
        keygroup.keyHigh = kgData[0x04];
        keygroup.tuneCents = (int8_t)kgData[0x05];
        keygroup.tuneSemitones = (int8_t)kgData[0x06];
        keygroup.filterFreq = kgData[0x07];

        size_t vzOffset = 0x22;
        keygroup.numZones = 0;

        for (int vz = 0; vz < 4; ++vz)
        {
            if (kgOffset + vzOffset + 24 > dataSize)
                break;

            const uint8_t* vzData = kgData + vzOffset;

            AkaiVelocityZone zone;
            zone.sampleName = akaiNameToString(vzData, 12);

            bool validSampleName = true;
            for (int j = 0; j < 12; ++j)
            {
                if (vzData[j] > 40) { validSampleName = false; break; }
            }

            if (!validSampleName || zone.sampleName.isEmpty() || zone.sampleName.trim().isEmpty())
            {
                vzOffset += 24;
                continue;
            }

            zone.velLow = vzData[0x0C];
            zone.velHigh = vzData[0x0D];
            zone.tuneCents = (int8_t)vzData[0x0E];
            zone.tuneSemitones = (int8_t)vzData[0x0F];
            zone.loudnessOffset = (int8_t)vzData[0x10];
            zone.filterOffset = (int8_t)vzData[0x11];
            zone.panOffset = (int8_t)vzData[0x12];

            keygroup.zones[keygroup.numZones] = zone;
            keygroup.numZones++;
            vzOffset += 24;
        }

        if (keygroup.numZones > 0)
            program.keygroups.add(keygroup);

        kgOffset += KEYGROUP_SIZE;
    }

    return !program.keygroups.isEmpty();
}

// ══════════════════════════════════════════════════════════════════════════════
// Sample parsing & WAV conversion
// ══════════════════════════════════════════════════════════════════════════════

juce::MemoryBlock AkaiIsoReader::parseSampleToWav(const juce::MemoryBlock& data, AkaiSampleInfo& info) const
{
    const uint8_t* d = static_cast<const uint8_t*>(data.getData());
    size_t dataSize = data.getSize();

    if (dataSize < 150)
        return {};

    uint8_t headerId = d[0x00];
    if (headerId != 1 && headerId != 3)
        return {};

    uint8_t bandwidth = d[0x01];
    info.originalPitch = d[0x02];
    if (info.originalPitch < 24 || info.originalPitch > 127)
        info.originalPitch = 60;

    if (dataSize > 0x13)
        info.loopMode = d[0x13];

    if (dataSize > 0x8C)
    {
        info.sampleRate = readU16(d + 0x8A);
        if (info.sampleRate == 0)
            info.sampleRate = bandwidth == 0 ? 22050 : 44100;
    }
    else
        info.sampleRate = bandwidth == 0 ? 22050 : 44100;

    uint32_t dataLength = 0;
    if (dataSize >= 0x1E)
        dataLength = readU32(d + 0x1A);

    if (dataLength == 0)
        return {};

    if (dataSize >= 0x26 + 12)
    {
        uint32_t loopMarker = readU32(d + 0x26);
        uint32_t loopLength = readU32(d + 0x2A);
        if (loopLength > 0 && loopMarker < dataLength)
        {
            info.loopStart = (int)loopMarker;
            info.loopEnd = (int)(loopMarker + loopLength);
            if (info.loopEnd > (int)dataLength)
                info.loopEnd = (int)dataLength;
        }
    }

    size_t headerSize = (headerId == 3) ? 192 : 150;
    if (headerSize >= dataSize)
        return {};

    size_t availablePCMBytes = dataSize - headerSize;
    uint32_t numSamples = (uint32_t)(availablePCMBytes / 2);
    if (dataLength > 0 && dataLength < numSamples)
        numSamples = dataLength;

    if (numSamples == 0)
        return {};

    info.numSamples = numSamples;
    const int16_t* pcmData = reinterpret_cast<const int16_t*>(d + headerSize);
    return createWavFromPCM(pcmData, numSamples, info.sampleRate, 1);
}

juce::MemoryBlock AkaiIsoReader::parseStereoSamplesToWav(const juce::MemoryBlock& leftData, const juce::MemoryBlock& rightData,
                                                          AkaiSampleInfo& leftInfo, AkaiSampleInfo& rightInfo) const
{
    const uint8_t* ld = static_cast<const uint8_t*>(leftData.getData());
    const uint8_t* rd = static_cast<const uint8_t*>(rightData.getData());
    size_t leftSize = leftData.getSize();
    size_t rightSize = rightData.getSize();

    if (leftSize < 150 || rightSize < 150)
        return {};

    uint8_t lHeaderId = ld[0x00];
    uint8_t rHeaderId = rd[0x00];
    if ((lHeaderId != 1 && lHeaderId != 3) || (rHeaderId != 1 && rHeaderId != 3))
        return {};

    leftInfo.originalPitch = ld[0x02];
    if (leftInfo.originalPitch < 24 || leftInfo.originalPitch > 127)
        leftInfo.originalPitch = 60;

    uint8_t lBandwidth = ld[0x01];
    if (leftSize > 0x8C)
    {
        leftInfo.sampleRate = readU16(ld + 0x8A);
        if (leftInfo.sampleRate == 0)
            leftInfo.sampleRate = lBandwidth == 0 ? 22050 : 44100;
    }
    else
        leftInfo.sampleRate = lBandwidth == 0 ? 22050 : 44100;

    if (leftSize > 0x13)
        leftInfo.loopMode = ld[0x13];

    uint32_t lDataLength = 0;
    if (leftSize >= 0x1E)
        lDataLength = readU32(ld + 0x1A);

    if (leftSize >= 0x26 + 12)
    {
        uint32_t loopMarker = readU32(ld + 0x26);
        uint32_t loopLength = readU32(ld + 0x2A);
        if (loopLength > 0 && loopMarker < lDataLength)
        {
            leftInfo.loopStart = (int)loopMarker;
            leftInfo.loopEnd = (int)(loopMarker + loopLength);
            if (leftInfo.loopEnd > (int)lDataLength)
                leftInfo.loopEnd = (int)lDataLength;
        }
    }

    uint8_t rBandwidth = rd[0x01];
    if (rightSize > 0x8C)
    {
        rightInfo.sampleRate = readU16(rd + 0x8A);
        if (rightInfo.sampleRate == 0)
            rightInfo.sampleRate = rBandwidth == 0 ? 22050 : 44100;
    }
    else
        rightInfo.sampleRate = rBandwidth == 0 ? 22050 : 44100;

    uint32_t rDataLength = 0;
    if (rightSize >= 0x1E)
        rDataLength = readU32(rd + 0x1A);

    size_t lHeaderSize = (lHeaderId == 3) ? 192 : 150;
    size_t rHeaderSize = (rHeaderId == 3) ? 192 : 150;

    if (lHeaderSize >= leftSize || rHeaderSize >= rightSize)
        return {};

    size_t lAvailPCM = (leftSize - lHeaderSize) / 2;
    size_t rAvailPCM = (rightSize - rHeaderSize) / 2;

    uint32_t lNumSamples = (lDataLength > 0 && lDataLength < lAvailPCM) ? lDataLength : (uint32_t)lAvailPCM;
    uint32_t rNumSamples = (rDataLength > 0 && rDataLength < rAvailPCM) ? rDataLength : (uint32_t)rAvailPCM;

    if (lNumSamples == 0 || rNumSamples == 0)
        return {};

    uint32_t numSamples = juce::jmin(lNumSamples, rNumSamples);
    leftInfo.numSamples = numSamples;
    rightInfo.numSamples = numSamples;

    const int16_t* lPCM = reinterpret_cast<const int16_t*>(ld + lHeaderSize);
    const int16_t* rPCM = reinterpret_cast<const int16_t*>(rd + rHeaderSize);

    std::vector<int16_t> stereoData(numSamples * 2);
    for (uint32_t i = 0; i < numSamples; ++i)
    {
        stereoData[i * 2]     = lPCM[i];
        stereoData[i * 2 + 1] = rPCM[i];
    }

    return createWavFromPCM(stereoData.data(), numSamples, leftInfo.sampleRate, 2);
}

// ══════════════════════════════════════════════════════════════════════════════
// Utilities
// ══════════════════════════════════════════════════════════════════════════════

char AkaiIsoReader::akaiToAscii(uint8_t c)
{
    if (c <= 9)  return '0' + (char)c;
    if (c == 10) return ' ';
    if (c <= 36) return 'A' + (char)(c - 11);
    switch (c)
    {
        case 37: return '#';
        case 38: return '+';
        case 39: return '-';
        case 40: return '.';
        default: return '?';
    }
}

juce::String AkaiIsoReader::akaiNameToString(const uint8_t* data, int len)
{
    juce::String result;
    for (int i = 0; i < len; ++i)
        result += akaiToAscii(data[i]);
    return result.trimEnd();
}

uint16_t AkaiIsoReader::readU16(const uint8_t* buf)
{
    return (uint16_t)(buf[0] | (buf[1] << 8));
}

uint32_t AkaiIsoReader::readU24(const uint8_t* buf)
{
    return (uint32_t)(buf[0] | (buf[1] << 8) | (buf[2] << 16));
}

uint32_t AkaiIsoReader::readU32(const uint8_t* buf)
{
    return (uint32_t)(buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24));
}

juce::MemoryBlock AkaiIsoReader::createWavFromPCM(const int16_t* pcmData, size_t numSamples, uint32_t sampleRate, uint8_t numChannels)
{
    juce::MemoryBlock wavData;

    uint32_t dataSize = (uint32_t)(numSamples * numChannels * 2);
    uint32_t fileSize = 36 + dataSize;

    wavData.append("RIFF", 4);
    wavData.append(&fileSize, 4);
    wavData.append("WAVE", 4);

    wavData.append("fmt ", 4);
    uint32_t fmtSize = 16;
    wavData.append(&fmtSize, 4);

    uint16_t audioFormat = 1;
    wavData.append(&audioFormat, 2);

    uint16_t channels = numChannels;
    wavData.append(&channels, 2);
    wavData.append(&sampleRate, 4);

    uint32_t byteRate = sampleRate * numChannels * 2;
    wavData.append(&byteRate, 4);

    uint16_t blockAlign = numChannels * 2;
    wavData.append(&blockAlign, 2);

    uint16_t bitsPerSample = 16;
    wavData.append(&bitsPerSample, 2);

    wavData.append("data", 4);
    wavData.append(&dataSize, 4);
    wavData.append(pcmData, dataSize);

    return wavData;
}

const AkaiIsoReader::AkaiFileEntry* AkaiIsoReader::findSampleInVolume(const AkaiVolume& volume, const juce::String& sampleName) const
{
    juce::String searchName = sampleName.trim().toUpperCase();

    for (const auto& file : volume.files)
    {
        if (file.isSample() && file.name.trim().toUpperCase() == searchName)
            return &file;
    }

    for (const auto& file : volume.files)
    {
        if (file.isSample() && file.name.trim().toUpperCase().startsWith(searchName.substring(0, 10)))
            return &file;
    }

    return nullptr;
}

} // namespace akaiLib

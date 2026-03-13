#pragma once

#include <juce_core/juce_core.h>
#include <cstdint>
#include <string>
#include <vector>

namespace akaiLib
{
    /**
        Standalone Akai S1000/S3000 ISO disk image reader.
        Reads Akai CD-ROM/HDD ISO images containing partitions, volumes,
        programs and samples. Adapted from discoDSP Bliss AkaiIsoImporter.
    */
    class AkaiIsoReader
    {
    public:
        AkaiIsoReader() = default;
        ~AkaiIsoReader() = default;

        // ── Akai filesystem constants ────────────────────────────────────────
        static constexpr uint32_t AKAI_BLOCK_SIZE           = 0x2000;   // 8192 bytes
        static constexpr uint32_t AKAI_FAT_OFFSET           = 0x070A;
        static constexpr uint32_t AKAI_DIR_ENTRY_OFFSET     = 0x00CA;
        static constexpr uint32_t AKAI_DIR_ENTRY_SIZE       = 16;
        static constexpr uint32_t AKAI_FILE_ENTRY_SIZE      = 24;
        static constexpr uint16_t AKAI_PARTITION_END_MARK   = 0x8000;
        static constexpr uint16_t AKAI_FAT_CODE_FILEEND     = 0xC000;
        static constexpr uint16_t AKAI_FAT_CODE_SYS         = 0x4000;
        static constexpr uint16_t AKAI_FAT_SPECIAL_MASK     = 0xC000;
        static constexpr int AKAI_MAX_FILE_ENTRIES_S1000     = 341;
        static constexpr int AKAI_MAX_FILE_ENTRIES_S3000     = 509;
        static constexpr int AKAI_MAX_VOLUMES                = 100;
        static constexpr int AKAI_NAME_SIZE                  = 12;

        // File type identifiers
        static constexpr uint8_t AKAI_TYPE_S1000_PROGRAM    = 0x70;
        static constexpr uint8_t AKAI_TYPE_S1000_SAMPLE     = 0x73;
        static constexpr uint8_t AKAI_TYPE_S3000_PROGRAM    = 0xF0;
        static constexpr uint8_t AKAI_TYPE_S3000_SAMPLE     = 0xF3;

        // ── Data structures ──────────────────────────────────────────────────

        struct AkaiFileEntry
        {
            juce::String name;
            uint8_t type = 0;
            uint32_t size = 0;
            uint16_t startBlock = 0;

            bool isProgram() const { return type == AKAI_TYPE_S1000_PROGRAM || type == AKAI_TYPE_S3000_PROGRAM; }
            bool isSample() const  { return type == AKAI_TYPE_S1000_SAMPLE || type == AKAI_TYPE_S3000_SAMPLE; }
            bool isS3000() const   { return type == AKAI_TYPE_S3000_PROGRAM || type == AKAI_TYPE_S3000_SAMPLE; }
        };

        struct AkaiVolume
        {
            juce::String name;
            uint16_t type = 0;          // 0=inactive, 1=S1000, 3=S3000
            uint16_t startBlock = 0;
            juce::Array<AkaiFileEntry> files;

            bool isActive() const { return type == 1 || type == 3; }
            bool isS3000() const  { return type == 3; }
        };

        struct AkaiPartition
        {
            int64_t offset = 0;
            uint32_t sizeInBlocks = 0;
            juce::Array<AkaiVolume> volumes;
            juce::Array<uint16_t> fat;
        };

        struct AkaiSampleInfo
        {
            uint8_t originalPitch = 60;
            uint16_t sampleRate = 44100;
            uint32_t numSamples = 0;
            int loopStart = -1;
            int loopEnd = -1;
            int loopMode = 0;
        };

        struct AkaiVelocityZone
        {
            juce::String sampleName;
            int velLow = 0;
            int velHigh = 127;
            int tuneSemitones = 0;
            int tuneCents = 0;
            int loudnessOffset = 0;
            int filterOffset = 0;
            int panOffset = 0;
        };

        struct AkaiKeygroup
        {
            int keyLow = 0;
            int keyHigh = 127;
            int tuneSemitones = 0;
            int tuneCents = 0;
            int filterFreq = 99;
            AkaiVelocityZone zones[4];
            int numZones = 0;
        };

        struct AkaiProgram
        {
            juce::String name;
            int midiProgram = 0;
            int polyphony = 16;
            int keyLow = 24;
            int keyHigh = 127;
            int loudness = 80;
            juce::Array<AkaiKeygroup> keygroups;
        };

        // Flat reference: a program within the ISO hierarchy
        struct AkaiProgramRef
        {
            int partitionIndex = 0;
            int volumeIndex = 0;
            int fileIndex = 0;
            juce::String partitionLabel;  // "A", "B", "C", ...
            juce::String volumeName;
            juce::String programName;
        };

        // ── Public API ───────────────────────────────────────────────────────

        /** Check if a file looks like an Akai ISO image (ISO/BIN/IMG/AKAI). */
        static bool canReadFile(const juce::File& file);

        /**
            Open an Akai ISO and scan all partitions/volumes/programs.
            Returns true on success. After this call, getProgramRefs() is populated.
        */
        bool open(const juce::File& file);

        /** Get all discovered program references. */
        const juce::Array<AkaiProgramRef>& getProgramRefs() const { return m_programRefs; }

        /**
            Load a single program by index (into getProgramRefs()).
            Extracts all samples as in-memory WAV files and builds an SFZ text
            that maps them across the correct key/velocity ranges.

            Returns the SFZ text. Populates wavFiles with name→WAV data pairs.
            Returns empty string on failure.
        */
        juce::String loadProgram(int programIndex,
                                 std::vector<std::pair<juce::String, juce::MemoryBlock>>& wavFiles);

    private:
        // ── Internal helpers ─────────────────────────────────────────────────
        bool isValidAkaiIso(juce::InputStream& stream) const;
        bool scanPartitions(juce::InputStream& stream, juce::Array<AkaiPartition>& partitions) const;
        bool readFAT(juce::InputStream& stream, AkaiPartition& partition) const;
        bool readVolumes(juce::InputStream& stream, AkaiPartition& partition) const;
        bool readFileEntries(juce::InputStream& stream, const AkaiPartition& partition, AkaiVolume& volume) const;
        juce::MemoryBlock readFileData(juce::InputStream& stream, const AkaiPartition& partition, const AkaiFileEntry& entry) const;
        bool parseProgram(const juce::MemoryBlock& data, AkaiProgram& program) const;
        juce::MemoryBlock parseSampleToWav(const juce::MemoryBlock& data, AkaiSampleInfo& info) const;
        juce::MemoryBlock parseStereoSamplesToWav(const juce::MemoryBlock& leftData, const juce::MemoryBlock& rightData,
                                                   AkaiSampleInfo& leftInfo, AkaiSampleInfo& rightInfo) const;
        const AkaiFileEntry* findSampleInVolume(const AkaiVolume& volume, const juce::String& sampleName) const;

        static char akaiToAscii(uint8_t c);
        static juce::String akaiNameToString(const uint8_t* data, int len = AKAI_NAME_SIZE);
        static uint16_t readU16(const uint8_t* buf);
        static uint32_t readU24(const uint8_t* buf);
        static uint32_t readU32(const uint8_t* buf);
        static juce::MemoryBlock createWavFromPCM(const int16_t* pcmData, size_t numSamples, uint32_t sampleRate, uint8_t numChannels = 1);

        // ── State ────────────────────────────────────────────────────────────
        juce::File m_isoFile;
        juce::Array<AkaiPartition> m_partitions;
        juce::Array<AkaiProgramRef> m_programRefs;
    };
}

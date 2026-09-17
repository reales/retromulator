#include "RenderCli.h"

#include "HeadlessProcessor.h"
#include "SynthFactory.h"
#include "SynthType.h"

#include "Emu88Tones.h"
#include "ronaldo/88emu/88lib/deviceModel.h"
#include "ronaldo/88emu/88lib/romloader.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <cstdio>
#include <string>
#include <vector>

namespace retromulator
{
    namespace
    {
        struct Options
        {
            juce::File  input;
            juce::File  output;
            int         model = static_cast<int>(emu88Lib::DeviceModel::Sc55Mk2);
            juce::File  romPath;
            bool        mp3   = true;
            bool        listBoards = false;
            bool        listTones  = false;
            bool        help  = false;
            juce::String error;
        };

        // Accepts either a board index or a name, matched loosely against the profile
        // display names so "sc-88pro", "SC88Pro" and "2" all land on the same board.
        juce::String normaliseBoard(const juce::String& s)
        {
            return s.removeCharacters(" -_").toLowerCase();
        }

        int parseBoard(const juce::String& value)
        {
            if(value.containsOnly("0123456789") && value.isNotEmpty())
            {
                const int i = value.getIntValue();
                return i >= 0 && i < static_cast<int>(emu88Lib::deviceModelCount()) ? i : -2;
            }

            const auto wanted = normaliseBoard(value);
            for(uint32_t i = 0; i < emu88Lib::deviceModelCount(); ++i)
            {
                const auto& p = emu88Lib::getDeviceProfile(static_cast<emu88Lib::DeviceModel>(i));
                if(normaliseBoard(p.displayName) == wanted)
                    return static_cast<int>(i);
            }
            return -2;
        }

        juce::File resolve(const juce::String& path)
        {
            return juce::File::getCurrentWorkingDirectory().getChildFile(path.unquoted());
        }

        Options parse(int argc, char* argv[])
        {
            Options o;

            for(int i = 1; i < argc; ++i)
            {
                const juce::String arg(juce::CharPointer_UTF8{argv[i]});

                // Every option below takes a value, so the lookahead is shared.
                const auto value = [&]() -> juce::String
                {
                    if(i + 1 < argc)
                        return juce::String(juce::CharPointer_UTF8{argv[++i]});
                    o.error = arg + " needs a value";
                    return {};
                };

                if(arg == "--render" || arg == "-i")
                    o.input = resolve(value());
                else if(arg == "--out" || arg == "-o")
                    o.output = resolve(value());
                else if(arg == "--board" || arg == "-b")
                {
                    const auto v = value();
                    const int m = parseBoard(v);
                    if(m == -2)
                        o.error = "unknown board: " + v;
                    else
                        o.model = m;
                }
                else if(arg == "--rom" || arg == "-r")
                    o.romPath = resolve(value());
                else if(arg == "--format" || arg == "-f")
                {
                    const auto v = value().toLowerCase();
                    if(v == "mp3")       o.mp3 = true;
                    else if(v == "wav")  o.mp3 = false;
                    else                 o.error = "unknown format: " + v;
                }
                else if(arg == "--list-boards")
                    o.listBoards = true;
                else if(arg == "--list-tones")
                    o.listTones = true;
                else if(arg == "--help" || arg == "-h")
                    o.help = true;
                else if(arg.startsWith("-"))
                {
                    if(o.error.isEmpty())
                        o.error = "unknown option: " + arg;
                }
                else if(o.input == juce::File())
                    o.input = resolve(arg);   // bare path, so "Retromulator song.mid" works
                else if(o.error.isEmpty())
                    o.error = "unexpected argument: " + arg;
            }

            // The output extension picks the format when --format was not given, so
            // "-o song.wav" does what it looks like.
            const auto ext = o.output.getFileExtension();
            if(ext.equalsIgnoreCase(".mp3") || ext.equalsIgnoreCase(".wav"))
            {
                bool explicitFormat = false;
                for(int i = 1; i < argc; ++i)
                {
                    const juce::String a(juce::CharPointer_UTF8{argv[i]});
                    explicitFormat = explicitFormat || a == "--format" || a == "-f";
                }
                if(!explicitFormat)
                    o.mp3 = ext.equalsIgnoreCase(".mp3");
            }

            return o;
        }

        void printUsage()
        {
            std::printf(
                "Retromulator - offline 88emu MIDI rendering\n"
                "\n"
                "Usage: Retromulator <file.mid> [options]\n"
                "\n"
                "  -i, --render <file>   MIDI file to render (also accepted as a bare argument)\n"
                "  -o, --out <file>      output file (default: the input path with the format's extension)\n"
                "  -b, --board <board>   SC board to boot, by name or index (default: SC-55mk2)\n"
                "  -r, --rom <dir>       extra ROM search folder\n"
                "  -f, --format wav|mp3  output format (default: from the output extension, else mp3)\n"
                "      --list-boards     list the boards whose ROM set is complete\n"
                "      --list-tones      list the tone table a board reads from its ROM\n"
                "  -h, --help            this text\n"
                "\n"
                "Without a MIDI file the normal GUI starts.\n");
        }

        void printBoards(const juce::File& romPath)
        {
            // The loader only knows what it has scanned, and the scan happens when a
            // device is created, so an explicit one is needed here.
            SynthFactory::addDefaultRomSearchPaths(
                romPath.isDirectory() ? romPath.getFullPathName().toStdString() : std::string{});
            emu88Lib::RomLoader::rescan();

            std::printf("Boards:\n");
            for(uint32_t i = 0; i < emu88Lib::deviceModelCount(); ++i)
            {
                const auto model = static_cast<emu88Lib::DeviceModel>(i);
                const auto& p = emu88Lib::getDeviceProfile(model);
                std::printf("  %u  %-10s %s\n", i, p.displayName,
                            emu88Lib::RomLoader::isDeviceAvailable(model) ? "ready" : "ROMs missing");
            }
        }
    }

    bool RenderCli::wanted(const int argc, char* argv[])
    {
        for(int i = 1; i < argc; ++i)
        {
            const juce::String arg(juce::CharPointer_UTF8{argv[i]});
            if(arg == "--render" || arg == "-i" || arg == "--list-boards"
               || arg == "--list-tones" || arg == "--help" || arg == "-h")
                return true;
            // macOS hands a double-clicked document to the app as a bare path, and the
            // OS passes -psn_... on some launches, so only a MIDI file counts here.
            if(!arg.startsWith("-")
               && (arg.endsWithIgnoreCase(".mid") || arg.endsWithIgnoreCase(".midi")))
                return true;
        }
        return false;
    }

    int RenderCli::run(const int argc, char* argv[])
    {
        const auto opts = parse(argc, argv);

        if(opts.error.isNotEmpty())
        {
            std::fprintf(stderr, "Error: %s\n", opts.error.toRawUTF8());
            return 1;
        }

        if(opts.help)
        {
            printUsage();
            return 0;
        }

        // The ROM scan and the device both need JUCE's singletons up; the normal app
        // start-up does this for the GUI path.
        juce::ScopedJuceInitialiser_GUI juceInit;

        if(opts.listBoards)
        {
            printBoards(opts.romPath);
            return 0;
        }

        if(opts.listTones)
        {
            SynthFactory::addDefaultRomSearchPaths(
                opts.romPath.isDirectory() ? opts.romPath.getFullPathName().toStdString() : std::string{});
            emu88Lib::RomLoader::rescan();
            const auto model = static_cast<emu88Lib::DeviceModel>(opts.model);
            const auto tones = emu88LoadCapitalTones(model);
            const auto kits  = emu88LoadDrumKits(model);
            std::printf("Board: %s\n", emu88Lib::getDeviceProfile(model).displayName);
            std::printf("Tones: %zu  Kits: %zu\n", tones.size(), kits.size());
            for(size_t i = 0; i < tones.size() && i < 16; ++i)
                std::printf("  %03zu %s\n", i + 1, tones[i].c_str());
            return tones.empty() ? 1 : 0;
        }

        if(!opts.input.existsAsFile())
        {
            std::fprintf(stderr, "Error: cannot read %s\n", opts.input.getFullPathName().toRawUTF8());
            return 1;
        }

        auto output = opts.output;
        if(output == juce::File())
            output = opts.input.withFileExtension(opts.mp3 ? "mp3" : "wav");
        output.getParentDirectory().createDirectory();

        SynthFactory::setEmu88Model(opts.model);

        HeadlessProcessor proc;
        proc.setSynthType(SynthType::Emu88,
                          opts.romPath.isDirectory() ? opts.romPath.getFullPathName().toStdString()
                                                     : std::string{});
        if(proc.hasDeviceError())
        {
            std::fprintf(stderr, "Error: no complete SC ROM set found. "
                                 "Use --rom <dir> or --list-boards.\n");
            return 1;
        }

        const auto booted = proc.getEmu88Model();
        if(booted >= 0)
        {
            const auto& p = emu88Lib::getDeviceProfile(static_cast<emu88Lib::DeviceModel>(booted));
            std::printf("Board:  %s\n", p.displayName);
        }

        juce::MemoryBlock mb;
        if(!opts.input.loadFileAsData(mb))
        {
            std::fprintf(stderr, "Error: cannot read %s\n", opts.input.getFullPathName().toRawUTF8());
            return 1;
        }

        std::vector<uint8_t> data(static_cast<const uint8_t*>(mb.getData()),
                                  static_cast<const uint8_t*>(mb.getData()) + mb.getSize());
        if(!proc.loadMidiFile(std::move(data), opts.input.getFileName().toStdString()))
        {
            std::fprintf(stderr, "Error: %s is not a MIDI file this core can play\n",
                         opts.input.getFileName().toRawUTF8());
            return 1;
        }

        std::printf("Song:   %s\n", opts.input.getFileName().toRawUTF8());
        std::printf("Output: %s\n", output.getFullPathName().toRawUTF8());

        // The processor renders on its own thread, the same one the GUI drives, so the
        // wait below is what the editor's progress display would otherwise be doing.
        if(!proc.startMidiRender(juce::URL(output),
                                 opts.mp3 ? HeadlessProcessor::RenderFormat::Mp3
                                          : HeadlessProcessor::RenderFormat::Wav))
        {
            std::fprintf(stderr, "Error: could not start the render\n");
            return 1;
        }

        int shown = -1;
        while(proc.isMidiRendering())
        {
            const int pct = static_cast<int>(proc.getMidiRenderProgress() * 100.0f);
            if(pct != shown)
            {
                shown = pct;
                std::printf("\rRendering %3d%%", pct);
                std::fflush(stdout);
            }
            juce::Thread::sleep(50);
        }
        std::printf("\rRendering 100%%\n");

        if(!output.existsAsFile() || output.getSize() == 0)
        {
            std::fprintf(stderr, "Error: the render produced no output\n");
            return 1;
        }

        std::printf("Done:   %s (%.1f MB)\n", output.getFullPathName().toRawUTF8(),
                    static_cast<double>(output.getSize()) / (1024.0 * 1024.0));
        return 0;
    }
}

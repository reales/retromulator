#pragma once

namespace retromulator
{
    // Offline MIDI rendering from the command line, for the Standalone build.
    // run() returns false when the arguments ask for the normal GUI instead, in which
    // case nothing has been printed and no device was booted.
    class RenderCli
    {
    public:
        static bool wanted(int argc, char* argv[]);
        static int  run(int argc, char* argv[]);
    };
}

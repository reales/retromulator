/*
OpenB3: an open source sound synthesis engine and JUCE application/plugin that simulates
the magnificent sound of the Hammond B3 organ and Leslie rotating speaker
Copyright (C) 2021-2022 Michele Perrone
Github: https://github.com/michele-perrone/OpenPiano
Author e-mail: perrone(dot)michele(at)outlook(dot)com
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published
by the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

/* This is a class that acts as a wrapper for the Beatrix C engine.
 * The goal is to make the usage of Beatric much simpler.
 */

#include <stdlib.h>
#include <string.h>
#include <functional>

#include "global_inst.h"
#include "global_definitions.h"

struct Beatrix
{
    b_instance inst;

    float bufA[BUFFER_SIZE_SAMPLES];
    float bufB[BUFFER_SIZE_SAMPLES];
    float bufC[BUFFER_SIZE_SAMPLES];
    float bufD[2][BUFFER_SIZE_SAMPLES]; // drum, tmp.
    float bufL[2][BUFFER_SIZE_SAMPLES]; // leslie, out
    float bufJ[2][BUFFER_SIZE_SAMPLES];
    int boffset = BUFFER_SIZE_SAMPLES;

    // replaces the built-in preamp when set (e.g. oversampled overdrive)
    std::function<void (float* in, float* out, int n)> preamp_hook;
    bool use_internal_reverb = true;

    char* defaultConfigFile    = NULL;
    char* defaultProgrammeFile = NULL;    

    Beatrix(double sample_rate)
    {
        ::SampleRateD = sample_rate;

        memset (&inst, 0, sizeof (b_instance));

        alloc_all();

        init_all();
    }
    ~Beatrix()
    {
        free(defaultConfigFile);
        free(defaultProgrammeFile);

        freeReverb (inst.reverb);
        freeWhirl (inst.whirl);

        freeToneGenerator (inst.synth);
        freeMidiCfg (inst.midicfg);
        freePreamp (inst.preamp);
        freeProgs (inst.progs);
        freeRunningConfig (inst.state);

        fprintf (stderr, "bye\n");
    }
    void alloc_all()
    {
        inst.state = allocRunningConfig();
        inst.progs = allocProgs();
        inst.reverb = allocReverb();
        inst.whirl = allocWhirl();
        inst.synth = allocTonegen();
        inst.midicfg = allocMidiCfg(inst.state);
        inst.preamp = allocPreamp();
    }
    void init_all()
    {
        fprintf (stderr, "Oscillators : ");
        fflush (stderr);
        initToneGenerator (inst.synth, inst.midicfg);

        fprintf (stderr, "Scanner : ");
        fflush (stderr);
        initVibrato (inst.synth, inst.midicfg);

        fprintf (stderr, "Overdrive : ");
        fflush (stderr);
        initPreamp (inst.preamp, inst.midicfg);

        fprintf (stderr, "Reverb : ");
        fflush (stderr);
        initReverb (inst.reverb, inst.midicfg, SampleRateD);

        fprintf (stderr, "Whirl : ");
        fflush (stderr);
        initWhirl (inst.whirl, inst.midicfg, SampleRateD);

        fprintf (stderr, "RC : ");
        fflush (stderr);
        initRunningConfig (inst.state, inst.midicfg);

        fprintf (stderr, "Midi config : ");
        fflush (stderr);
        initMidiTables (inst.midicfg);

        fprintf (stderr, "Drawbars : ");
        fflush (stderr);
        setDrawBars (&inst, UPPER_MANUAL, defaultPresetUpperManual);
        setDrawBars (&inst, LOWER_MANUAL, defaultPresetLowerManual);
        setDrawBars (&inst, PEDAL_BOARD, defaultPresetPedalBoard);

        fprintf (stderr, "..done.\n");
        fflush (stderr);
    }
    void get_next_block(float* buffer_L, float* buffer_R, int nframes)
    {
        int written = 0;

        while (written < nframes)
        {
            int nremain = nframes - written;

            if (boffset >= BUFFER_SIZE_SAMPLES)
            {
                boffset = 0;
                oscGenerateFragment (inst.synth, bufA, BUFFER_SIZE_SAMPLES);
                if (preamp_hook)
                    preamp_hook (bufA, bufB, BUFFER_SIZE_SAMPLES);
                else
                    preamp (inst.preamp, bufA, bufB, BUFFER_SIZE_SAMPLES);
                if (use_internal_reverb)
                    reverb (inst.reverb, bufB, bufC, BUFFER_SIZE_SAMPLES);
                else
                    memcpy (bufC, bufB, BUFFER_SIZE_SAMPLES * sizeof (float));
                whirlProc3 (inst.whirl, bufC, bufL[0], bufL[1], bufD[0], bufD[1], BUFFER_SIZE_SAMPLES);
            }

            int nread = MIN (nremain, (BUFFER_SIZE_SAMPLES - boffset));            

            memcpy (&buffer_L[written], &bufL[0][boffset], nread * sizeof (float));
            memcpy (&buffer_R[written], &bufL[1][boffset], nread * sizeof (float));

            written += nread;
            boffset += nread;
        }
    }

    void process_midi_message(const uint8_t *midi_buffer, size_t n_messages)
    {
        parse_raw_midi_data(&this->inst, midi_buffer, n_messages);
    }

    /**** Keys ****/
    /** Keys are numbered as such:
     *   0-- 63, upper manual (  0-- 60 in use)
     *  64--127, lower manual ( 64--124 in use)
     * 128--160, pedal        (128--159 in use)
     */
    void note_on(uint8_t midi_note)
    {
        oscKeyOn (this->inst.synth, midi_note, midi_note);
    }
    void note_off(uint8_t midi_note)
    {
        oscKeyOff (this->inst.synth, midi_note, midi_note);
    }

    /**** Drawbars ****/
    /**
     * @brief Set one of the three drawbars
     * @param manual UPPER_MANUAL, LOWER_MANUAL, PEDAL_BOARD
     * @param setting An integer array of length 9, with values from 0 to 8
     */
    void set_drawbars(unsigned int manual, unsigned int setting[])
    {
        setDrawBars (&this->inst, manual, setting);
    }

    /**** Vibrato ****/
    void set_vibrato_upper(bool is_enabled)
    {
        setVibratoUpper(this->inst.synth, is_enabled);
    }
    void set_vibrato_lower(bool is_enabled)
    {
        setVibratoLower(this->inst.synth, is_enabled);
    }

    /**** Vibrato&Chorus ****/
    /**
     * @brief Set the type of vibrato
     * @param vibrato_type VIB1, VIB2, VIB3, CHO1, CHO2, CHO3
     */
    void set_vibrato(int vibrato_type)
    {
        setVibrato(this->inst.synth, vibrato_type);
    }

    /**** Percussion ****/
    void set_percussion_enabled(bool is_enabled)
    {
        setPercussionEnabled(this->inst.synth, is_enabled);
    }
    void set_percussion_fast(bool is_fast)
    {
        setPercussionFast(this->inst.synth, is_fast);
    }
    void set_percussion_first(bool is_first)
    {
        setPercussionFirst(this->inst.synth, is_first);
    }
    void set_percussion_volume(bool is_soft)
    {
        setPercussionVolume(this->inst.synth, is_soft);
    }

    /**** Overdrive ****/
    void set_preamp_clean(bool is_clean)
    {
        setClean(this->inst.preamp, is_clean);
    }
    void set_input_gain(float gain)
    {
        setDrive(this->inst.preamp, gain);
    }

    /**** Volume ****/
    void set_swell(float gain)
    {
        this->inst.synth->swellPedalGain = this->inst.synth->outputLevelTrim * gain;
    }

    /**** Reverb ****/
    /**
     * @brief Set the dry/wet reverb amount
     * @param wet 0.0 Dry ... 1.0 wet
     */
    void set_reverb_dry_wet(float wet)
    {
        setReverbMix(this->inst.reverb, wet);
    }

    /**** Rotary speaker ****/
    /**
     * @brief Set the rotary speaker speed
     * @param speed WHIRL_FAST, WHIRL_STOP, WHIRL_SLOW
     */
    void set_rotary_speed(int speed)
    {
        setRevSelect (this->inst.whirl, speed);
    }

    /**
     * @brief Apply cabinet model, speeds, ramps, balance, mic distance, brake and bypass.
     * Call from the audio thread between blocks; geometry changes clear the rotor buffers.
     */
    void apply_leslie(const whirl_settings& s)
    {
        whirlApplySettings (this->inst.whirl, &s, leslie_applied ? &leslie_prev : NULL);
        leslie_prev = s;
        leslie_applied = true;
    }

    whirl_settings leslie_prev {};
    bool leslie_applied = false;
};

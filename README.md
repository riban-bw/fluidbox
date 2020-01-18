# fluidbox
fluidbox is a Fluidsynth based synthesizer for Raspberry Pi

Fluidsynth is a soundfont player. This project provides an instance of Fluidsynth running on a Raspberry Pi. Configuration is persisted by a configuration file and soundfonts must exist in the sf2 subdirectory.

Control of note on / off and patch selection is via MIDI input. All alas_seq MIDI inputs are automatically routed to the input of Fluidsynth including USB MIDI interfaces plugged after boot.

(1) A symlink is created from sf2/default to /usr/share/sounds/sf2 and this content is regarded as default soundfonts and may not be deleted.

# Dependencies
Fluidbox depends on the following software modules:
- Fluidsynth 2.1
- ribanfblib
- wiringPi

# Usage
See [wiki](https://github.com/riban-bw/fluidbox/wiki) for details of building, installing and using fluidbox. At its simplest, just plug in a USB MIDI keyboard and start playing. Select instruments via MIDI Program Change. Use any / all MIDI channels simultaneously for different instruments. Use MIDI Control Change to modify sounds, e.g. sustain, pitch bend, etc.

# fluidbox
Fluidsynth based synthesizer for Raspberry Pi

Fluidsynth is a soundfont player. This project provides an instance of Fluidsynth running on a Raspberry Pi. Configuration is persisted by a configuration file and soundfonts must exist in the sf2 subdirectory.

Control of note on / off and patch selection is via MIDI input. All alas_seq MIDI inputs are automatically routed to the input of Fluidsynth including USB MIDI interfaces plugged after boot.

(1) A symlink is created from sf2/default to /usr/share/sounds/sf2 and this content is regarded as default soundfonts and may not be deleted.

Dependencies Fluidbox depends on the following software modules:
- Fluidsynth 2.1

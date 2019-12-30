#include "fluidsynth.h"
#include "ribanfblib/ribanfblib.h"
#include <wiringPi.h>
#include <cstdio> //provides printf
#include <iostream> //provides streams
#include <fstream> //provides file stream
#include <map>
#include <unistd.h> //provides pause

#define DEFAULT_SOUNDFONT "default/TimGM6mb.sf2"
#define SF_ROOT "sf2/"
#define BUTTON 9

using namespace std;

fluid_synth_t* g_pSynth;
ribanfblib* g_pScreen;
map<string,string> config;
int g_nCurrentSoundfont = FLUID_FAILED;

int onMidiEvent(void* pData, fluid_midi_event_t* pEvent)
{
    if(fluid_midi_event_get_type(pEvent) == 0xC0)
    {
        printf("Program change %d on channel %d\n", fluid_midi_event_get_program(pEvent)+1, fluid_midi_event_get_channel(pEvent)+1);
    }
    //printf("event type: 0x%02x\n", fluid_midi_event_get_type(pEvent));
    fluid_synth_handle_midi_event(pData, pEvent);
    return 0;
}

string toLower(string sString)
{
    string sReturn;
    for(size_t i=0; i<sString.length(); ++i)
        sReturn += tolower(sString[i]);
    return sReturn;
}

bool loadConfig(string sFilename = "./fb.config")
{
    ifstream fileConfig;
    fileConfig.open(sFilename, ios::in);
    if(!fileConfig.is_open())
    {
        printf("Error: Failed to open configuration: %s\n", sFilename.c_str());
        return false;
    }
    string sLine, sGroup;
    while(getline(fileConfig, sLine))
    {
        //Skip blank lines
        if(sLine == "")
            continue;
        //Strip leading and trailing  whitespace
        size_t nStart = sLine.find_first_not_of(" \t");
        size_t nEnd = sLine.find_last_not_of(" \t");
        sLine = sLine.substr(nStart, nEnd - nStart + 1);
        //Ignore comments
        if(sLine[0] == '#')
            continue;
        //Handle group
        if(sLine[0] == '[')
        {
            int nEnd = sLine.find_first_of(']');
            sGroup = sLine.substr(1, nEnd-1);
            continue;
        }
        size_t nDelim = sLine.find_first_of("=");
        if(nDelim == string::npos)
            continue; //Not a valid value definition
        string sParam = sGroup + "." + sLine.substr(0, nDelim);
        string sValue = sLine.substr(nDelim + 1);
        config[toLower(sParam)] = sValue;
        printf("Found config: %s, %s\n", sParam.c_str(), sValue.c_str());
    }
    fileConfig.close();
    return true;
}

bool saveConfig(string sFilename = "./fb.config")
{
    ofstream fileConfig;
    fileConfig.open(sFilename, ios::out);
    if(!fileConfig.is_open())
    {
        printf("Error: Failed to open configuration: %s\n", sFilename.c_str());
        return false;
    }
    string sLine, sGroup;
    for(auto it=config.begin(); it!=config.end(); ++it)
    {
        string sParam = it->first;
        size_t nDelim = sParam.find_first_of('.');
        string sNewGroup;
        if(nDelim != string::npos)
        {
            sNewGroup = sParam.substr(0, nDelim);
            sParam = sParam.substr(nDelim + 1);
        }
        if(sGroup != sNewGroup)
        {
            sGroup = sNewGroup;
            fileConfig << endl << "[" << sGroup << "]" << endl;
        }
        fileConfig << sParam << "=" << it->second << endl;
    }
    fileConfig.close();
    return true;
}

bool loadSoundfont(string sFilename)
{
    if(g_nCurrentSoundfont >= 0)
    {
        fluid_synth_sfunload(g_pSynth, g_nCurrentSoundfont, 0);
        g_nCurrentSoundfont = -1;
    }
    string sPath = SF_ROOT;
    sPath += sFilename;
    g_nCurrentSoundfont = fluid_synth_sfload(g_pSynth, sPath.c_str(), 1);
    if(g_nCurrentSoundfont >= 0)
        config["general.current_soundfont"] = sFilename;
    return (g_nCurrentSoundfont >= 0);
}

void onButton()
{
    printf("Button pressed\n");
    g_pScreen->Clear(BLUE);
    g_pScreen->DrawText("Shutting down...", 10, 30);
    system("sudo poweroff");
}

int main(int argc, char** argv)
{
    printf("riban fluidbox\n");
    g_pScreen = new ribanfblib("/dev/fb1");
    g_pScreen->Clear();
    g_pScreen->DrawText("riban fluidbox", 10, 20);
    g_pScreen->LoadBitmap("logo.bmp", "logo");
    g_pScreen->DrawBitmap("logo", 0, 0);
    loadConfig();
    wiringPiSetup();
    wiringPiISR(BUTTON, INT_EDGE_FALLING, onButton);

    // Create and populate settings
    fluid_settings_t* pSettings = new_fluid_settings();
    fluid_settings_setint(pSettings, "midi.autoconnect", 1);
    fluid_settings_setint(pSettings, "synth.cpu-cores", 3);
    fluid_settings_setint(pSettings, "synth.chorus.active", 0);
    fluid_settings_setint(pSettings, "synth.reverb.active", 0);
    fluid_settings_setstr(pSettings, "audio.driver", "alsa");
    fluid_settings_setstr(pSettings, "midi.driver", "alsa_seq");

    // Create synth
    g_pSynth = new_fluid_synth(pSettings);
    if(g_pSynth)
        cout << "Created synth engine" << endl;
    else
        cerr << "Failed to create synth engine" << endl;

    // Create MIDI router
    fluid_midi_router_t* pRouter = new_fluid_midi_router(pSettings, onMidiEvent, g_pSynth);
    if(pRouter)
        cout << "Created MIDI router" << endl;
    else
        cerr << "Failed to create MIDI router" << endl;

    // Create MIDI driver
    fluid_midi_driver_t* pMidiDriver = new_fluid_midi_driver(pSettings, fluid_midi_router_handle_midi_event, pRouter);
    if(g_pSynth)
        cout << "Created MIDI driver" << endl;
    else
        cerr << "Failed to create MIDI driver" << endl;

    // Create audio driver
    fluid_audio_driver_t* pAudioDriver = new_fluid_audio_driver(pSettings, g_pSynth);
    if(g_pSynth)
        cout << "Created audio driver" << endl;
    else
        cerr << "Failed to create audio driver" << endl;

    // Load soundfont
    
    //g_nCurrentSoundfont = loadSoundfont("");
    if(!loadSoundfont(config["general.current_soundfont"]))
    {
        printf("Cannot load soundfont: %s. Trying default: %s\n", config["general.current_soundfont"].c_str(), DEFAULT_SOUNDFONT);
        loadSoundfont(DEFAULT_SOUNDFONT);
    }
    if(g_nCurrentSoundfont == FLUID_FAILED)
        cerr << "Failed to load soundfont" << endl;
    else
        cout << "Loaded soundfont: " << config["general.current_soundfont"] << endl;

    //Wait for keyboard input <enter> before ending
    //getchar(); //!@todo Implement proper program loop
    int nSignal = pause();
    printf("Caught signal %d. Closing fluidbox.\n", nSignal);


    //Write configuration
    saveConfig();

    g_pScreen->Clear();

    // Clean up
    delete_fluid_midi_router(pRouter);
    delete_fluid_audio_driver(pAudioDriver);
    delete_fluid_midi_driver(pMidiDriver);
    delete_fluid_synth(g_pSynth);
    delete_fluid_settings(pSettings);
    delete g_pScreen;
    return 0;

    /*
        Create synth with settings: MidiAutoconnect=1, cores=3, SynthChorusActive=0, SynthReverbActive=0, driver="alsa", midi_driver="alsa_seq")
        Load configuration
        Load soundfont
    */
}


#include "fluidsynth.h"
#include "ribanfblib/ribanfblib.h"
#include <wiringPi.h>
#include <cstdio> //provides printf
#include <iostream> //provides streams
#include <fstream> //provides file stream
#include <map>
#include <unistd.h> //provides pause
#include <signal.h> //provides signal handling

#define DEFAULT_SOUNDFONT "default/TimGM6mb.sf2"
#define SF_ROOT "sf2/"
#define BUTTON 9

enum {
    SCREEN_BLANK,
    SCREEN_LOGO,
    SCREEN_PROGRAM_1_8,
    SCREEN_PROGRAM_9_16
};

enum {
    PANIC_NOTES,
    PANIC_SOUNDS,
    PANIC_RESET
};

using namespace std;

fluid_synth_t* g_pSynth;
ribanfblib* g_pScreen;
map<string,string> config;
int g_nCurrentSoundfont = FLUID_FAILED;
int g_nScreen = SCREEN_LOGO;
int g_nRunState = 1;
unsigned int g_nNoteCount[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

/** PANIC
    param nMode Panic mode [PANIC_NOTES | PANIC_SOUNDS | PANIC_RESET]
    param nChannel MIDI channel to reset [0-15, 16=ALL]
**/
void panic(int nMode=PANIC_NOTES, int nChannel=16)
{
    if(nMode == PANIC_RESET)
    {
        fluid_synth_system_reset(g_pSynth);
        return;
    }
    int nMin = nChannel==16?0:nChannel;
    int nMax = nChannel;
    for(int i=nMin; i<nMax; ++i)
    {
        switch(nMode)
        {
            case PANIC_NOTES:
                fluid_synth_all_notes_off(g_pSynth, nChannel);
                break;
            case PANIC_SOUNDS:
                fluid_synth_all_sounds_off(g_pSynth, nChannel);
                break;
        }
    }
}

void ShowProgram(int nChannel)
{
    int nOffset = 0;
    if(g_nScreen == SCREEN_PROGRAM_1_8 && nChannel < 8)
        nOffset = 0;
    else if(g_nScreen == SCREEN_PROGRAM_9_16 && nChannel > 7)
        nOffset = 8;
    else
        return;

    int nProgram, nBank, nSfId;
    fluid_synth_get_program(g_pSynth, nChannel, &nSfId, &nBank, &nProgram);
    fluid_sfont_t* pSoundfont = fluid_synth_get_sfont_by_id(g_pSynth, nSfId);
    if(pSoundfont)
    {
        fluid_preset_t* pPreset = fluid_sfont_get_preset(pSoundfont, nBank, nProgram);
        if(pPreset)
        {
            char sPrefix[6];
            sprintf(sPrefix, "%02d: ", nChannel);
            string sPreset = (char*)sPrefix;
            sPreset += fluid_preset_get_name(pPreset);
            int nY = (nChannel - nOffset) * 16;
            g_pScreen->DrawRect(0,nY, 160,nY+15, BLACK, 0, BLACK);
            g_pScreen->DrawText(sPreset.c_str(), 2, 15+nY, WHITE);
        }
    }
}

/**     Update the MIDI note on indicator on the program screens showing quantity of on notes (max 15)
*       @param nChannel MIDI channel to update
*/
void showMidiActivity(int nChannel)
{
    int nOffset = 0;
    if(g_nScreen == SCREEN_PROGRAM_1_8 && nChannel < 8)
        nOffset = 0;
    else if(g_nScreen == SCREEN_PROGRAM_9_16 && nChannel > 7)
        nOffset = 8;
    else
        return;
    int nY = (nChannel - nOffset) * 16;
    g_pScreen->DrawRect(0,nY, 2,nY+15, BLACK, 0, BLACK);
    nY -= (g_nNoteCount[nChannel] < 16)?g_nNoteCount[nChannel]:15;
    g_pScreen->DrawRect(0,nY, 2,nY+15, RED, 0, RED);
}

void showScreen(int nScreen)
{
    g_nScreen = nScreen;
    g_pScreen->Clear();
    if(nScreen == SCREEN_BLANK)
        return;
    else if(nScreen == SCREEN_PROGRAM_1_8)
    {
        for(int nChannel = 0; nChannel < 8; ++nChannel)
            ShowProgram(nChannel);
    }
    else if(nScreen == SCREEN_PROGRAM_9_16)
    {
        for(int nChannel = 8; nChannel < 16; ++nChannel)
            ShowProgram(nChannel);
    }
    else if(nScreen == SCREEN_LOGO)
    {
        g_pScreen->LoadBitmap("logo.bmp", "logo");
        g_pScreen->DrawBitmap("logo", 0, 0);
    }
}

int onMidiEvent(void* pData, fluid_midi_event_t* pEvent)
{
    fluid_synth_handle_midi_event(pData, pEvent);
    int nChannel = fluid_midi_event_get_channel(pEvent);
    int nType = fluid_midi_event_get_type(pEvent) == 0xC0;
    switch(nType)
    {
        case 0xC0: // Program change
        {
            int nProgram = fluid_midi_event_get_program(pEvent);
            string sKey = "midi.program_";
            sKey += to_string(nChannel);
            config[sKey] = to_string(nProgram);
            ShowProgram(nChannel);
            break;
        }
        case 0x80: // Note off
            if(g_nNoteCount > 0)
                g_nNoteCount[nChannel]--;
            showMidiActivity(nChannel);
            break;
        case 0x90: // Note on
            g_nNoteCount[nChannel]++;
            showMidiActivity(nChannel);
            break;
        default:
            printf("event type: 0x%02x\n", fluid_midi_event_get_type(pEvent));
    }
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
    saveConfig();
    system("sudo poweroff");
}

void onSignal(int nSignal)
{
    switch(nSignal)
    {
    	case SIGALRM:
    		if(g_nScreen == SCREEN_LOGO || g_nScreen == SCREEN_PROGRAM_1_8)
        		showScreen(SCREEN_PROGRAM_1_8);
    		if(g_nScreen == SCREEN_PROGRAM_9_16)
        		showScreen(SCREEN_PROGRAM_9_16);
		break;
    	case SIGINT:
    	case SIGTERM:
		printf("Recieved signal to quit...\n");
        	g_nRunState = 0;
		break;
    }
}

int main(int argc, char** argv)
{
    printf("riban fluidbox\n");
    g_pScreen = new ribanfblib("/dev/fb1");
    showScreen(SCREEN_LOGO);
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

    // Select saved progams
    for(int i = 0; i < 16; ++i)
    {
        string sKey = "midi.program_";
        sKey += to_string(i);
        auto it = config.find(sKey);
        if(it == config.end())
            continue;
        fluid_synth_program_change(g_pSynth, i, stoi(it->second));
    }

    // Configure signal handlers
    signal(SIGALRM, onSignal);
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);

    // Show splash screen for a while
    alarm(2);

    while(g_nRunState)
        pause();

    // If we are here then it is all over so let's tidy up...

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
}


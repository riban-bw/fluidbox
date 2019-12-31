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
#define MAX_PRESETS 127
#define BUTTON 9

using namespace std;

enum {
    SCREEN_BLANK,
    SCREEN_LOGO,
    SCREEN_PERFORMANCE,
    SCREEN_PROGRAM_1_8,
    SCREEN_PROGRAM_9_16
};

enum {
    PANIC_NOTES,
    PANIC_SOUNDS,
    PANIC_RESET
};

struct Program {
    string name = "New program";
    unsigned int bank = 0;
    unsigned int program = 0;
    unsigned int level = 100;
    unsigned int balance = 63;
};

struct Reverb {
    bool enable = false;
    double roomsize = 0;
    double damping = 0;
    double width = 0;
    double level = 0;
};

struct Chorus {
    bool enable = false;
    int voicecount = 0;
    double level = 0;
    double speed = 0;
    double depth = 0;
    int type = 0;
};

struct Preset {
    string name;
    string soundfont;
    Program program[16];
    Reverb reverb;
    Chorus chorus;
};

fluid_synth_t* g_pSynth;
ribanfblib* g_pScreen;
unsigned int g_nCurrentPreset = 0;
bool g_bPresetDirty = false;
int g_nCurrentSoundfont = FLUID_FAILED;
int g_nScreen = SCREEN_LOGO;
int g_nRunState = 1;
unsigned int g_nNoteCount[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
Preset g_presets[MAX_PRESETS];

/**     Return a converted and vaildated value
*       @param sValue Value as a string
*       @param min Minimum permitted value
*       @param max Maximum permitted value
*       @retval double Value converted to double limited to range min-max
*/
double validateDouble(string sValue, double min, double max)
{
    double fValue = 0.0;
    try
    {
        fValue = stod(sValue);
    }
    catch(...)
    {
        cerr << "Error converting string '" << sValue << "' to double" << endl;
    }
    if(fValue < min)
        return min;
    if(fValue > max)
        return max;
    return fValue;
}

/**     Return a converted and vaildated value
*       @param sValue Value as a string
*       @param min Minimum permitted value
*       @param max Maximum permitted value
*       @retval int Value converted to int limited to range min-max
*/
int validateInt(string sValue, int min, int max)
{
    int nValue = 0;
    try
    {
        nValue = stoi(sValue);
    }
    catch(...)
    {
        cerr << "Error converting string '" << sValue << "' to integer" << endl;
    }
    if(nValue < min)
        return min;
    if(nValue > max)
        return max;
    return nValue;
}

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

void ShowPreset(int nPreset)
{
    if(nPreset > MAX_PRESETS)
        return;
    string sName = to_string(nPreset);
    sName += ": ";
    sName += g_presets[nPreset].name;
    g_pScreen->Clear();
    g_pScreen->DrawText(sName, 0, 20);
}

/**     Updates the display of a program for the specified channel */
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
    int nY = (nChannel - nOffset) * 16; //Upper left corner of channel indicator
    g_pScreen->DrawRect(0,nY, 2,nY+15, BLACK, 0, BLACK); // Clear the indicator
    int nCount = (g_nNoteCount[nChannel] < 16)?g_nNoteCount[nChannel]:15; // Limit max note indication to 15
    if(nCount)
        g_pScreen->DrawRect(0,nY+15, 2,nY+15-nCount, RED, 0, RED); // Draw indicator
}

/**     Display the requested screen */
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

/**     Handle MIDI events */
int onMidiEvent(void* pData, fluid_midi_event_t* pEvent)
{
    fluid_synth_handle_midi_event(pData, pEvent);
    int nChannel = fluid_midi_event_get_channel(pEvent);
    int nType = fluid_midi_event_get_type(pEvent);
    switch(nType)
    {
        case 0xC0: // Program change
        {
            int nProgram = fluid_midi_event_get_program(pEvent);
            string sKey = "midi.program_";
            sKey += to_string(nChannel);
            g_presets[0].program[nChannel].program = nProgram;
            ShowProgram(nChannel);
            break;
        }
        case 0x80: // Note off
            if(g_nNoteCount[nChannel] > 0)
                g_nNoteCount[nChannel]--;
            showMidiActivity(nChannel);
            break;
        case 0x90: // Note on
            g_nNoteCount[nChannel]++;
            showMidiActivity(nChannel);
            break;
        default:
            printf("event type: 0x%02x\n", nType);
    }
    return 0;
}

/**     Convert string to lowercase */
string toLower(string sString)
{
    string sReturn;
    for(size_t i=0; i<sString.length(); ++i)
        sReturn += tolower(sString[i]);
    return sReturn;
}

/**     Load configuration from file */
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
        string sParam = sLine.substr(0, nDelim);
        string sValue = sLine.substr(nDelim + 1);

        if(sGroup.substr(0,7) == "preset_")
        {
            //!@todo Optimise - don't need to calculate preset id on every iteration
            unsigned int nIndex = validateInt(sGroup.substr(7), 0, MAX_PRESETS);
            if(nIndex > MAX_PRESETS)
                continue;
            if(sParam == "name")
                g_presets[nIndex].name = sValue;
            else if(sParam == "soundfont")
                g_presets[nIndex].soundfont = sValue;
            else if(sParam.substr(0,5) == "prog_")
            {
                int nChan = validateInt(sParam.substr(5), 0, 15);
                nDelim = sValue.find_first_of(':');
                if(nDelim == string::npos)
                    continue; // Not a valid bank:program value pair
                g_presets[nIndex].program[nChan].bank = validateInt(sValue.substr(0,nDelim), 0, 16383);
                g_presets[nIndex].program[nChan].program = validateInt(sValue.substr(nDelim + 1), 0, 127);
            }
            else if(sParam.substr(0,6) == "level_")
            {
                int nChan = validateInt(sParam.substr(6), 0, 15);
                g_presets[nIndex].program[nChan].level = validateInt(sValue, 0, 127);
            }
            else if(sParam.substr(0,8) == "balance_")
            {
                int nChan = validateInt(sParam.substr(8), 0, 15);
                g_presets[nIndex].program[nChan].balance = validateInt(sValue, 0, 127);
            }
            else if (sParam.substr(0,7) == "reverb_")
            {
                if(sParam.substr(7) == "enable") 
                    g_presets[nIndex].reverb.enable = (sValue == "1");
                if(sParam.substr(7) == "roomsize")
                    g_presets[nIndex].reverb.roomsize = validateDouble(sValue, 0.0, 1.0);
                if(sParam.substr(7) == "damping")
                    g_presets[nIndex].reverb.damping = validateDouble(sValue, 0.0, 1.0);
                if(sParam.substr(7) == "width")
                    g_presets[nIndex].reverb.width = validateDouble(sValue, 0.0, 100.0);
                if(sParam.substr(7) == "level")
                    g_presets[nIndex].reverb.level = validateDouble(sValue, 0.0, 1.0);
            }
            else if (sParam.substr(0,7) == "chorus_")
            {
                if(sParam.substr(7) == "enable")
                    g_presets[nIndex].chorus.enable = (sValue == "1");
                if(sParam.substr(7) == "voicecount")
                    g_presets[nIndex].chorus.voicecount = validateInt(sValue, 0, 99);
                if(sParam.substr(7) == "level")
                    g_presets[nIndex].chorus.level = validateDouble(sValue, 0.0, 10.0);
                if(sParam.substr(7) == "speed")
                    g_presets[nIndex].chorus.speed = validateDouble(sValue, 0.1, 5.0);
                if(sParam.substr(7) == "depth")
                    g_presets[nIndex].chorus.depth = validateDouble(sValue, 0.0, 21.0);
                if(sParam.substr(7) == "type")
                    g_presets[nIndex].chorus.type = validateInt(sValue, 0, 1);
            }
        }

    }
    fileConfig.close();
    return true;
}

/**     Save persistent data to configuration file */
bool saveConfig(string sFilename = "./fb.config")
{
    ofstream fileConfig;
    fileConfig.open(sFilename, ios::out);
    if(!fileConfig.is_open())
    {
        printf("Error: Failed to open configuration: %s\n", sFilename.c_str());
        return false;
    }
    for(unsigned int nPreset = 0; nPreset < MAX_PRESETS; ++nPreset)
    {
        fileConfig << endl << "[preset_" << nPreset << "]" << endl;
        fileConfig << endl << "name=" << g_presets[nPreset].name << endl;
        fileConfig << endl << "soundfont=" << g_presets[nPreset].soundfont << endl;
        for(unsigned int nProgram = 0; nProgram < 16; ++nProgram)
        {
            fileConfig << "prog_" << nProgram << "=" << g_presets[nPreset].program[nProgram].bank << ":" << g_presets[nPreset].program[nProgram].program << endl;
            fileConfig << "level_" << nProgram << "=" << g_presets[nPreset].program[nProgram].level << endl;
            fileConfig << "balance_" << nProgram << "=" << g_presets[nPreset].program[nProgram].balance << endl;
        }
        fileConfig << "reverb_enable=" <<  (g_presets[nPreset].reverb.enable?"1":"0") << endl;
        fileConfig << "reverb_roomsize=" <<  g_presets[nPreset].reverb.roomsize << endl;
        fileConfig << "reverb_damping=" <<  g_presets[nPreset].reverb.damping << endl;
        fileConfig << "reverb_width=" <<  g_presets[nPreset].reverb.width << endl;
        fileConfig << "reverb_level=" <<  g_presets[nPreset].reverb.level << endl;
        fileConfig << "chorus_enable=" <<  g_presets[nPreset].chorus.enable << endl;
        fileConfig << "chorus_voicecount=" <<  g_presets[nPreset].chorus.voicecount << endl;
        fileConfig << "chorus_level=" <<  g_presets[nPreset].chorus.level << endl;
        fileConfig << "chorus_speed=" <<  g_presets[nPreset].chorus.speed << endl;
        fileConfig << "chorus_depth=" <<  g_presets[nPreset].chorus.depth << endl;
        fileConfig << "chorus_type=" <<  g_presets[nPreset].chorus.type << endl;
    }
    fileConfig.close();
    return true;
}

/**     Loads a soundfont from file, unloading previously loaded soundfont */
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
    {
        g_presets[0].soundfont = sFilename;
        g_bPresetDirty = true;
    }
    return (g_nCurrentSoundfont >= 0);
}

/**     Select a preset
*       @param nPreset Index of preset to load
*       @retval bool True on success
*/
bool selectPreset(unsigned int nPreset)
{
    if(nPreset > MAX_PRESETS)
        return false;
    Preset* pPreset = &(g_presets[nPreset]);
    Preset* pPreset0 = &(g_presets[0]);
    // We use preset 0 for the currently selected and edited preset
    if((nPreset == 0) || (pPreset->soundfont != pPreset0->soundfont))
        if(!loadSoundfont(pPreset->soundfont))
            return false;

    for(unsigned int nChannel = 0; nChannel < 16; ++nChannel)
    {
        fluid_synth_program_select(g_pSynth, nChannel, g_nCurrentSoundfont, pPreset->program[nChannel].bank, pPreset->program[nChannel].program);
        pPreset0->program[nChannel].program = pPreset->program[nChannel].program;
        pPreset0->program[nChannel].bank = pPreset->program[nChannel].bank;
        pPreset0->program[nChannel].level = pPreset->program[nChannel].level;
        pPreset0->program[nChannel].balance = pPreset->program[nChannel].balance;
        //!@todo set level and balance
    }

    //!@todo Extract this to a presetCopy function
    pPreset0->name = pPreset->name;
    pPreset0->soundfont = pPreset->soundfont;
    pPreset0->reverb.enable = pPreset->reverb.enable;
    pPreset0->reverb.roomsize = pPreset->reverb.roomsize;
    pPreset0->reverb.damping = pPreset->reverb.damping;
    pPreset0->reverb.width = pPreset->reverb.width;
    pPreset0->reverb.level = pPreset->reverb.level;
    pPreset0->chorus.enable = pPreset->chorus.enable;
    pPreset0->chorus.voicecount = pPreset->chorus.voicecount;
    pPreset0->chorus.level = pPreset->chorus.level;
    pPreset0->chorus.speed = pPreset->chorus.speed;
    pPreset0->chorus.depth = pPreset->chorus.depth;
    pPreset0->chorus.type = pPreset->chorus.type;
    return true;
}

/**     Handle button press - powers off device */
void onButton()
{
    printf("Button pressed\n");
    g_pScreen->Clear(BLUE);
    g_pScreen->DrawText("Shutting down...", 10, 30);
    saveConfig();
    system("sudo poweroff");
}

/*     Handles signal */
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


/**     Main application */
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

    // Select preset zero
    selectPreset(0);

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


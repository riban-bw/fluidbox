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
#define CHANNELS_IN_PROG_SCREEN 6

// Define GPIO pin usage (note some are not used by code but useful for planning
#define BUTTON_POWER   3
#define BUTTON_UP      4
#define BUTTON_DOWN   17
#define BUTTON_LEFT   27
#define BUTTON_RIGHT  22
#define BUTTON_PANIC  23
#define SPI_CE1        7
#define DISPLAY_CS     8
#define SPI_CE0        8
#define SPI_MISO       9
#define SPI_MOSI      10
#define SPI_SCLK      11
#define DISPLAY_DC    24
#define DISPLAY_RESET 25

using namespace std;

enum {
    SCREEN_BLANK,
    SCREEN_LOGO,
    SCREEN_PERFORMANCE,
    SCREEN_PROGRAM,
    SCREEN_SELECT_PROGRAM
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

fluid_synth_t* g_pSynth; // Pointer to the synth object
ribanfblib* g_pScreen; // Pointer to the screen object
bool g_bPresetDirty = false; // True if preset 0 has been modified
int g_nCurrentSoundfont = FLUID_FAILED; // ID of currently loaded soundfont
int g_nScreen = SCREEN_LOGO; // ID of currently displayed screen - maybe should be derived from state model
int g_nRunState = 1; // Current run state [1=running, 0=closing]
unsigned int g_nNoteCount[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}; // Quantity of notes playing on each MIDI channel
Preset g_presets[MAX_PRESETS]; // Preset configurations
unsigned int g_nCurrentPreset = 1; // Index of the selected preset
unsigned int g_nSelectedChannel = 0; // Index of the selected (highlighted) program
unsigned int g_nProgScreenFirst = 0; // Channel at top of program screen

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

/** Updates the display of a program for the specified channel
    @param nChannel MIDI channel to display
*/
void showProgram(unsigned int nChannel)
{
    if(g_nScreen != SCREEN_PROGRAM || nChannel < g_nProgScreenFirst || nChannel > g_nProgScreenFirst + CHANNELS_IN_PROG_SCREEN)
        return;

    int nSfId, nBank, nProgram;
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
            int nY = 32 + (nChannel - g_nProgScreenFirst) * 16;
            g_pScreen->DrawRect(0,nY, 160,nY+15, BLACK, 0, (g_nSelectedChannel == nChannel)?BLUE:BLACK);
            g_pScreen->DrawText(sPreset.c_str(), 2, 15+nY, WHITE);
        }
    }
}

/**     Update the MIDI note on indicator on the program screens showing quantity of on notes (max 15)
*       @param nChannel MIDI channel to update
*/
void showMidiActivity(int nChannel)
{
    if(g_nScreen != SCREEN_PROGRAM || nChannel < g_nProgScreenFirst || nChannel > g_nProgScreenFirst + CHANNELS_IN_PROG_SCREEN)
        return;
    int nY = 32 + (nChannel - g_nProgScreenFirst) * 16; //Upper left corner of channel indicator
    g_pScreen->DrawRect(0,nY, 2,nY+15, BLACK, 0, BLACK); // Clear the indicator
    int nCount = (g_nNoteCount[nChannel] < 16)?g_nNoteCount[nChannel]:15; // Limit max note indication to 15
    if(nCount)
        g_pScreen->DrawRect(0,nY+15, 2,nY+15-nCount, RED, 0, RED); // Draw indicator
}

void showPerformanceScreen()
{
    g_pScreen->Clear();
    g_pScreen->DrawRect(0,0, 160,16, BLUE, 0, BLUE); //!@todo Use header colour to distinguish screens?
    string sTitle = "riban fluidbox 0.1";
    g_pScreen->DrawText(sTitle, 0, 15);
    string sPreset;
    if(g_bPresetDirty)
        sPreset = "*";
    else
        sPreset = " ";
    sPreset += g_presets[0].name;
    g_pScreen->DrawText(sPreset, 0, 50);
    //!@todo Improve performance screen
}

/** Display list of programs for each channel in the currently selected preset
*/
void showProgramScreen()
{
    g_pScreen->Clear();
    g_pScreen->DrawRect(0,0, 160,16, BLUE, 0, BLUE);
    string sTitle = "Preset: ";
    sTitle += to_string(g_nCurrentPreset);
    sTitle += " - Program Select";
    g_pScreen->DrawText(sTitle, 0, 0);
    // Ensure selected channel is in view
    if(g_nSelectedChannel < g_nProgScreenFirst)
        g_nProgScreenFirst = g_nSelectedChannel;
    else if(g_nSelectedChannel > g_nProgScreenFirst + CHANNELS_IN_PROG_SCREEN)
        g_nProgScreenFirst = g_nSelectedChannel - CHANNELS_IN_PROG_SCREEN; //!@todo validate CHANNELS_IN_PROG_SCREEN usage (may be one less)
    for(unsigned int nChannel = g_nProgScreenFirst; nChannel < g_nProgScreenFirst + CHANNELS_IN_PROG_SCREEN; ++ nChannel)
    {
        if(nChannel > 15)
            return;
        showProgram(nChannel);
    }
}

/** Display the requested screen
    @param nScreen ID of screen to display
*/
void showScreen(int nScreen)
{
    g_nScreen = nScreen;
    g_pScreen->Clear();
    switch(nScreen)
    {
        case SCREEN_PERFORMANCE:
            showPerformanceScreen();
            break;
        case SCREEN_PROGRAM:
            showProgramScreen();
            break;
        case SCREEN_BLANK:
            return;
        case SCREEN_LOGO:
            g_pScreen->DrawBitmap("logo", 0, 0);
            break;
    }
}

/** Handle MIDI events */
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
            g_bPresetDirty = true;
            showProgram(nChannel);
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

/** Convert string to lowercase */
string toLower(string sString)
{
    string sReturn;
    for(size_t i=0; i<sString.length(); ++i)
        sReturn += tolower(sString[i]);
    return sReturn;
}

/** Load configuration from file */
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
        else if(sGroup == "global")
        {
            if(sParam == "preset")
                g_nCurrentPreset = validateInt(sValue, 1, MAX_PRESETS);
        }
    }
    fileConfig.close();
    return true;
}

/** Save persistent data to configuration file */
bool saveConfig(string sFilename = "./fb.config")
{
    ofstream fileConfig;
    fileConfig.open(sFilename, ios::out);
    if(!fileConfig.is_open())
    {
        printf("Error: Failed to open configuration: %s\n", sFilename.c_str());
        return false;
    }

    // Save global settings
    fileConfig << "[global]" << endl;
    fileConfig << "preset=" << to_string(g_nCurrentPreset) << endl;

    // Save presets
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

/** Loads a soundfont from file, unloading previously loaded soundfont */
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

/**  Copy a preset
*    @param nSource Index of the source preset
*    @param nDestination Index of the destination preset
*    @retval bool True on success
*/
bool copyPreset(unsigned int nSource, unsigned int nDestination)
{
    if(nSource > MAX_PRESETS || nDestination > MAX_PRESETS || nSource == nDestination)
        return false;
    Preset* pPresetSrc = &(g_presets[nSource]);
    Preset* pPresetDst = &(g_presets[nDestination]);
    for(unsigned int nChannel = 0; nChannel < 16; ++nChannel)
    {
        pPresetDst->program[nChannel].program = pPresetSrc->program[nChannel].program;
        pPresetDst->program[nChannel].bank = pPresetSrc->program[nChannel].bank;
        pPresetDst->program[nChannel].level = pPresetSrc->program[nChannel].level;
        pPresetDst->program[nChannel].balance = pPresetSrc->program[nChannel].balance;
    }

    pPresetDst->name = pPresetSrc->name;
    pPresetDst->soundfont = pPresetSrc->soundfont;
    pPresetDst->reverb.enable = pPresetSrc->reverb.enable;
    pPresetDst->reverb.roomsize = pPresetSrc->reverb.roomsize;
    pPresetDst->reverb.damping = pPresetSrc->reverb.damping;
    pPresetDst->reverb.width = pPresetSrc->reverb.width;
    pPresetDst->reverb.level = pPresetSrc->reverb.level;
    pPresetDst->chorus.enable = pPresetSrc->chorus.enable;
    pPresetDst->chorus.voicecount = pPresetSrc->chorus.voicecount;
    pPresetDst->chorus.level = pPresetSrc->chorus.level;
    pPresetDst->chorus.speed = pPresetSrc->chorus.speed;
    pPresetDst->chorus.depth = pPresetSrc->chorus.depth;
    pPresetDst->chorus.type = pPresetSrc->chorus.type;
    return true;
}

/** Select a preset
*   @param nPreset Index of preset to load
*   @retval bool True on success
*/
bool selectPreset(unsigned int nPreset)
{
    if(nPreset > MAX_PRESETS)
        return false;
    // We use preset 0 for the currently selected and edited preset
    Preset* pPreset = &(g_presets[0]);
    if((nPreset == 0) || (pPreset->soundfont != g_presets[nPreset].soundfont))
        if(!loadSoundfont(g_presets[nPreset].soundfont))
            return false;
    copyPreset(nPreset, 0);
    for(unsigned int nChannel = 0; nChannel < 16; ++nChannel)
    {
        fluid_synth_program_select(g_pSynth, nChannel, g_nCurrentSoundfont, pPreset->program[nChannel].bank, pPreset->program[nChannel].program);
        fluid_synth_cc(g_pSynth, nChannel, 7, pPreset->program[nChannel].level);
        fluid_synth_cc(g_pSynth, nChannel, 8, pPreset->program[nChannel].balance);
    }
    g_nCurrentPreset = nPreset;
    g_bPresetDirty = false;
    return true;
}

/** Handle navigation buttons
    @param nButton Index of button pressed
*/
void onNavigation(unsigned int nButton)
{
    cout << "onNavigation(" << nButton << ")" << endl;
    switch(g_nScreen)
    {
        case SCREEN_LOGO:
            // Let's go somewhere useful if we are showing logo
            showScreen(SCREEN_PERFORMANCE);
            break;
        case SCREEN_PERFORMANCE:
            break;
        case SCREEN_PROGRAM:
            switch(nButton)
            {
                case BUTTON_UP:
                    if(g_nSelectedChannel == 0)
                        return;
                    --g_nSelectedChannel;
                    showScreen(SCREEN_PROGRAM);
                    break;
                case BUTTON_DOWN:
                    if(g_nSelectedChannel > 14)
                        return;
                    ++g_nSelectedChannel;
                    showScreen(SCREEN_PROGRAM);
                    break;
                case BUTTON_LEFT:
                    showScreen(SCREEN_PERFORMANCE);
                    break;
                case BUTTON_RIGHT:
                    showScreen(SCREEN_SELECT_PROGRAM);
                    break;
            }
    }
}

/** Handle button press - powers off device */
void onButtonPower()
{
    printf("Button pressed\n");
    g_pScreen->Clear(BLUE);
    g_pScreen->DrawText("Shutting down...", 10, 30);
    saveConfig();
    system("sudo poweroff");
}

void onButtonUp()
{
    onNavigation(BUTTON_UP);
}

void onButtonDown()
{
    onNavigation(BUTTON_DOWN);
}

void onButtonLeft()
{
    onNavigation(BUTTON_LEFT);
}

void onButtonRight()
{
    onNavigation(BUTTON_RIGHT);
}

void onButtonPanic()
{
    panic();
}

/**  Handles signal */
void onSignal(int nSignal)
{
    switch(nSignal)
    {
    	case SIGALRM:
    	    // We use alarm to drop back to performance screen after idle delay
            if(g_nScreen == SCREEN_LOGO)
                showScreen(SCREEN_PERFORMANCE);
            break;
    	case SIGINT:
    	case SIGTERM:
            printf("Received signal to quit...\n");
        	g_nRunState = 0;
            break;
    }
}


/** Main application */
int main(int argc, char** argv)
{
    printf("riban fluidbox\n");
    g_pScreen = new ribanfblib("/dev/fb1");
    g_pScreen->LoadBitmap("logo.bmp", "logo");
    showScreen(SCREEN_LOGO); // Show logo at start up - will go to performance screen after idle delay or button press
    loadConfig();

    // Create and populate fluidsynth settings
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

    // Select preset
    selectPreset(g_nCurrentPreset);

    // Configure buttons
    wiringPiSetupGpio();
    wiringPiISR(BUTTON_POWER, INT_EDGE_FALLING, onButtonPower);
    wiringPiISR(BUTTON_PANIC, INT_EDGE_FALLING, onButtonPanic);
    wiringPiISR(BUTTON_UP, INT_EDGE_FALLING, onButtonUp);
    wiringPiISR(BUTTON_DOWN, INT_EDGE_FALLING, onButtonDown);
    wiringPiISR(BUTTON_LEFT, INT_EDGE_FALLING, onButtonLeft);
    wiringPiISR(BUTTON_RIGHT, INT_EDGE_FALLING, onButtonRight);

    // Configure signal handlers
    signal(SIGALRM, onSignal);
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);

    // Show splash screen for a while (idle delay)
    alarm(2);

    while(g_nRunState)
        pause();

    // If we are here then it is all over so let's tidy up...

    //Write configuration
    saveConfig();

    // Clean up
    delete_fluid_midi_router(pRouter);
    delete_fluid_audio_driver(pAudioDriver);
    delete_fluid_midi_driver(pMidiDriver);
    delete_fluid_synth(g_pSynth);
    delete_fluid_settings(pSettings);
    g_pScreen->Clear();
    delete g_pScreen;
    return 0;
}


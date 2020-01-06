#include "fluidsynth.h"
#include "buttonhandler.hpp"
#include "ribanfblib/ribanfblib.h"
#include "screen.hpp"
#include <wiringPi.h>
#include <cstdio> //provides printf
#include <iostream> //provides streams
#include <fstream> //provides file stream
#include <unistd.h> //provides pause, alarm
#include <signal.h> //provides signal handling
#include <vector>
#include <map>

#define DEFAULT_SOUNDFONT "default/TimGM6mb.sf2"
#define SF_ROOT "sf2/"
#define MAX_PRESETS 127
#define CHANNELS_IN_PROG_SCREEN 6

// Define GPIO pin usage (note some are not used by code but useful for planning
#define BUTTON_UP      4
#define BUTTON_DOWN   17
#define BUTTON_LEFT   27
#define BUTTON_RIGHT   3
#define BUTTON_PANIC  23
#define SPI_CE1        7
#define DISPLAY_CS     8
#define SPI_CE0        8
#define SPI_MISO       9
#define SPI_MOSI      10
#define SPI_SCLK      11
#define DISPLAY_DC    24
#define DISPLAY_RESET 25
#define DISPLAY_LED   12

using namespace std;

enum {
	SCREEN_NONE,
	SCREEN_PERFORMANCE,
	SCREEN_BLANK,
	SCREEN_LOGO,
	SCREEN_EDIT,
	SCREEN_POWER,
	SCREEN_EDIT_PRESET,
	SCREEN_PRESET_NAME,
	SCREEN_PRESET_SF,
	SCREEN_PRESET_PROGRAM,
	SCREEN_EFFECTS,
        SCREEN_MIXER,
	SCREEN_SOUNDFONT,
	SCREEN_REBOOT,
	SCREEN_EOL
};

enum {
    PANIC_NOTES,
    PANIC_SOUNDS,
    PANIC_RESET
};

enum {
    POWER_OFF,
    POWER_OFF_SAVE,
    POWER_REBOOT,
    POWER_REBOOT_SAVE
};

enum {
    REVERB_ENABLE,
    REVERB_ROOMSIZE,
    REVERB_DAMPING,
    REVERB_WIDTH,
    REVERB_LEVEL
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
    string name = "New preset";
    string soundfont = DEFAULT_SOUNDFONT;
    Program program[16];
    Reverb reverb;
    Chorus chorus;
    bool dirty = false;
};

void showScreen(int nScreen);

fluid_synth_t* g_pSynth; // Pointer to the synth object
ribanfblib* g_pScreen; // Pointer to the screen object
int g_nCurrentSoundfont = FLUID_FAILED; // ID of currently loaded soundfont
int g_nRunState = 1; // Current run state [1=running, 0=closing]
unsigned int g_nNoteCount[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}; // Quantity of notes playing on each MIDI channel
vector<Preset*> g_vPresets; // Map of presets indexed by id
//Preset g_presets[MAX_PRESETS]; // Preset configurations
unsigned int g_nCurrentPreset = 0; // Index of the selected preset
//!@todo Cannot handle zero presets!!!
unsigned int g_nSelectedChannel = 0; // Index of the selected (highlighted) program
unsigned int g_nListSelection = 0; // Currently highlighted entry in a list
unsigned char debouncePin[32]; // Debounce streams for each GPIO pin

map<unsigned int,ListScreen*> g_mapScreens; // Map of screens indexed by id
unsigned int g_nCurrentScreen; // Id of currently displayed screen


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

/** Convert string to lowercase */
string toLower(string sString)
{
    string sReturn;
    for(size_t i=0; i<sString.length(); ++i)
        sReturn += tolower(sString[i]);
    return sReturn;
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
    for(unsigned int nPreset = 0; nPreset < g_vPresets.size(); ++nPreset)
    {
        fileConfig << endl << "[preset_" << nPreset << "]" << endl;
        fileConfig << endl << "name=" << g_vPresets[nPreset]->name << endl;
        fileConfig << endl << "soundfont=" << g_vPresets[nPreset]->soundfont << endl;
        for(unsigned int nProgram = 0; nProgram < 16; ++nProgram)
        {
            fileConfig << "prog_" << nProgram << "=" << g_vPresets[nPreset]->program[nProgram].bank << ":" << g_vPresets[nPreset]->program[nProgram].program << endl;
            fileConfig << "level_" << nProgram << "=" << g_vPresets[nPreset]->program[nProgram].level << endl;
            fileConfig << "balance_" << nProgram << "=" << g_vPresets[nPreset]->program[nProgram].balance << endl;
        }
        fileConfig << "reverb_enable=" <<  (g_vPresets[nPreset]->reverb.enable?"1":"0") << endl;
        fileConfig << "reverb_roomsize=" <<  g_vPresets[nPreset]->reverb.roomsize << endl;
        fileConfig << "reverb_damping=" <<  g_vPresets[nPreset]->reverb.damping << endl;
        fileConfig << "reverb_width=" <<  g_vPresets[nPreset]->reverb.width << endl;
        fileConfig << "reverb_level=" <<  g_vPresets[nPreset]->reverb.level << endl;
        fileConfig << "chorus_enable=" <<  g_vPresets[nPreset]->chorus.enable << endl;
        fileConfig << "chorus_voicecount=" <<  g_vPresets[nPreset]->chorus.voicecount << endl;
        fileConfig << "chorus_level=" <<  g_vPresets[nPreset]->chorus.level << endl;
        fileConfig << "chorus_speed=" <<  g_vPresets[nPreset]->chorus.speed << endl;
        fileConfig << "chorus_depth=" <<  g_vPresets[nPreset]->chorus.depth << endl;
        fileConfig << "chorus_type=" <<  g_vPresets[nPreset]->chorus.type << endl;
    }
    fileConfig.close();
    return true;
}

void power(unsigned int nAction)
{
    string sCommand, sMessage;
    switch(nAction)
    {
        case POWER_OFF:
            sCommand = "sudo poweroff";
            sMessage = " POWERING DOWN";
            break;
        case POWER_OFF_SAVE:
            saveConfig();
            sCommand = "sudo poweroff";
            sMessage = " POWERING DOWN";
            break;
        case POWER_REBOOT:
            sCommand = "sudo reboot";
            sMessage = "      REBOOTING";
            break;
        case POWER_REBOOT_SAVE:
            saveConfig();
            sCommand = "sudo reboot";
            sMessage = "      REBOOTING";
            break;
        default:
            return;
    }
    g_pScreen->Clear(DARK_RED);
    g_pScreen->DrawText(sMessage, 0, 60);
    system(sCommand.c_str());
}

void editReverb(unsigned int nParam)
{
    switch(nParam)
    {
        case REVERB_ENABLE:
        case REVERB_ROOMSIZE:
        case REVERB_DAMPING:
        case REVERB_WIDTH:
        case REVERB_LEVEL:
            break;
    }
}

/**  Shows the edit program screen */
void showEditProgram(unsigned int=0)
{
    g_mapScreens[SCREEN_PRESET_PROGRAM]->ClearList();
    int nSfId, nBank, nProgram;
    char sPrefix[6];
    for(unsigned int nChannel = 0; nChannel < 16; ++nChannel)
    {
        fluid_synth_get_program(g_pSynth, nChannel, &nSfId, &nBank, &nProgram);
        fluid_sfont_t* pSoundfont = fluid_synth_get_sfont_by_id(g_pSynth, nSfId);
        sprintf(sPrefix, "%02d: ", nChannel+1);
        string sName = (char*)sPrefix;
        if(pSoundfont)
        {
            fluid_preset_t* pPreset = fluid_sfont_get_preset(pSoundfont, nBank, nProgram);
            if(pPreset)
                sName += fluid_preset_get_name(pPreset);
        }
        g_mapScreens[SCREEN_PRESET_PROGRAM]->Add(sName);
    }
    showScreen(SCREEN_PRESET_PROGRAM);
}

/**     Update the MIDI note on indicator on the program screens showing quantity of on notes (max 15)
*       @param nChannel MIDI channel to update
*/
void showMidiActivity(int nChannel)
{
    if(g_nCurrentScreen != SCREEN_PRESET_PROGRAM || nChannel < g_mapScreens[SCREEN_PRESET_PROGRAM]->GetFirstShown() || nChannel > g_mapScreens[SCREEN_PRESET_PROGRAM]->GetFirstShown() + 6)
        return;
    int nY = 16 + (nChannel - g_mapScreens[SCREEN_PRESET_PROGRAM]->GetFirstShown()) * 16; //Upper left corner of channel indicator
    g_pScreen->DrawRect(0,nY, 1,nY+15, BLACK, 0, BLACK); // Clear the indicator
    int nCount = (g_nNoteCount[nChannel] < 16)?g_nNoteCount[nChannel]:15; // Limit max note indication to 15
    if(nCount)
        g_pScreen->DrawRect(0,nY+15, 1,nY+15-nCount, RED, 0, RED); // Draw indicator
}

/** Display the requested screen
    @param pScreen Pointer to the screen to display
*/
void showScreen(int nScreen)
{
    if(nScreen == SCREEN_NONE)
        return;
    auto it = g_mapScreens.find(nScreen);
    if(it == g_mapScreens.end())
        return;
    if(nScreen == SCREEN_PERFORMANCE)
        it->second->SetSelection(g_nCurrentPreset);
    it->second->Draw();
    it->second->SetPreviousScreen(g_nCurrentScreen);
    g_nCurrentScreen = nScreen;
}

void save(int)
{
    saveConfig();
    showScreen(SCREEN_PERFORMANCE);
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
            g_vPresets[g_nCurrentPreset]->program[nChannel].program = nProgram;
            g_vPresets[g_nCurrentPreset]->dirty = true;
            if(g_nCurrentScreen == SCREEN_PRESET_PROGRAM)
                showEditProgram();
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
    for(auto it = g_vPresets.begin(); it!= g_vPresets.end(); ++it)
        delete *it;
    g_vPresets.clear();
    string sLine, sGroup;
    unsigned int nPreset = 0;
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

            if(sGroup.substr(0,7) == "preset_")
            {
                //!@todo We are not using the preset index defined in config file
                //nPreset = validateInt(sGroup.substr(7), 0, MAX_PRESETS);
                nPreset = g_vPresets.size();
                g_vPresets.push_back(new Preset);
            }
            continue;
        }
        size_t nDelim = sLine.find_first_of("=");
        if(nDelim == string::npos)
            continue; //Not a valid value definition
        string sParam = sLine.substr(0, nDelim);
        string sValue = sLine.substr(nDelim + 1);

        if(sGroup.substr(0,7) == "preset_")
        {
            if(sParam == "name")
                g_vPresets[nPreset]->name = sValue;
            else if(sParam == "soundfont")
                g_vPresets[nPreset]->soundfont = sValue;
            else if(sParam.substr(0,5) == "prog_")
            {
                int nChan = validateInt(sParam.substr(5), 0, 15);
                nDelim = sValue.find_first_of(':');
                if(nDelim == string::npos)
                    continue; // Not a valid bank:program value pair
                g_vPresets[nPreset]->program[nChan].bank = validateInt(sValue.substr(0,nDelim), 0, 16383);
                g_vPresets[nPreset]->program[nChan].program = validateInt(sValue.substr(nDelim + 1), 0, 127);
            }
            else if(sParam.substr(0,6) == "level_")
            {
                int nChan = validateInt(sParam.substr(6), 0, 15);
                g_vPresets[nPreset]->program[nChan].level = validateInt(sValue, 0, 127);
            }
            else if(sParam.substr(0,8) == "balance_")
            {
                int nChan = validateInt(sParam.substr(8), 0, 15);
                g_vPresets[nPreset]->program[nChan].balance = validateInt(sValue, 0, 127);
            }
            else if (sParam.substr(0,7) == "reverb_")
            {
                if(sParam.substr(7) == "enable")
                    g_vPresets[nPreset]->reverb.enable = (sValue == "1");
                if(sParam.substr(7) == "roomsize")
                    g_vPresets[nPreset]->reverb.roomsize = validateDouble(sValue, 0.0, 1.0);
                if(sParam.substr(7) == "damping")
                    g_vPresets[nPreset]->reverb.damping = validateDouble(sValue, 0.0, 1.0);
                if(sParam.substr(7) == "width")
                    g_vPresets[nPreset]->reverb.width = validateDouble(sValue, 0.0, 100.0);
                if(sParam.substr(7) == "level")
                    g_vPresets[nPreset]->reverb.level = validateDouble(sValue, 0.0, 1.0);
            }
            else if (sParam.substr(0,7) == "chorus_")
            {
                if(sParam.substr(7) == "enable")
                    g_vPresets[nPreset]->chorus.enable = (sValue == "1");
                if(sParam.substr(7) == "voicecount")
                    g_vPresets[nPreset]->chorus.voicecount = validateInt(sValue, 0, 99);
                if(sParam.substr(7) == "level")
                    g_vPresets[nPreset]->chorus.level = validateDouble(sValue, 0.0, 10.0);
                if(sParam.substr(7) == "speed")
                    g_vPresets[nPreset]->chorus.speed = validateDouble(sValue, 0.1, 5.0);
                if(sParam.substr(7) == "depth")
                    g_vPresets[nPreset]->chorus.depth = validateDouble(sValue, 0.0, 21.0);
                if(sParam.substr(7) == "type")
                    g_vPresets[nPreset]->chorus.type = validateInt(sValue, 0, 1);
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
    g_pScreen->DrawRect(20,40, 120,80, BLACK, 0, DARK_BLUE, QUADRANT_ALL, 5);
    g_pScreen->DrawText("Loading soundfont", 30, 50, WHITE);
    g_nCurrentSoundfont = fluid_synth_sfload(g_pSynth, sPath.c_str(), 1);
    if(g_nCurrentSoundfont >= 0)
    {
        g_vPresets[g_nCurrentPreset]->soundfont = sFilename;
        g_vPresets[g_nCurrentPreset]->dirty = true;
    }
    g_pDisplay->Draw();
    return (g_nCurrentSoundfont >= 0);
}

/**  Copy a preset
*    @param nSource Index of the source preset
*    @param nDestination Index of the destination preset
*    @retval bool True on success
*/
bool copyPreset(unsigned int nSource, unsigned int nDestination)
{
    if(nSource > g_vPresets.size() || nDestination > g_vPresets.size() || nSource == nDestination)
        return false;
    Preset* pPresetSrc = g_vPresets[nSource];
    Preset* pPresetDst = g_vPresets[nDestination];
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
    if(nPreset >= g_vPresets.size())
        nPreset = g_vPresets.size() - 1;
    if(nPreset >= g_vPresets.size())
        return false;
    cout << "Select preset " << nPreset << endl;
    Preset* pPreset = g_vPresets[nPreset];
    bool bSoundfontChanged = (g_nCurrentPreset >= 0 && g_nCurrentPreset < g_vPresets.size() && pPreset->soundfont != g_vPresets[g_nCurrentPreset]->soundfont);
    g_nCurrentPreset = nPreset;
    if(bSoundfontChanged || g_nCurrentSoundfont < 0)
        if(!loadSoundfont(pPreset->soundfont))
            return false;
    for(unsigned int nChannel = 0; nChannel < 16; ++nChannel)
    {
        fluid_synth_program_select(g_pSynth, nChannel, g_nCurrentSoundfont, pPreset->program[nChannel].bank, pPreset->program[nChannel].program);
        fluid_synth_cc(g_pSynth, nChannel, 7, pPreset->program[nChannel].level);
        fluid_synth_cc(g_pSynth, nChannel, 8, pPreset->program[nChannel].balance);
    }
    return true;
}

void newPreset(unsigned int)
{
    //!@todo Insert new preset at current position
    unsigned int nPreset = g_vPresets.size();;
    g_vPresets.push_back(new Preset);
    g_mapScreens[SCREEN_PERFORMANCE]->Add(g_vPresets[nPreset]->name, showScreen, SCREEN_EDIT);
    selectPreset(g_vPresets.size() - 1);
    showScreen(SCREEN_PERFORMANCE);
}

/**  Delete the currently selected preset */
void deletePreset(unsigned int)
{
    if(g_vPresets.size() == 1)
    {
        showScreen(SCREEN_PERFORMANCE);
        return; // Must have at least one preset
    }
    auto it = g_vPresets.begin();
    for(unsigned i = 0; i < g_nCurrentPreset; ++i)
        ++it;
    delete *it;
    g_vPresets.erase(it);
    g_mapScreens[SCREEN_PERFORMANCE]->Remove(g_nCurrentPreset);
    if(g_nCurrentPreset)
        --g_nCurrentPreset;
    selectPreset(g_nCurrentPreset);
    showScreen(SCREEN_PERFORMANCE);
}

/** Handle navigation buttons
    @param nButton Index of button pressed
*/
void onNavigate(unsigned int nButton)
{
    switch(g_nCurrentScreen)
    {
        case SCREEN_LOGO:
        case SCREEN_BLANK:
            showScreen(SCREEN_PERFORMANCE);
            break;
        default:
            switch(nButton)
            {
                case BUTTON_UP:
                    g_mapScreens[g_nCurrentScreen]->Previous();
                    break;
                case BUTTON_DOWN:
                    g_mapScreens[g_nCurrentScreen]->Next();
                    break;
                case BUTTON_RIGHT:
                    g_mapScreens[g_nCurrentScreen]->Select();
                    break;
                case BUTTON_LEFT:
                    if(g_nCurrentScreen == SCREEN_POWER)
                        showScreen(g_mapScreens[g_nCurrentScreen]->GetPreviousScreen());
                    else
                        showScreen(g_mapScreens[g_nCurrentScreen]->GetParent());
                    break;
            }
    }
    if(g_nCurrentScreen == SCREEN_PERFORMANCE && (nButton == BUTTON_UP || nButton == BUTTON_DOWN))
        selectPreset(g_mapScreens[SCREEN_PERFORMANCE]->GetSelection());
}

void onLeftHold(unsigned int nGpio)
{
    switch(g_nCurrentScreen)
    {
        case SCREEN_PERFORMANCE:
            showScreen(SCREEN_POWER);
            break;
        default:
            showScreen(SCREEN_PERFORMANCE);
    }
}

void onRightHold(unsigned int nGpio)
{
    switch(g_nCurrentScreen)
    {
        case SCREEN_PRESET_NAME:
            showScreen(SCREEN_EDIT_PRESET);
            break;
        default:
            panic();
    }
}

/**  Handles signal */
void onSignal(int nSignal)
{
    switch(nSignal)
    {
    	case SIGALRM:
    	    // We use alarm to drop back to performance screen after idle delay
            if(g_nCurrentScreen == SCREEN_LOGO)
                showScreen(SCREEN_PERFORMANCE);
            break;
    	case SIGINT:
    	case SIGTERM:
            printf("\nReceived signal to quit...\n");
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
    g_pScreen->DrawBitmap("logo", 0, 0);
    g_nCurrentScreen = SCREEN_LOGO;

    system("gpio mode 26 pwm");
    system("gpio pwm 26 900");

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

    // Configure buttons
    wiringPiSetupGpio();
    ButtonHandler buttonHandler;
    buttonHandler.AddButton(BUTTON_UP, onNavigate);
    buttonHandler.AddButton(BUTTON_DOWN, onNavigate);
    buttonHandler.AddButton(BUTTON_LEFT, NULL, onNavigate, onLeftHold);
    buttonHandler.AddButton(BUTTON_RIGHT, NULL, onNavigate, onRightHold);
    buttonHandler.SetRepeatPeriod(BUTTON_UP, 100);
    buttonHandler.SetRepeatPeriod(BUTTON_DOWN, 100);

    // Configure signal handlers
    signal(SIGALRM, onSignal);
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);

    g_mapScreens[SCREEN_PERFORMANCE] = new ListScreen(g_pScreen, "  riban Fluidbox", SCREEN_NONE);
    g_mapScreens[SCREEN_EDIT_PRESET] = new ListScreen(g_pScreen, "Edit Preset", SCREEN_EDIT);
    g_mapScreens[SCREEN_EDIT] = new ListScreen(g_pScreen, "Edit", SCREEN_PERFORMANCE);
    g_mapScreens[SCREEN_POWER] = new ListScreen(g_pScreen, "Power", SCREEN_PERFORMANCE);
    g_mapScreens[SCREEN_PRESET_NAME] = new ListScreen(g_pScreen, "Preset Name", SCREEN_EDIT_PRESET);
    g_mapScreens[SCREEN_PRESET_SF] = new ListScreen(g_pScreen, "Preset Soundfont", SCREEN_EDIT_PRESET);
    g_mapScreens[SCREEN_PRESET_PROGRAM] = new ListScreen(g_pScreen,  "Preset Program", SCREEN_EDIT_PRESET);
    g_mapScreens[SCREEN_EFFECTS] = new ListEditScreen(g_pScreen, "Effects", SCREEN_EDIT);
    g_mapScreens[SCREEN_MIXER] = new ListScreen(g_pScreen,  "Mixer", SCREEN_EDIT);
    g_mapScreens[SCREEN_SOUNDFONT] = new ListScreen(g_pScreen, "Soundfont", SCREEN_EDIT);

    for(unsigned int nPreset=0; nPreset < g_vPresets.size(); ++nPreset)
        g_mapScreens[SCREEN_PERFORMANCE]->Add(g_vPresets[nPreset]->name, showScreen, SCREEN_EDIT);

    g_mapScreens[SCREEN_EDIT]->Add("Mixer", showScreen, SCREEN_MIXER);
    g_mapScreens[SCREEN_EDIT]->Add("Effects", showScreen, SCREEN_EFFECTS);
    g_mapScreens[SCREEN_EDIT]->Add("Edit preset", showScreen, SCREEN_EDIT_PRESET);
    g_mapScreens[SCREEN_EDIT]->Add("New preset", newPreset);
    g_mapScreens[SCREEN_EDIT]->Add("Delete preset", deletePreset);
    g_mapScreens[SCREEN_EDIT]->Add("Manage soundfonts", showScreen, SCREEN_SOUNDFONT);
    g_mapScreens[SCREEN_EDIT]->Add("Save", save);
    g_mapScreens[SCREEN_EDIT]->Add("Power", showScreen, SCREEN_POWER);

    g_mapScreens[SCREEN_EDIT_PRESET]->Add("Name", showScreen, SCREEN_PRESET_NAME);
    g_mapScreens[SCREEN_EDIT_PRESET]->Add("Soundfont", showScreen, SCREEN_PRESET_SF);
    g_mapScreens[SCREEN_EDIT_PRESET]->Add("Program", showEditProgram);

    g_mapScreens[SCREEN_POWER]->Add("Save and power off", power, POWER_OFF_SAVE);
    g_mapScreens[SCREEN_POWER]->Add("Save and reboot", power, POWER_REBOOT_SAVE);
    g_mapScreens[SCREEN_POWER]->Add("Power off", power, POWER_OFF);
    g_mapScreens[SCREEN_POWER]->Add("Reboot",  power, POWER_REBOOT);

    g_mapScreens[SCREEN_EFFECTS]->Add("Reverb enable", editReverb, REVERB_ENABLE);
    g_mapScreens[SCREEN_EFFECTS]->Add("Reverb room size", editReverb, REVERB_ROOMSIZE);
    g_mapScreens[SCREEN_EFFECTS]->Add("Reverb damping", editReverb, REVERB_DAMPING);
    g_mapScreens[SCREEN_EFFECTS]->Add("Reverb width", editReverb, REVERB_WIDTH);
    g_mapScreens[SCREEN_EFFECTS]->Add("Reverb level", editReverb, REVERB_LEVEL);


    // Select preset
    if(g_vPresets.size() == 0)
        newPreset(0);
    selectPreset(g_nCurrentPreset);
    g_mapScreens[SCREEN_PERFORMANCE]->SetSelection(g_nCurrentPreset);

    // Show splash screen for a while (idle delay)
    alarm(2);

    while(g_nRunState)
    {
        buttonHandler.Process();
        delay(5);
    }

    // If we are here then it is all over so let's tidy up...

    // Clean up
    delete_fluid_midi_router(pRouter);
    delete_fluid_audio_driver(pAudioDriver);
    delete_fluid_midi_driver(pMidiDriver);
    delete_fluid_synth(g_pSynth);
    delete_fluid_settings(pSettings);
    g_pScreen->Clear();
    for(auto it = g_mapScreens.begin(); it!= g_mapScreens.end(); ++it)
        delete it->second;
    g_mapScreens.clear();
    for(auto it = g_vPresets.begin(); it!= g_vPresets.end(); ++it)
        delete *it;
    g_vPresets.clear();
    return 0;
}


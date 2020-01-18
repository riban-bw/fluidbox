#include "fluidsynth.h"
#include "buttonhandler.hpp"
#include "ribanfblib/ribanfblib.h"
#include "screen.hpp"

#include <vector>
#include <map>

#define DEFAULT_SOUNDFONT "default/TimGM6mb.sf2"
#define SF_ROOT "sf2/"
#define MAX_PRESETS 127
#define CHANNELS_IN_PROG_SCREEN 6
#define PI 3.14159265359
#define MAX_NAME_LEN 20
#define DEFAULT_FONT_SIZE 16, 12

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

enum SCREEN_ID
{
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
    SCREEN_PROGRAM,
    SCREEN_EFFECTS,
    SCREEN_EDIT_VALUE,
    SCREEN_MIXER,
    SCREEN_SOUNDFONT,
    SCREEN_SOUNDFONT_LIST,
    SCREEN_REBOOT,
    SCREEN_ALERT,
    SCREEN_EOL
};

enum PANIC_MODE
{
    PANIC_NOTES,
    PANIC_SOUNDS,
    PANIC_RESET
};

enum ADMIN_MODE
{
    POWER_OFF,
    POWER_OFF_SAVE,
    POWER_REBOOT,
    POWER_REBOOT_SAVE,
    RESTART_SERVICE,
    SAVE_CONFIG,
    SAVE_BACKUP,
    LOAD_BACKUP,
    LOAD_CONFIG
};

enum EFFECT_PARAM
{
    REVERB_ENABLE,
    REVERB_ROOMSIZE,
    REVERB_DAMPING,
    REVERB_WIDTH,
    REVERB_LEVEL,
    CHORUS_ENABLE,
    CHORUS_VOICES,
    CHORUS_LEVEL,
    CHORUS_SPEED,
    CHORUS_DEPTH,
    CHORUS_TYPE
};

enum SOUNDFONT_ACTION
{
    SF_ACTION_NONE,
    SF_ACTION_COPY,
    SF_ACTION_DELETE,
    SF_ACTION_SELECT
};

/** MIDI program parameters */
struct Program
{
    string name = "New program";
    unsigned int bank = 0;
    unsigned int program = 0;
    unsigned int level = 100;
    unsigned int balance = 63;
};

/** Reverb parameters */
struct Reverb
{
    bool enable = false;
    double roomsize = 0;
    double damping = 0;
    double width = 0;
    double level = 0;
};

/** Chorus parameters */
struct Chorus
{
    bool enable = false;
    int voicecount = 0;
    double level = 0;
    double speed = 0.1;
    double depth = 0;
    int type = FLUID_CHORUS_MOD_SINE;
};

/** Parameters used by presets */
struct Preset
{
    string name = "New preset          ";
    string soundfont = DEFAULT_SOUNDFONT;
    Program program[16];
    Reverb reverb;
    Chorus chorus;
    bool dirty = false;
};

/** Limits of each effect parameter */
struct EffectParams
{
    double min;
    double max;
    double delta;
    string name;
};


std::map <unsigned int,EffectParams> g_mapEffectParams;
fluid_synth_t* g_pSynth; // Pointer to the synth object
ribanfblib* g_pScreen; // Pointer to the screen object
int g_nCurrentSoundfont = FLUID_FAILED; // ID of currently loaded soundfont
int g_nRunState = 1; // Current run state [1=running, 0=closing]
unsigned int g_nNoteCount[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}; // Quantity of notes playing on each MIDI channel
std::vector<Preset*> g_vPresets; // Map of presets indexed by id
Preset* g_pCurrentPreset = NULL; // Pointer to the currently selected preset
unsigned int g_nSelectedChannel = 0; // Index of the selected (highlighted) program
unsigned char debouncePin[32]; // Debounce streams for each GPIO pin

std::map<unsigned int,ListScreen*> g_mapScreens; // Map of screens indexed by id
unsigned int g_nCurrentScreen; // Id of currently displayed screen
unsigned int g_nCurrentChannel = 0; // Selected channel, e.g. within mixer screen
unsigned int g_nCurrentChar = 0; // Index of highlighted character in name edit
unsigned int g_nCurrentEffect;
bool g_bDirty = false;// True if configuration needs to be saved
SOUNDFONT_ACTION g_nSoundfontAction = SF_ACTION_NONE;
std::function<void(void)> g_pAlertCallback = NULL;
Style g_style;
string g_sFont = "liberation/LiberationMono-Regular.ttf";
uint32_t g_colourMixerHighlight = ORANGE;
uint32_t g_colourMixerFaderBg = DARK_GREEN;
uint32_t g_colourMixerFaderFg =GREY;
uint32_t g_colourAlertCanvas = ORANGE;
uint32_t g_colourAlertYesBg = DARK_BLUE;
uint32_t g_colourAlertNoBg = DARK_RED;
uint32_t g_colourToastBg = DARK_BLUE;

/**   Populate effect parameter data */
void configParams();

/**     Return a converted and vaildated value
*       @param sValue Value as a string
*       @param min Minimum permitted value
*       @param max Maximum permitted value
*       @retval double Value converted to double limited to range min-max
*/
double validateDouble(string sValue, double min, double max);

/**     Return a converted and vaildated value
*       @param sValue Value as a string
*       @param min Minimum permitted value
*       @param max Maximum permitted value
*       @retval int Value converted to int limited to range min-max
*/
int validateInt(string sValue, int min, int max);

/** Convert string to lowercase */
string toLower(string sString);

/** Check if USB storage is mounted
*   retval bool True if mounted
*/
bool isUsbMounted();

/** Get the index of a preset
*   @param  pPreset Pointer to a preset
*   @retval int Index of preset or -1 if none found
*/
int getPresetIndex(Preset* pPreset);

/** PANIC
    param nMode Panic mode [PANIC_NOTES | PANIC_SOUNDS | PANIC_RESET]
    param nChannel MIDI channel to reset [0-15, 16=ALL]
**/
void panic(int nMode=PANIC_NOTES, int nChannel=16);

/**  Refresh the content of the presets list in the performance screen */
void refreshPresetList();

/**  Set the dirty flag of a preset
*    @param pPreset Pointer to the preset - Default: current preset
*    @param bDirty True to flag as dirty - Default: true
*/
void setDirty(Preset* pPreset = NULL, bool bDirty = true);

/** Save persistent data to configuration file */
bool saveConfig(string sFilename = "./fluidbox.config");

/** Handle admin events
*   @param  nAction ID of event
*/
void admin(unsigned int nAction);

/** Draws representation of current effect parameter value
*   @param  nParam Index of the effect parameter
*   @param  nValue Value of parameter, scaled to 0..70
*/
void drawEffectValue(unsigned int nParam, double dValue);

/** Alters the value of an effect parameter
*   @param  nParam Index of the effect parameter to alter
*   @param  nChange Amount to change value [-1, 0, +1]
*   @retval double Value of the parameter after adjustment
*   @note   If nChange is non-zero then screen is drawn with new value
*/
double adjustEffect(unsigned int nParam, int nChange = 0);

/**  Set enable or disable an effect
*    @param nEffect Index of the effect [REVERB_ENABLE | CHORUS_ENABLE]
*    @param bEnable True to enable, false to disable - default true
*/
void enableEffect(unsigned int nEffect, bool bEnable = true);

void editEffect(unsigned int nParam);

/**  Shows the edit program screen */
void showEditProgram(unsigned int=0);

/**     Update the MIDI note on indicator on the program screens showing quantity of on notes (max 15)
*       @param nChannel MIDI channel to update
*/
void showMidiActivity(int nChannel);

/** Draw a mixer channel
*   @param  nChannel MIDI channel [0-15]
*/
void drawMixerChannel(unsigned int nChannel);

/** Draw preset name screen
*/
void drawPresetName();

/** Handle "manage soundfont" events
*   @param nAction Action to take on selected soundfont
*/
void listSoundfont(int nAction);

/** Copy a file
*   sSource Full path and filename of file to copy
*   sDest Full path and filename of new file
*/
void copyFile(string sSource, string sDest);

/** Delete file selected in soundfont file list */
void deleteFile();

/** Show an alert
*   @param sMessage Message to display (limit to single 20 char line)
*   @param sTitle Title to display at top of alert
*   @param pFunction Pointer to a function to call if RIGHT / SELECT / ENTER button pressed - Default: None
*   @param nTimeout Period in seconds before alert clears - Default: 0, no timeout
*   @note  Pressing LEFT / CANCEL button clears the alert
*   @note  Callback must be in form: void function()
*/
void alert(string sMessage, string sTitle = "    ALERT", function<void(void)> pFunction = NULL, unsigned int nTimeout = 0);

/** Loads a soundfont from file, unloading previously loaded soundfont
*   @param sFilename Full path and filename of soundfont to load
*   @retval bool True on succes
*/
bool loadSoundfont(string sFilename);

/**  Handle select soundfont action
*    @param nAction Action to perform on currently selected soundfont
*/
void onSelectSoundfont(int nAction);

void populateSoundfontList();

/** Display the requested screen
    @param pScreen Pointer to the screen to display
*/
void showScreen(int nScreen);

/** Handle MIDI events
*   @param pData Pointer to fluidsynth instance
*   @param pEvent Pointer to MIDI event
*   @retval int 0 on success
*   @note Called by fluidsynth MIDI router
*/
int onMidiEvent(void* pData, fluid_midi_event_t* pEvent);

/** Load configuration from file
*   @param  sFilename Full path and filename of configuration file
*/
bool loadConfig(string sFilename = "./fluidbox.config");

/** Select a preset
*   @param pPreset Pointer to preset to load
*   @retval bool True on success
*/
bool selectPreset(Preset* pPreset);

/** Create a new preset object
*   @retval Preset* Pointer to the new preset object
*/
Preset* createPreset();

/**  Copy a preset
*    @param pSrc Pointer to the source preset
*    @param pDst Pointer to the destination preset (NULL to create new preset)
*    @retval bool True on success
*/
bool copyPreset(Preset* pSrc, Preset* pDst = NULL);

/** Handle newPreset event */
void newPreset(unsigned int);

/**  Delete the currently selected preset */
void deletePreset();

/**  Handle request to delete the currently selected preset */
void requestDeletePreset(unsigned int);

/** Handle navigation buttons
    @param nButton Index of button pressed
*/
void onButton(unsigned int nButton);

/** Handle hold left button
*   @param nGpio Index of GPIO being held
*/
void onLeftHold(unsigned int nGpio);

/** Handle hold right button
*   @param nGpio Index of GPIO being held
*/
void onRightHold(unsigned int nGpio);

/** Handles signal
*   @param nSignal Signal number
*/
void onSignal(int nSignal);

/** Set the bank and program for the currently selected channel and preset
*   @param nBankProgram Bank (most significant 8 bits) and program (least significant 8 bits)
*/
void setPresetProgram(int nBankProgram);

/** Populates the program screen
*   @param nChannel Channel to edit
*/
void populateProgram(int nChannel);

/** Get the name of the MIDI program (patch) currently loaded to specified channel
*   @param nChannel MIDI channel
*   @retval string Name of program
*/
string getProgramName(unsigned int nChannel);


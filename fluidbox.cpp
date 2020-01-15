#include "fluidbox.h"
#include <wiringPi.h>
#include <cstdio> //provides printf
#include <iostream> //provides streams
#include <fstream> //provides file stream
#include <sys/stat.h> // provides stat for file detection
#include <fcntl.h>   // open
#include <unistd.h>  // read, write, close
#include <dirent.h> //provides directory management
#include <unistd.h> //provides pause, alarm
#include <signal.h> //provides signal handling

void showScreen(int nScreen)
{
    // Handle non-ListScreen screens
    if(nScreen == SCREEN_LOGO)
    {
        g_pScreen->DrawBitmap("logo", 0, 0);
        g_nCurrentScreen = SCREEN_LOGO;
        return;
    }

    // Check the ListScreen exists
    auto it = g_mapScreens.find(nScreen);
    if(it == g_mapScreens.end())
        return;
    ListScreen* pScreen = it->second;

    // Action before showing ListScreen
    switch(nScreen)
    {
    case SCREEN_PERFORMANCE:
        pScreen->SetSelection(getPresetIndex(g_pCurrentPreset));
        if(g_bDirty)
            g_mapScreens[SCREEN_PERFORMANCE]->SetTitle("  *riban Fluidbox");
        else
            g_mapScreens[SCREEN_PERFORMANCE]->SetTitle("   riban Fluidbox");
        break;
    case SCREEN_EDIT_VALUE:
        g_mapScreens[SCREEN_EDIT_VALUE]->SetTitle(g_mapEffectParams[g_nCurrentEffect].name);
        break;
    case SCREEN_SOUNDFONT:
        g_nSoundfontAction = SF_ACTION_NONE;
        break;
    case SCREEN_SOUNDFONT_LIST:
        populateSoundfontList();
    }
    pScreen->Draw();
    g_nCurrentScreen = nScreen;

    // Action after showing ListScreen
    switch(nScreen)
    {
    case SCREEN_MIXER:
        g_nCurrentChannel = 0;
        for(unsigned int nChannel = 0; nChannel < 16; ++nChannel)
            drawMixerChannel(nChannel);
        break;
    case SCREEN_PRESET_NAME:
        g_nCurrentChar = 0;
        drawPresetName();
        break;
    case SCREEN_EDIT_VALUE:
        drawEffectValue(g_nCurrentEffect, adjustEffect(g_nCurrentEffect, 0));
        break;
    }
}

void configParams()
{
    g_mapEffectParams[REVERB_ENABLE].name = "Reverb enable";
    g_mapEffectParams[REVERB_ENABLE].min = 0.0;
    g_mapEffectParams[REVERB_ENABLE].max = 1.0;
    g_mapEffectParams[REVERB_ENABLE].delta = 1.0;
    g_mapEffectParams[REVERB_ROOMSIZE].name = "Reverb roomsize";
    g_mapEffectParams[REVERB_ROOMSIZE].min = 0.0;
    g_mapEffectParams[REVERB_ROOMSIZE].max = 1.2;
    g_mapEffectParams[REVERB_ROOMSIZE].delta = 0.1;
    g_mapEffectParams[REVERB_DAMPING].name = "Reverb damping";
    g_mapEffectParams[REVERB_DAMPING].min = 0.0;
    g_mapEffectParams[REVERB_DAMPING].max = 1.0;
    g_mapEffectParams[REVERB_DAMPING].delta = 0.1;
    g_mapEffectParams[REVERB_WIDTH].name = "Reverb width";
    g_mapEffectParams[REVERB_WIDTH].min = 0.0;
    g_mapEffectParams[REVERB_WIDTH].max = 100.0;
    g_mapEffectParams[REVERB_WIDTH].delta = 5.0;
    g_mapEffectParams[REVERB_LEVEL].name = "Reverb level";
    g_mapEffectParams[REVERB_LEVEL].min = 0.0;
    g_mapEffectParams[REVERB_LEVEL].max = 1.0;
    g_mapEffectParams[REVERB_LEVEL].delta = 0.05;
    g_mapEffectParams[CHORUS_ENABLE].name = "Chorus enable";
    g_mapEffectParams[CHORUS_ENABLE].min = 0.0;
    g_mapEffectParams[CHORUS_ENABLE].max = 1.0;
    g_mapEffectParams[CHORUS_ENABLE].delta = 1.0;
    g_mapEffectParams[CHORUS_VOICES].name = "Chorus voices";
    g_mapEffectParams[CHORUS_VOICES].min = 0.0;
    g_mapEffectParams[CHORUS_VOICES].max = 99.0;
    g_mapEffectParams[CHORUS_VOICES].delta = 1.0;
    g_mapEffectParams[CHORUS_LEVEL].name = "Chorus level";
    g_mapEffectParams[CHORUS_LEVEL].min = 0.0;
    g_mapEffectParams[CHORUS_LEVEL].max = 10.0;
    g_mapEffectParams[CHORUS_LEVEL].delta = 1.0;
    g_mapEffectParams[CHORUS_SPEED].name = "Chorus speed";
    g_mapEffectParams[CHORUS_SPEED].min = 0.1;
    g_mapEffectParams[CHORUS_SPEED].max = 5.0;
    g_mapEffectParams[CHORUS_SPEED].delta = 0.5;
    g_mapEffectParams[CHORUS_DEPTH].name = "Chorus depth";
    g_mapEffectParams[CHORUS_DEPTH].min = 0.0;
    g_mapEffectParams[CHORUS_DEPTH].max = 21.0;
    g_mapEffectParams[CHORUS_DEPTH].delta = 1.0;
    g_mapEffectParams[CHORUS_TYPE].name = "Chorus type";
    g_mapEffectParams[CHORUS_TYPE].min = FLUID_CHORUS_MOD_SINE;
    g_mapEffectParams[CHORUS_TYPE].max = FLUID_CHORUS_MOD_TRIANGLE;
    g_mapEffectParams[CHORUS_TYPE].delta = 1.0;
}

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

string toLower(string sString)
{
    string sReturn;
    for(size_t i=0; i<sString.length(); ++i)
        sReturn += tolower(sString[i]);
    return sReturn;
}

bool isUsbMounted()
{
    return (system("mount | grep /media/usb > /dev/null") ==0);
}

int getPresetIndex(Preset* pPreset)
{
    int nIndex = 0;
    for(int nIndex = 0; nIndex < g_vPresets.size(); ++nIndex)
    {
        if(g_vPresets[nIndex] == pPreset)
            return nIndex;
    }
    return -1;
}

void panic(int nMode, int nChannel)
{
    if(nMode == PANIC_RESET)
    {
        fluid_synth_system_reset(g_pSynth);
        return;
    }
    int nMin = nChannel;
    int nMax = nChannel;
    if(nChannel > 15)
    {
        nMin = 0;
        nMax = 15;
    }
    for(int i = nMin; i <= nMax; ++i)
    {
        switch(nMode)
        {
        case PANIC_NOTES:
            fluid_synth_all_notes_off(g_pSynth, i);
            break;
        case PANIC_SOUNDS:
            fluid_synth_all_sounds_off(g_pSynth, i);
            break;
        }
    }
}

void refreshPresetList()
{
    g_mapScreens[SCREEN_PERFORMANCE]->ClearList();
    for(auto it = g_vPresets.begin(); it != g_vPresets.end(); ++it)
    {
        Preset* pPreset = *it;
        string sName;
        if(pPreset->dirty)
            sName += "*";
        sName += pPreset->name;
        g_mapScreens[SCREEN_PERFORMANCE]->Add(sName, showScreen, SCREEN_EDIT);
    }
    g_mapScreens[SCREEN_PERFORMANCE]->SetSelection(g_mapScreens[SCREEN_PERFORMANCE]->GetSelection()); // Sets to end if overrun
}

void setDirty(Preset* pPreset, bool bDirty)
{
    if(!pPreset)
        pPreset = g_pCurrentPreset;
    if(!pPreset)
        return;
    pPreset->dirty = bDirty;
    if(bDirty)
        g_bDirty = true;
    refreshPresetList();
    if(g_nCurrentScreen == SCREEN_PERFORMANCE)
        showScreen(SCREEN_PERFORMANCE);
}

bool saveConfig(string sFilename)
{
    ofstream fileConfig;
    fileConfig.open(sFilename, ios::out);
    if(!fileConfig.is_open())
    {
        printf("Error: Failed to open configuration: %s\n", sFilename.c_str());
        return false;
    }

    // Save presets
    for(auto it = g_vPresets.begin(); it != g_vPresets.end(); ++it)
    {
        Preset* pPreset = *it;
        fileConfig << endl << "[preset]" << endl;
        if(pPreset == g_pCurrentPreset)
            fileConfig << "selected=1" << endl;
        fileConfig << "name=" << pPreset->name << endl;
        fileConfig << "soundfont=" << pPreset->soundfont << endl;
        for(unsigned int nProgram = 0; nProgram < 16; ++nProgram)
        {
            fileConfig << "prog_" << nProgram << "=" << pPreset->program[nProgram].bank << ":" << pPreset->program[nProgram].program << endl;
            fileConfig << "level_" << nProgram << "=" << pPreset->program[nProgram].level << endl;
            fileConfig << "balance_" << nProgram << "=" << pPreset->program[nProgram].balance << endl;
        }
        fileConfig << "reverb_enable=" <<  (pPreset->reverb.enable?"1":"0") << endl;
        fileConfig << "reverb_roomsize=" <<  pPreset->reverb.roomsize << endl;
        fileConfig << "reverb_damping=" <<  pPreset->reverb.damping << endl;
        fileConfig << "reverb_width=" <<  pPreset->reverb.width << endl;
        fileConfig << "reverb_level=" <<  pPreset->reverb.level << endl;
        fileConfig << "chorus_enable=" <<  pPreset->chorus.enable << endl;
        fileConfig << "chorus_voicecount=" <<  pPreset->chorus.voicecount << endl;
        fileConfig << "chorus_level=" <<  pPreset->chorus.level << endl;
        fileConfig << "chorus_speed=" <<  pPreset->chorus.speed << endl;
        fileConfig << "chorus_depth=" <<  pPreset->chorus.depth << endl;
        fileConfig << "chorus_type=" <<  pPreset->chorus.type << endl;
        setDirty(pPreset, false);
    }

    fileConfig.close();
    g_bDirty = false;
    return true;
}

void power(unsigned int nAction)
{
    string sCommand, sMessage;
    switch(nAction)
    {
    case POWER_OFF:
        sCommand = "sudo poweroff";
        sMessage = "POWERING DOWN";
        break;
    case POWER_OFF_SAVE:
        saveConfig();
        sCommand = "sudo poweroff";
        sMessage = "POWERING DOWN";
        break;
    case POWER_REBOOT:
        sCommand = "sudo reboot";
        sMessage = "  REBOOTING";
        break;
    case POWER_REBOOT_SAVE:
        saveConfig();
        sCommand = "sudo reboot";
        sMessage = "  REBOOTING";
        break;
    default:
        return;
    }
    g_pScreen->Clear(DARK_RED);
    g_pScreen->SetFont(20);
    g_pScreen->DrawText(sMessage, 0, 70);
    g_pScreen->SetFont(DEFAULT_FONT_SIZE);
    system(sCommand.c_str());
}

void drawEffectValue(unsigned int nParam, double dValue)
{
    if(g_nCurrentScreen != SCREEN_EDIT_VALUE || nParam > 10)
        return;
    g_pScreen->DrawRect(0,16, 159,127, BLACK, 0, BLACK); // clear data area (keep title)
    unsigned int nValue = (unsigned int)(dValue * (70 / g_mapEffectParams[nParam].max));
    if(nValue > 70)
        nValue = 70;
    unsigned int nX = 10; // x-coord of origin of triangle
    unsigned int nY = 100; // y-coord of origin of triangle
    if(nParam == CHORUS_TYPE)
    {
        if(nValue == FLUID_CHORUS_MOD_SINE)
        {
            for(unsigned int nX = 10; nX < 150; ++nX)
            {
                float nYoffset = 32.0 * sin(2.0 * PI * (nX - 10) / 140.0);
                g_pScreen->DrawPixel(nX, 67 + nYoffset, DARK_BLUE);
            }
        }
        else
        {
            g_pScreen->DrawLine(10, 67, 45, 35, DARK_BLUE);
            g_pScreen->DrawLine(45, 35, 115, 100, DARK_BLUE);
            g_pScreen->DrawLine(115, 100, 150, 67, DARK_BLUE);
        }
        g_pScreen->DrawText(nValue==FLUID_CHORUS_MOD_SINE?"SINE":"TRIANGLE", nX, nY + 20);
        return;
    }
    g_pScreen->DrawTriangle(nX, nY, nX + nValue * 2, nY, nX + nValue * 2, nY - nValue, DARK_BLUE, 0, DARK_BLUE);
    char sValue[10];
    sprintf(sValue, "%0.2f", dValue);
    g_pScreen->DrawText(sValue, nX, nY + 20);
}

double adjustEffect(unsigned int nParam, int nChange)
{
    if(nParam > 10)
        return 0;
    if(nChange)
        setDirty();
    double dValue;
    double dMax = g_mapEffectParams[nParam].max;
    double dMin = g_mapEffectParams[nParam].min;
    double dDelta = g_mapEffectParams[nParam].delta;
    int nValue;
    switch(nParam)
    {
    case REVERB_DAMPING:
        dValue = fluid_synth_get_reverb_damp(g_pSynth);
        dValue += nChange * dDelta;
        if(dValue > dMax)
            dValue = dMax;
        else if(dValue < dMin)
            dValue = dMin;
        fluid_synth_set_reverb_damp(g_pSynth, dValue);
        g_pCurrentPreset->reverb.damping = dValue;
        break;
    case REVERB_LEVEL:
        dValue = fluid_synth_get_reverb_level(g_pSynth);
        dValue += nChange * dDelta;
        if(dValue > dMax)
            dValue = dMax;
        else if(dValue < dMin)
            dValue = dMin;
        fluid_synth_set_reverb_level(g_pSynth, dValue);
        g_pCurrentPreset->reverb.level = dValue;
        break;
    case REVERB_ROOMSIZE:
        dValue = fluid_synth_get_reverb_roomsize(g_pSynth);
        dValue += nChange * dDelta;
        if(dValue > dMax)
            dValue = dMax;
        else if(dValue < dMin)
            dValue = dMin;
        fluid_synth_set_reverb_roomsize(g_pSynth, dValue);
        g_pCurrentPreset->reverb.roomsize = dValue;
        break;
    case REVERB_WIDTH:
        dValue = fluid_synth_get_reverb_width(g_pSynth);
        dValue += nChange * dDelta;
        if(dValue > dMax)
            dValue = dMax;
        else if(dValue < dMin)
            dValue = dMin;
        fluid_synth_set_reverb_width(g_pSynth, dValue);
        g_pCurrentPreset->reverb.width = dValue;
        break;
    case CHORUS_DEPTH:
        dValue = fluid_synth_get_chorus_depth(g_pSynth);
        dValue += nChange * dDelta;
        if(dValue > dMax)
            dValue = dMax;
        else if(dValue < dMin)
            dValue = dMin;
        fluid_synth_set_chorus_depth(g_pSynth, dValue);
        g_pCurrentPreset->chorus.depth = dValue;
        break;
    case CHORUS_LEVEL:
        dValue = fluid_synth_get_chorus_level(g_pSynth);
        dValue += nChange * dDelta;
        if(dValue > dMax)
            dValue = dMax;
        else if(dValue < dMin)
            dValue = dMin;
        fluid_synth_set_chorus_level(g_pSynth, dValue);
        g_pCurrentPreset->chorus.level = dValue;
        break;
    case CHORUS_SPEED:
        dValue = fluid_synth_get_chorus_speed(g_pSynth);
        dValue += nChange * dDelta;
        if(dValue > dMax)
            dValue = dMax;
        else if(dValue < dMin)
            dValue = dMin;
        fluid_synth_set_chorus_speed(g_pSynth, dValue);
        g_pCurrentPreset->chorus.speed = dValue;
        break;
    case CHORUS_TYPE:
    {
        // min:FLUID_CHORUS_MOD_SINE==0, max:FLUID_CHORUS_MOD_TRIANGLE==1
        if(nChange > 0)
            fluid_synth_set_chorus_type(g_pSynth, FLUID_CHORUS_MOD_TRIANGLE);
        else if(nChange < 0)
            fluid_synth_set_chorus_type(g_pSynth, FLUID_CHORUS_MOD_SINE);
        nValue = fluid_synth_get_chorus_type(g_pSynth);
        g_pCurrentPreset->chorus.type = nValue;
        string sText = "Chorus type       ";
        sText += (nValue==FLUID_CHORUS_MOD_SINE)?"SINE":" TRI";
        g_mapScreens[SCREEN_EFFECTS]->SetEntryText(nParam, sText);
        return nValue;
        break;
    }
    case CHORUS_VOICES:
        dValue = fluid_synth_get_chorus_nr(g_pSynth);
        dValue += nChange * dDelta;
        if(dValue > dMax)
            dValue = dMax;
        else if(dValue < dMin)
            dValue = dMin;
        fluid_synth_set_chorus_nr(g_pSynth, (int)dValue);
        g_pCurrentPreset->chorus.voicecount = dValue;
        break;
    default:
        return 0;
    }
    char sTemp[10];
    sprintf(sTemp, "%0.2f", dValue);
    string sValue = sTemp;
    string sText = g_mapEffectParams[nParam].name;
    sText.resize(22 - sValue.length(), ' ');
    sText += sValue;
    g_mapScreens[SCREEN_EFFECTS]->SetEntryText(nParam, sText);
    return dValue;
}

void enableEffect(unsigned int nEffect, bool bEnable)
{
    string sText;
    bool bWasEnabled;
    if(nEffect == REVERB_ENABLE)
    {
        fluid_synth_set_reverb_on(g_pSynth, bEnable);
        if(g_pCurrentPreset->reverb.enable != bEnable)
            setDirty();
        g_pCurrentPreset->reverb.enable = bEnable;
        sText = "Reverb ";
    }
    else if(nEffect == CHORUS_ENABLE)
    {
        fluid_synth_set_chorus_on(g_pSynth, bEnable);
        if(g_pCurrentPreset->chorus.enable != bEnable)
            setDirty();
        g_pCurrentPreset->chorus.enable = bEnable;
        sText = "Chorus ";
    }
    else
        return;
    for(unsigned int nIndex = nEffect + 1; nIndex < (nEffect==REVERB_ENABLE?5:12); ++nIndex)
        g_mapScreens[SCREEN_EFFECTS]->Enable(nIndex, bEnable);
    sText += bEnable?"        Enabled":"       Disabled";
    g_mapScreens[SCREEN_EFFECTS]->SetEntryText(nEffect, sText);
}

void editEffect(unsigned int nParam)
{
    switch(nParam)
    {
    case REVERB_ENABLE:
        {
            enableEffect(nParam, !(g_pCurrentPreset->reverb.enable));
            g_mapScreens[SCREEN_EFFECTS]->Draw();
            break;
        }
    case CHORUS_ENABLE:
        {
            enableEffect(nParam, !(g_pCurrentPreset->chorus.enable));
            g_mapScreens[SCREEN_EFFECTS]->Draw();
            break;
        }
    case CHORUS_VOICES:
    case CHORUS_LEVEL:
    case CHORUS_SPEED:
    case CHORUS_DEPTH:
    case CHORUS_TYPE:
    case REVERB_ROOMSIZE:
    case REVERB_DAMPING:
    case REVERB_WIDTH:
    case REVERB_LEVEL:
        g_nCurrentEffect = nParam;
        showScreen(SCREEN_EDIT_VALUE);
        break;
    }
}

void showEditProgram(unsigned int)
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

void drawMixerChannel(unsigned int nChannel)
{
    int nLevel;
    if(g_nCurrentScreen != SCREEN_MIXER || nChannel > 15)
        return;
    if(FLUID_FAILED == fluid_synth_get_cc(g_pSynth, nChannel, 7, &nLevel))
        return;
    nLevel = (nLevel * 100) / 127;
    g_pScreen->DrawRect(nChannel * 10, 19, nChannel * 10 + 9, 121, GREY, 1, BLACK); // Frame for fader
    g_pScreen->DrawRect(nChannel * 10 + 1, 120, nChannel * 10 + 8, 120 - nLevel, DARK_GREEN, 0, DARK_GREEN); // Fader
    g_pScreen->DrawRect(0,127, 159,122, BLACK, 0, BLACK); // Frame for selection highlight
    g_pScreen->DrawRect(g_nCurrentChannel * 10, 124, g_nCurrentChannel * 10 + 10,122, BLUE, 0, BLUE); // Selection highlight
    g_pScreen->SetFont(10);
    g_pScreen->DrawText(to_string(nChannel + 1), nChannel * 10 + 9, 121, GREY, 90);
    g_pScreen->SetFont(DEFAULT_FONT_SIZE);
}

void drawPresetName()
{
    g_pScreen->DrawRect(0, 16, 159, 127, BLACK, 0, BLACK);
    g_pScreen->DrawText(g_pCurrentPreset->name, 8, 68);
    g_pScreen->DrawRect(7 + g_nCurrentChar * 7, 71, 14 + g_nCurrentChar * 7, 72, BLACK, 0, BLUE);
}

void listSoundfont(int nAction)
{
    g_nSoundfontAction = (SOUNDFONT_ACTION)nAction;
    showScreen(SCREEN_SOUNDFONT_LIST);
}

void copyFile(string sSource, string sDest)
{
    int nSrc = open(sSource.c_str(), O_RDONLY, 0);
    if(nSrc == -1)
        return;
    int nDst = open(sDest.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR); //!@todo File is created with wrong permissions
    if(nDst == -1)
    {
        close(nSrc);
        return;
    }
    struct stat fileStat;
    stat(sSource.c_str(), &fileStat);
    if(fileStat.st_blocks < 1)
    {
        close(nSrc);
        close(nDst);
        return;
    }
    size_t nBlocks = fileStat.st_blocks * 512 / fileStat.st_blksize; // Quantity of blocks based on file system block size
    float fX = 0;
    float fDx =  159.0 / nBlocks;
    int nX = 0;
    char* pBuffer = (char*)malloc(sizeof(char)*fileStat.st_blksize);
    g_pScreen->DrawRect(0,100, 159,127, WHITE, 1, GREY);
    size_t nSize;
    while(g_nRunState && ((nSize = read(nSrc, pBuffer, fileStat.st_blksize)) > 0))
    {
        write(nDst, pBuffer, nSize);
        // Update progress bar
        fX += fDx;
        if(int(fX) > nX)
        {
            nX = fX;
            g_pScreen->DrawRect(0,100, nX,127, WHITE, 1, GREEN);
        }
    }
    g_pScreen->DrawText("Flushing file...", 5, 120, BLACK);
    close(nDst); // There is a delay whilst file is flushed to disk
    close(nSrc);
    free(pBuffer);
}

void deleteFile()
{
    string sCommand = "sudo rm sf2/'";
    sCommand += g_mapScreens[SCREEN_SOUNDFONT_LIST]->GetEntryText(g_mapScreens[SCREEN_SOUNDFONT_LIST]->GetSelection());
    sCommand += "'";
    system(sCommand.c_str());
    cout << sCommand << endl;
}

void alert(string sMessage, string sTitle, function<void(void)>  pFunction, unsigned int nTimeout)
{
    g_mapScreens[SCREEN_ALERT]->SetTitle(sTitle);
    g_mapScreens[SCREEN_ALERT]->SetParent(g_nCurrentScreen);
    showScreen(SCREEN_ALERT);
    uint32_t nColour = ribanfblib::GetColour32(200, 150, 50);
    g_pScreen->DrawRect(0,16,159,127, nColour, 0, nColour);
    g_pScreen->DrawText(sMessage, 5, 55); //!@todo Allow multiline alert messages
    g_pScreen->DrawCircle(25,100, 20, WHITE, 1, DARK_RED);
    g_pScreen->DrawCircle(134,100, 20, WHITE, 1, DARK_BLUE);
    g_pScreen->SetFont(16);
    g_pScreen->DrawText("NO", 16, 107, WHITE);
    g_pScreen->DrawText("YES", 119, 107,  WHITE);
    g_pScreen->SetFont(DEFAULT_FONT_SIZE);
    g_pAlertCallback = pFunction;
    if(nTimeout)
        alarm(nTimeout);
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
    g_pScreen->DrawRect(2,100, 157,124, DARK_BLUE, 5, DARK_BLUE, QUADRANT_ALL, 5);
    g_pScreen->DrawText("Loading soundfont", 4, 118, WHITE);
    g_nCurrentSoundfont = fluid_synth_sfload(g_pSynth, sPath.c_str(), 1);
    if(g_nCurrentSoundfont >= 0)
    {
        g_pCurrentPreset->soundfont = sFilename;
    }
    showScreen(g_nCurrentScreen);
    return (g_nCurrentSoundfont >= 0);
}

void onSelectSoundfont(int nAction)
{
    string sFilename = g_mapScreens[SCREEN_SOUNDFONT_LIST]->GetEntryText();
    string sCommand;
    switch(nAction)
    {
    case SF_ACTION_COPY:
    {
        string sSrc = "/media/usb/";
        sSrc += sFilename;
        string sDst = "sf2/";
        sDst += sFilename;
        copyFile(sSrc, sDst);
        showScreen(g_mapScreens[SCREEN_SOUNDFONT_LIST]->GetParent());
        break;
    }
    case SF_ACTION_DELETE:
        //!@todo Validate file should be deleted, e.g. is it in use?
        alert(sFilename, " **CONFIRM DELETE**", deleteFile, 5);
        break;
    case SF_ACTION_SELECT:
        if(sFilename[0] == '~')
            sFilename = "default/" + sFilename.substr(1);
        loadSoundfont(sFilename);
        g_pCurrentPreset->soundfont=sFilename;
        setDirty(g_pCurrentPreset);
        break;
    }
}

void populateSoundfontList()
{
    DIR *dir;
    struct dirent *ent;

    ListScreen* pScreen = g_mapScreens[SCREEN_SOUNDFONT_LIST];
    pScreen->ClearList();
    vector<string> vPaths;

    switch(g_nSoundfontAction)
    {
    case SF_ACTION_COPY:
        vPaths.push_back("/media/usb");
        pScreen->SetTitle("Copy soundfont");
        pScreen->SetParent(SCREEN_SOUNDFONT);
        break;
    case SF_ACTION_DELETE:
        vPaths.push_back("sf2");
        pScreen->SetTitle("Delete soundfont");
        pScreen->SetParent(SCREEN_SOUNDFONT);
        break;
    case SF_ACTION_SELECT:
        vPaths.push_back("sf2");
        vPaths.push_back("sf2/default");
        pScreen->SetTitle("Select soundfont");
        pScreen->SetParent(SCREEN_EDIT_PRESET);
        break;
    }
    for(auto it = vPaths.begin(); it != vPaths.end(); ++it)
    {
        if((dir = opendir((*it).c_str())) != NULL)
        {
            while((ent = readdir(dir)) != NULL)
            {
                string sFilename = ent->d_name;
                if(sFilename.length() < 4 || toLower(sFilename.substr(sFilename.length() - 4, 4)) != ".sf2")
                    continue; // not the sf2 file we are looking for...
                if((*it) == "sf2/default")
                    sFilename = "~" + sFilename;
                pScreen->Add(sFilename, onSelectSoundfont, g_nSoundfontAction);
            }
        }
    }
}


void save(int)
{
    saveConfig();
    showScreen(SCREEN_PERFORMANCE);
}

int onMidiEvent(void* pData, fluid_midi_event_t* pEvent)
{
    fluid_synth_handle_midi_event(pData, pEvent);
    int nChannel = fluid_midi_event_get_channel(pEvent);
    int nType = fluid_midi_event_get_type(pEvent);
    switch(nType)
    {
    case 0xC0: //PROGRAM_CHANGE
    {
        int nProgram = fluid_midi_event_get_program(pEvent);
        g_pCurrentPreset->program[nChannel].program = nProgram;
        setDirty();
        if(g_nCurrentScreen == SCREEN_PRESET_PROGRAM)
            showEditProgram();
        break;
    }
    case 0x80: //NOTE_OFF
        if(g_nNoteCount[nChannel] > 0)
            g_nNoteCount[nChannel]--;
        showMidiActivity(nChannel);
        break;
    case 0x90: //NOTE_ON
        g_nNoteCount[nChannel]++;
        showMidiActivity(nChannel);
        break;
    case 0xB0: //CONTROL_CHANGE
        if(fluid_midi_event_get_control(pEvent) == 7)
        {
            g_pCurrentPreset->program[nChannel].level = fluid_midi_event_get_value(pEvent);
            drawMixerChannel(nChannel);
            setDirty();
        }
        break;
    default:
        printf("Unhandled MIDI event type: 0x%02x\n", nType);
    }
    return 0;
}

bool loadConfig(string sFilename)
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
    Preset* pPreset = NULL;
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

            if(sGroup.substr(0,6) == "preset")
            {
                pPreset = new Preset;
                g_vPresets.push_back(pPreset);
            }
            continue;
        }
        size_t nDelim = sLine.find_first_of("=");
        if(nDelim == string::npos)
            continue; //Not a valid value definition
        string sParam = sLine.substr(0, nDelim);
        string sValue = sLine.substr(nDelim + 1);

        if(sGroup.substr(0,6) == "preset" && pPreset)
        {
            if(sParam == "selected" && sValue != "0")
                g_pCurrentPreset = pPreset;
            if(sParam == "name")
            {
                sValue.resize(MAX_NAME_LEN, ' ');
                pPreset->name = sValue;
            }
            else if(sParam == "soundfont")
                pPreset->soundfont = sValue;
            else if(sParam.substr(0,5) == "prog_")
            {
                int nChan = validateInt(sParam.substr(5), 0, 15);
                nDelim = sValue.find_first_of(':');
                if(nDelim == string::npos)
                    continue; // Not a valid bank:program value pair
                pPreset->program[nChan].bank = validateInt(sValue.substr(0,nDelim), 0, 16383);
                pPreset->program[nChan].program = validateInt(sValue.substr(nDelim + 1), 0, 127);
            }
            else if(sParam.substr(0,6) == "level_")
            {
                int nChan = validateInt(sParam.substr(6), 0, 15);
                pPreset->program[nChan].level = validateInt(sValue, 0, 127);
            }
            else if(sParam.substr(0,8) == "balance_")
            {
                int nChan = validateInt(sParam.substr(8), 0, 15);
                pPreset->program[nChan].balance = validateInt(sValue, 0, 127);
            }
            else if (sParam.substr(0,7) == "reverb_")
            {
                if(sParam.substr(7) == "enable")
                    pPreset->reverb.enable = (sValue == "1");
                if(sParam.substr(7) == "roomsize")
                    pPreset->reverb.roomsize = validateDouble(sValue, 0.0, 1.2);
                if(sParam.substr(7) == "damping")
                    pPreset->reverb.damping = validateDouble(sValue, 0.0, 1.0);
                if(sParam.substr(7) == "width")
                    pPreset->reverb.width = validateDouble(sValue, 0.0, 100.0);
                if(sParam.substr(7) == "level")
                    pPreset->reverb.level = validateDouble(sValue, 0.0, 1.0);
            }
            else if (sParam.substr(0,7) == "chorus_")
            {
                if(sParam.substr(7) == "enable")
                    pPreset->chorus.enable = (sValue == "1");
                if(sParam.substr(7) == "voicecount")
                    pPreset->chorus.voicecount = validateInt(sValue, 0, 99);
                if(sParam.substr(7) == "level")
                    pPreset->chorus.level = validateDouble(sValue, 0.0, 10.0);
                if(sParam.substr(7) == "speed")
                    pPreset->chorus.speed = validateDouble(sValue, 0.1, 5.0);
                if(sParam.substr(7) == "depth")
                    pPreset->chorus.depth = validateDouble(sValue, 0.0, 21.0);
                if(sParam.substr(7) == "type")
                    pPreset->chorus.type = validateInt(sValue, FLUID_CHORUS_MOD_SINE, FLUID_CHORUS_MOD_TRIANGLE);
            }
        }
        if(!g_pCurrentPreset)
            g_pCurrentPreset = pPreset;
    }
    fileConfig.close();
    return true;
}

bool selectPreset(Preset* pPreset)
{
    int nPreset = getPresetIndex(pPreset);
    if(nPreset == -1)
        return false;
    cout << "Select preset " << nPreset << endl;
    bool bSoundfontChanged = (g_pCurrentPreset && pPreset->soundfont != g_pCurrentPreset->soundfont);
    g_pCurrentPreset = pPreset;
    g_mapScreens[SCREEN_PERFORMANCE]->SetSelection(getPresetIndex(g_pCurrentPreset));
    fluid_synth_set_reverb(g_pSynth, pPreset->reverb.roomsize, pPreset->reverb.damping, pPreset->reverb.width, pPreset->reverb.level);
    enableEffect(REVERB_ENABLE, g_pCurrentPreset->reverb.enable);
    fluid_synth_set_chorus(g_pSynth, pPreset->chorus.voicecount,  pPreset->chorus.level,  pPreset->chorus.speed,  pPreset->chorus.depth,  pPreset->chorus.type);
    enableEffect(CHORUS_ENABLE, g_pCurrentPreset->chorus.enable);
    for(unsigned int nParam = REVERB_ENABLE; nParam <= CHORUS_TYPE; ++nParam)
        adjustEffect(nParam);
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

Preset* createPreset()
{
    //!@todo Insert new preset at current position
    Preset* pPreset = new Preset;
    g_vPresets.push_back(pPreset);
    setDirty(pPreset);
    refreshPresetList();
    return pPreset;
}

bool copyPreset(Preset* pSrc, Preset* pDst)
{
    if(getPresetIndex(pSrc) < 0)
        return false;
    if(getPresetIndex(pDst) < 0)
        pDst = createPreset();
    for(unsigned int nChannel = 0; nChannel < 16; ++nChannel)
    {
        pDst->program[nChannel].program = pSrc->program[nChannel].program;
        pDst->program[nChannel].bank = pSrc->program[nChannel].bank;
        pDst->program[nChannel].level = pSrc->program[nChannel].level;
        pDst->program[nChannel].balance = pSrc->program[nChannel].balance;
    }

    pDst->name = pSrc->name;
    pDst->soundfont = pSrc->soundfont;
    pDst->reverb.enable = pSrc->reverb.enable;
    pDst->reverb.roomsize = pSrc->reverb.roomsize;
    pDst->reverb.damping = pSrc->reverb.damping;
    pDst->reverb.width = pSrc->reverb.width;
    pDst->reverb.level = pSrc->reverb.level;
    pDst->chorus.enable = pSrc->chorus.enable;
    pDst->chorus.voicecount = pSrc->chorus.voicecount;
    pDst->chorus.level = pSrc->chorus.level;
    pDst->chorus.speed = pSrc->chorus.speed;
    pDst->chorus.depth = pSrc->chorus.depth;
    pDst->chorus.type = pSrc->chorus.type;
    setDirty(pDst);
    return true;
}

void newPreset(unsigned int)
{
    selectPreset(createPreset());
    showScreen(SCREEN_PERFORMANCE);
}

void deletePreset()
{
    auto it = find(g_vPresets.begin(), g_vPresets.end(), g_pCurrentPreset);
    if(it == g_vPresets.end())
        return;
    delete g_pCurrentPreset;
    g_vPresets.erase(it);
    if(it == g_vPresets.end())
        g_pCurrentPreset = g_vPresets[0];
    else
        g_pCurrentPreset = *it;
    refreshPresetList();
    selectPreset(g_pCurrentPreset);
    g_bDirty = true;
    showScreen(SCREEN_PERFORMANCE);
}

void requestDeletePreset(unsigned int)
{
    if(g_vPresets.size() == 1)
    {
        showScreen(SCREEN_PERFORMANCE);
        return; // Must have at least one preset
    }

    auto it = find(g_vPresets.begin(), g_vPresets.end(), g_pCurrentPreset);
    if(it == g_vPresets.end())
        return;
    alert((*it)->name, "  **DELETE PRESET**", deletePreset);
}

void onButton(unsigned int nButton)
{
    switch(g_nCurrentScreen)
    {
    case SCREEN_LOGO:
    case SCREEN_BLANK:
        showScreen(SCREEN_PERFORMANCE);
        break;
    case SCREEN_ALERT:
        if(nButton == BUTTON_LEFT)
            showScreen(g_mapScreens[g_nCurrentScreen]->GetParent());
        else if(nButton == BUTTON_RIGHT && g_pAlertCallback)
        {
            g_pAlertCallback();
            g_pAlertCallback = NULL;
            showScreen(g_mapScreens[g_nCurrentScreen]->GetParent());
        }
        break;
    case SCREEN_MIXER:
        switch(nButton)
        {
        case BUTTON_LEFT:
            if(g_nCurrentChannel)
                drawMixerChannel(--g_nCurrentChannel);
            else
                showScreen(g_mapScreens[g_nCurrentScreen]->GetParent());
            break;
        case BUTTON_RIGHT:
            if(g_nCurrentChannel < 15)
                drawMixerChannel(++g_nCurrentChannel);
            else
                g_nCurrentChannel = 15;
            break;
        case BUTTON_UP:
            {
                int nLevel;
                if(FLUID_FAILED == fluid_synth_get_cc(g_pSynth, g_nCurrentChannel, 7, &nLevel))
                    return;
                if(nLevel >= 127)
                    return;
                fluid_synth_cc(g_pSynth, g_nCurrentChannel, 7, ++nLevel);
                drawMixerChannel(g_nCurrentChannel);
                g_pCurrentPreset->program[g_nCurrentChannel].level = nLevel;
                setDirty();
                break;
            }
        case BUTTON_DOWN:
            {
                int nLevel;
                if(FLUID_FAILED == fluid_synth_get_cc(g_pSynth, g_nCurrentChannel, 7, &nLevel))
                    return;
                if(nLevel < 1)
                    return;
                fluid_synth_cc(g_pSynth, g_nCurrentChannel, 7, --nLevel);
                drawMixerChannel(g_nCurrentChannel);
                g_pCurrentPreset->program[g_nCurrentChannel].level = nLevel;
                setDirty();
                break;
            }
        }
        break;
    case SCREEN_PRESET_NAME:
        switch(nButton)
        {
        case BUTTON_UP:
            {
                char c = g_pCurrentPreset->name[g_nCurrentChar];
                if(++c < 32 || c > 126)
                    c = 32;
                g_pCurrentPreset->name[g_nCurrentChar] = c;
                setDirty(g_pCurrentPreset, true);
                drawPresetName();
                break;
            }
        case BUTTON_DOWN:
            {
                char c = g_pCurrentPreset->name[g_nCurrentChar];
                if(--c < 32 || c > 126)
                    c = 126;
                g_pCurrentPreset->name[g_nCurrentChar] = c;
                setDirty(g_pCurrentPreset, true);
                drawPresetName();
                break;
            }
        case BUTTON_RIGHT:
            if(g_nCurrentChar >= MAX_NAME_LEN - 1)
                return;
            ++g_nCurrentChar;
            drawPresetName();
            break;
        case BUTTON_LEFT:
            if(g_nCurrentChar == 0)
            {
                showScreen(g_mapScreens[g_nCurrentScreen]->GetParent());
                return;
            }
            --g_nCurrentChar;
            drawPresetName();
        }
        break;
    case SCREEN_EDIT_VALUE:
        switch(nButton)
        {
        case BUTTON_DOWN:
            drawEffectValue(g_nCurrentEffect, adjustEffect(g_nCurrentEffect, -1));
            break;
        case BUTTON_UP:
            drawEffectValue(g_nCurrentEffect, adjustEffect(g_nCurrentEffect, 1));
            break;
        case BUTTON_LEFT:
        case BUTTON_RIGHT:
            showScreen(g_mapScreens[g_nCurrentScreen]->GetParent());
            break;
        }
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
            showScreen(g_mapScreens[g_nCurrentScreen]->GetParent());
            break;
        }
    }
    int nSelection = g_mapScreens[SCREEN_PERFORMANCE]->GetSelection();
    if(g_nCurrentScreen == SCREEN_PERFORMANCE && (nButton == BUTTON_UP || nButton == BUTTON_DOWN)
        && nSelection >= 0
        && nSelection < g_vPresets.size())
        selectPreset(g_vPresets[nSelection]);
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

void onSignal(int nSignal)
{
    switch(nSignal)
    {
    case SIGALRM:
        // We use alarm to drop back to performance screen after idle delay
        if(g_nCurrentScreen == SCREEN_LOGO)
            showScreen(SCREEN_PERFORMANCE);
        else if(g_nCurrentScreen == SCREEN_ALERT)
        {
            g_pAlertCallback = NULL;
            showScreen(g_mapScreens[SCREEN_ALERT]->GetParent());
        }
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
    showScreen(SCREEN_LOGO);
    g_pScreen->SetFont(DEFAULT_FONT_SIZE, "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf");
    configParams();

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
    buttonHandler.AddButton(BUTTON_UP, onButton);
    buttonHandler.AddButton(BUTTON_DOWN, onButton);
    buttonHandler.AddButton(BUTTON_LEFT, NULL, onButton, onLeftHold);
    buttonHandler.AddButton(BUTTON_RIGHT, NULL, onButton, onRightHold);
    buttonHandler.SetRepeatPeriod(BUTTON_UP, 100);
    buttonHandler.SetRepeatPeriod(BUTTON_DOWN, 100);
    cout << "Configured buttons" << endl;

    // Configure signal handlers
    signal(SIGALRM, onSignal);
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    cout << "Configured signal handler" << endl;

    g_mapScreens[SCREEN_PERFORMANCE] = new ListScreen(g_pScreen, "   riban Fluidbox", SCREEN_NONE);
    g_mapScreens[SCREEN_EDIT_PRESET] = new ListScreen(g_pScreen, "Edit Preset", SCREEN_EDIT);
    g_mapScreens[SCREEN_EDIT] = new ListScreen(g_pScreen, "Edit", SCREEN_PERFORMANCE);
    g_mapScreens[SCREEN_POWER] = new ListScreen(g_pScreen, "Power", SCREEN_PERFORMANCE);
    g_mapScreens[SCREEN_PRESET_NAME] = new ListScreen(g_pScreen, "Preset Name", SCREEN_EDIT_PRESET);
    g_mapScreens[SCREEN_PRESET_SF] = new ListScreen(g_pScreen, "Preset Soundfont", SCREEN_EDIT_PRESET);
    g_mapScreens[SCREEN_PRESET_PROGRAM] = new ListScreen(g_pScreen,  "Preset Program", SCREEN_EDIT_PRESET);
    g_mapScreens[SCREEN_EFFECTS] = new ListScreen(g_pScreen, "Effects", SCREEN_EDIT);
    g_mapScreens[SCREEN_MIXER] = new ListScreen(g_pScreen,  "Mixer", SCREEN_EDIT);
    g_mapScreens[SCREEN_SOUNDFONT] = new ListScreen(g_pScreen, "Manage soundfonts", SCREEN_EDIT);
    g_mapScreens[SCREEN_SOUNDFONT_LIST] = new ListScreen(g_pScreen, "Available soundfonts", SCREEN_SOUNDFONT);
    g_mapScreens[SCREEN_EDIT_VALUE] = new ListScreen(g_pScreen, "Effect parameter", SCREEN_EFFECTS);
    g_mapScreens[SCREEN_ALERT] = new ListScreen(g_pScreen, "     ALERT", SCREEN_PERFORMANCE);

    g_mapScreens[SCREEN_EDIT]->Add("Mixer", showScreen, SCREEN_MIXER);
    g_mapScreens[SCREEN_EDIT]->Add("Effects", showScreen, SCREEN_EFFECTS);
    g_mapScreens[SCREEN_EDIT]->Add("Edit preset", showScreen, SCREEN_EDIT_PRESET);
    g_mapScreens[SCREEN_EDIT]->Add("New preset", newPreset);
    g_mapScreens[SCREEN_EDIT]->Add("Delete preset", requestDeletePreset);
    g_mapScreens[SCREEN_EDIT]->Add("Manage soundfonts", showScreen, SCREEN_SOUNDFONT);
    g_mapScreens[SCREEN_EDIT]->Add("Save", save);
    g_mapScreens[SCREEN_EDIT]->Add("Power", showScreen, SCREEN_POWER);

    g_mapScreens[SCREEN_EDIT_PRESET]->Add("Name", showScreen, SCREEN_PRESET_NAME);
    g_mapScreens[SCREEN_EDIT_PRESET]->Add("Soundfont", listSoundfont, SF_ACTION_SELECT);
    g_mapScreens[SCREEN_EDIT_PRESET]->Add("Program", showEditProgram);

    g_mapScreens[SCREEN_POWER]->Add("Save and power off", power, POWER_OFF_SAVE);
    g_mapScreens[SCREEN_POWER]->Add("Save and reboot", power, POWER_REBOOT_SAVE);
    g_mapScreens[SCREEN_POWER]->Add("Power off", power, POWER_OFF);
    g_mapScreens[SCREEN_POWER]->Add("Reboot",  power, POWER_REBOOT);

    g_mapScreens[SCREEN_EFFECTS]->Add("Reverb enable", editEffect, REVERB_ENABLE);
    g_mapScreens[SCREEN_EFFECTS]->Add("Reverb room size", editEffect, REVERB_ROOMSIZE);
    g_mapScreens[SCREEN_EFFECTS]->Add("Reverb damping", editEffect, REVERB_DAMPING);
    g_mapScreens[SCREEN_EFFECTS]->Add("Reverb width", editEffect, REVERB_WIDTH);
    g_mapScreens[SCREEN_EFFECTS]->Add("Reverb level", editEffect, REVERB_LEVEL);

    g_mapScreens[SCREEN_EFFECTS]->Add("Chorus enable", editEffect, CHORUS_ENABLE);
    g_mapScreens[SCREEN_EFFECTS]->Add("Chorus voices", editEffect, CHORUS_VOICES);
    g_mapScreens[SCREEN_EFFECTS]->Add("Chorus level", editEffect, CHORUS_LEVEL);
    g_mapScreens[SCREEN_EFFECTS]->Add("Chorus speed", editEffect, CHORUS_SPEED);
    g_mapScreens[SCREEN_EFFECTS]->Add("Chorus depth", editEffect, CHORUS_DEPTH);
    g_mapScreens[SCREEN_EFFECTS]->Add("Chorus type", editEffect, CHORUS_TYPE);

    g_mapScreens[SCREEN_SOUNDFONT]->Add("Copy from USB", listSoundfont, SF_ACTION_COPY);
    g_mapScreens[SCREEN_SOUNDFONT]->Add("Delete", listSoundfont, SF_ACTION_DELETE);
    cout << "Configured screens" << endl;

    // Select preset
    if(g_vPresets.size() == 0)
        g_pCurrentPreset = createPreset();
    refreshPresetList();
    selectPreset(g_pCurrentPreset);

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


#include "buttonhandler.hpp"
#include "screen.hpp"
#include "ribanfblib/ribanfblib.h"
#include <wiringPi.h>
#include <iostream> //provides streams
#include <sys/stat.h> // provides stat for file detection
#include <unistd.h> //provides usleep
#include <signal.h> //provides signal handler
#include <fcntl.h>   // open
#include <unistd.h>  // read, write, close
#include <cstdio>    // BUFSIZ

#define BUTTON_UP      4
#define BUTTON_DOWN   17
#define BUTTON_LEFT   27
#define BUTTON_RIGHT   3

using namespace std;

enum {
    UPDATE_FLUIDBOX,
    UPDATE_SOUNDFONT,
    UPDATE_BOTH
};

bool g_bRun = true;
unsigned int g_nCountdown = 159;
ListScreen* g_pDisplay;
ribanfblib* g_pScreen;

/**  Handles signal */
void onSignal(int nSignal)
{
    switch(nSignal)
    {
        case SIGINT:
        case SIGTERM:
            printf("Received signal to quit...\n");
                g_bRun = false;
            break;
    }
}

void copyFile(std::string sSource, std::string sDest)
{
    int nSrc = open(sSource.c_str(), O_RDONLY, 0);
    if(nSrc == -1)
        return;
    int nDst = open(sDest.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
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
    while((nSize = read(nSrc, pBuffer, fileStat.st_blksize)) > 0)
    {
        write(nDst, pBuffer, nSize);
        // Update progresss bar
        fX += fDx;
        if(int(fX) > nX)
        {
            nX = fX;
            g_pScreen->DrawRect(0,100, nX,127, WHITE, 1, GREEN);
        }
    }
    free(pBuffer);
    close(nDst);
    close(nSrc);
}

void update(int nMode)
{
    g_pScreen->Clear(DARK_RED);
    g_pScreen->DrawText("Updating...", 40, 55);
    g_pScreen->DrawText("Do not turn off", 20, 85);
    if(nMode == UPDATE_FLUIDBOX || nMode == UPDATE_BOTH)
        copyFile("/media/usb/fluidbox", "./fluidbox");
    if(nMode == UPDATE_SOUNDFONT  || nMode == UPDATE_BOTH)
        copyFile("/media/usb/fluidbox.sf2", "./sf2/fluidbox.sf2");
    g_pScreen->Clear();
    g_pScreen->DrawText("Update complete", 10, 72);
    usleep(3000000);
}

void onButton(unsigned int nButton)
{
    g_nCountdown = 0;
    switch(nButton)
    {
        case BUTTON_UP:
            cout << "Up" << endl;
            g_pDisplay->Previous();
            break;
        case BUTTON_DOWN:
            cout << "Down" << endl;
            g_pDisplay->Next();
            break;
        case BUTTON_LEFT:
            cout << "Left" << endl;
            g_bRun = false;
            break;
        case BUTTON_RIGHT:
            cout << "Right" << endl;
            g_bRun = !g_pDisplay->Select();
            break;
    }
}

/** Main application */
int main(int argc, char** argv)
{
    cout << "fluidbox manager" << endl;

    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);

    // Display flash
    ribanfblib screen("/dev/fb1");
    g_pScreen = & screen;
    screen.LoadBitmap("logo.bmp", "logo");
    screen.DrawBitmap("logo", 0, 0);

    // Turn backlight on
    system("gpio mode 26 pwm");
    system("gpio pwm 26 900");

    // Look for update files
    struct stat fileStat;
    bool bFluidbox = (stat ("/media/usb0/fluidbox", &fileStat) == 0 && S_ISREG(fileStat.st_mode));
    bool bSoundfont = (stat ("/media/usb0/fluidbox.sf2", &fileStat) == 0 && S_ISREG(fileStat.st_mode));
    if(bFluidbox || bSoundfont)
    {
        ListScreen display(&screen, "Update available", 0);
        g_pDisplay = &display;
        display.Add("Update fluidbox", update, UPDATE_FLUIDBOX);
        if(!bFluidbox)
            display.Enable(0, false);
        display.Add("Update soundfont", update, UPDATE_SOUNDFONT);
        if(!bSoundfont)
            display.Enable(1, false);
        display.Add("Update both", update, UPDATE_BOTH);
        if(!bFluidbox || !bSoundfont)
            display.Enable(2, false);
        display.Draw();

        // Configure buttons
        wiringPiSetupGpio();
        ButtonHandler buttonHandler;
        buttonHandler.AddButton(BUTTON_UP, NULL, onButton);
        buttonHandler.AddButton(BUTTON_DOWN, NULL, onButton);
        buttonHandler.AddButton(BUTTON_LEFT, NULL, onButton);
        buttonHandler.AddButton(BUTTON_RIGHT, NULL, onButton);
    
        screen.DrawRect(0,100, 159,127, WHITE, 1, RED);
        while(g_bRun)
        {
            if(g_nCountdown)
            {
                if(--g_nCountdown)
                    screen.DrawRect(g_nCountdown, 100, 159, 127, WHITE, 1, BLACK);
                else
                    g_bRun = false;
            }
            for(int i = 0; i < 200; ++i)
            {
                buttonHandler.Process();
                usleep(10);
            }
        }
    }

    cout << "Launching fluidbox..." << endl;
    system("./fluidbox");

    cout << "All done!" << endl;
    return 0;
}

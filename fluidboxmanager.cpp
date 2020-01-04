#include "buttonhandler.hpp"
#include "screen.hpp"
#include "ribanfblib/ribanfblib.h"
#include <wiringPi.h>
#include <iostream> //provides streams
#include <sys/stat.h> // provides stat for file detection
#include <unistd.h> //provides usleep
#include <signal.h> //provides signal handler

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

void update(int nMode)
{
    g_pScreen->Clear(DARK_RED);
    g_pScreen->DrawText("Updating...", 40, 55);
    g_pScreen->DrawText("Do not turn off", 20, 100);
    switch(nMode)
    {
        case UPDATE_FLUIDBOX:
            system("cp /media/usb/fluidbox .");
            break;
        case UPDATE_SOUNDFONT:
            system("cp /media/usb/fluidbox.sf2 ./sf2/");
            break;
        case UPDATE_BOTH:
            system("cp /media/usb/fluidbox .");
            system("cp /media/usb/fluidbox.sf2 ./sf2/");
            break;
    }
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
            update(g_pDisplay->GetSelection());
            g_bRun = false;
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
    if(bFluidbox)
        cout << "Found fluidbox file on USB" << endl;
    bool bSoundfont = (stat ("/media/usb0/fluidbox.sf2", &fileStat) == 0 && S_ISREG(fileStat.st_mode));
    if(bSoundfont)
        cout << "Found fluidbox.sf2 file on USB" << endl;
    if(bFluidbox || bSoundfont)
    {
        ListScreen display(&screen, "Update available", 0);
        g_pDisplay = &display;
        if(bFluidbox)
            display.Add("Update fluidbox", update, UPDATE_FLUIDBOX);
        if(bSoundfont)
            display.Add("Update soundfont", update, UPDATE_SOUNDFONT);
        if(bFluidbox && bSoundfont)
            display.Add("Update both", update, UPDATE_BOTH);
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

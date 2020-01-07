/*	Raspberry Pi Button Handler - riban 2020 <brian@riban.co.uk>

	Provides handers for button press, release, hold and auto-repeat with associated debounce filters.
        Depends on wiringPi.
*/

#include <map> // provides std::map
#include <functional>   // provides std::function
#include <wiringPi.h> // provides wiringPi gpio functions

#define ALL_BUTTONS -1

enum
{
    STATE_RELEASED,
    STATE_PRESSED,
    STATE_HELD,
    STATE_REPEAT
};

/**	Button class implements core functionality for each button - do not use this class, use ButtonHandler class */
class Button
{
public:
    Button(unsigned int gpio, std::function<void(int)> onPress, std::function<void(int)> onRelease = 0, std::function<void(int)> onHold = 0, unsigned int holdDelay = 1000) :
        m_nGpio(gpio),
        m_fnOnPress(onPress),
        m_fnOnRelease(onRelease),
        m_fnOnHold(onHold),
        m_nHoldDelay(holdDelay),
        m_nState(STATE_RELEASED)
    {
        pinMode(m_nGpio, INPUT);
        pullUpDnControl(m_nGpio, PUD_UP);
        m_nLastPress = millis();
    }

    void Process()
    {
        m_cDebounce <<=1;
        m_cDebounce |= digitalRead(m_nGpio);
        switch(m_nState)
        {
        case STATE_RELEASED:
            if(m_cDebounce == 0x80)
            {
                m_nState = STATE_PRESSED;
                m_nLastPress = millis();
                if(m_fnOnPress) m_fnOnPress(m_nGpio);
            }
            break;
        case STATE_PRESSED:
            if(m_cDebounce == 0x7F)
            {
                m_nState = STATE_RELEASED;
                if(m_fnOnRelease) m_fnOnRelease(m_nGpio);
            }
            else if(millis() - m_nLastPress > m_nHoldDelay)
            {
                m_nLastPress = millis();
                if(m_fnOnHold)
                {
                    m_nState = STATE_HELD;
                    m_fnOnHold(m_nGpio);
                }
                else
                {
                    m_nState = STATE_REPEAT;
                }
            }
            break;
        case STATE_HELD:
            if(m_cDebounce == 0x7F)
            {
                m_nState = STATE_RELEASED;
            }
            break;
        case STATE_REPEAT:
            if(m_cDebounce == 0x7F)
            {
                m_nState = STATE_RELEASED;
                if(m_fnOnRelease) m_fnOnRelease(m_nGpio);
            }
            else if(m_nRepeatPeriod && (millis() - m_nLastPress > m_nRepeatPeriod))
            {
                m_nLastPress = millis();
                if(m_fnOnPress) m_fnOnPress(m_nGpio);
            }
            break;
        }
    }

    /**	Set the delay before hold event or auto repeat triggers
    *	@param	delay Delay in ms
    */
    void SetHoldDelay(unsigned int delay)
    {
        m_nHoldDelay = delay;
    }

    void SetRepeatPeriod(unsigned int period)
    {
        m_nRepeatPeriod = period;
    }


private:
    unsigned int m_nGpio;
    std::function<void(int)> m_fnOnPress = 0; // Function called when button pressed
    std::function<void(int)> m_fnOnRelease = 0; // Function called when button released
    std::function<void(int)> m_fnOnHold = 0; // Function called when button held
    unsigned int m_nHoldDelay = 1000; // Duration in ms for onHold trigger
    unsigned int m_nRepeatPeriod = 0; // Period between each auto-repeat - zero to disable auto-repeat
    unsigned int m_nLastPress; // Time of last button press
    unsigned char m_cDebounce = 0x7f; // Button debounce filter state
    unsigned int m_nState = STATE_RELEASED; // Current state of button
};

/**	ButtonHandler class handles button events for multiple buttons connected via GPIO */
class ButtonHandler
{
public:
    /**	Instantiate handler
    	@note Call wiringPiSetupGpio() before creating an instance of ButtonHandler
    */
    ButtonHandler()
    {
    }

    /**	Destructor cleans up when ButtonHandler object is deleted
            ~ButtonHandler()
    {
    		for(auto it = m_mapButtons.begin(); it!= m_mapButtons.end(); ++it)
    			delete it->second;
    }

    /**	Call Process() rapidly (approx. every 10ms) */
    void Process()
    {
        //!@todo Create thread to run process loop
        for(auto it = m_mapButtons.begin(); it!= m_mapButtons.end(); ++it)
        {
            it->second->Process();
        }
    }

    /**	Add a button to the handler
    *	@param gpio GPIO pin number
    *	@param onPress Name of function to call when button is pressed (also triggered by auto repeat)
    *	@param onRelease Name of function to call when button is released
    *	@param onHold Name of function to call when button is held
    */
    void AddButton(unsigned int gpio, std::function<void(int)> onPress, std::function<void(int)> onRelease = 0, std::function<void(int)> onHold = 0)
    {
        auto it = m_mapButtons.find(gpio);
        if(it != m_mapButtons.end())
            delete it->second;
        m_mapButtons[gpio] = new Button(gpio, onPress, onRelease, onHold);
    }

    /**	Set period before button hold or auto-repeat is triggered
    *	@param period Period in ms
    *	@param gpio Index of gpio to configure [Default: All gpios are set]
    *	@note  If auto-repeat is enabled and triggers before hold then hold will not trigger
    */
    void SetHoldDelay(unsigned int delay, unsigned int gpio = ALL_BUTTONS)
    {
        if(gpio == ALL_BUTTONS)
            for(auto it = m_mapButtons.begin(); it != m_mapButtons.end(); ++it)
                it->second->SetHoldDelay(delay);
        else
        {
            auto it = m_mapButtons.find(gpio);
            if(it == m_mapButtons.end())
                return;
            it->second->SetHoldDelay(delay);
        }
    }

    /**     Set period between auto-repeat triggers
    *       @param period Period in ms - set to zero to disable auto-repeat
    *       @param gpio Index of gpio to configure [Default: All gpios are set]
    */
    void SetRepeatPeriod(unsigned int gpio, unsigned int period)
    {
        if(gpio == ALL_BUTTONS)
            for(auto it = m_mapButtons.begin(); it != m_mapButtons.end(); ++it)
                it->second->SetRepeatPeriod(period);
        else
        {
            auto it = m_mapButtons.find(gpio);
            if(it == m_mapButtons.end())
                return;
            it->second->SetRepeatPeriod(period);
        }
    }

private:
    std::map<unsigned int, Button*> m_mapButtons;
};


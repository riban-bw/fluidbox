/*	Classes implementing screen views
*/

#include "ribanfblib/ribanfblib.h"

#define TITLE_BG DARK_RED
#define TITLE_FG WHITE
#define SELECT_BG BLUE
#define ENTRY_FG WHITE
#define DISABLED_FG GREY


using namespace std;


// An entry in a list screen
struct ListEntry
{
    string title; // Title to display in list
    std::function<void(int)> function = NULL;  // Function to call on selection - default is none
    int param;
    bool enabled = true;
};


class ListScreen
{
public:
    /**	Instantiate a Screen object
    *	@param	pScreen Pointer to a riban frame buffer object
    *	@param	sTitle	Title to display at top of screen
    *	@param	nParent Index of parent screen
    */
    ListScreen(ribanfblib* pScreen, string sTitle, unsigned int nParent) :
        m_pScreen(pScreen),
        m_sTitle(sTitle),
        m_nParent(nParent)
    {
    }

    ~ListScreen()
    {
        for(auto it = m_vEntries.begin(); it != m_vEntries.end(); ++it)
            delete *it;
    }

    void SetPreviousScreen(unsigned int nScreen)
    {
        m_nPreviousScreen = nScreen;
    }

    unsigned int GetPreviousScreen()
    {
        return m_nPreviousScreen;
    }

    unsigned int GetParent()
    {
        return m_nParent;
    }

    int GetSelection()
    {
        return m_nSelection;
    }

    void SetSelection(int nSelection)
    {
        if(nSelection < m_vEntries.size())
            m_nSelection = nSelection;
    }

    int GetFirstShown()
    {
        return m_nFirstEntry;
    }

    /**	Display the screen */
    virtual void Draw()
    {
        m_pScreen->Clear();
        m_pScreen->DrawRect(0,0, 160,16, TITLE_BG, 0, TITLE_BG);
        m_pScreen->DrawText(m_sTitle, 5, 14, TITLE_FG);
        if(!m_vEntries.size())
            return;

        int nY = (1 + m_nSelection - m_nFirstEntry) * 16;
        // Draw highlight
        if(m_nSelection > -1)
        {
            m_pScreen->DrawRect(2,nY, 160,nY+15, SELECT_BG, 0, SELECT_BG);
        }
        // Draw entries
        for(unsigned int nRow = 0; nRow < 7; ++nRow)
        {
            if(nRow + m_nFirstEntry > m_vEntries.size() - 1)
                return; // Reached end of list
            if(m_vEntries[nRow + m_nFirstEntry]->enabled)
                m_pScreen->DrawText(m_vEntries[nRow + m_nFirstEntry]->title, 2, 16*(nRow+2) - 2, ENTRY_FG);
            else
                m_pScreen->DrawText(m_vEntries[nRow + m_nFirstEntry]->title, 2, 16*(nRow+2) - 2, DISABLED_FG);
        }
    }

    int Add(string title, std::function<void(int)> function = NULL, int param=0)
    {
        ListEntry* pEntry = new ListEntry;
        pEntry->title = title;
        pEntry->function = function;
        pEntry->param = param;
        m_vEntries.push_back(pEntry);
        if(m_nSelection < 1)
            m_nSelection = 0;
        return m_vEntries.size() - 1;
    };

    void Remove(unsigned int nIndex)
    {
        if(nIndex >= m_vEntries.size())
            return;
        auto it = m_vEntries.begin();
        for(unsigned i = 0; i < nIndex; ++i)
            ++it;
        if(it == m_vEntries.end())
            return;
        m_vEntries.erase(it);
        if(m_nSelection >= nIndex)
            --m_nSelection;
    }

    void ClearList()
    {
        for(auto it = m_vEntries.begin(); it != m_vEntries.end(); ++it)
            delete *it;
        m_vEntries.clear();
    }

    /**  Trigger the function of the currently selected entry
    *    @retval bool True if function triggered
    */
    bool Select()
    {
        if(m_nSelection >= 0 && m_nSelection < m_vEntries.size() && m_vEntries[m_nSelection]->enabled && m_vEntries[m_nSelection]->function)
        {
            m_vEntries[m_nSelection]->function(m_vEntries[m_nSelection]->param);
            return true;
        }
        return false;
    };

    void Next()
    {
        for(int nIndex = m_nSelection + 1; nIndex < m_vEntries.size(); ++nIndex)
        {
            if(m_vEntries[nIndex]->enabled)
            {
                m_nSelection = nIndex;
                break;
            }
        }
        if(m_nSelection > m_nFirstEntry + 6)
            m_nFirstEntry = m_nSelection - 6;
        Draw();
    };

    void Previous()
    {
        for(int nIndex = m_nSelection -1; nIndex >= 0; --nIndex)
        {
            if(m_vEntries[nIndex]->enabled)
            {
                m_nSelection = nIndex;
                break;
            }
        }
        if(m_nSelection < m_nFirstEntry)
            m_nFirstEntry = m_nSelection;
        Draw();
    };

    void Enable(unsigned int nEntry, bool bEnable = true)
    {
        auto it = m_vEntries.begin();
        unsigned int nIndex = 0;
        for(nIndex = 0; nIndex < nEntry; ++nIndex)
            ++it;
        if(it == m_vEntries.end())
            return;
        (*it)->enabled = bEnable;
        if(m_nSelection == nIndex)
            --m_nSelection;
    }


protected:
    ribanfblib* m_pScreen;
    string m_sTitle;

    unsigned int m_nPreviousScreen = 0;
    unsigned int m_nParent = 0;
    int m_nSelection = -1; // Index of selected item
    std::vector<ListEntry*> m_vEntries; // List of entries
    unsigned int m_nFirstEntry = 0; // Index of first item to display
};


class ListEditScreen : public ListScreen
{
public:
    ListEditScreen(ribanfblib* pScreen, string sTitle, unsigned int nParent) :
        ListScreen(pScreen, sTitle, nParent)
    {
    }

    void Draw()
    {
        ListScreen::Draw();
        if(!m_vEntries.size())
            return;
        for(unsigned int nRow = 0; nRow < 7; ++nRow)
        {
            if(nRow + m_nFirstEntry > m_vEntries.size() - 1)
                return; // Reached end of list
            m_pScreen->DrawRect(140, 16*(nRow+2), 159, 16*(nRow + 1), WHITE);
        }
    }
};

/*	Classes implementing screen views
*/

#include "ribanfblib/ribanfblib.h"

using namespace std;


// An entry in a list screen
struct ListEntry
{
    string title; // Title to display in list
    std::function<void(int)> function = NULL;  // Function to call on selection - default is none
    int param;
    bool enabled = true;
};

struct Style
{
    uint32_t canvas = BLACK;
    uint32_t title_background = 0x410447;
    uint32_t title_text = WHITE;
    uint32_t select_background = DARK_BLUE;
    uint32_t entry_text = WHITE;
    uint32_t disabled_text = GREY;
};


class ListScreen
{
public:
    /**	Instantiate a Screen object
    *	@param	pScreen Pointer to a riban frame buffer object
    *	@param	sTitle	Title to display at top of screen
    *	@param	nParent Index of parent screen
    */
    ListScreen(ribanfblib* pScreen, string sTitle, unsigned int nParent, Style* pStyle) :
        m_pScreen(pScreen),
        m_sTitle(sTitle),
        m_nParent(nParent),
        m_pStyle(pStyle)
    {
    }

    ~ListScreen()
    {
        for(auto it = m_vEntries.begin(); it != m_vEntries.end(); ++it)
            delete *it;
    }

    /** Set id of the parent screen
    *   @param nScreen ID of parent screen
    */
    void SetParent(unsigned int nScreen)
    {
        m_nParent = nScreen;
    }

    /** Get id of parent screen
    *   @retval unsigned int ID of parent screen
    */
    unsigned int GetParent()
    {
        return m_nParent;
    }

    /** Get id of the currently selected list entry
    *   @retval int ID of currently selected entry (-1 for none)
    */
    int GetSelection()
    {
        return m_nSelection;
    }

    /** Set id of currently selected list entry
    *   @param nSelection ID of entry to select
    */
    void SetSelection(int nSelection)
    {
        if(nSelection < m_vEntries.size())
            m_nSelection = nSelection;
        else
            m_nSelection = m_vEntries.size() - 1;
    }

    /** Get id of the list entry shown at top of screen (first displayed entry of possibly scrolled list)
    *   @retval int ID of first displayed item (-1 if none displayed)
    */
    int GetFirstShown()
    {
        return m_nFirstEntry;
    }

    /**	Display the screen */
    virtual void Draw()
    {
        m_pScreen->Clear(m_pStyle->canvas);
        m_pScreen->DrawRect(0,0, 160,16, m_pStyle->title_background, 0, m_pStyle->title_background);
        m_pScreen->DrawText(m_sTitle, 5, 13, m_pStyle->title_text);
        if(!m_vEntries.size())
            return;
        if(m_nSelection >= m_vEntries.size())
            m_nSelection = m_vEntries.size() - 1;

        if(m_nSelection < m_nFirstEntry)
            m_nFirstEntry = m_nSelection;
        if(m_nSelection > m_nFirstEntry + 6)
            m_nFirstEntry = m_nSelection - 6;
        int nY = (1 + m_nSelection - m_nFirstEntry) * 16;
        // Draw highlight
        if(m_nSelection > -1)
        {
            m_pScreen->DrawRect(2,nY, 160,nY+15, m_pStyle->select_background, 0, m_pStyle->select_background);
        }
        // Draw entries
        for(unsigned int nRow = 0; nRow < 7; ++nRow)
        {
            if(nRow + m_nFirstEntry > m_vEntries.size() - 1)
                return; // Reached end of list
            if(m_vEntries[nRow + m_nFirstEntry]->enabled)
                m_pScreen->DrawText(m_vEntries[nRow + m_nFirstEntry]->title, 2, 16*(nRow+2) - 2, m_pStyle->entry_text);
            else
                m_pScreen->DrawText(m_vEntries[nRow + m_nFirstEntry]->title, 2, 16*(nRow+2) - 2, m_pStyle->disabled_text);
        }
    }

    /** Add an entry to the list
    *   @param sTitle Title of the list entry shown in the list
    *   @param pFunction Pointer to a function to call when the entry is selected - Default: None
    *   @param nParam Value of integer parameter to pass to function - Default: 0
    *   @retval int Index of the new entry within the list
    *   @note Callback function must be in form: void function(int)
    */
    int Add(string sTitle, std::function<void(int)> pFunction = NULL, int nParam=0)
    {
        ListEntry* pEntry = new ListEntry;
        pEntry->title = sTitle;
        pEntry->function = pFunction;
        pEntry->param = nParam;
        m_vEntries.push_back(pEntry);
        if(m_nSelection < 1)
            m_nSelection = 0;
        return m_vEntries.size() - 1;
    };

    /** Remove an entry from the list
    *   @param nIndex Index of the list entry to remove
    */
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

    /**  Select the next entry (with respect to the currently selected entry)
    *    @note If last entry already selected then do nothing
    */
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
        Draw();
    };

    /** Select the previous entry (with respect to the currently selected entry)
    *   @note If first entry already selected then do nothing
    */
    void Previous()
    {
        for(int nIndex = m_nSelection - 1; nIndex >= 0; --nIndex)
        {
            if(m_vEntries[nIndex]->enabled)
            {
                m_nSelection = nIndex;
                break;
            }
        }
        Draw();
    };

    /** Enable or disable a list entry
    *   @param nEntry Index of the entry to enable / disable
    *   @param bEnable True to enable, false to disable - Default: true
    */
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

    /** Set the text of a list entry
    *   @param nEntry Index of the entry in the list
    *   @param sText Text of the entry
    */
    void SetEntryText(unsigned int nEntry, string sText)
    {
        if(nEntry >= m_vEntries.size())
            return;
        m_vEntries[nEntry]->title = sText;
    }

    /** Get the text of a list entry
    *   @param nEntry Index of the entry in the list
    *   @retval string Text of the entry
    */
    string GetEntryText(unsigned int nEntry)
    {
        if(nEntry < m_vEntries.size())
            return m_vEntries[nEntry]->title;
        return "";
    }

    /** Get the text of the currently selected list entry
    *   @retval string Text of the entry
    */
    string GetEntryText()
    {
        return GetEntryText(m_nSelection);
    }

    /** Get the title shown at the top of the screen
    *   @retval string Text of title
    */
    string GetTitle()
    {
        return m_sTitle;
    }

    /** Set the title shown at the top of the screen
    *   @param sTitle Text of title
    *   @param bRefresh Redraw title
    */
    void SetTitle(string sTitle, bool bRefresh = false)
    {
        m_sTitle = sTitle;
        if(bRefresh)
        {
            m_pScreen->DrawRect(0,0, 160,16, m_pStyle->title_background, 0, m_pStyle->title_background);
            m_pScreen->DrawText(m_sTitle, 5, 13, m_pStyle->title_text);
        }
    }


protected:
    ribanfblib* m_pScreen;
    string m_sTitle;
    Style* m_pStyle;

    unsigned int m_nPreviousScreen = 0;
    unsigned int m_nParent = 0;
    int m_nSelection = -1; // Index of selected item
    std::vector<ListEntry*> m_vEntries; // List of entries
    unsigned int m_nFirstEntry = 0; // Index of first item to display
};


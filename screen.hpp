/*	Classes implementing screen views
*/

#include "ribanfblib/ribanfblib.h"

using namespace std;


// An entry in a list screen
struct ListEntry {
    string title; // Title to display in list
    std::function<void(int)> function = NULL;  // Function to call on selection - default is none
    int param;
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
		m_pScreen->DrawRect(0,0, 160,16, BLUE, 0, BLUE);
		m_pScreen->DrawText(m_sTitle, 5, 15, WHITE);
		if(!m_vEntries.size())
			return;

		int nY = (1 + m_nSelection - m_nFirstEntry) * 16;
	        // Draw highlight
        	if(m_nSelection > -1)
        	{
         	   	m_pScreen->DrawRect(0,nY, 160,nY+15, BLUE, 0, BLUE);
        	}
        	// Draw entries
        	for(unsigned int nRow = 0; nRow < 7; ++nRow)
        	{
           	 	if(nRow + m_nFirstEntry > m_vEntries.size() - 1)
               			 return; // Reached end of list
           	 	m_pScreen->DrawText(m_vEntries[nRow + m_nFirstEntry]->title, 2, 16*(nRow+2), WHITE);
        	}
	}

    void Add(string title, std::function<void(int)> function = NULL, int param=0)
    {
        ListEntry* pEntry = new ListEntry;
        pEntry->title = title;
        pEntry->function = function;
        pEntry->param = param;
        m_vEntries.push_back(pEntry);
        if(m_nSelection < 1)
            m_nSelection = 0;
    };

    void ClearList()
    {
        for(auto it = m_vEntries.begin(); it != m_vEntries.end(); ++it)
            delete *it;
        m_vEntries.clear();
    }

    void Select()
    {
        if(m_nSelection < m_vEntries.size())
        {
            if(!m_vEntries[m_nSelection]->function)
                return;
            m_vEntries[m_nSelection]->function(m_vEntries[m_nSelection]->param);
        }
    };

    void Next()
    {
        if(m_nSelection < m_vEntries.size() - 1)
        {
            if(++m_nSelection > m_nFirstEntry + 6)
                ++m_nFirstEntry;
            Draw();
        }
    };
    void Previous()
    {
        if(m_nSelection > 0)
        {
            if(--m_nSelection < m_nFirstEntry)
                --m_nFirstEntry;
            Draw();
        }
    };


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

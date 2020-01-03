/*	Classes implementing screen views
*/

#include "ribanfblib/ribanfblib.h"
#include <string>
#include <functional>   // provides std::function

using namespace std;

class Screen
{
	public:
	Screen(ribanfblib* pScreen, string sTitle) :
		m_pScreen(pScreen),
		m_sTitle(sTitle)
		
	{
	}

	void Draw()
	{
		m_pScreen->Clear();
		m_pScreen->DrawRect(0,0, 160,16, BLUE, 0, BLUE);
		m_pScreen->DrawText(m_sTitle, 5, 15, WHITE);
	}

	protected:
	ribanfblib* m_pScreen;
	string m_sTitle;
};

// An entry in a list screen
struct ListEntry {
    string title; // Title to display in list
    std::function<void(int)> function = NULL;  // Function to call on selection - default is none
    unsigned int param; // Parameter for function
};


class ListScreen : public Screen
{
	public:
	ListScreen(ribanfblib* pScreen, string sTitle) :
		Screen(pScreen, sTitle)
        {
        }

	
	~ListScreen()
	{
		for(auto it = m_vEntries.begin(); it != m_vEntries.end(); ++it)
			delete *it;
	}

	void Draw() 
	{
		Screen::Draw();
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

    void Add(string title, std::function<void(int)> function = NULL, unsigned int param = 0)
    {
        ListEntry* pEntry = new ListEntry;
        pEntry->title = title;
        pEntry->function = function;
	pEntry->param = param;
        m_vEntries.push_back(pEntry);
        if(m_nSelection < 1)
            m_nSelection = 0;
    };

    void Select()
    {
        if(m_nSelection < m_vEntries.size())
        {
            if(m_vEntries[m_nSelection]->function)
                m_vEntries[m_nSelection]->function(m_vEntries[m_nSelection]->param);
        }
    };

    void Next()
    {
        if(m_nSelection < m_vEntries.size() - 1)
            ++m_nSelection;
    };
    void Previous()
    {
        if(m_nSelection > 0)
            --m_nSelection;
    };


	private:
	int m_nSelection = -1; // Index of selected item
	std::vector<ListEntry*> m_vEntries; // List of entries
	unsigned int m_nFirstEntry = 0; // Index of first item to display
};

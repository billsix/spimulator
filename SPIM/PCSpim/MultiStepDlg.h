// MultiStepDlg.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CMultiStepDlg dialog

class CMultiStepDlg : public CDialog
{
// Construction
public:
	CMultiStepDlg(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CMultiStepDlg)
	enum { IDD = IDD_MULTISTEP };
	DWORD	m_cSteps;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CMultiStepDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CMultiStepDlg)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};
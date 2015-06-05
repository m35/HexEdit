// BookmarkDlg.cpp : implements dialog for viewing/editing bookmarks
//
// Copyright (c) 2003-2010 by Andrew W. Phillips.
//
// No restrictions are placed on the noncommercial use of this code,
// as long as this text (from the above copyright notice to the
// disclaimer below) is preserved.
//
// This code may be redistributed as long as it remains unmodified
// and is not sold for profit without the author's written consent.
//
// This code, or any part of it, may not be used in any software that
// is sold for profit, without the author's written consent.
//
// DISCLAIMER: This file is provided "as is" with no expressed or
// implied warranty. The author accepts no liability for any damage
// or loss of business that this product may cause.
//

#include "stdafx.h"

#include <io.h>

#include "HexEdit.h"
#include "MainFrm.h"
#include "HexEditDoc.h"
#include "HexEditView.h"
#include "Bookmark.h"
#include "BookmarkDlg.h"
#include <HtmlHelp.h>

#include "resource.hm"
#include "HelpID.hm"            // For dlg help ID

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// We need access to the grid since the callback sorting function does not have
// access to it and we need to know which column we are sorting on (GetSortColumn).
// Note: can be static as there is only one bookmarks dlg, hence need one grid ctrl.
static CGridCtrl *p_grid;

// The sorting callback function can't be a member function
static int CALLBACK bl_compare(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	int retval;

	(void)lParamSort;

	CGridCellBase* pCell1 = (CGridCellBase*) lParam1;
	CGridCellBase* pCell2 = (CGridCellBase*) lParam2;
	if (!pCell1 || !pCell2) return 0;

	ASSERT(p_grid->GetSortColumn() < p_grid->GetColumnCount());
	switch (p_grid->GetSortColumn() - p_grid->GetFixedColumnCount())
	{
	case CBookmarkDlg::COL_NAME:
	case CBookmarkDlg::COL_LOCN:
		// Do text compare (ignore case)
#if _MSC_VER >= 1300
		return _wcsicmp(pCell1->GetText(), pCell2->GetText());
#else
		return _stricmp(pCell1->GetText(), pCell2->GetText());
#endif
	case CBookmarkDlg::COL_FILE:
#if _MSC_VER >= 1300
		retval = _wcsicmp(pCell1->GetText(), pCell2->GetText());
		if (retval != 0)
			return retval;
		// Now compare based on file directory
		pCell1 = (CGridCellBase*)pCell1->GetData();
		pCell2 = (CGridCellBase*)pCell2->GetData();
		retval = _wcsicmp(pCell1->GetText(), pCell2->GetText());
#else
		retval = _stricmp(pCell1->GetText(), pCell2->GetText());
		if (retval != 0)
			return retval;
		// Now compare based on file directory
		pCell1 = (CGridCellBase*)pCell1->GetData();
		pCell2 = (CGridCellBase*)pCell2->GetData();
		retval = _stricmp(pCell1->GetText(), pCell2->GetText());
#endif
		if (retval != 0)
			return retval;
		// Now compare based on file position
		pCell1 = (CGridCellBase*)pCell1->GetData();
		pCell2 = (CGridCellBase*)pCell2->GetData();
		if (pCell1->GetData() < pCell2->GetData())
			return -1;
		else if (pCell1->GetData() == pCell2->GetData())
			return 0;
		else
			return 1;
		break;
	case CBookmarkDlg::COL_POS:
	case CBookmarkDlg::COL_MODIFIED:
	case CBookmarkDlg::COL_ACCESSED:
		// Do date compare
		if (pCell1->GetData() < pCell2->GetData())
			return -1;
		else if (pCell1->GetData() == pCell2->GetData())
			return 0;
		else
			return 1;
		break;
	}
	ASSERT(0);
	return 0;
}

static DWORD id_pairs[] = { 
	IDC_BOOKMARK_ADD, HIDC_BOOKMARK_ADD,
	IDC_GRID_BL, HIDC_GRID_BL,
	IDC_BOOKMARK_REMOVE, HIDC_BOOKMARK_REMOVE,
	IDC_BOOKMARK_GOTO, HIDC_BOOKMARK_GOTO,
	IDC_BOOKMARKS_VALIDATE, HIDC_BOOKMARKS_VALIDATE,
	0,0
};

/////////////////////////////////////////////////////////////////////////////
// CBookmarkDlg dialog

CBookmarkDlg::CBookmarkDlg() : CDialog()
{
	help_hwnd_ = (HWND)0;
	p_grid = &grid_;
	pdoc_ = NULL;
	m_first = true;
}

BOOL CBookmarkDlg::Create(CWnd *pParentWnd)
{
	if (!CDialog::Create(MAKEINTRESOURCE(IDD), pParentWnd)) // IDD_BOOKMARKS
	{
		TRACE0("Failed to create bookmarks dialog\n");
		return FALSE; // failed to create
	}

	ctl_add_.SetImage(IDB_BM_ADD, IDB_BM_ADD_HOT);
	ctl_add_.m_bTransparent = TRUE;
	ctl_add_.m_nFlatStyle = CMFCButton::BUTTONSTYLE_FLAT;
	ctl_add_.SetTooltip(_T("Create a bookmark at current location"));
	//ctl_add_.Invalidate();

	ctl_del_.SetImage(IDB_BM_DEL, IDB_BM_DEL_HOT);
	ctl_del_.m_bTransparent = TRUE;
	ctl_del_.m_nFlatStyle = CMFCButton::BUTTONSTYLE_FLAT;
	ctl_del_.SetTooltip(_T("Delete selected bookmark(s)"));
	//ctl_del_.Invalidate();

	ctl_goto_.SetImage(IDB_BM_GOTO, IDB_BM_GOTO_HOT);
	ctl_goto_.m_bTransparent = TRUE;
	ctl_goto_.m_nFlatStyle = CMFCButton::BUTTONSTYLE_FLAT;
	ctl_goto_.SetTooltip(_T("Jump to the selected bookmark"));
	//ctl_goto_.Invalidate();

	ctl_validate_.m_bDefaultClick = TRUE;     // Default click validates (also drop-down menu for options)
	ctl_validate_.m_bOSMenu = FALSE;
	ctl_validate_.m_bStayPressed = TRUE;
	VERIFY(validate_menu_.LoadMenu(IDR_BOOKMARKS_VALIDATE));
	ctl_validate_.m_hMenu = validate_menu_.GetSubMenu(0)->GetSafeHmenu();  // Add drop-down menu (see ValidateOptions())
	ctl_validate_.SetImage(IDB_BM_VALIDATE, IDB_BM_VALIDATE_HOT);
	ctl_validate_.m_bTransparent = TRUE;
	ctl_validate_.m_nFlatStyle = CMFCButton::BUTTONSTYLE_FLAT;
	ctl_validate_.SetTooltip(_T("Check that all bookmarks are for existing files."));
	//ctl_validate_.Invalidate();

	ctl_help_.SetImage(IDB_HELP, IDB_HELP_HOT);
	ctl_help_.m_bTransparent = TRUE;
	ctl_help_.m_nFlatStyle = CMFCButton::BUTTONSTYLE_FLAT;
	ctl_help_.SetTooltip(_T("Help"));
	//ctl_help_.Invalidate();

	ASSERT(GetDlgItem(IDC_GRID_BL) != NULL);
	if (!grid_.SubclassWindow(GetDlgItem(IDC_GRID_BL)->m_hWnd))
	{
		TRACE0("Failed to subclass grid control\n");
		return FALSE;
	}

	// Set up the grid control
	grid_.SetDoubleBuffering();
	grid_.SetAutoFit();
	grid_.SetCompareFunction(&bl_compare);
	grid_.SetGridLines(GVL_BOTH); // GVL_HORZ | GVL_VERT
	grid_.SetTrackFocusCell(FALSE);
	grid_.SetFrameFocusCell(FALSE);
	grid_.SetListMode(TRUE);
	grid_.SetSingleRowSelection(FALSE);
	grid_.SetHeaderSort(TRUE);

	grid_.SetFixedRowCount(1);

	InitColumnHeadings();
	grid_.SetColumnResize();

	grid_.EnableRowHide(FALSE);
	grid_.EnableColumnHide(FALSE);
	grid_.EnableHiddenRowUnhide(FALSE);
	grid_.EnableHiddenColUnhide(FALSE);

	grid_.SetEditable(TRUE);
	grid_.SetFixedColumnSelection(FALSE);
	grid_.SetFixedRowSelection(FALSE);

	FillGrid();

	grid_.ExpandColsNice(FALSE);

	// Set up resizer control
	// We must set the 4th parameter true else we get a resize border
	// added to the dialog and this really stuffs things up inside a pane.
	m_resizer.Create(GetSafeHwnd(), TRUE, 100, TRUE);

	// It needs an initial size for it's calcs
	CRect rct;
	GetWindowRect(&rct);
	m_resizer.SetInitialSize(rct.Size());
	m_resizer.SetMinimumTrackingSize(rct.Size());

	// This can cause problems if done too early (OnCreate or OnInitDialog)
	m_resizer.Add(IDC_GRID_BL, 0, 0, 100, 100);
	m_resizer.Add(IDC_BOOKMARKS_HELP, 100, 0, 0, 0);

	return TRUE;
}

void CBookmarkDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_BOOKMARK_ADD,  ctl_add_);
	DDX_Control(pDX, IDC_BOOKMARK_REMOVE,  ctl_del_);
	DDX_Control(pDX, IDC_BOOKMARK_GOTO,  ctl_goto_);
	DDX_Control(pDX, IDC_BOOKMARKS_VALIDATE,  ctl_validate_);
	DDX_Control(pDX, IDC_BOOKMARKS_HELP,  ctl_help_);
}

BEGIN_MESSAGE_MAP(CBookmarkDlg, CDialog)
	//{{AFX_MSG_MAP(CBookmarkDlg)
	ON_WM_DESTROY()
	ON_BN_CLICKED(IDC_BOOKMARK_ADD, OnAdd)
	ON_BN_CLICKED(IDC_BOOKMARK_REMOVE, OnRemove)
	ON_BN_CLICKED(IDC_BOOKMARK_GOTO, OnGoTo)
	ON_BN_CLICKED(IDC_BOOKMARKS_VALIDATE, OnValidate)
	ON_WM_HELPINFO()
	ON_BN_CLICKED(IDC_BOOKMARKS_HELP, OnHelp)
	//}}AFX_MSG_MAP
	ON_WM_CONTEXTMENU()
	ON_MESSAGE(WM_KICKIDLE, OnKickIdle)
	ON_NOTIFY(NM_CLICK, IDC_GRID_BL, OnGridClick)
	ON_NOTIFY(NM_DBLCLK, IDC_GRID_BL, OnGridDoubleClick)
	ON_NOTIFY(NM_RCLICK, IDC_GRID_BL, OnGridRClick)
	ON_NOTIFY(GVN_ENDLABELEDIT, IDC_GRID_BL, OnGridEndLabelEdit)
	//ON_MESSAGE_VOID(WM_INITIALUPDATE, OnInitialUpdate)
	ON_WM_ERASEBKGND()
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

void CBookmarkDlg::InitColumnHeadings()
{
	static char *heading[] =
	{
		"Name",
		"File",
		"Folder",
		"Byte No",
		"Modified",
		"Accessed",
		NULL
	};

	ASSERT(sizeof(heading)/sizeof(*heading) == COL_LAST + 1);

	CString strWidths = theApp.GetProfileString("File-Settings", 
												"BookmarkDialogColumns",
												"80,80,,60,,138");
	int curr_col = grid_.GetFixedColumnCount();
	bool all_hidden = true;

	for (int ii = 0; ii < COL_LAST; ++ii)
	{
		CString ss;

		AfxExtractSubString(ss, strWidths, ii, ',');
		int width = atoi(ss);
		if (width != 0) all_hidden = false;                  // we found a visible column

		ASSERT(heading[ii] != NULL);
		grid_.SetColumnCount(curr_col + 1);
		if (width == 0)
			grid_.SetColumnWidth(curr_col, 0);              // make column hidden
		else
			grid_.SetUserColumnWidth(curr_col, width);      // set user specified size (or -1 to indicate fit to cells)

		// Set column heading text (centred).  Also set item data so we know what goes in this column
		GV_ITEM item;
		item.row = 0;                                       // top row is header
		item.col = curr_col;                                // column we are changing
		item.mask = GVIF_PARAM|GVIF_FORMAT|GVIF_TEXT;       // change data+centered+text
		item.lParam = ii;                                   // data that says what's in this column
		item.nFormat = DT_CENTER|DT_VCENTER|DT_SINGLELINE;  // centre the heading
		item.strText = heading[ii];                         // text of the heading
		grid_.SetItem(&item);

		++curr_col;
	}

	// Show at least one column
	if (all_hidden)
	{
		grid_.SetColumnWidth(grid_.GetFixedColumnCount(), 1);
		grid_.AutoSizeColumn(grid_.GetFixedColumnCount(), GVS_BOTH);
	}
}

void CBookmarkDlg::FillGrid()
{
	CHexEditView *pview;                // Active view
	CHexEditDoc *pdoc = NULL;           // Active document (doc of the active view)
	int first_sel_row = -1;             // Row of first bookmark found in the active document

	if (FALSE && (pview = GetView()) != NULL)
		pdoc = pview->GetDocument();

	// Load all the bookmarks into the grid control
	CBookmarkList *pbl = theApp.GetBookmarkList();
	ASSERT(pbl->name_.size() == pbl->file_.size());

	int index, row;
	for (index = pbl->name_.size()-1, row = grid_.GetFixedRowCount(); index >= 0; index--)
	{
		// Don't show empty (deleted) names or those starting with an underscore (internal use)
		if (!pbl->name_[index].IsEmpty() && pbl->name_[index][0] != '_')
		{
			grid_.SetRowCount(row + 1);
			// If this bookmark is in the currently active document then select it
			if (pdoc != NULL && std::find(pdoc->bm_index_.begin(), pdoc->bm_index_.end(), index) != pdoc->bm_index_.end())
			{
				if (first_sel_row == -1) first_sel_row = row;
				UpdateRow(index, row, TRUE);
			}
			else
				UpdateRow(index, row);
			++row;
		}
	}
	if (first_sel_row != -1)
		grid_.EnsureVisible(first_sel_row, 0);
}

void CBookmarkDlg::RemoveBookmark(int index)
{
	// First find the bookmark in the list
	int row;
	int col = COL_NAME + grid_.GetFixedColumnCount();
	for (row = grid_.GetFixedRowCount(); row < grid_.GetRowCount(); ++row)
	{
		if (grid_.GetItemData(row, col) == index)
			break;
	}
	if (row < grid_.GetRowCount())
	{
		grid_.DeleteRow(row);
		grid_.Refresh();
	}
}

// Given a bookmark it updates the row in the dialog.  First it finds the
// row or appends a row if the bookmark is not found then calls UpdateRow
void CBookmarkDlg::UpdateBookmark(int index, BOOL select /*=FALSE*/)
{
	// First find the bookmark in the list
	int row;
	int col = COL_NAME + grid_.GetFixedColumnCount();
	for (row = grid_.GetFixedRowCount(); row < grid_.GetRowCount(); ++row)
	{
		if (grid_.GetItemData(row, col) == index)
			break;
	}
	if (row == grid_.GetRowCount())
	{
		// Not found so append a row and update that
		grid_.SetRowCount(row + 1);
	}

	UpdateRow(index, row, select);
}

// Given a bookmark (index) and a row in the grid control (row) update
// the row with the bookmark info and (optionally) select the row
void CBookmarkDlg::UpdateRow(int index, int row, BOOL select /*=FALSE*/)
{
	CBookmarkList *pbl = theApp.GetBookmarkList();

	int fcc = grid_.GetFixedColumnCount();

#if 0
	// If the file is already open we have to check the document to get the latest position
	if (last_file_ != pbl->file_[index])
	{
		if (theApp.m_pDocTemplate->MatchDocType(pbl->file_[index], pdoc_) != 
			CDocTemplate::yesAlreadyOpen)
		{
			pdoc_ = NULL;
		}
		last_file_ = pbl->file_[index];
	}
#endif

	GV_ITEM item;
	item.row = row;
	item.mask = GVIF_STATE|GVIF_FORMAT|GVIF_TEXT|GVIF_PARAM;
	item.nState = 0;
	if (select) item.nState |= GVIS_SELECTED;

	struct tm *timeptr;             // Used in displaying dates
	char disp[128];

	// Split filename into name and path
	int path_len;                   // Length of path (full name without filename)
	path_len = pbl->file_[index].ReverseFind('\\');
	if (path_len == -1) path_len = pbl->file_[index].ReverseFind('/');
	if (path_len == -1) path_len = pbl->file_[index].ReverseFind(':');
	if (path_len == -1)
		path_len = 0;
	else
		++path_len;

	int ext_pos = pbl->file_[index].ReverseFind('.');
	if (ext_pos < path_len) ext_pos = -1;              // '.' in a sub-directory name

	for (int column = 0; column < COL_LAST; ++column)
	{
		item.col = column + fcc;

		switch (column)
		{
		case COL_NAME:
			item.nFormat = DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS;
			item.strText = pbl->name_[index];
			item.lParam = index;
			break;
		case COL_FILE:
			item.nState = GVIS_READONLY;
			item.nFormat = DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS;
			item.strText = pbl->file_[index].Mid(path_len);
			item.lParam = (LONGLONG)grid_.GetCell(item.row, fcc + COL_LOCN);
			break;
		case COL_LOCN:
			item.nState = GVIS_READONLY;
			item.nFormat = DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS;
			item.strText = pbl->file_[index].Left(path_len);
			item.lParam = (LONGLONG)grid_.GetCell(item.row, fcc + COL_POS);
			break;
		case COL_POS:
			item.nState = GVIS_READONLY;
			item.nFormat = DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS;
			FILE_ADDRESS addr;
			if (pdoc_ != NULL)
				addr = __int64(((CHexEditDoc *)pdoc_)->GetBookmarkPos(index));
			else
				addr = __int64(pbl->filepos_[index]);
			sprintf(disp, "%I64d", addr);
			item.strText = disp;
			::AddCommas(item.strText);
			item.lParam = addr;          // lParam is now 64 bit
			break;
		case COL_MODIFIED:
			item.nState = GVIS_READONLY;
			item.nFormat = DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS;
			if ((timeptr = localtime(&pbl->modified_[index])) == NULL)
				item.strText = "Invalid";
			else
			{
				strftime(disp, sizeof(disp), "%c", timeptr);
				item.strText = disp;
			}
			item.lParam = pbl->modified_[index];
			break;
		case COL_ACCESSED:
			item.nState = GVIS_READONLY;
			item.nFormat = DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS;
			if ((timeptr = localtime(&pbl->accessed_[index])) == NULL)
				item.strText = "Invalid";
			else
			{
				strftime(disp, sizeof(disp), "%c", timeptr);
				item.strText = disp;
			}
			item.lParam = pbl->accessed_[index];
			break;
		default:
			ASSERT(0);
		}

		// Make sure we don't deselect a row that was already selected
		if (column == 0 && !select && (grid_.GetItemState(row, 0) & GVIS_SELECTED) != 0)
			item.nState |= GVIS_SELECTED;

		grid_.SetItem(&item);
	}
	grid_.RedrawRow(row);
}

// Message handlers
void CBookmarkDlg::OnDestroy()
{
	if (grid_.m_hWnd != 0)
	{
		// Save column widths
		CString strWidths;

		for (int ii = grid_.GetFixedColumnCount(); ii < grid_.GetColumnCount(); ++ii)
		{
			CString ss;
			ss.Format("%ld,", long(grid_.GetUserColumnWidth(ii)));
			strWidths += ss;
		}

		theApp.WriteProfileString("File-Settings", "BookmarkDialogColumns", strWidths);
	}

	CDialog::OnDestroy();

	if (m_pBrush != NULL)
	{
		delete m_pBrush;
		m_pBrush = NULL;
	}
}

LRESULT CBookmarkDlg::OnKickIdle(WPARAM, LPARAM lCount)
{
	ASSERT(GetDlgItem(IDC_BOOKMARK_ADD) != NULL);
	ASSERT(GetDlgItem(IDC_GRID_BL) != NULL);
	ASSERT(GetDlgItem(IDC_BOOKMARK_GOTO) != NULL);
	ASSERT(GetDlgItem(IDC_BOOKMARK_REMOVE) != NULL);
	ASSERT(GetDlgItem(IDC_BOOKMARKS_VALIDATE) != NULL);
	ASSERT(GetDlgItem(IDC_BOOKMARKS_HELP) != NULL);

	validate_menu_.GetSubMenu(0)->CheckMenuItem(ID_KEEP, GetKeepNetwork() ? MF_CHECKED : MF_UNCHECKED);

	if (m_first)
	{
		m_first = false;
	}

	// If there are no views or no associated files disallow adding of bookmarks
	CHexEditView *pview = GetView();
	if (pview == NULL || pview->GetDocument()->pfile1_ == NULL)
	{
		GetDlgItem(IDC_BOOKMARK_ADD)->EnableWindow(FALSE);
	}
	else
	{
		GetDlgItem(IDC_BOOKMARK_ADD)->EnableWindow(TRUE);
	}

	CCellRange sel = grid_.GetSelectedCellRange();
	GetDlgItem(IDC_BOOKMARK_GOTO)->EnableWindow(sel.IsValid() && sel.GetMinRow() == sel.GetMaxRow());
	GetDlgItem(IDC_BOOKMARK_REMOVE)->EnableWindow(sel.IsValid());

	// Display context help for ctrl set up in OnHelpInfo
	if (help_hwnd_ != (HWND)0)
	{
		theApp.HtmlHelpWmHelp(help_hwnd_, id_pairs);
		help_hwnd_ = (HWND)0;
	}
	return FALSE;
}

BOOL CBookmarkDlg::PreTranslateMessage(MSG* pMsg)
{
	//if (pMsg->message == WM_CHAR && pMsg->wParam == '\r')
	//{
	//	OnAdd();
	//	return TRUE;
	//}

	return CDialog::PreTranslateMessage(pMsg);
}

CBrush * CBookmarkDlg::m_pBrush = NULL;

BOOL CBookmarkDlg::OnEraseBkgnd(CDC *pDC)
{
	// We check for changed look in erase background event as it's done
	// before other drawing.  This is necessary (to update m_pBrush etc) 
	// because there is no message sent when the look changes.
	static UINT saved_look = 0;
	if (theApp.m_nAppLook != saved_look)
	{
		// Create new brush used for background of static controls
		if (m_pBrush != NULL)
			delete m_pBrush;
		m_pBrush = new CBrush(afxGlobalData.clrBarFace);

		// Change background colour of grid headers
		grid_.SetFixedBackClr(afxGlobalData.clrBarFace);

		saved_look = theApp.m_nAppLook;
	}

	CRect rct;
	GetClientRect(&rct);

	// Fill the background with a colour that matches the current BCG theme (and hence sometimes with the Windows Theme)
	pDC->FillSolidRect(rct, afxGlobalData.clrBarFace);
	return TRUE;
}

HBRUSH CBookmarkDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (nCtlColor == CTLCOLOR_STATIC)
	{
		pDC->SetBkMode(TRANSPARENT);                            // Make sure text has no background
//		return(HBRUSH) afxGlobalData.brWindow.GetSafeHandle();  // window colour
		if (m_pBrush != NULL)
			return (HBRUSH)*m_pBrush;
	}

	return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CBookmarkDlg::OnAdd()
{
	int col = grid_.GetFixedColumnCount() + COL_NAME;

	CBookmarkList *pbl = theApp.GetBookmarkList();
	CHexEditView *pview = GetView();
	if (pbl == NULL || pview == NULL || pview->GetDocument()->pfile1_ == NULL)
		return;

	CString file_name = pview->GetDocument()->pfile1_->GetFilePath();

	// Work out default bookmark name
	int dummy;
	const char *prefix = "Bookmark";
	long last = pbl->GetSetLast(prefix, dummy);
	if (last == 0) last = 1;    // start at 1 not zero
	CString bookmark_name;
	bookmark_name.Format("%s%ld", prefix, last);

	// Add bookmark (adds to our control (grid_), internal list (pbl), and document list)
	int ii = pbl->AddBookmark(bookmark_name, file_name, pview->GetPos(), NULL, pview->GetDocument());

	//theApp.SaveToMacro(km_bookmarks_add, name);

	// Check that AddBookmark has caused our grid to be updated
	int row = grid_.GetRowCount() - 1;    // new row was added at the end
	ASSERT(grid_.GetItemData(row, grid_.GetFixedColumnCount()) == ii);

	// Make sure the name column is visible
	if (grid_.GetColumnWidth(col) <= 0)
	{
		grid_.SetColumnWidth(col, 1);
		grid_.AutoSizeColumn(col, GVS_BOTH);
	}

	// Now select the bookmark name cell of the row ready for user to enter the name
	grid_.RedrawWindow();
	grid_.EnsureVisible(row, col);
	grid_.SetFocus();
	//grid_.SetFocusCell(row, col);

	// The following starts editing of the cell
	//grid_.OnEditCell(row, col, CPoint( -1, -1), VK_LBUTTON);
    CCellID cell(row, col);
    CRect rect;
	CGridCellBase * pCell;
    if (grid_.IsCellVisible(row, col) &&
		grid_.GetCellRect(cell, rect) &&
		(pCell = grid_.GetCell(row, col)) != NULL)
	{
		// Begin cell editing
        pCell->Edit(row, col, rect, CPoint(-1, -1), IDC_INPLACE_CONTROL, VK_LBUTTON);

		// Select cell contents so user can just overtype with a new name
		((CEdit *)pCell->GetEditWnd())->SetSel(0, -1);
	}
}

void CBookmarkDlg::OnGoTo()
{
	CCellRange sel = grid_.GetSelectedCellRange();
	ASSERT(sel.IsValid() && sel.GetMinRow() == sel.GetMaxRow());

	int row = sel.GetMinRow();
	int index = grid_.GetItemData(row, COL_NAME + grid_.GetFixedColumnCount());

	CBookmarkList *pbl = theApp.GetBookmarkList();
	pbl->GoTo(index);
}

void CBookmarkDlg::OnRemove()
{
	CBookmarkList *pbl = theApp.GetBookmarkList();
	int fcc = grid_.GetFixedColumnCount();
	range_set<int> tt;                  // the grid rows to be removed
	CCellRange sel = grid_.GetSelectedCellRange();
	ASSERT(sel.IsValid());

	// Mark all selected rows for deletion
	for (int row = sel.GetMinRow(); row <= sel.GetMaxRow(); ++row)
		if (grid_.IsCellSelected(row, fcc))
		{
			int index = grid_.GetItemData(row, fcc + COL_NAME);

#if 0 // moved to pbl->RemoveBookmark()
			// Check for open document and remove from the local (doc) bookmarks as well
			CDocument *pdoc;
			if (theApp.m_pDocTemplate->MatchDocType(pbl->file_[index], pdoc) == 
				CDocTemplate::yesAlreadyOpen)
			{
				((CHexEditDoc *)pdoc)->RemoveBookmark(index);
			}
#endif

			// Remove from the bookmark list
			pbl->Remove(index);

			// Mark for deletion from the grid (see below)
			tt.insert(row);
		}

	// Now remove the rows from the display starting from the end
	range_set<int>::const_iterator pp;

	// Remove elements from the grid starting at end so that we don't muck up the row order
	for (pp = tt.end(); pp != tt.begin(); )
		grid_.DeleteRow(*(--pp));
	grid_.Refresh();
}

void CBookmarkDlg::ValidateOptions()
{
	switch (ctl_validate_.m_nMenuResult)
	{
	case ID_KEEP:
		SetKeepNetwork(!GetKeepNetwork());
		break;
	default:
		ASSERT(0);   // new menu item added which we are not handling?
	}
}

void CBookmarkDlg::OnValidate()
{
	if (ctl_validate_.m_nMenuResult != 0)
	{
		ValidateOptions();
		return;
	}

	int move_count = 0;      // Number of bookmarks moved due to being past EOF

	CWaitCursor wc;
	CBookmarkList *pbl = theApp.GetBookmarkList();
	int fcc = grid_.GetFixedColumnCount();
	range_set<int> tt;                  // the grid rows to be removed

	for (int row = grid_.GetFixedRowCount(); row < grid_.GetRowCount(); ++row)
	{
		int index = grid_.GetItemData(row, fcc + COL_NAME);
		CDocument *pdoc;
		ASSERT(index > -1 && index < int(pbl->file_.size()));

		if (theApp.m_pDocTemplate->MatchDocType(pbl->file_[index], pdoc) == 
			CDocTemplate::yesAlreadyOpen)
		{
			// Bookmarks are checked when we open the document so we don't need to validate again here
			// (Ie this bookmark was checked when the file was opened.)
		}
		else if (_access(pbl->file_[index], 0) != -1)
		{
			// File exists but make sure the bookmark is <= file length
			CFileStatus fs;                 // Used to get file size

			if (CFile64::GetStatus(pbl->file_[index], fs) &&
				pbl->filepos_[index] > __int64(fs.m_size))
			{
				TRACE2("Validate: moving %s, file %s is too short\n", pbl->name_[index], pbl->file_[index]);
				pbl->filepos_[index] = fs.m_size;
				UpdateRow(index, row);
				++move_count;
			}
		}
		else
		{
			ASSERT(pbl->file_[index].Mid(1, 2) == ":\\"); // GetDriveType expects "D:\" under win9x

			// File not found so remove bookmark from the list
			if (!GetKeepNetwork() || ::GetDriveType(pbl->file_[index].Left(3)) == DRIVE_FIXED)
			{
				TRACE2("Validate: removing bookmark %s, file %s not found\n", pbl->name_[index], pbl->file_[index]);
				//pbl->RemoveBookmark(index);
				//tt.insert(row);
				tt.insert(index);
			}
		}
	}

	// Now remove the rows from the display starting from the end
	range_set<int>::const_iterator pp;

	// Remove elements starting at end so that we don't muck up the row order
//	for (pp = tt.end(); pp != tt.begin(); )
//		grid_.DeleteRow(*(--pp));
//	grid_.Refresh();
	for (pp = tt.begin(); pp != tt.end(); ++pp)
		pbl->RemoveBookmark(*pp);

	if (move_count > 0 || tt.size() > 0)
	{
		CString mess;

		mess.Format("%ld bookmarks were deleted (files missing)\n"
					"%ld bookmarks were moved (past EOF)",
					long(tt.size()), long(move_count));
		CAvoidableDialog::Show(IDS_BOOKMARKS_DELETED, mess);
	}
	else
		CAvoidableDialog::Show(IDS_BOOKMARKS_NONE_DELETED, 
		                 "No bookmarks were deleted or moved.");
}

// Handlers for messages from grid control (IDC_GRID_BL)
void CBookmarkDlg::OnGridEndLabelEdit(NMHDR *pNotifyStruct, LRESULT* pResult)
{
	*pResult = -1;

	NM_GRIDVIEW* pItem = (NM_GRIDVIEW*) pNotifyStruct;
	if (pItem->iColumn == grid_.GetFixedColumnCount() + COL_NAME)
	{
		CBookmarkList *pbl = theApp.GetBookmarkList();
		CHexEditView *pview = GetView();
		if (pbl == NULL || pview == NULL || pview->GetDocument()->pfile1_ == NULL)
		{
			TaskMessageBox("No Disk File", "You can only create a bookmark for a file that exists on disk");
			return;
		}

		CString ss = (CString)grid_.GetItemText(pItem->iRow, pItem->iColumn);
		// Ensure the bookmark name is valid
		// - not empty
		// - does not begin with underscore
		// - does not contain invalid characters like |
		// - not the same as another bookmark in the same file
		if (ss.IsEmpty())
		{
			TaskMessageBox("Empty Bookmark", "Please enter the bookmark name");
			return;
		}
		if (ss[0] == '_')
		{
			TaskMessageBox("Invalid Bookmark",
				"Bookmark names that begin with an underscore are reserved for internal use.");
			return;
		}
		if (ss.FindOneOf("|") != -1)
		{
			TaskMessageBox("Invalid Bookmark",
				"The bookmark name contains characters that are not permitted such as \"|\"");
			return;
		}

		CString file_name = pview->GetDocument()->pfile1_->GetFilePath();
		if (pbl->GetIndex(ss, file_name) != -1)
		{
			TaskMessageBox("Bookmark Exists",
				"A bookmark with the same name already exists in this file.");
			return;
		}

		// update the name of the bookmark
		pbl->Rename(grid_.GetItemData(pItem->iRow, pItem->iColumn), ss);
	}
	*pResult = 0;   // do not force back into edit of field
}

/// xxx remove this?
void CBookmarkDlg::OnGridClick(NMHDR *pNotifyStruct, LRESULT* /*pResult*/)
{
	NM_GRIDVIEW* pItem = (NM_GRIDVIEW*) pNotifyStruct;
	TRACE("Left button click on row %d, col %d\n", pItem->iRow, pItem->iColumn);

	if (pItem->iRow < grid_.GetFixedRowCount())
		return;                         // Don't do anything for header rows
}

void CBookmarkDlg::OnGridDoubleClick(NMHDR *pNotifyStruct, LRESULT* /*pResult*/)
{
	NM_GRIDVIEW* pItem = (NM_GRIDVIEW*) pNotifyStruct;
	TRACE("Left button D-click on row %d, col %d\n", pItem->iRow, pItem->iColumn);

	if (pItem->iRow < grid_.GetFixedRowCount())
		return;                         // Don't do anything for header rows

	CCellRange sel = grid_.GetSelectedCellRange();
	if (sel.IsValid() && sel.GetMinRow() == sel.GetMaxRow())
		OnGoTo();
}

void CBookmarkDlg::OnGridRClick(NMHDR *pNotifyStruct, LRESULT* /*pResult*/)
{
	NM_GRIDVIEW* pItem = (NM_GRIDVIEW*) pNotifyStruct;
	TRACE("Right button click on row %d, col %d\n", pItem->iRow, pItem->iColumn);
	int fcc = grid_.GetFixedColumnCount();

	if (pItem->iRow < grid_.GetFixedRowCount() && pItem->iColumn >= fcc)
	{
		// Right click on column headings - create menu of columns available
		CMenu mm;
		mm.CreatePopupMenu();

		mm.AppendMenu(MF_ENABLED|(grid_.GetColumnWidth(fcc+0)>0?MF_CHECKED:0), 1, "Name");
		mm.AppendMenu(MF_ENABLED|(grid_.GetColumnWidth(fcc+1)>0?MF_CHECKED:0), 2, "File");
		mm.AppendMenu(MF_ENABLED|(grid_.GetColumnWidth(fcc+2)>0?MF_CHECKED:0), 3, "Folder");
		mm.AppendMenu(MF_ENABLED|(grid_.GetColumnWidth(fcc+3)>0?MF_CHECKED:0), 4, "Byte No");
		mm.AppendMenu(MF_ENABLED|(grid_.GetColumnWidth(fcc+4)>0?MF_CHECKED:0), 5, "Modified");
		mm.AppendMenu(MF_ENABLED|(grid_.GetColumnWidth(fcc+5)>0?MF_CHECKED:0), 6, "Accessed");

		// Work out where to display the popup menu
		CRect rct;
		grid_.GetCellRect(pItem->iRow, pItem->iColumn, &rct);
		grid_.ClientToScreen(&rct);
		int item = mm.TrackPopupMenu(
				TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD,
				(rct.left+rct.right)/2, (rct.top+rct.bottom)/2, this);
		if (item != 0)
		{
			item += fcc-1;
			if (grid_.GetColumnWidth(item) > 0)
				grid_.SetColumnWidth(item, 0);
			else
			{
				grid_.SetColumnWidth(item, 1);
				grid_.AutoSizeColumn(item, GVS_BOTH);
			}
			grid_.ExpandColsNice(FALSE);
		}
	}
}

void CBookmarkDlg::OnHelp()
{
	if (!::HtmlHelp(AfxGetMainWnd()->m_hWnd, theApp.htmlhelp_file_, HH_HELP_CONTEXT, HIDD_BOOKMARKS_HELP))
		AfxMessageBox(AFX_IDP_FAILED_TO_LAUNCH_HELP);
}

BOOL CBookmarkDlg::OnHelpInfo(HELPINFO* pHelpInfo)
{
	// Note calling theApp.HtmlHelpWmHelp here seems to make the window go behind 
	// and then disappear when mouse up evenet is seen.  The only soln I could
	// find after a lot of experimenetation is to do it later (in OnKickIdle).
	help_hwnd_ = (HWND)pHelpInfo->hItemHandle;
	return TRUE;
}

void CBookmarkDlg::OnContextMenu(CWnd* pWnd, CPoint point)
{
	// Don't show context menu if right-click on grid top row (used to display column menu)
	if (pWnd->IsKindOf(RUNTIME_CLASS(CGridCtrl)))
	{
		grid_.ScreenToClient(&point);
		CCellID cell = grid_.GetCellFromPt(point);
		if (cell.row == 0)
			return;
	}

	// NOTE: IMPORTANT For this to work with static text controls the control
	// must have the "Notify" (SS_NOTIFY) checkbox set.
	// It took me days to work out how right click of the "Name:" static
	// text should show the "What's This" context menu.

	theApp.HtmlHelpContextMenu(pWnd, id_pairs);
}

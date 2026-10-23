// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2026 - TortoiseGit

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
#pragma once

/**
 * \ingroup TortoiseProc
 * Support for showing the log dialogs of different repositories as tabs of one window.
 *
 * Every log dialog keeps running in its own TortoiseGitProc process (g_Git and all
 * repository state are per process). The tab host window lives in a separate
 * TortoiseGitProc process (/command:logtabhost), which is started on demand. A log
 * dialog turns itself into a child window of the host and registers itself as a tab
 * (WM_COPYDATA), the host only arranges the log dialogs and draws the tab strip.
 * All other dialogs keep being independent windows.
 */
namespace LogTabHost
{
	inline constexpr wchar_t HostWindowClassName[] = L"TortoiseGit_LogTabHost";
	// set on a host window which is about to close, such a host does not accept new tabs anymore
	inline constexpr wchar_t HostClosingProp[] = L"TortoiseGit_LogTabHostClosing";

	// WM_COPYDATA identifier, sent by a log dialog (wParam) to register itself as a tab
	inline constexpr ULONG_PTR COPYDATA_REGISTERTAB = 0x54474C54;

	struct RegisterTabData
	{
		DWORD cbSize;
		LONG minWidth;
		LONG minHeight;
		wchar_t label[MAX_PATH];
		wchar_t path[MAX_PATH];
	};

	// wParam of WM_TGIT_LOGTAB
	enum TabCommand : WPARAM
	{
		CMD_NEXTTAB = 1,	// log dialog -> host
		CMD_PREVTAB,		// log dialog -> host
		CMD_SELECTTAB,		// log dialog -> host, lParam: index of the tab, -1 for the last one
		CMD_UNREGISTERTAB,	// log dialog -> host, lParam: HWND of the log dialog
		CMD_FOCUS,			// host -> log dialog, tab got activated, lParam: HWND which had the focus before (or nullptr)
		CMD_HOSTMOVED,		// host -> log dialog, the host window was moved or resized
	};

	/// true if a log dialog should be shown as a tab of the log tab host window
	/// (HKCU\Software\TortoiseGit\LogDialogTabs, enabled by default; not for elevated processes)
	bool ShouldOpenAsTab();
	/// returns a running host window, starts a new host process if needed; nullptr on failure
	HWND FindOrStartHost();
}

const UINT WM_TGIT_LOGTAB = RegisterWindowMessage(L"TORTOISEGIT_LOGTAB_MSG");

/**
 * \ingroup TortoiseProc
 * Top level window which shows registered log dialogs (of other processes) as tabs.
 */
class CLogTabHostWnd : public CWnd
{
public:
	CLogTabHostWnd() = default;
	virtual ~CLogTabHostWnd();

	/// creates the (still hidden) host window, it gets shown as soon as the first tab registers
	bool CreateHostWindow();

protected:
	BOOL PreTranslateMessage(MSG* pMsg) override;

	afx_msg BOOL OnCopyData(CWnd* pWnd, COPYDATASTRUCT* pCopyDataStruct);
	afx_msg LRESULT OnLogTabMessage(WPARAM wParam, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnExitSizeMove();
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnMButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized);
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg void OnClose();
	afx_msg void OnDestroy();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnSysColorChange();
	afx_msg LRESULT OnDPIChanged(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()

private:
	struct Tab
	{
		HWND hWnd = nullptr;
		CString label;
		CString path;
		CSize minSize;
		HWND hLastFocus = nullptr;
	};

	struct TabColors
	{
		COLORREF strip;
		COLORREF active;
		COLORREF hover;
		COLORREF text;
		COLORREF inactiveText;
		COLORREF separator;
		COLORREF accent;
	};

	int FindTab(HWND hWnd) const;
	void ActivateTab(int index);
	void RemoveTab(int index);
	void CloseTab(int index);
	void CloseOtherTabs(int index);
	void CloseTabsToTheRight(int index);
	void CheckTabs();
	void LayoutActiveTab(bool bSynchronous, bool bShow = false);
	void RememberFocus();
	void FocusActiveTab();
	void UpdateTitle();
	void OpenNewTab();

	void UpdateMetrics();
	void SetTheme(bool bDark);
	TabColors GetColors() const;
	CRect GetContentRect() const;
	int GetTabWidth() const;
	CRect GetTabRect(int index) const;
	CRect GetCloseButtonRect(const CRect& rcTab) const;
	CRect GetNewTabButtonRect() const;
	int HitTestTab(CPoint point, bool* pOnCloseButton) const;
	void DrawTabStrip(CDC& dc, const CRect& rcStrip);
	void InvalidateTabStrip();

	static CString GetPlacementId(LPCWSTR name);
	static bool LoadPlacement(LPCWSTR name, WINDOWPLACEMENT& wp);
	void RestorePlacement();
	void SavePlacement();

	std::vector<Tab> m_tabs;
	int m_activeTab = -1;
	int m_hoverTab = -1;
	bool m_bHoverClose = false;
	bool m_bHoverNewTab = false;
	bool m_bTrackingMouse = false;
	bool m_bClosing = false;
	bool m_bPlacementSaved = false;
	bool m_bDark = false;
	int m_nInitialShowCmd = SW_SHOWNORMAL;
	int m_themeCallbackId = 0;

	CFont m_font;
	int m_stripHeight = 30;
	int m_tabTop = 4;
	int m_tabPadding = 10;
	int m_closeButtonSize = 16;
	int m_minTabWidth = 90;
	int m_maxTabWidth = 220;
	int m_newTabButtonWidth = 28;
	int m_margin = 4;
};

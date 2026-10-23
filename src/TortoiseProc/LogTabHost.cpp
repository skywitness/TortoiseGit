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
#include "stdafx.h"
#include "resource.h"
#include "LogTabHost.h"
#include "registry.h"
#include "PathUtils.h"
#include "CommonAppUtils.h"
#include "BrowseFolder.h"
#include "DPIAware.h"
#include "Theme.h"
#include "DarkModeHelper.h"
#include "StandAloneDlg.h"

namespace
{
constexpr UINT_PTR TIMER_CHECKTABS = 1;
constexpr UINT_PTR TIMER_STARTUP = 2;
constexpr UINT_PTR TIMER_CLOSING = 3;

enum ContextMenuCommand
{
	ID_CLOSETAB = 1,
	ID_CLOSEOTHERTABS,
	ID_CLOSETABSRIGHT,
	ID_NEWTAB,
};

HWND FindHost()
{
	HWND hWnd = nullptr;
	while ((hWnd = ::FindWindowEx(nullptr, hWnd, LogTabHost::HostWindowClassName, nullptr)) != nullptr)
	{
		if (!::GetProp(hWnd, LogTabHost::HostClosingProp))
			return hWnd;
	}
	return nullptr;
}

bool IsProcessElevated()
{
	CAutoGeneralHandle hToken;
	if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, hToken.GetPointer()))
		return false;
	TOKEN_ELEVATION elevation{};
	DWORD returnLength = 0;
	if (!::GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &returnLength))
		return false;
	return elevation.TokenIsElevated != 0;
}

COLORREF BlendColor(COLORREF color1, COLORREF color2, int percentOfColor2)
{
	auto blend = [percentOfColor2](int value1, int value2) { return static_cast<BYTE>((value1 * (100 - percentOfColor2) + value2 * percentOfColor2) / 100); };
	return RGB(blend(GetRValue(color1), GetRValue(color2)), blend(GetGValue(color1), GetGValue(color2)), blend(GetBValue(color1), GetBValue(color2)));
}

void DrawCross(CDC& dc, const CRect& rc, COLORREF color, float penWidth)
{
	Gdiplus::Graphics graphics(dc.GetSafeHdc());
	graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
	Gdiplus::Color penColor;
	penColor.SetFromCOLORREF(color);
	Gdiplus::Pen pen(penColor, penWidth);
	const Gdiplus::REAL inset = rc.Width() * 0.3f;
	const Gdiplus::REAL left = rc.left + inset;
	const Gdiplus::REAL top = rc.top + inset;
	const Gdiplus::REAL right = rc.right - inset;
	const Gdiplus::REAL bottom = rc.bottom - inset;
	graphics.DrawLine(&pen, left, top, right, bottom);
	graphics.DrawLine(&pen, right, top, left, bottom);
}

void DrawPlus(CDC& dc, const CRect& rc, COLORREF color, float penWidth)
{
	Gdiplus::Graphics graphics(dc.GetSafeHdc());
	graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
	Gdiplus::Color penColor;
	penColor.SetFromCOLORREF(color);
	Gdiplus::Pen pen(penColor, penWidth);
	const Gdiplus::REAL size = std::min(rc.Width(), rc.Height()) * 0.25f;
	const Gdiplus::REAL centerX = (rc.left + rc.right) / 2.0f;
	const Gdiplus::REAL centerY = (rc.top + rc.bottom) / 2.0f;
	graphics.DrawLine(&pen, centerX - size, centerY, centerX + size, centerY);
	graphics.DrawLine(&pen, centerX, centerY - size, centerX, centerY + size);
}
}

bool LogTabHost::ShouldOpenAsTab()
{
	if (!CRegDWORD(L"Software\\TortoiseGit\\LogDialogTabs", TRUE))
		return false;
	// an elevated log dialog cannot be controlled by a non-elevated host (and vice versa)
	return !IsProcessElevated();
}

HWND LogTabHost::FindOrStartHost()
{
	// serialize concurrently started log dialogs, so that only one host process gets started
	CAutoGeneralHandle startupMutex = ::CreateMutex(nullptr, FALSE, L"TortoiseGitLogTabHostStartup");
	const DWORD waitResult = startupMutex ? ::WaitForSingleObject(startupMutex, 10000) : WAIT_FAILED;
	SCOPE_EXIT
	{
		if (waitResult == WAIT_OBJECT_0 || waitResult == WAIT_ABANDONED)
			::ReleaseMutex(startupMutex);
	};

	if (HWND hWnd = FindHost(); hWnd)
		return hWnd;

	// start the host from the application directory, so that it does not lock any repository directory
	const CString sAppDir = CPathUtils::GetAppDirectory();
	CString sCmd;
	sCmd.Format(L"\"%sTortoiseGitProc.exe\" /command:logtabhost", static_cast<LPCWSTR>(sAppDir));
	if (!CCommonAppUtils::LaunchApplication(sCmd, CCommonAppUtils::LaunchApplicationFlags().WaitForStartup().UseCWD(sAppDir)))
		return nullptr;

	for (int i = 0; i < 50; ++i)
	{
		if (HWND hWnd = FindHost(); hWnd)
			return hWnd;
		::Sleep(100);
	}
	return nullptr;
}

BEGIN_MESSAGE_MAP(CLogTabHostWnd, CWnd)
	ON_WM_COPYDATA()
	ON_REGISTERED_MESSAGE(WM_TGIT_LOGTAB, OnLogTabMessage)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_EXITSIZEMOVE()
	ON_WM_GETMINMAXINFO()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_MBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_CONTEXTMENU()
	ON_WM_ACTIVATE()
	ON_WM_SETFOCUS()
	ON_WM_CLOSE()
	ON_WM_DESTROY()
	ON_WM_TIMER()
	ON_WM_SYSCOLORCHANGE()
	ON_MESSAGE(WM_DPICHANGED, OnDPIChanged)
END_MESSAGE_MAP()

CLogTabHostWnd::~CLogTabHostWnd()
{
	CTheme::Instance().RemoveRegisteredCallback(m_themeCallbackId);
}

bool CLogTabHostWnd::CreateHostWindow()
{
	const int iconWidth = ::GetSystemMetrics(SM_CXICON);
	const int iconHeight = ::GetSystemMetrics(SM_CYICON);
	const int smallIconWidth = ::GetSystemMetrics(SM_CXSMICON);
	const int smallIconHeight = ::GetSystemMetrics(SM_CYSMICON);
	WNDCLASSEX wc = { sizeof(WNDCLASSEX) };
	wc.style = CS_DBLCLKS;
	wc.lpfnWndProc = ::DefWindowProc;
	wc.hInstance = AfxGetInstanceHandle();
	wc.hIcon = CCommonAppUtils::LoadIconEx(IDR_MAINFRAME, iconWidth, iconHeight);
	wc.hIconSm = CCommonAppUtils::LoadIconEx(IDR_MAINFRAME, smallIconWidth, smallIconHeight);
	wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
	wc.lpszClassName = LogTabHost::HostWindowClassName;
	if (!::RegisterClassEx(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		return false;

	if (!CreateEx(0, LogTabHost::HostWindowClassName, L"TortoiseGit", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr))
		return false;

	m_themeCallbackId = CTheme::Instance().RegisterThemeChangeCallback([this]() { SetTheme(CTheme::Instance().IsDarkTheme()); });
	SetTheme(CTheme::Instance().IsDarkTheme());
	UpdateMetrics();
	RestorePlacement();

	SetTimer(TIMER_CHECKTABS, 1000, nullptr);
	// the log dialog which started the host should register within a few seconds
	SetTimer(TIMER_STARTUP, 30000, nullptr);
	return true;
}

BOOL CLogTabHostWnd::PreTranslateMessage(MSG* pMsg)
{
	// usually the keyboard focus is inside of a log dialog, which forwards these keys
	if (pMsg->message == WM_KEYDOWN && pMsg->hwnd == m_hWnd && GetKeyState(VK_CONTROL) < 0)
	{
		if (pMsg->wParam == VK_TAB)
		{
			OnLogTabMessage(GetKeyState(VK_SHIFT) < 0 ? LogTabHost::CMD_PREVTAB : LogTabHost::CMD_NEXTTAB, 0);
			return TRUE;
		}
		if (pMsg->wParam == 'W' || pMsg->wParam == VK_F4)
		{
			CloseTab(m_activeTab);
			return TRUE;
		}
	}
	return __super::PreTranslateMessage(pMsg);
}

BOOL CLogTabHostWnd::OnCopyData(CWnd* pWnd, COPYDATASTRUCT* pCopyDataStruct)
{
	if (!pWnd || !pCopyDataStruct || pCopyDataStruct->dwData != LogTabHost::COPYDATA_REGISTERTAB || pCopyDataStruct->cbData < sizeof(LogTabHost::RegisterTabData) || !pCopyDataStruct->lpData)
		return __super::OnCopyData(pWnd, pCopyDataStruct);

	const HWND hLogDlg = pWnd->GetSafeHwnd();
	if (m_bClosing || !::IsWindow(hLogDlg) || ::GetParent(hLogDlg) != m_hWnd || FindTab(hLogDlg) >= 0)
		return FALSE;

	const auto pData = static_cast<const LogTabHost::RegisterTabData*>(pCopyDataStruct->lpData);
	Tab tab;
	tab.hWnd = hLogDlg;
	tab.label = CString(pData->label, static_cast<int>(wcsnlen(pData->label, _countof(pData->label))));
	tab.path = CString(pData->path, static_cast<int>(wcsnlen(pData->path, _countof(pData->path))));
	tab.minSize = CSize(pData->minWidth, pData->minHeight);
	if (tab.label.IsEmpty())
		tab.label = L"Log";
	m_tabs.push_back(tab);

	KillTimer(TIMER_STARTUP);
	if (!IsWindowVisible())
		ShowWindow(m_nInitialShowCmd);
	else if (IsIconic())
		ShowWindow(SW_RESTORE);

	// synchronously, so that the log dialog already has its final size when it continues its initialization
	ActivateTab(static_cast<int>(m_tabs.size()) - 1);
	::SetForegroundWindow(m_hWnd);
	return TRUE;
}

LRESULT CLogTabHostWnd::OnLogTabMessage(WPARAM wParam, LPARAM lParam)
{
	const int count = static_cast<int>(m_tabs.size());
	switch (wParam)
	{
	case LogTabHost::CMD_NEXTTAB:
		if (count > 1)
			ActivateTab((m_activeTab + 1) % count);
		break;
	case LogTabHost::CMD_PREVTAB:
		if (count > 1)
			ActivateTab((m_activeTab + count - 1) % count);
		break;
	case LogTabHost::CMD_SELECTTAB:
		if (lParam < 0)
			ActivateTab(count - 1);
		else if (lParam < count)
			ActivateTab(static_cast<int>(lParam));
		break;
	case LogTabHost::CMD_UNREGISTERTAB:
		RemoveTab(FindTab(reinterpret_cast<HWND>(lParam)));
		break;
	default:
		break;
	}
	return 0;
}

int CLogTabHostWnd::FindTab(HWND hWnd) const
{
	for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i)
	{
		if (m_tabs[i].hWnd == hWnd)
			return i;
	}
	return -1;
}

void CLogTabHostWnd::ActivateTab(int index)
{
	if (index < 0 || index >= static_cast<int>(m_tabs.size()))
		return;

	const int previous = m_activeTab;
	if (previous != index)
		RememberFocus();
	m_activeTab = index;
	// show the new tab before hiding the old one in order to avoid flickering,
	// synchronously so that it is already visible when it receives CMD_FOCUS
	LayoutActiveTab(true, true);
	if (previous >= 0 && previous < static_cast<int>(m_tabs.size()) && previous != index)
		::ShowWindowAsync(m_tabs[previous].hWnd, SW_HIDE);

	UpdateTitle();
	InvalidateTabStrip();
	if (::GetForegroundWindow() == m_hWnd)
		FocusActiveTab();
}

void CLogTabHostWnd::RemoveTab(int index)
{
	if (index < 0 || index >= static_cast<int>(m_tabs.size()))
		return;

	const bool wasActive = index == m_activeTab;
	m_tabs.erase(m_tabs.begin() + index);
	m_hoverTab = -1;
	if (m_tabs.empty())
	{
		// the last tab got closed, so close the window as well
		m_activeTab = -1;
		DestroyWindow();
		return;
	}

	if (wasActive)
	{
		m_activeTab = -1;
		ActivateTab(std::min(index, static_cast<int>(m_tabs.size()) - 1));
		return;
	}

	if (index < m_activeTab)
		--m_activeTab;
	InvalidateTabStrip();
}

void CLogTabHostWnd::CloseTab(int index)
{
	if (index < 0 || index >= static_cast<int>(m_tabs.size()))
		return;
	// same as closing a standalone log dialog, the log dialog unregisters itself
	::PostMessage(m_tabs[index].hWnd, WM_CLOSE, 0, 0);
}

void CLogTabHostWnd::CloseOtherTabs(int index)
{
	for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i)
	{
		if (i != index)
			CloseTab(i);
	}
}

void CLogTabHostWnd::CloseTabsToTheRight(int index)
{
	for (int i = index + 1; i < static_cast<int>(m_tabs.size()); ++i)
		CloseTab(i);
}

void CLogTabHostWnd::CheckTabs()
{
	// remove tabs of log dialogs which went away without unregistering (e.g., crashed)
	for (int i = static_cast<int>(m_tabs.size()) - 1; i >= 0; --i)
	{
		if (::IsWindow(m_tabs[i].hWnd) && ::GetParent(m_tabs[i].hWnd) == m_hWnd)
			continue;
		RemoveTab(i);
		if (!::IsWindow(GetSafeHwnd()))
			return;
	}
	// a log dialog might have changed its title
	UpdateTitle();
}

void CLogTabHostWnd::LayoutActiveTab(bool bSynchronous, bool bShow)
{
	if (m_activeTab < 0 || m_activeTab >= static_cast<int>(m_tabs.size()))
		return;
	const CRect rc = GetContentRect();
	// asynchronously while resizing, in order not to block on log dialogs which are busy
	::SetWindowPos(m_tabs[m_activeTab].hWnd, HWND_TOP, rc.left, rc.top, rc.Width(), rc.Height(), SWP_NOACTIVATE | (bShow ? SWP_SHOWWINDOW : 0) | (bSynchronous ? 0 : SWP_ASYNCWINDOWPOS));
}

void CLogTabHostWnd::RememberFocus()
{
	if (m_activeTab < 0 || m_activeTab >= static_cast<int>(m_tabs.size()))
		return;
	// the input queues of the host and the log dialogs are attached, so we can see their focus
	HWND hFocus = ::GetFocus();
	if (hFocus && ::IsChild(m_tabs[m_activeTab].hWnd, hFocus))
		m_tabs[m_activeTab].hLastFocus = hFocus;
}

void CLogTabHostWnd::FocusActiveTab()
{
	if (m_activeTab < 0 || m_activeTab >= static_cast<int>(m_tabs.size()))
		return;
	// let the log dialog set the focus itself, it belongs to another thread
	::PostMessage(m_tabs[m_activeTab].hWnd, WM_TGIT_LOGTAB, LogTabHost::CMD_FOCUS, reinterpret_cast<LPARAM>(m_tabs[m_activeTab].hLastFocus));
}

void CLogTabHostWnd::UpdateTitle()
{
	CString title;
	if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size()))
	{
		const int length = ::GetWindowTextLength(m_tabs[m_activeTab].hWnd);
		if (length > 0)
		{
			::GetWindowText(m_tabs[m_activeTab].hWnd, title.GetBuffer(length + 1), length + 1);
			title.ReleaseBuffer();
		}
	}
	if (title.IsEmpty())
		title = L"TortoiseGit";

	CString currentTitle;
	GetWindowText(currentTitle);
	if (currentTitle != title)
		SetWindowText(title);
}

void CLogTabHostWnd::OpenNewTab()
{
	CString defaultPath;
	if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size()))
		defaultPath = m_tabs[m_activeTab].path;

	CBrowseFolder browseFolder;
	browseFolder.m_style = BIF_EDITBOX | BIF_NEWDIALOGSTYLE | BIF_RETURNFSANCESTORS | BIF_RETURNONLYFSDIRS;
	browseFolder.SetInfo(L"Select a folder of a Git repository to show its log in a new tab");
	CString path;
	if (browseFolder.Show(GetSafeHwnd(), path, defaultPath) != CBrowseFolder::OK || path.IsEmpty())
		return;

	// the new log dialog registers itself as a new tab of this window
	CString sCmd;
	sCmd.Format(L"/command:log /path:\"%s\"", static_cast<LPCWSTR>(path));
	CCommonAppUtils::RunTortoiseGitProc(sCmd);
}

void CLogTabHostWnd::UpdateMetrics()
{
	const HWND hWnd = GetSafeHwnd();
	LOGFONT lf{};
	lf.lfHeight = -CDPIAware::Instance().PointsToPixels(hWnd, 9);
	lf.lfWeight = FW_NORMAL;
	lf.lfCharSet = DEFAULT_CHARSET;
	lf.lfQuality = CLEARTYPE_QUALITY;
	wcscpy_s(lf.lfFaceName, L"Segoe UI");
	m_font.DeleteObject();
	m_font.CreateFontIndirect(&lf);

	CClientDC dc(this);
	CFont* pOldFont = dc.SelectObject(&m_font);
	TEXTMETRIC tm{};
	dc.GetTextMetrics(&tm);
	dc.SelectObject(pOldFont);

	m_stripHeight = tm.tmHeight + CDPIAware::Instance().ScaleY(hWnd, 14);
	m_tabTop = CDPIAware::Instance().ScaleY(hWnd, 4);
	m_tabPadding = CDPIAware::Instance().ScaleX(hWnd, 10);
	m_closeButtonSize = CDPIAware::Instance().ScaleX(hWnd, 16);
	m_minTabWidth = CDPIAware::Instance().ScaleX(hWnd, 90);
	m_maxTabWidth = CDPIAware::Instance().ScaleX(hWnd, 220);
	m_newTabButtonWidth = CDPIAware::Instance().ScaleX(hWnd, 28);
	m_margin = CDPIAware::Instance().ScaleX(hWnd, 4);
}

void CLogTabHostWnd::SetTheme(bool bDark)
{
	m_bDark = bDark;
	const HWND hWnd = GetSafeHwnd();
	if (bDark)
		DarkModeHelper::Instance().AllowDarkModeForApp(TRUE);
	DarkModeHelper::Instance().AllowDarkModeForWindow(hWnd, bDark ? TRUE : FALSE);
	BOOL darkFlag = bDark ? TRUE : FALSE;
	DarkModeHelper::WINDOWCOMPOSITIONATTRIBDATA data = { DarkModeHelper::WINDOWCOMPOSITIONATTRIB::WCA_USEDARKMODECOLORS, &darkFlag, sizeof(darkFlag) };
	DarkModeHelper::Instance().SetWindowCompositionAttribute(hWnd, &data);
	DarkModeHelper::Instance().FlushMenuThemes();
	DarkModeHelper::Instance().RefreshImmersiveColorPolicyState();
	if (!bDark)
		DarkModeHelper::Instance().AllowDarkModeForApp(FALSE);
	BOOL dark = darkFlag;
	DwmSetWindowAttribute(hWnd, 19, &dark, sizeof(dark));
	DwmSetWindowAttribute(hWnd, 20, &dark, sizeof(dark));
	::RedrawWindow(hWnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_ERASE | RDW_INTERNALPAINT);
}

CLogTabHostWnd::TabColors CLogTabHostWnd::GetColors() const
{
	TabColors colors{};
	if (m_bDark)
	{
		// the active tab has the background color of the (dark) log dialog
		colors.active = CTheme::darkBkColor;
		colors.strip = BlendColor(CTheme::darkBkColor, RGB(0, 0, 0), 50);
		colors.hover = BlendColor(CTheme::darkBkColor, CTheme::darkBkHotColor, 50);
		colors.text = CTheme::darkTextColor;
		colors.inactiveText = BlendColor(CTheme::darkTextColor, CTheme::darkBkColor, 30);
		colors.separator = CTheme::darkBkHotColor;
	}
	else
	{
		colors.active = ::GetSysColor(COLOR_3DFACE);
		colors.strip = BlendColor(colors.active, ::GetSysColor(COLOR_3DSHADOW), 30);
		colors.hover = BlendColor(colors.active, colors.strip, 50);
		colors.text = ::GetSysColor(COLOR_BTNTEXT);
		colors.inactiveText = BlendColor(colors.text, colors.strip, 30);
		colors.separator = ::GetSysColor(COLOR_3DSHADOW);
	}
	colors.accent = ::GetSysColor(COLOR_HIGHLIGHT);
	return colors;
}

CRect CLogTabHostWnd::GetContentRect() const
{
	CRect rc;
	GetClientRect(&rc);
	rc.top = std::min(rc.bottom, static_cast<LONG>(m_stripHeight));
	return rc;
}

int CLogTabHostWnd::GetTabWidth() const
{
	if (m_tabs.empty())
		return 0;
	CRect rc;
	GetClientRect(&rc);
	const int available = rc.Width() - 2 * m_margin - m_newTabButtonWidth;
	return std::clamp(available / static_cast<int>(m_tabs.size()), m_minTabWidth, m_maxTabWidth);
}

CRect CLogTabHostWnd::GetTabRect(int index) const
{
	const int width = GetTabWidth();
	return CRect(m_margin + index * width, m_tabTop, m_margin + (index + 1) * width, m_stripHeight);
}

CRect CLogTabHostWnd::GetCloseButtonRect(const CRect& rcTab) const
{
	const int right = rcTab.right - m_tabPadding / 2;
	const int top = rcTab.top + (rcTab.Height() - m_closeButtonSize) / 2;
	return CRect(right - m_closeButtonSize, top, right, top + m_closeButtonSize);
}

CRect CLogTabHostWnd::GetNewTabButtonRect() const
{
	CRect rcClient;
	GetClientRect(&rcClient);
	// keep the button visible, even if there are too many tabs
	const int left = std::min(m_margin + static_cast<int>(m_tabs.size()) * GetTabWidth(), std::max(0, static_cast<int>(rcClient.right) - m_margin - m_newTabButtonWidth));
	return CRect(left, m_tabTop, left + m_newTabButtonWidth, m_stripHeight);
}

int CLogTabHostWnd::HitTestTab(CPoint point, bool* pOnCloseButton) const
{
	if (pOnCloseButton)
		*pOnCloseButton = false;
	if (GetNewTabButtonRect().PtInRect(point))
		return -1;
	for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i)
	{
		const CRect rcTab = GetTabRect(i);
		if (!rcTab.PtInRect(point))
			continue;
		if (pOnCloseButton)
			*pOnCloseButton = !!GetCloseButtonRect(rcTab).PtInRect(point);
		return i;
	}
	return -1;
}

void CLogTabHostWnd::DrawTabStrip(CDC& dc, const CRect& rcStrip)
{
	const TabColors colors = GetColors();
	dc.FillSolidRect(&rcStrip, colors.strip);

	CFont* pOldFont = dc.SelectObject(&m_font);
	dc.SetBkMode(TRANSPARENT);
	const int lineWidth = std::max(1, CDPIAware::Instance().ScaleX(GetSafeHwnd(), 1));
	const float penWidth = CDPIAware::Instance().ScaleX(GetSafeHwnd(), 12) / 10.0f;
	const int count = static_cast<int>(m_tabs.size());
	for (int i = 0; i < count; ++i)
	{
		const CRect rcTab = GetTabRect(i);
		const bool isActive = i == m_activeTab;
		const bool isHover = i == m_hoverTab;
		if (isActive)
		{
			dc.FillSolidRect(&rcTab, colors.active);
			dc.FillSolidRect(rcTab.left, rcTab.top, rcTab.Width(), 2 * lineWidth, colors.accent);
		}
		else
		{
			if (isHover)
				dc.FillSolidRect(&rcTab, colors.hover);
			// separator between two inactive tabs
			if (i + 1 < count && i + 1 != m_activeTab)
				dc.FillSolidRect(rcTab.right - lineWidth, rcTab.top + m_tabTop, lineWidth, rcTab.Height() - 2 * m_tabTop, colors.separator);
		}

		const CRect rcClose = GetCloseButtonRect(rcTab);
		CRect rcText(rcTab.left + m_tabPadding, rcTab.top, rcClose.left - m_tabPadding / 2, rcTab.bottom);
		dc.SetTextColor(isActive ? colors.text : colors.inactiveText);
		dc.DrawText(m_tabs[i].label, rcText, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX);

		if (isHover && m_bHoverClose)
			dc.FillSolidRect(&rcClose, isActive ? colors.hover : colors.separator);
		DrawCross(dc, rcClose, isActive || isHover ? colors.text : colors.inactiveText, penWidth);
	}

	const CRect rcNewTab = GetNewTabButtonRect();
	if (m_bHoverNewTab)
	{
		CRect rcHover(rcNewTab);
		rcHover.DeflateRect(lineWidth * 2, lineWidth * 2);
		dc.FillSolidRect(&rcHover, colors.hover);
	}
	DrawPlus(dc, rcNewTab, colors.text, penWidth);

	dc.SelectObject(pOldFont);
}

void CLogTabHostWnd::InvalidateTabStrip()
{
	CRect rc;
	GetClientRect(&rc);
	rc.bottom = std::min(rc.bottom, static_cast<LONG>(m_stripHeight));
	InvalidateRect(&rc, FALSE);
}

void CLogTabHostWnd::OnPaint()
{
	CPaintDC dc(this);
	CRect rcClient;
	GetClientRect(&rcClient);
	CRect rcStrip(rcClient);
	rcStrip.bottom = std::min(rcClient.bottom, static_cast<LONG>(m_stripHeight));

	CDC memDC;
	CBitmap bitmap;
	if (!rcStrip.IsRectEmpty() && memDC.CreateCompatibleDC(&dc) && bitmap.CreateCompatibleBitmap(&dc, rcStrip.Width(), rcStrip.Height()))
	{
		CBitmap* pOldBitmap = memDC.SelectObject(&bitmap);
		DrawTabStrip(memDC, rcStrip);
		dc.BitBlt(rcStrip.left, rcStrip.top, rcStrip.Width(), rcStrip.Height(), &memDC, 0, 0, SRCCOPY);
		memDC.SelectObject(pOldBitmap);
	}

	// normally covered by the active log dialog (WS_CLIPCHILDREN)
	const CRect rcContent = GetContentRect();
	if (!rcContent.IsRectEmpty())
		dc.FillSolidRect(&rcContent, GetColors().active);
}

BOOL CLogTabHostWnd::OnEraseBkgnd(CDC* /*pDC*/)
{
	// everything is drawn in OnPaint
	return TRUE;
}

void CLogTabHostWnd::OnSize(UINT nType, int cx, int cy)
{
	__super::OnSize(nType, cx, cy);
	if (nType == SIZE_MINIMIZED)
		return;
	LayoutActiveTab(false);
	// the width of the tabs depends on the window width
	InvalidateTabStrip();
}

void CLogTabHostWnd::OnExitSizeMove()
{
	__super::OnExitSizeMove();
	// e.g., the patch view of a log dialog sticks to the window
	if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size()))
		::PostMessage(m_tabs[m_activeTab].hWnd, WM_TGIT_LOGTAB, LogTabHost::CMD_HOSTMOVED, 0);
}

void CLogTabHostWnd::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	__super::OnGetMinMaxInfo(lpMMI);

	// the log dialogs cannot be made smaller than their initial size
	CSize minClientSize(CDPIAware::Instance().ScaleX(GetSafeHwnd(), 300), CDPIAware::Instance().ScaleY(GetSafeHwnd(), 200));
	for (const auto& tab : m_tabs)
	{
		minClientSize.cx = std::max(minClientSize.cx, tab.minSize.cx);
		minClientSize.cy = std::max(minClientSize.cy, tab.minSize.cy);
	}
	CRect rc(0, 0, minClientSize.cx, minClientSize.cy + m_stripHeight);
	::AdjustWindowRectEx(&rc, GetStyle(), FALSE, GetExStyle());
	lpMMI->ptMinTrackSize.x = std::max(lpMMI->ptMinTrackSize.x, static_cast<LONG>(rc.Width()));
	lpMMI->ptMinTrackSize.y = std::max(lpMMI->ptMinTrackSize.y, static_cast<LONG>(rc.Height()));
}

void CLogTabHostWnd::OnLButtonDown(UINT nFlags, CPoint point)
{
	__super::OnLButtonDown(nFlags, point);

	if (GetNewTabButtonRect().PtInRect(point))
	{
		OpenNewTab();
		return;
	}

	bool onCloseButton = false;
	const int index = HitTestTab(point, &onCloseButton);
	if (index < 0)
		return;
	if (onCloseButton)
		CloseTab(index);
	else
		ActivateTab(index);
}

void CLogTabHostWnd::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	__super::OnLButtonDblClk(nFlags, point);

	// double click on the free area of the tab strip opens a new tab
	// (do not close tabs here: the second click of a double click could hit the next close button)
	if (point.y < m_stripHeight && HitTestTab(point, nullptr) < 0 && !GetNewTabButtonRect().PtInRect(point))
		OpenNewTab();
}

void CLogTabHostWnd::OnMButtonUp(UINT nFlags, CPoint point)
{
	__super::OnMButtonUp(nFlags, point);

	CloseTab(HitTestTab(point, nullptr));
}

void CLogTabHostWnd::OnMouseMove(UINT nFlags, CPoint point)
{
	__super::OnMouseMove(nFlags, point);

	if (!m_bTrackingMouse)
	{
		TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, m_hWnd, 0 };
		m_bTrackingMouse = !!::TrackMouseEvent(&tme);
	}

	bool onCloseButton = false;
	const int hoverTab = HitTestTab(point, &onCloseButton);
	const bool hoverNewTab = !!GetNewTabButtonRect().PtInRect(point);
	if (hoverTab == m_hoverTab && onCloseButton == m_bHoverClose && hoverNewTab == m_bHoverNewTab)
		return;
	m_hoverTab = hoverTab;
	m_bHoverClose = onCloseButton;
	m_bHoverNewTab = hoverNewTab;
	InvalidateTabStrip();
}

void CLogTabHostWnd::OnMouseLeave()
{
	__super::OnMouseLeave();

	m_bTrackingMouse = false;
	m_hoverTab = -1;
	m_bHoverClose = false;
	m_bHoverNewTab = false;
	InvalidateTabStrip();
}

void CLogTabHostWnd::OnContextMenu(CWnd* pWnd, CPoint point)
{
	// log dialogs pass unhandled context menu requests to their parent
	if (pWnd && pWnd->GetSafeHwnd() != m_hWnd)
		return;

	int index = m_activeTab;
	if (point.x == -1 && point.y == -1)
	{
		// opened with the keyboard
		const CRect rcTab = index >= 0 ? GetTabRect(index) : GetNewTabButtonRect();
		point = CPoint(rcTab.left, rcTab.bottom);
		ClientToScreen(&point);
	}
	else
	{
		CPoint clientPoint(point);
		ScreenToClient(&clientPoint);
		if (clientPoint.y >= m_stripHeight)
			return;
		index = HitTestTab(clientPoint, nullptr);
	}

	CMenu popup;
	if (!popup.CreatePopupMenu())
		return;
	const int count = static_cast<int>(m_tabs.size());
	if (index >= 0)
	{
		popup.AppendMenu(MF_STRING, ID_CLOSETAB, L"&Close tab\tCtrl+W");
		popup.AppendMenu(MF_STRING | (count > 1 ? MF_ENABLED : MF_GRAYED), ID_CLOSEOTHERTABS, L"Close &other tabs");
		popup.AppendMenu(MF_STRING | (index + 1 < count ? MF_ENABLED : MF_GRAYED), ID_CLOSETABSRIGHT, L"Close tabs to the &right");
		popup.AppendMenu(MF_SEPARATOR);
	}
	popup.AppendMenu(MF_STRING, ID_NEWTAB, L"&Open log of another repository...");

	switch (popup.TrackPopupMenu(TPM_RETURNCMD | TPM_LEFTALIGN | TPM_NONOTIFY | TPM_RIGHTBUTTON, point.x, point.y, this))
	{
	case ID_CLOSETAB:
		CloseTab(index);
		break;
	case ID_CLOSEOTHERTABS:
		CloseOtherTabs(index);
		break;
	case ID_CLOSETABSRIGHT:
		CloseTabsToTheRight(index);
		break;
	case ID_NEWTAB:
		OpenNewTab();
		break;
	default:
		break;
	}
}

void CLogTabHostWnd::OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized)
{
	if (nState == WA_INACTIVE)
		RememberFocus();
	else if (!bMinimized && m_activeTab >= 0)
	{
		// do not let DefWindowProc set the focus to the host window
		FocusActiveTab();
		return;
	}
	__super::OnActivate(nState, pWndOther, bMinimized);
}

void CLogTabHostWnd::OnSetFocus(CWnd* pOldWnd)
{
	__super::OnSetFocus(pOldWnd);
	FocusActiveTab();
}

void CLogTabHostWnd::OnClose()
{
	if (m_tabs.empty())
	{
		__super::OnClose();
		return;
	}
	if (m_bClosing)
		return;

	// close all log dialogs, the window gets destroyed as soon as the last one unregistered
	m_bClosing = true;
	::SetProp(m_hWnd, LogTabHost::HostClosingProp, m_hWnd);
	SavePlacement();
	ShowWindow(SW_HIDE);
	for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i)
		CloseTab(i);
	SetTimer(TIMER_CLOSING, 10000, nullptr);
}

void CLogTabHostWnd::OnDestroy()
{
	if (!m_bPlacementSaved && IsWindowVisible())
		SavePlacement();
	::RemoveProp(m_hWnd, LogTabHost::HostClosingProp);
	KillTimer(TIMER_CHECKTABS);
	KillTimer(TIMER_STARTUP);
	KillTimer(TIMER_CLOSING);

	__super::OnDestroy();

	if (ContinueModal())
		EndModalLoop(0);
}

void CLogTabHostWnd::OnTimer(UINT_PTR nIDEvent)
{
	switch (nIDEvent)
	{
	case TIMER_CHECKTABS:
		CheckTabs();
		break;
	case TIMER_STARTUP:
		KillTimer(TIMER_STARTUP);
		if (m_tabs.empty())
			DestroyWindow();
		break;
	case TIMER_CLOSING:
		// some log dialogs did not close in time
		KillTimer(TIMER_CLOSING);
		DestroyWindow();
		break;
	default:
		__super::OnTimer(nIDEvent);
		break;
	}
}

void CLogTabHostWnd::OnSysColorChange()
{
	__super::OnSysColorChange();
	CTheme::Instance().OnSysColorChanged();
	SetTheme(CTheme::Instance().IsDarkTheme());
}

LRESULT CLogTabHostWnd::OnDPIChanged(WPARAM /*wParam*/, LPARAM lParam)
{
	CDPIAware::Instance().Invalidate();
	UpdateMetrics();
	const auto rect = reinterpret_cast<const RECT*>(lParam);
	SetWindowPos(nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
	Invalidate();
	return 0;
}

CString CLogTabHostWnd::GetPlacementId(LPCWSTR name)
{
	// same format as ResizableLib uses for the dialogs
	CString id(name);
	id += L'_';
	id += GetMonitorSetupHash().c_str();
	id += L"WindowPlacement";
	return id;
}

bool CLogTabHostWnd::LoadPlacement(LPCWSTR name, WINDOWPLACEMENT& wp)
{
	const CString data = AfxGetApp()->GetProfileString(L"ResizableState", GetPlacementId(name));
	if (data.IsEmpty())
		return false;

	int left = 0, top = 0, right = 0, bottom = 0, showCmd = SW_SHOWNORMAL, flags = 0, minX = 0, minY = 0;
	if (swscanf_s(data, L"%d,%d,%d,%d,%d,%d,%d,%d", &left, &top, &right, &bottom, &showCmd, &flags, &minX, &minY) != 8 || right <= left || bottom <= top)
		return false;

	wp.rcNormalPosition = { left, top, right, bottom };
	wp.showCmd = static_cast<UINT>(showCmd);
	return true;
}

void CLogTabHostWnd::RestorePlacement()
{
	// use the size of the tab host from last time, or the one of the standalone log dialog
	WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
	if (GetWindowPlacement(&wp) && (LoadPlacement(L"LogTabHost", wp) || LoadPlacement(L"LogDlg", wp)))
	{
		CDPIAware::Instance().ScaleWindowPlacement(GetSafeHwnd(), &wp);
		if (::MonitorFromRect(&wp.rcNormalPosition, MONITOR_DEFAULTTONULL))
		{
			m_nInitialShowCmd = wp.showCmd == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
			wp.showCmd = SW_HIDE;
			wp.flags = 0;
			SetWindowPlacement(&wp);
			return;
		}
	}

	POINT pt{};
	::GetCursorPos(&pt);
	MONITORINFO mi = { sizeof(MONITORINFO) };
	::GetMonitorInfo(::MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
	const CRect rcWork(mi.rcWork);
	const int width = std::min(rcWork.Width() * 4 / 5, CDPIAware::Instance().ScaleX(GetSafeHwnd(), 1200));
	const int height = std::min(rcWork.Height() * 4 / 5, CDPIAware::Instance().ScaleY(GetSafeHwnd(), 850));
	SetWindowPos(nullptr, rcWork.left + (rcWork.Width() - width) / 2, rcWork.top + (rcWork.Height() - height) / 2, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

void CLogTabHostWnd::SavePlacement()
{
	WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
	if (!GetWindowPlacement(&wp))
		return;
	const bool maximized = wp.showCmd == SW_SHOWMAXIMIZED || (wp.showCmd == SW_SHOWMINIMIZED && (wp.flags & WPF_RESTORETOMAXIMIZED));
	CDPIAware::Instance().UnscaleWindowPlacement(GetSafeHwnd(), &wp);
	const RECT& rc = wp.rcNormalPosition;
	CString data;
	data.Format(L"%d,%d,%d,%d,%d,%d,%d,%d", static_cast<int>(rc.left), static_cast<int>(rc.top), static_cast<int>(rc.right), static_cast<int>(rc.bottom), maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL, 0, 0, 0);
	AfxGetApp()->WriteProfileString(L"ResizableState", GetPlacementId(L"LogTabHost"), data);
	m_bPlacementSaved = true;
}

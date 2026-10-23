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
#include "LogTabHostCommand.h"
#include "LogTabHost.h"
#include "PathUtils.h"

bool LogTabHostCommand::Execute()
{
	// do not keep the directory of a repository locked while the host is running
	::SetCurrentDirectory(CPathUtils::GetAppDirectory());
	// same as for all other commands, so that (un)installers can detect a running TortoiseGitProc
	CAutoGeneralHandle TGitMutex = ::CreateMutex(nullptr, FALSE, L"TortoiseGitProc.exe");

	CLogTabHostWnd wnd;
	if (!wnd.CreateHostWindow())
		return false;

	theApp.m_pMainWnd = &wnd;
	// runs until the host window is destroyed (last tab closed)
	wnd.RunModalLoop(MLF_NOIDLEMSG | MLF_NOKICKIDLE);
	theApp.m_pMainWnd = nullptr;
	return true;
}

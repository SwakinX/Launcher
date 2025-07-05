#define _CRT_SECURE_NO_WARNINGS
#pragma execution_character_set("utf-8")

#include <iostream>
#include <filesystem>
#include <Windows.h>
#include "INIReader.h"
#include <shellapi.h>
#include <vector>
#include <sstream>
#include <algorithm>
#include <string>
#include <tlhelp32.h> // 用于进程快照

namespace VersionUtils {
    // 验证版本号格式是否正确
    bool IsValidVersion(const std::wstring& versionStr) {
        if (versionStr.empty()) return false;
        for (wchar_t c : versionStr) {
            if (!iswdigit(c) && c != L'.') {
                return false;
            }
        }
        return true;
    }

    // 将版本号字符串转换为可比较的整数数组
    std::vector<int> ParseVersion(const std::wstring& versionStr) {
        std::vector<int> versionParts;
        if (!IsValidVersion(versionStr)) {
            return versionParts;
        }

        std::wistringstream iss(versionStr);
        std::wstring part;
        while (std::getline(iss, part, L'.')) {
            try {
                versionParts.push_back(std::stoi(part));
            } catch (...) {
                versionParts.push_back(0);
            }
        }
        return versionParts;
    }

    // 比较两个版本号
    bool IsVersionLower(const std::wstring& versionA, const std::wstring& versionB) {
        if (!IsValidVersion(versionA) || !IsValidVersion(versionB)) {
            return false;
        }

        auto partsA = ParseVersion(versionA);
        auto partsB = ParseVersion(versionB);

        size_t maxLength = (std::max)(partsA.size(), partsB.size());
        for (size_t i = 0; i < maxLength; ++i) {
            int partA = (i < partsA.size()) ? partsA[i] : 0;
            int partB = (i < partsB.size()) ? partsB[i] : 0;
            
            if (partA < partB) return true;
            if (partA > partB) return false;
        }
        return false;
    }
}

#pragma comment(linker, "/subsystem:windows /entry:wmainCRTStartup")

using namespace std::filesystem;
using namespace VersionUtils;


HANDLE stdOut;


void Log(std::wstring output)
{
	if (stdOut)
	{
		SYSTEMTIME time;
		GetSystemTime(&time);
		std::wstring timeStr = std::format(L"[{:02}:{:02}:{:02}.{:03}] ", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
		WriteConsole(stdOut, timeStr.c_str(), timeStr.length(), NULL, NULL);
		WriteConsole(stdOut, output.c_str(), output.length(), NULL, NULL);
		WriteConsole(stdOut, L"\r\n", 2, NULL, NULL);
	}
}

void RunAsAdmin(const std::wstring& exePath, const std::wstring& args)
{
	SHELLEXECUTEINFO sei = { sizeof(sei) };
	sei.lpVerb = L"runas"; // 请求管理员权限
	sei.lpFile = exePath.c_str();
	sei.lpParameters = args.c_str();
	sei.nShow = SW_SHOWNORMAL;

	if (!ShellExecuteEx(&sei))
	{
		DWORD error = GetLastError();
		Log(L"Failed to run as admin: " + std::to_wstring(error));
	}
	else
	{
		Log(L"Process started with admin privileges");
	}
}
// 获取配置版本信息
std::wstring GetConfiguredVersion(const path& base_folder) {
	auto version_file = path(base_folder).append("version.ini");
	if (exists(version_file)) {
		INIReader ini(version_file.string());
		if (ini.ParseError() == 0) {
			auto exe_path = ini.Get("", "exe_path", "");
			if (!exe_path.empty()) {
				auto target_exe = path(base_folder).append(exe_path);
				if (exists(target_exe)) {
					auto folder_name = path(target_exe).parent_path().filename().wstring();
					if (folder_name.starts_with(L"app-")) {
						return folder_name.substr(4);
					}
				}
				return target_exe.wstring();
			}
		}
	}
	return L"";
}
#include <windows.h>

// 获取进程的主窗口句柄
HWND GetMainWindowHandle(DWORD processId) {
	HWND hWnd = GetTopWindow(NULL);
	while (hWnd) {
		DWORD windowProcessId = 0;
		GetWindowThreadProcessId(hWnd, &windowProcessId);
		if (windowProcessId == processId && IsWindowVisible(hWnd)) {
			return hWnd; // 找到主窗口
		}
		hWnd = GetNextWindow(hWnd, GW_HWNDNEXT);
	}
	return NULL; // 未找到主窗口
}

// 单实例检测
bool IsAlreadyRunning()
{
	// 创建进程快照
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnapshot != INVALID_HANDLE_VALUE) {
		PROCESSENTRY32 pe;
		pe.dwSize = sizeof(PROCESSENTRY32);

		// 遍历进程列表
		if (Process32First(hSnapshot, &pe)) {
			do {
				// 检查是否有目标程序的进程
				if (_wcsicmp(pe.szExeFile, L"MoonCancer.exe") == 0) { // 替换为目标启动器的实际可执行文件名
					// 获取主窗口句柄
					HWND hWnd = GetMainWindowHandle(pe.th32ProcessID);
					if (hWnd) {
						if (IsIconic(hWnd)) {
							ShowWindow(hWnd, SW_RESTORE); // 如果窗口最小化，则还原
						}
						SetForegroundWindow(hWnd); // 将窗口置于前台
					}
					else {
						Log(L"Failed to find the main window for the process.");
					}
					CloseHandle(hSnapshot);
					return true;
				}
			} while (Process32Next(hSnapshot, &pe));
		}
		CloseHandle(hSnapshot);
	}
	return false;
}


int wmain(int argc, wchar_t* argv[])
{
	// 单实例检查
	if (IsAlreadyRunning()) {
		return 0;
	}

	for (size_t i = 0; i < argc; i++)
	{
		if (!wcscmp(argv[i], L"--trace"))
		{
			AllocConsole();
		}
	}

	stdOut = GetStdHandle(STD_OUTPUT_HANDLE);



	std::wstring run_exe;
	auto base_folder = path(argv[0]).parent_path();
	auto target_version = GetConfiguredVersion(base_folder);

	// 执行配置指定的版本
	if (!target_version.empty()) {
		for (auto folder : directory_iterator(base_folder)) {
			if (folder.is_directory()) {
				auto folder_name = folder.path().filename().wstring();
				if (folder_name.starts_with(L"app-")) {
					auto current_version = folder_name.substr(4);
					if (current_version == target_version) {
						auto exe = path(folder).append(L"MoonCancer.exe");
						if (exists(exe)) {
							run_exe = exe.wstring();
							break;
						}
					}
				}
			}
		}
	}

	// 回退机制：如果没有找到配置版本，查找最新版本
	if (run_exe.empty())
	{
		path target_exe;
		file_time_type last_time;
		for (auto folder : directory_iterator(base_folder))
		{
			if (folder.is_directory())
			{
				auto exe = path(folder).append(L"MoonCancer.exe");
				if (exists(exe))
				{
					auto time = last_write_time(exe);
					if (time > last_time)
					{
						target_exe = exe;
						last_time = time;
					}
				}
			}
		}
		run_exe = target_exe.wstring();
	}

	Log(L"run_exe: " + run_exe);

	if (run_exe.length())
	{
		std::wstring arg = std::wstring(GetCommandLine()).substr(std::wstring(argv[0]).length() + 2);
		Log(L"arg: " + arg);
		Log(L"Starting process with admin privileges");
		RunAsAdmin(run_exe, arg);

		// 从exe_path提取配置版本号
		std::wstring config_version;
		if (run_exe.length()) {
			auto folder_name = path(run_exe).parent_path().filename().wstring();
			if (folder_name.starts_with(L"app-")) {
				config_version = folder_name.substr(4);
			}
		}

		// 清理低于配置版本的旧版本
		for (auto folder : directory_iterator(base_folder))
		{
			auto folder_name = folder.path().filename().wstring();
			if (folder.is_directory() && folder_name.starts_with(L"app-"))
			{
				auto version = folder_name.substr(4);
				if (config_version.length() && IsVersionLower(version, config_version)) {
					Log(std::format(L"Moving old version to recycle bin: {} ({} < {})",
						folder_name, version, config_version));

					// 准备双空终止路径字符串
					std::wstring path_str = folder.path().wstring() + L'\0';

					SHFILEOPSTRUCTW file_op = { 0 };
					file_op.wFunc = FO_DELETE;
					file_op.pFrom = path_str.c_str();
					file_op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;

					int result = SHFileOperationW(&file_op);
					if (result != 0) {
						Log(L"Failed to move to recycle bin: " + std::to_wstring(result));
					}
				}
			}
		}
	}
	else
	{
		Log(L"MoonCancer.exe not found");
		SetProcessDPIAware();
		auto ok = MessageBox(NULL, L"MoonCancer files not found.\r\nWould you like to download it now?\r\nhttps://github.com/SwakinX/MoonCancer", L"MoonCancer", MB_ICONWARNING | MB_OKCANCEL);
		if (ok == IDOK)
		{
			ShellExecute(NULL, NULL, L"https://github.com/SwakinX/MoonCancer", NULL, NULL, SW_SHOWNORMAL);
		}
	}

	if (stdOut)
	{
		Log(L"Wait for 10s to exit...");
		Sleep(10000);
	}
}


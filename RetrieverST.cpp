#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <csignal>
#include <tchar.h>
#include <windows.h>
#include <aclapi.h>
#include <strsafe.h>
#include <wininet.h>
#include <stdexcept>
#include "RetrieverST.h"

#pragma comment(lib, "wininet.lib")

//using namespace std;

vector<wstring> existing_defaults;
vector<wstring> existing_archives;
vector<wstring> objects;
vector<bool> temp_ready;
vector<int> progress = { 0, 0 };
bool go = 1;

typedef struct {
	HWND       hWindow;
	int        nStatusList;
	HINTERNET hresource;
	char szMemo[512];
} REQUEST_CONTEXT;

class CSV {
	int GID;
	wstring region_name;
public:
	CSV() : GID(0), region_name(L"\0") {};
	~CSV() {}
	int set_GID(wstring gid);
	void set_name(wstring name) { region_name = name; }
	int get_GID() { return GID; }
	wstring get_name() { return region_name; }
	void download_self(wstring, wstring&, wstring);
	int plan_B(wstring, wstring&, wstring);
};

class CATALOGUE {
	wstring name;
	wstring year; 
	vector<CSV> pages;
	wstring default_url;
	wstring default_backup_url;
public:
	CATALOGUE(wstring the_year) : name(L"\0"), year(the_year), pages(NULL), default_url(L"\0"), default_backup_url(L"\0") {};
	CATALOGUE() : name(L"\0"), year(L"\0"), pages(NULL), default_url(L"\0"), default_backup_url(L"\0") {};
	~CATALOGUE() {}
	void set_name(wstring&, size_t);
	wstring get_name() { return name; }
	void set_year(wstring the_year) { year = the_year; }
	wstring get_year() { return year; }
	void make_folder(wstring&);
	bool check_default();
	void set_default_url(wstring&, wstring, wstring);
	wstring make_url(int);
	int check_named(wstring&, size_t);
	int make_CSV();
	void purge_CSVs();
	int set_CSV_gid(int, wstring&, int);
	void set_CSV_name(int, wstring&, int);
	vector<int> get_CSV_wishlist(wstring);
	void download_CSVs(wstring);
	int consistency_check();
	void graveyard(int);
	void archive();
};

// Hit ctrl+c to have the program stop downloading new files, and terminate when 
// the files currently downloading have finished. A graceful exit. 
inline void halt(int signum)
{
	go = 0;
	wcout << L"Aborting retrieval..." << endl;
}

// For a given function name, will retrieve the most current Windows error code and log 
// the code's English description in a text file. "err" will also terminate the application.
void err(wstring func)
{
	DWORD num = GetLastError();
	LPWSTR buffer = new WCHAR[512];
	wstring mod = L"wininet.dll";
	LPCWSTR modul = mod.c_str();
	DWORD buffer_length = FormatMessageW((FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_FROM_HMODULE), GetModuleHandleW(modul), num, 0, buffer, 512, NULL);
	DWORD a = GetLastError();
	wstring message(buffer, 512);
	delete[] buffer;
	wstring output = func + L" caused error " + to_wstring(num) + L": " + message;
	wcout << output << endl;
	exit(EXIT_FAILURE);
}
void warn(wstring func)
{
	DWORD num = GetLastError();
	LPWSTR buffer = new WCHAR[512];
	DWORD buffer_length = FormatMessageW((FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER), NULL, num, LANG_SYSTEM_DEFAULT, buffer, 0, NULL);
	wstring message(buffer, 512);
	delete[] buffer;
	wstring output = func + L" caused error " + to_wstring(num) + L": " + message;
	wcout << output << endl;
}

// Error outputs for caught exceptions. 
void inv_arg(wstring func, invalid_argument& ia)
{
	wcout << func << L" caused " << ia.what() << endl;
}

// Return a string that has been decoded from the input HTML string.
string decode_HTML(string html)
{
	string output(html);
	size_t pos1 = 0;
	while (pos1 < html.size())
	{
		pos1++;
		pos1 = output.find("&amp;", pos1);
		if (pos1 >= 0 && pos1 < html.size())
		{
			output.replace(pos1, 5, "&");
		}
	}
	return output;
}
wstring wdecode_HTML(wstring html)
{
	wstring output(html);
	size_t bbq = html.size();
	size_t pos1 = 0;
	while (pos1 >= 0 && pos1 < bbq)
	{
		pos1 = output.find(L"&amp;", pos1 + 1);
		if (pos1 >= 0 && pos1 < bbq)
		{
			output.replace(pos1, 5, L"&");
		}
	}
	return output;
}

// Removes troublesome characters from strings. 
wstring clean(wstring in)
{
	wstring out = in;
	size_t pos1 = 0;

	do
	{
		pos1 = out.find(L'|', pos1);
		if (pos1 < out.size())
		{
			out.replace(pos1, 1, L"or");
			pos1++;
		}
	} while (pos1 < out.size());

	return out;
}
wstring clean_B(wstring in)
{
	wstring out = in;
	for (int ii = 0; ii < in.size(); ii++)
	{
		if (in[ii] != L'\t')
		{
			out.erase(0, ii);
			break;
		}
	}
	for (int ii = out.size() - 1; ii >= 0; ii--)
	{
		if (out[ii] == L' ' || out[ii] == L'\r') { out.pop_back(); }
		else { break; }
	}
	for (int ii = out.size() - 1; ii >= 0; ii--)
	{
		if (out[ii] == L',') { out.erase(ii, 1); }
	}
	return out;
}

// Read into memory a (local .bin file / webpage).
wstring bin_memory(HANDLE& hfile)
{
	DWORD size = GetFileSize(hfile, NULL);
	DWORD bytes_read;
	LPWSTR buffer = new WCHAR[size / 2];
	if (!ReadFile(hfile, buffer, size, &bytes_read, NULL)) { err(L"ReadFile-bin_memory"); }
	wstring bin(buffer, size / 2);
	delete[] buffer;
	return bin;
}
wstring webpage_memory(HINTERNET& hrequest)
{
	BOOL yesno = 0;
	DWORD bytes_available;
	DWORD bytes_read = 0;
	LPSTR bufferA = new CHAR[1];
	LPWSTR bufferW = new WCHAR[1];
	int size1, size2;
	wstring webpage;

	do
	{
		bytes_available = 0;
		InternetQueryDataAvailable(hrequest, &bytes_available, 0, 0);
		bufferA = new CHAR[bytes_available + 1];
		ZeroMemory(bufferA, bytes_available + 1);
		if (!InternetReadFile(hrequest, bufferA, bytes_available, &bytes_read))
		{
			err(L"InternetReadFile");
		}
		size1 = MultiByteToWideChar(CP_UTF8, 0, bufferA, bytes_available, NULL, 0);
		bufferW = new WCHAR[size1];
		size2 = MultiByteToWideChar(CP_UTF8, 0, bufferA, bytes_available, bufferW, size1);
		webpage.append(bufferW, size1);
	} while (bytes_available > 0);
	delete[] bufferA;
	delete[] bufferW;

	return webpage;
}

// Open a file, and check for erroneous content (rather than data) within. 
// Return 0 = no error found, Return 1 = failed to open file, Return 2 = HTML error found. 
int file_consistency(wstring filename)
{
	HANDLE hfile = CreateFileW(filename.c_str(), (GENERIC_READ | GENERIC_WRITE), (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE), NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hfile == INVALID_HANDLE_VALUE) { warn(L"CreateFile-file_consistency"); return 1; }
	wstring wfile = bin_memory(hfile);
	if (!CloseHandle(hfile)) { warn(L"CloseHandle-file_consistency"); }
	size_t pos1 = wfile.find(L"<!DOCTYPE html>", 0);
	if (pos1 < wfile.size()) { return 2; }
	return 0;
};

// Given a full path name, delete the file/folder.
void delete_file(wstring filename)
{
	HANDLE hfile = CreateFileW(filename.c_str(), (GENERIC_READ | GENERIC_WRITE), (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE), NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hfile == INVALID_HANDLE_VALUE) { warn(L"CreateFile-delete_file"); }
	if (!DeleteFileW(filename.c_str())) { warn(L"DeleteFile-delete_file"); }
	if (!CloseHandle(hfile)) { warn(L"CloseHandle-delete_file"); }
}
void delete_folder(wstring folder_name)
{
	wstring folder_search = folder_name + L"\\*";
	WIN32_FIND_DATAW info;
	HANDLE hfile1 = FindFirstFileW(folder_search.c_str(), &info);
	wstring file_name;
	do
	{
		file_name = folder_name + L"\\" + info.cFileName;
		if (file_name.back() != L'.') { delete_file(file_name); }
	} while (FindNextFileW(hfile1, &info));
	
	BOOL yesno = RemoveDirectoryW(folder_name.c_str());
	if (yesno) { wcout << L"Succeeded in deleting folder " << folder_name << endl; }
	else { wcout << L"Failed to delete folder " << folder_name << endl; }
}

// Contains pre-programmed responses to certain automated events during server-client communications.
void CALLBACK call(HINTERNET hint, DWORD_PTR dw_context, DWORD dwInternetStatus, LPVOID status_info, DWORD status_info_length)
{
	REQUEST_CONTEXT* cpContext;
	cpContext = (REQUEST_CONTEXT*)status_info;
	string temp;
	wstring wtemp;
	bool yesno = 0;
	int count = 0;

	switch (dwInternetStatus)
	{
	case INTERNET_STATUS_CLOSING_CONNECTION:

		break;
	case INTERNET_STATUS_CONNECTED_TO_SERVER:

		break;
	case INTERNET_STATUS_CONNECTING_TO_SERVER:

		break;
	case INTERNET_STATUS_CONNECTION_CLOSED:

		break;
	case INTERNET_STATUS_HANDLE_CLOSING:

		break;
	case INTERNET_STATUS_HANDLE_CREATED:

		break;
	case INTERNET_STATUS_INTERMEDIATE_RESPONSE:

		break;
	case INTERNET_STATUS_NAME_RESOLVED:

		break;
	case INTERNET_STATUS_RECEIVING_RESPONSE:

		break;
	case INTERNET_STATUS_RESPONSE_RECEIVED:

		break;
	case INTERNET_STATUS_REDIRECT:
		for (int ii = 0; ii < 512; ii++)
		{
			if ((cpContext->szMemo)[ii] != '\0') { temp.push_back((cpContext->szMemo)[ii]); count = 0; }
			else { count++; }

			if (count > 2) { break; }
		}
		for (int ii = 0; ii < temp.size(); ii++)
		{
			if (temp[ii] == '/') { yesno = 1; }
			else if (temp[ii] < 0)
			{
				break;
			}
			if (yesno)
			{
				wtemp.push_back((wchar_t)temp[ii]);
			}
		}
		objects.push_back(wtemp);
		break;
	case INTERNET_STATUS_REQUEST_COMPLETE:

		break;
	case INTERNET_STATUS_REQUEST_SENT:

		break;
	case INTERNET_STATUS_RESOLVING_NAME:

		break;
	case INTERNET_STATUS_SENDING_REQUEST:

		break;
	case INTERNET_STATUS_STATE_CHANGE:

		break;
	}

}

// Given destination folder and filename, will download the file at the URL.
int download(wstring url, wstring folder, wstring filename)
{
	wstring filepath = folder + L"\\" + filename;
	wstring server_name;
	wstring object_name;
	size_t cut_here;
	for (int ii = 0; ii < domains.size(); ii++)
	{
		cut_here = url.rfind(domains[ii]);
		if (cut_here <= url.size())
		{
			server_name = url.substr(0, cut_here + domains[ii].size());
			object_name = url.substr(cut_here + domains[ii].size(), url.size() - cut_here - domains[ii].size());
			break;
		}
	}

	INTERNET_STATUS_CALLBACK InternetStatusCallback;
	DWORD context = 1;
	BOOL yesno = 0;
	wstring agent = L"downloader";
	HINTERNET hint = NULL;
	HINTERNET hconnect = NULL;
	HINTERNET hrequest = NULL;
	DWORD bytes_available;
	DWORD bytes_read = 0;
	LPSTR bufferA = new CHAR[1];
	LPWSTR bufferW = new WCHAR[1];
	int size1, size2;
	wstring file;
	DWORD ex_code;

	hint = InternetOpenW(agent.c_str(), INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
	if (hint)
	{
		InternetStatusCallback = InternetSetStatusCallback(hint, (INTERNET_STATUS_CALLBACK)call);
		hconnect = InternetConnectW(hint, server_name.c_str(), INTERNET_DEFAULT_HTTP_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, context);
	}
	else { warn(L"InternetOpen"); return 1; }
	if (hconnect)
	{
		hrequest = HttpOpenRequestW(hconnect, NULL, object_name.c_str(), NULL, NULL, NULL, 0, context);
	}
	else { warn(L"InternetConnect"); return 2; }
	if (hrequest)
	{
		yesno = HttpSendRequest(hrequest, NULL, 0, NULL, 0);
	}
	else { warn(L"HttpOpenRequest"); return 3; }
	if (yesno)
	{
		do
		{
			bytes_available = 0;
			InternetQueryDataAvailable(hrequest, &bytes_available, 0, 0);
			bufferA = new CHAR[bytes_available];
			if (!InternetReadFile(hrequest, (LPVOID)bufferA, bytes_available, &bytes_read))
			{
				warn(L"InternetReadFile");
				return 4;
			}
			size1 = MultiByteToWideChar(CP_UTF8, 0, bufferA, bytes_available, NULL, 0);
			bufferW = new WCHAR[size1];
			size2 = MultiByteToWideChar(CP_UTF8, 0, bufferA, bytes_available, bufferW, size1);
			file.append(bufferW, size1);
		} while (bytes_available > 0);
		delete[] bufferA;
		delete[] bufferW;
	}
	else { warn(L"HttpSendRequest"); return 5; }

	HANDLE hprinter = CreateFileW(filepath.c_str(), (GENERIC_READ | GENERIC_WRITE), (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE), NULL, CREATE_NEW, 0, NULL);
	if (hprinter == INVALID_HANDLE_VALUE) { warn(L"CreateFile"); return 6; }
	DWORD bytes_written;
	DWORD file_size = file.size() * 2;
	if (!WriteFile(hprinter, file.c_str(), file_size, &bytes_written, NULL)) { warn(L"WriteFile"); return 7; }

	if (hrequest) { InternetCloseHandle(hrequest); }
	if (hconnect) { InternetCloseHandle(hconnect); }
	if (hint) { InternetCloseHandle(hint); }
	return 0;
}

// Sort a vector of integers... quickly.
int partition(vector<int>& list, int low, int high)
{
	int pivot = list[high];
	int ii = low - 1;
	for (int jj = low; jj < high; jj++)
	{
		if (list[jj] < pivot)
		{
			ii++;
			swap(list[ii], list[jj]);
		}
	}
	swap(list[ii + 1], list[high]);
	return (ii + 1);
}
void quicksort(vector<int>& list, int low, int high)
{
	int pi;
	if (low < high)
	{
		pi = partition(list, low, high);
		quicksort(list, low, pi - 1);
		quicksort(list, pi + 1, high);
	}
}

// Return a vector of sorted integer GIDs already present in the given folder.
// Will also include GIDs listed in that catalogue's graveyard (do not download).
vector<int> gid_scanner(wstring folder)
{
	vector<int> list;
	wstring file_generic = folder + L"\\*.csv";
	wstring file_name, gid;
	size_t pos1, pos2;
	WIN32_FIND_DATAW info;
	HANDLE hfile = INVALID_HANDLE_VALUE;
	hfile = FindFirstFileW(file_generic.c_str(), &info);
	DWORD error = GetLastError();
	if (hfile == INVALID_HANDLE_VALUE && error != ERROR_FILE_NOT_FOUND) { warn(L"FindFirstFile-gid_scanner"); }
	else if (hfile == INVALID_HANDLE_VALUE) {}
	else
	{
		do
		{
			pos1 = 0;
			file_name.clear();
			while (info.cFileName[pos1] != L'\0')
			{
				file_name.push_back(info.cFileName[pos1]);
				pos1++;
			}
			if (file_name.size() == 0) { return list; }
			pos1 = file_name.find(L" (", 0);
			pos1 += 2;
			pos2 = file_name.find(L')', pos1);
			gid = file_name.substr(pos1, pos2 - pos1);
			try
			{
				list.push_back(stoi(gid));
			}
			catch (invalid_argument& ia)
			{
				inv_arg(gid + L" into gid_scanner", ia);
				continue;
			}
		} while (FindNextFileW(hfile, &info));
	}

	pos1 = folder.rfind(L'\\');
	wstring cata_name = folder.substr(pos1 + 1);
	pos2 = folder.rfind(L'\\', pos1 - 1);
	pos2++;
	wstring year = folder.substr(pos2, pos1 - pos2);
	wstring gy_filename = folder.substr(0, pos1);
	gy_filename += L"\\" + year + L".Graveyard\\" + cata_name + L".bin";
	wstring gy_list;
	HANDLE hfile2 = CreateFileW(gy_filename.c_str(), (GENERIC_READ | GENERIC_WRITE), (FILE_SHARE_READ | FILE_SHARE_WRITE), NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hfile2 == INVALID_HANDLE_VALUE) 
	{
		error = GetLastError();
		if (error != ERROR_FILE_NOT_FOUND) { err(L"CreateFile-gid_scanner"); }
	}
	else
	{
		gy_list = bin_memory(hfile2);
		pos1 = 0;
		while (pos1 < gy_list.size())
		{
			pos2 = gy_list.find(L'\r', pos1);
			gid = gy_list.substr(pos1, pos2 - pos1);
			try
			{
				list.push_back(stoi(gid));
			}
			catch (invalid_argument& ia)
			{
				inv_arg(gid + L" into gid_scanner", ia);
			}
			pos1 = gy_list.find_first_of(L"1234567890", pos2);
		}
		CloseHandle(hfile2);
	}
	
	quicksort(list, 0, list.size() - 1);
	return list;
}

// Return TRUE or FALSE as to whether a given GID is already included in the given list.
bool gid_check(int GID, vector<int>& list)
{
	if (list.size() == 0) { return 0; }
	int pos1 = GID - list[0];
	int pos2 = list[list.size() - 1] - GID;
	if (pos1 < 0 || pos2 < 0) { return 0; }
	if (pos1 < pos2)
	{
		for (int ii = 0; ii < list.size(); ii++)
		{
			if (list[ii] == GID) { return 1; }
		}
	}
	else
	{
		for (int ii = list.size() - 1; ii >= 0; ii--)
		{
			if (list[ii] == GID) { return 1; }
		}
	}
	return 0;
}

// Crude DOS progress bar.
void download_percentage(wstring cata_name)
{
	int percentage;
	progress[0]++;
	percentage = 100 * progress[0] / progress[1];
	wcout << L"Downloaded " << cata_name << L", year total is " << percentage << L"% complete." << endl;
}

// For a given folder, locate all the files of size 0 bytes and delete them. 
// Returns the number of such files deleted.
int remove_blank(wstring folder)
{
	int count = 0;
	wstring folder_search = folder + L"\\*";
	WIN32_FIND_DATAW info;
	HANDLE hfile1 = FindFirstFileW(folder_search.c_str(), &info);
	HANDLE hfile2 = INVALID_HANDLE_VALUE;
	wstring file_name;
	LARGE_INTEGER size;
	size.QuadPart = 1;
	do
	{
		file_name = folder + L"\\" + info.cFileName;
		if (file_name.back() == L'.') { continue; }
		hfile2 = CreateFileW(file_name.c_str(), 0, FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hfile2 == INVALID_HANDLE_VALUE) { warn(L"CreateFile-remove_blank"); }
		if (!GetFileSizeEx(hfile2, &size)) { warn(L"GetFileSize-remove_blank"); }
		if (size.QuadPart == 0)
		{
			if (!DeleteFileW(file_name.c_str())) { warn(L"DeleteFile-remove_blank"); }
			if (!CloseHandle(hfile2)) { warn(L"CloseHandle-remove_blank"); }
			count++;
		}
	} while (FindNextFileW(hfile1, &info));
	return count;
}

// For a given folder, locate all the files with purely numerical names and delete them. 
// Returns the number of such files deleted.
int remove_numerical(wstring folder)
{
	int count;
	wstring folder_search = folder + L"\\*";
	WIN32_FIND_DATAW info;
	HANDLE hfile = FindFirstFileW(folder_search.c_str(), &info);
	wstring file_name, temp1;
	size_t pos1, pos2;
	vector<wstring> death_row;
	
	do
	{
		count = 0;
		file_name = folder + L"\\" + info.cFileName;
		if (file_name.back() == L'.') { continue; }
		pos1 = file_name.find(L')', 0);
		pos1 += 2;
		pos2 = file_name.rfind(L'.', file_name.size() - 1);
		temp1 = file_name.substr(pos1, pos2 - pos1);
		pos1 = 0;
		while (pos1 < temp1.size())
		{
			pos2 = temp1.find_first_of(L"1234567890", pos1);
			if (pos2 < temp1.size())
			{
				count++;
				pos1 = pos2 + 1;
			}
			else { break; }
		}
		if (count > 3) { death_row.push_back(file_name); }

	} while (FindNextFileW(hfile, &info));
	if (!FindClose(hfile)) { warn(L"CloseHandle-remove_numerical"); }

	count = 0;
	for (int ii = 0; ii < death_row.size(); ii++)
	{
		delete_file(death_row[ii]);
		count++;
	}

	return count;
}

// For a given year, delete all the saved catalogue folders that did not pass the criteria filters. 
void remove_folder(wstring year, vector<CATALOGUE>& data_map)
{
	wstring year_folder = local_directory + L"\\" + year;
	wstring year_folder_search = year_folder + L"\\*";
	WIN32_FIND_DATAW info;
	HANDLE hfile1 = FindFirstFileW(year_folder_search.c_str(), &info);
	HANDLE hfile2 = INVALID_HANDLE_VALUE;
	wstring cata_folder;
	size_t pos1;
	vector<wstring> existing_cata_folders;
	vector<wstring> existing_cata_names;
	int dead_numbers = 0;

	do
	{
		cata_folder = year_folder + L"\\" + info.cFileName;
		pos1 = cata_folder.find(L'.', 0);
		if (pos1 < cata_folder.size()) { continue; }
		existing_cata_folders.push_back(cata_folder);
		existing_cata_names.push_back(info.cFileName);
	} while (FindNextFileW(hfile1, &info));

	wstring target_name;
	for (int ii = 0; ii < data_map.size(); ii++)
	{
		target_name = data_map[ii].get_name();
		for (int jj = existing_cata_names.size() - 1; jj >= 0; jj--)
		{
			if (target_name == existing_cata_names[jj])
			{
				dead_numbers = remove_numerical(existing_cata_folders[jj]);
				if (dead_numbers) { wcout << L"Removed " << dead_numbers << L" numerically-named .csv files from catalogue " << data_map[ii].get_name() << L" in " << year << L"." << endl; }

				existing_cata_folders.erase(existing_cata_folders.begin() + jj);
				existing_cata_names.erase(existing_cata_names.begin() + jj);
				break;
			}
		}
	}

	for (int ii = 0; ii < existing_cata_folders.size(); ii++)
	{
		delete_folder(existing_cata_folders[ii]);
	}
}

// CSV object functions.
void CSV::download_self(wstring folder, wstring& default_url, wstring catalogue_name)
{
	wstring filename = catalogue_name + L" (" + to_wstring(GID) + L") " + region_name + L".csv";
	wstring url = default_url;
	size_t pos1 = url.find(L"GID=", 0);
	pos1 += 4;
	size_t pos2 = url.find(L"&", pos1);
	url.replace(pos1, pos2 - pos1, to_wstring(GID));
	int error = download(url, folder, filename);
	if (error) { warn(L"download_self for GID " + to_wstring(GID)); }
}
int CSV::plan_B(wstring year, wstring& db_url, wstring catalogue_name)
{
	wstring filename = local_directory + L"\\" + year + L"\\" + catalogue_name + L"\\" + catalogue_name + L" (" + to_wstring(GID) + L") " + region_name + L".csv";
	wstring url = db_url;
	size_t pos1 = url.find(L"GID=", 0);
	pos1 += 4;
	size_t pos2 = url.find(L"&", pos1);
	url.replace(pos1, pos2 - pos1, to_wstring(GID));

	wstring agent = L"plan_B";
	pos1 = url.find(L'/', 0);
	wstring server_name = url.substr(0, pos1);
	wstring object_name = url.substr(pos1, url.size() - pos1);
	HINTERNET hconnect = NULL;
	HINTERNET hrequest = NULL;
	BOOL yesno;
	DWORD bytes_available, bytes_read;
	LPSTR bufferA = new CHAR[1];
	LPWSTR bufferW = new WCHAR[1];
	wstring webpage;
	int size1, size2;

	HINTERNET hint = InternetOpenW(agent.c_str(), INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
	if (hint)
	{
		hconnect = InternetConnectW(hint, server_name.c_str(), INTERNET_DEFAULT_HTTP_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, NULL);
	}
	else { warn(L"InternetOpen"); return 1; }
	if (hconnect)
	{
		hrequest = HttpOpenRequestW(hconnect, NULL, object_name.c_str(), NULL, NULL, NULL, 0, NULL);
	}
	else { warn(L"InternetConnect"); return 2; }
	if (hrequest)
	{
		yesno = HttpSendRequest(hrequest, NULL, 0, NULL, 0);
	}
	else { warn(L"HttpOpenRequest"); return 3; }
	if (yesno)
	{
		do
		{
			bytes_available = 0;
			InternetQueryDataAvailable(hrequest, &bytes_available, 0, 0);
			bufferA = new CHAR[bytes_available];
			if (!InternetReadFile(hrequest, (LPVOID)bufferA, bytes_available, &bytes_read))
			{
				warn(L"InternetReadFile");
				return 4;
			}
			size1 = MultiByteToWideChar(CP_UTF8, 0, bufferA, bytes_available, NULL, 0);
			bufferW = new WCHAR[size1];
			size2 = MultiByteToWideChar(CP_UTF8, 0, bufferA, bytes_available, bufferW, size1);
			webpage.append(bufferW, size1);
		} while (bytes_available > 0);
		delete[] bufferA;
		delete[] bufferW;
	}
	else { warn(L"HttpSendRequest"); return 5; }

	vector<vector<wstring>> rows;      // Type-1 webpages have 1 column of data per CSV. Type-2 webpages
	vector<wstring> variable(2);       // have multiple column sub-headers, each one of which has its own
	vector<wstring> header_row;        // column of data that must be re-structured as its own column.
	int type;                          // Condensing 2D data into 1D is necessary for SQL organization,
	wstring temp1, temp2;              // so that each CSV file can have its own unique row.
	pos1 = webpage.find(L"<thead", 0);
	if (pos1 > webpage.size()) { return 8; }  // In this case, even the backup is offline. Graveyard. 
	pos2 = webpage.find(L'>', pos1);     
	size_t pos3 = webpage.find(L"style", pos1);  
	if (pos3 > pos1 && pos3 < pos2) { type = 1; }
	else { type = 2; }

	pos1 = webpage.find(L"id=\"tabulation-title", 0);
	pos1 = webpage.find(L"</span>", pos1);
	pos1 += 7;
	pos2 = webpage.find(L'<', pos1);
	pos2 = webpage.find_last_not_of(L' ', pos2 - 1);
	pos2++;
	wstring catalogue_title = webpage.substr(pos1, pos2 - pos1);

	wstring source_line;
	size_t pos_start, pos_end, pos_table_start, pos_table_end;
	switch (type)
	{
	case 1:
		pos1 = webpage.rfind(L"<tbody>", webpage.size() - 10);
		pos1 = webpage.find(L"class", pos1 + 1);
		pos_end = webpage.rfind(L"</tbody>", webpage.size() - 10);
		do
		{
			rows.push_back(vector<wstring>(2));
			pos1 = webpage.find(L'\n', pos1 + 1); 
			pos1++;
			pos2 = webpage.find(L'\n', pos1);
			temp1 = webpage.substr(pos1, pos2 - pos1);
			rows[rows.size() - 1][0] = clean_B(temp1);

			pos1 = webpage.find(L"<td", pos2);
			pos1 = webpage.find(L'\n', pos1);
			pos1++;
			pos2 = webpage.find(L'\n', pos1);
			temp1 = webpage.substr(pos1, pos2 - pos1);
			rows[rows.size() - 1][1] = clean_B(temp1);
			pos1 = webpage.find(L"class", pos2);

		} while (pos1 < pos_end);

		pos1 = webpage.rfind(L"Source:", webpage.size() - 10);
		pos1 = webpage.find(L'>', pos1);
		pos1++;
		pos2 = webpage.find(L'\n', pos1);
		source_line = webpage.substr(pos1, pos2 - pos1 - 1);
		break;

	case 2:
		pos_end = webpage.find(L"id=\"tabulation\"", 0);
		pos_start = webpage.rfind(L"div-d1", pos_end);
		pos1 = webpage.find(L"option", pos_start);
		if (pos1 > pos_start && pos1 < pos_end)  // Store the variable names if this file has variables. 
		{
			pos2 = webpage.rfind(L"title", pos1);
			pos1 = webpage.find(L'>', pos2);
			pos1++;
			pos2 = webpage.find(L'<', pos1);
			temp1 = webpage.substr(pos1, pos2 - pos1);
			variable[0] = clean_B(temp1);

			pos2 = webpage.find(L"\"selected\"", pos2);
			pos1 = webpage.find(L'>', pos2);
			pos1++;
			pos2 = webpage.find(L'<', pos1);
			temp1 = webpage.substr(pos1, pos2 - pos1);
			variable[1] = clean_B(temp1);
		}

		pos1 = webpage.find(L"<thead", 0);
		pos1 = webpage.find(L"col-0", pos1);
		pos1 = webpage.find(L'>', pos1);
		pos1++;
		pos2 = webpage.find(L'<', pos1);
		temp1 = webpage.substr(pos1, pos2 - pos1);
		header_row.push_back(clean_B(temp1));

		pos_start = webpage.find(L"<tr", pos2);
		pos_end = webpage.find(L"</tr", pos_start);
		pos1 = webpage.find(L"id", pos_start);
		do
		{
			pos1 = webpage.find(L'>', pos1);
			pos1++;
			pos2 = webpage.find(L'<', pos1);
			temp1 = webpage.substr(pos1, pos2 - pos1);
			header_row.push_back(clean_B(temp1));
			pos1 = webpage.find(L"id", pos2);
		} while (pos1 < pos_end && pos1 > pos_start);

		pos_table_end = webpage.rfind(L"</tbody>", webpage.size() - 10);
		pos_table_start = webpage.rfind(L"<tbody>", pos_table_end);
		pos_start = webpage.find(L"<tr>", pos_table_start);
		do
		{
			rows.push_back(vector<wstring>());
			size1 = 0;
			pos_end = webpage.find(L"</tr>", pos_start);

			pos1 = webpage.find(L"font", pos_start);
			pos1 += 4;
			pos2 = webpage.find(L"align", pos1);
			temp1 = webpage.substr(pos1, pos2 - pos1);
			pos3 = temp1.find(L"indent", 0);
			if (pos3 < temp1.size())
			{
				pos1 = temp1.find_first_of(L"1234567890");
				temp2 = temp1.substr(pos1, 1);
				try
				{
					size1 = stoi(temp2);
				}
				catch (invalid_argument& ia)
				{
					inv_arg(to_wstring(GID) + L" into plan_B ", ia);
					size1 = 0;
				}
			}

			pos1 = webpage.find(L"</th>", pos_start);
			pos2 = webpage.rfind(L'\n', pos1);
			pos1 = webpage.rfind(L'\t', pos2);
			pos1++;
			temp1.clear();
			if (size1 > 0)
			{
				for (int ii = 0; ii < size1; ii++) { temp1 += L"  "; }
			}
			temp1 += webpage.substr(pos1, pos2 - pos1);
			rows[rows.size() - 1].push_back(clean_B(temp1));

			pos3 = webpage.find(L"</td>", pos2);
			do
			{
				pos2 = webpage.rfind(L"\t ", pos3 - 2);
				pos2++;
				pos1 = webpage.find_first_not_of(L' ', pos2);
				pos2 = webpage.find(L' ', pos1);
				temp1 = webpage.substr(pos1, pos2 - pos1);
				rows[rows.size() - 1].push_back(clean_B(temp1));
				pos3 = webpage.find(L"</td>", pos3 + 1);
			} while (pos3 < pos_end && pos3 > pos_start);

			pos_start = webpage.find(L"<tr>", pos_end);
		} while (pos_start < pos_table_end && pos_start > pos_table_start);

		pos1 = webpage.rfind(L"Source:", webpage.size() - 10);
		pos1 = webpage.find(L'>', pos1);
		pos1++;
		pos2 = webpage.find(L'\n', pos1);
		source_line = webpage.substr(pos1, pos2 - pos1 - 1);
		break;
	}

	HANDLE hfile = CreateFileW(filename.c_str(), (GENERIC_READ | GENERIC_WRITE), (FILE_SHARE_READ | FILE_SHARE_WRITE), NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hfile == INVALID_HANDLE_VALUE) { warn(L"CreateFile-plan_B"); return 6; }
	DWORD bytes_written;
	wstring file = L"\"" + catalogue_title + L"\"\r\n\"Geography = " + region_name + L"\"\r\n";
	if (type == 2)
	{
		if (variable[0].size() > 0) { file += L"\"" + variable[0] + L" = " + variable[1] + L"\"\r\n"; }
		for (int ii = 0; ii < header_row.size(); ii++)
		{
			file += L"\"" + header_row[ii] + L"\",";
			if (ii == header_row.size() - 1) { file += L" \r\n"; }
		}
	}
	for (int ii = 0; ii < rows.size(); ii++)
	{
		file += L"\"" + rows[ii][0] + L"\"";
		for (int jj = 1; jj < rows[ii].size(); jj++)
		{
			file += L"," + rows[ii][jj];
		}
		file += L" \r\n";
	}
	file += L"\"Note\"\r\n";
	file += L"\"Source:" + source_line + L"\"\r\n";
	DWORD file_size = file.size() * 2;
	if (!WriteFile(hfile, file.c_str(), file_size, &bytes_written, NULL)) { warn(L"WriteFile-plan_B"); return 7; }
	if (!CloseHandle(hfile)) { warn(L"CloseHandle-plan_B"); return 9; }

	if (hrequest) { InternetCloseHandle(hrequest); }
	if (hconnect) { InternetCloseHandle(hconnect); }
	if (hint) { InternetCloseHandle(hint); }
	return 0;
}
int CSV::set_GID(wstring gid)
{
	try
	{
		GID = stoi(gid);
	}
	catch (invalid_argument& ia)
	{
		inv_arg(gid + L" into set_GID", ia);
		return 1;
	}
	return 0;
}

// CATALOGUE object functions.
void CATALOGUE::set_name(wstring& webpage, size_t pos1)
{
	size_t pos2 = webpage.find(L"Dataset ", pos1);
	pos2 += 8;
	size_t pos3 = webpage.find(L' ', pos2);
	wstring temp1 = webpage.substr(pos2, pos3 - pos2);
	pos2 = temp1.find(L'/', 0);
	if (pos2 < temp1.size()) { temp1.replace(pos2, 1, L"of"); }
	name = temp1;
}
void CATALOGUE::make_folder(wstring& year_folder)
{
	DWORD gle;
	wstring cata_folder = year_folder + L"\\" + name;
	BOOL error = CreateDirectoryW(cata_folder.c_str(), NULL);
	if (!error)
	{
		gle = GetLastError();
		if (gle != ERROR_ALREADY_EXISTS) { warn(L"CreateDirectory-make_folder"); }
	}
}
bool CATALOGUE::check_default()
{
	if (default_url.size() > 1) { return 1; }
	else { return 0; }
}
void CATALOGUE::set_default_url(wstring& webpage, wstring server, wstring object)
{
	size_t pos1 = webpage.rfind(L"OFT=CSV");
	pos1 += 7;
	size_t pos2 = webpage.rfind(L"File.cfm", pos1);
	wstring	temp1 = webpage.substr(pos2, pos1 - pos2);

	pos1 = object.rfind(L'/');
	wstring temp2 = object.substr(0, pos1 + 1);
	default_url = wdecode_HTML(server + temp2 + temp1);
	default_backup_url = server + object;

	/*
	bool found = 0;
	HANDLE hfile = INVALID_HANDLE_VALUE;
	HANDLE hfile2 = INVALID_HANDLE_VALUE;
	wstring file_name = local_directory + L"\\" + year + L"\\" + year + L" default URLs.bin";
	wstring backup_file_name = local_directory + L"\\" + year + L"\\" + year + L" default backup URLs.bin";
	DWORD pos;
	wstring bin_url;
	for (int ii = 0; ii < existing_defaults.size(); ii++)
	{
		if (existing_defaults[ii] == name)
		{
			found = 1;
			break;
		}
	}
	if (!found)
	{
		hfile = CreateFileW(file_name.c_str(), (GENERIC_READ | GENERIC_WRITE), (FILE_SHARE_READ | FILE_SHARE_WRITE), NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hfile == INVALID_HANDLE_VALUE) { err(L"CreateFile-set_default_url"); }
		pos = SetFilePointer(hfile, 0, NULL, FILE_END);
		bin_url = L"[" + name + L"] " + default_url + L"\r\n";
		if (!WriteFile(hfile, bin_url.c_str(), bin_url.size() * 2, &pos, NULL)) { err(L"WriteFile-set_default_url"); }

		hfile2 = CreateFileW(backup_file_name.c_str(), (GENERIC_READ | GENERIC_WRITE), (FILE_SHARE_READ | FILE_SHARE_WRITE), NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hfile2 == INVALID_HANDLE_VALUE) { err(L"CreateFile-set_default_url(backup)"); }
		pos = SetFilePointer(hfile2, 0, NULL, FILE_END);
		bin_url = L"[" + name + L"] " + default_backup_url + L"\r\n";
		if (!WriteFile(hfile2, bin_url.c_str(), bin_url.size() * 2, &pos, NULL)) { err(L"WriteFile-set_default_url(backup)"); }
	}
	if (hfile2) { CloseHandle(hfile2); }
	if (hfile) { CloseHandle(hfile); }
	*/
}
wstring CATALOGUE::make_url(int GID)
{
	size_t pos1, pos2;
	wstring url = default_url;
	pos1 = url.find(L"GID=", 0);
	pos1 += 4;
	pos2 = url.find(L'&', pos1 + 1);
	url.replace(pos1, pos2 - pos1, to_wstring(GID));
	return url;
}
int CATALOGUE::check_named(wstring& webpage, size_t pos1)
{
	size_t pos2 = webpage.find(L'>', pos1);
	pos2++;
	size_t pos3 = webpage.find(L'<', pos2);
	wstring region = webpage.substr(pos2, pos3 - pos2);
	int count = 0;
	pos2 = 0;
	while (pos2 < region.size())
	{
		pos3 = region.find_first_of(L"1234567890", pos2);
		if (pos3 < region.size())
		{
			count++;
			pos2 = pos3 + 1;
		}
		else { break; }
	}
	if (count > 3) { return 0; }
	return 1;
}
int CATALOGUE::make_CSV()
{
	CSV csv;
	pages.push_back(csv);
	int index = pages.size() - 1;
	return index;
}
void CATALOGUE::purge_CSVs()
{
	pages.clear();
}
int CATALOGUE::set_CSV_gid(int index, wstring& webpage, int pos1)
{
	pos1 += 7;
	int pos2 = webpage.find(L'"', pos1);
	wstring gid = webpage.substr(pos1, pos2 - pos1);
	if (pages[index].set_GID(gid)) { return 1; }
	return 0;
}
void CATALOGUE::set_CSV_name(int index, wstring& webpage, int pos1)
{
	int pos2 = webpage.find(L'>', pos1);
	pos2++;
	int pos3 = webpage.find(L'<', pos2);
	wstring temp1 = webpage.substr(pos2, pos3 - pos2);
	wstring region = clean(temp1);
	pages[index].set_name(region);
}
vector<int> CATALOGUE::get_CSV_wishlist(wstring catalogue_folder)
{
	vector<int> have_list = gid_scanner(catalogue_folder);
	vector<int> wishlist;
	bool test;

	for (int ii = 0; ii < pages.size(); ii++)
	{
		test = gid_check(pages[ii].get_GID(), have_list);
		if (!test) { wishlist.push_back(ii); }
	}
	return wishlist;
}
void CATALOGUE::download_CSVs(wstring year_folder)
{
	wstring catalogue_folder = year_folder + L"\\" + name;
	//make_folder(catalogue_folder);
	int scrubbed = remove_blank(catalogue_folder);
	if (scrubbed) { wcout << L"Removed " << scrubbed << L" empty files from " << name << endl; }
	vector<int> csv_indices = get_CSV_wishlist(catalogue_folder);
	size_t pos_start = 0;
	for (int ii = 0; ii < csv_indices.size(); ii++)
	{
		if (go) { pages[csv_indices[ii]].download_self(catalogue_folder, default_url, name); }
	}
	download_percentage(name);
}
int CATALOGUE::consistency_check()
{
	wstring filename, temp1;
	int check, GID, error;
	int count = 0;
	size_t pos1, pos2;
	for (int ii = 0; ii < pages.size(); ii++)
	{
		if (go)
		{
			GID = pages[ii].get_GID();
			filename = local_directory + L"\\" + year + L"\\" + name + L"\\" + name + L" (";
			filename += to_wstring(GID) + L") " + pages[ii].get_name() + L".csv";
			check = file_consistency(filename);
			switch (check)
			{
			case 0:
				break;

			case 1:
				wcout << L"File name " + filename + L" failed to open during consistency_check." << endl;
				count++;
				break;

			case 2:
				count++;
				delete_file(filename);
				error = pages[ii].plan_B(year, default_backup_url, name);
				if (error == 8)
				{
					graveyard(GID);
				}
				else if (error > 0)
				{

					wcout << L"File name " << filename << L" failed to pass consistency_check after re-download." << endl;
				}
				else
				{
					wcout << L"File name " << filename << L" was downloaded through plan_B." << endl;
				}
				break;
			}
		}
	}
	return count;
}
void CATALOGUE::graveyard(int GID)
{
	wstring gy_filename = local_directory + L"\\" + year + L"\\" + year + L" Graveyard\\" + name + L".bin";
	HANDLE hfile = CreateFileW(gy_filename.c_str(), (GENERIC_READ | GENERIC_WRITE), (FILE_SHARE_READ | FILE_SHARE_WRITE), NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hfile == INVALID_HANDLE_VALUE) { err(L"CreateFile-graveyard"); }
	DWORD bbq = SetFilePointer(hfile, 0, NULL, FILE_END);
	wstring gid = to_wstring(GID) + L"\r\n";
	if (!WriteFile(hfile, gid.c_str(), gid.size() * 2, &bbq, NULL)) { err(L"WriteFile-graveyard"); }
	if (hfile) { CloseHandle(hfile); }
	wcout << L"Year " << year << L"  Catalogue " << name << L"  GID " << to_wstring(GID) << L" has been sent to the graveyard." << endl;
}
void CATALOGUE::archive()
{
	wstring temp1;
	DWORD bbq;
	wstring file_name = local_directory + L"\\" + year + L"\\" + year + L" archive.bin";
	HANDLE hfile = CreateFileW(file_name.c_str(), (GENERIC_READ | GENERIC_WRITE), (FILE_SHARE_READ | FILE_SHARE_WRITE), NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hfile == INVALID_HANDLE_VALUE) { warn(L"CreateFile-archive"); }
	bbq = SetFilePointer(hfile, 0, NULL, FILE_END);
	temp1 = L"[" + name + L"]" + L" " + default_url + L" || " + default_backup_url + L"\r\n";
	if (!WriteFile(hfile, temp1.c_str(), temp1.size() * 2, &bbq, NULL)) { warn(L"WriteFile-archive"); }
	if (hfile) { CloseHandle(hfile); }
}

// Extracts pieces from a URL.  
wstring get_server(wstring url)
{
	size_t pos;
	wstring server;
	for (int ii = 0; ii < domains.size(); ii++)
	{
		pos = url.rfind(domains[ii], url.size() - 1 - domains[ii].size());
		if (pos > 0 && pos < url.size()) { server = url.substr(0, pos + domains[ii].size()); break; }
	}
	return server;
}
wstring get_object(wstring url)
{
	size_t pos;
	wstring object;
	for (int ii = 0; ii < domains.size(); ii++)
	{
		pos = url.rfind(domains[ii], url.size() - 1 - domains[ii].size());
		if (pos > 0 && pos < url.size()) { object = url.substr(pos + domains[ii].size(), url.size() - pos + 1); break; }
	}
	return object;
}

// Archive a catalogue if needed, then erase the catalogue object from memory.
int cata_cleanup(vector<CATALOGUE>& data_map, int cata_index)
{
	wstring cata_name = data_map[cata_index].get_name();
	for (int ii = 0; ii < existing_archives.size(); ii++)
	{
		if (existing_archives[ii] == cata_name) { return 0; }
	}
	data_map[cata_index].archive();
	data_map.erase(data_map.begin() + cata_index);
	return 1;
}

// From a given URL, will use search criteria to find every downloadable .csv file within
// the expanding tree of catalogue pages/redirects. A template URL is saved in local storage for 
// every catalogue to reduce memory usage and repetition of work in case of interruption or restart. 
void navigator(HINTERNET& hconnect, wstring server, wstring object, vector<CATALOGUE>& data_map, int catalogue_index, wstring year)
{
	HINTERNET hrequest = NULL;
	BOOL yesno = 0;

	hrequest = HttpOpenRequestW(hconnect, NULL, object.c_str(), NULL, NULL, NULL, NULL, 1);
	if (hrequest)
	{
		if (!HttpSendRequestW(hrequest, NULL, 0, NULL, 0)) { err(L"HttpSendRequest"); }
	}
	else { err(L"HttpOpenRequest"); }

	if (objects.size() > 0) { object = objects[objects.size() - 1]; }
	objects.clear();

	CATALOGUE cata(year);
	vector<wstring> url_redir;
	wstring year_folder = local_directory + L"\\" + year;
	wstring complete_webpage = webpage_memory(hrequest);
	wstring temp1;
	size_t pos1, pos2, pos3, pos_start, pos_stop;
	int csv_index;

	pos1 = 0;
	pos1 = complete_webpage.rfind(L"OFT=CSV", complete_webpage.size() - 8);

	if (pos1 > 0 && pos1 < complete_webpage.size())
	{
		if (!data_map[catalogue_index].check_default())
		{
			data_map[catalogue_index].set_default_url(complete_webpage, server, object);
		}

		pos_start = complete_webpage.find(L"\"GID\"", 0);
		pos_stop = complete_webpage.find(L"</select>", pos_start);

		pos1 = pos_start + 1;
		while (pos1 > pos_start && pos1 < pos_stop)
		{
			pos1 = complete_webpage.find(L"value=", pos1 + 1);
			if (pos1 < pos_stop)
			{
				if (!data_map[catalogue_index].check_named(complete_webpage, pos1)) { continue; }
				csv_index = data_map[catalogue_index].make_CSV();
				if (data_map[catalogue_index].set_CSV_gid(csv_index, complete_webpage, pos1)) { continue; }
				data_map[catalogue_index].set_CSV_name(csv_index, complete_webpage, pos1);
			}
		}
	}
	/* else if (search_query[0] != L"\0")
	{
		yesno = 0;
		pos_start = complete_webpage.find(L"<tbody>", 0);
		pos_stop = complete_webpage.rfind(L"</tbody>", complete_webpage.size() - 10);
		pos1 = pos_start + 1;
		while (pos1 > pos_start && pos1 < pos_stop)
		{
			pos1 = complete_webpage.find(L"HTML ", pos1 + 1);
			if (pos1 > pos_stop) { break; }
			pos2 = complete_webpage.rfind(L"<td>", pos1);
			temp1 = complete_webpage.substr(pos2, pos1 - pos2);
			pos3 = 0;
			for (int ii = 0; ii < search_query.size(); ii++)
			{
				pos3 = temp1.find(search_query[ii], 0);
				if (pos3 > 0 && pos3 < temp1.size()) { yesno = 1; }
			}
			if (yesno)
			{
				if (pos1 < complete_webpage.size())
				{
					data_map.push_back(cata);
					catalogue_index++;
					data_map[catalogue_index].set_name(complete_webpage, pos1);
					data_map[catalogue_index].make_folder(year_folder, year);

					pos2 = complete_webpage.find(L'/', pos1);
					pos3 = complete_webpage.find(L'"', pos2);
					temp1 = complete_webpage.substr(pos2, pos3 - pos2);
					url_redir.push_back(temp1);
				}
			}
		}
		for (int ii = 0; ii < url_redir.size(); ii++)
		{
			navigator(hconnect, server, url_redir[ii], data_map, ii, year);
		}
	} */
	else
	{
		pos_start = complete_webpage.find(L"<tbody>", 0);
		pos_stop = complete_webpage.rfind(L"</tbody>", complete_webpage.size() - 10);
		pos1 = pos_start + 1;
		while (pos1 > pos_start && pos1 < pos_stop)
		{
			yesno = 0;
			pos1 = complete_webpage.find(L"HTML ", pos1 + 1);
			if (pos1 > pos_stop) { break; }
			pos2 = complete_webpage.rfind(L"<td>", pos1);
			temp1 = complete_webpage.substr(pos2, pos1 - pos2);
			pos3 = 0;
			for (int ii = 0; ii < negative_search_query.size(); ii++)
			{
				pos3 = temp1.find(negative_search_query[ii], 0);
				if (pos3 > 0 && pos3 < temp1.size()) { yesno = 1; }
			}
			if (!yesno)
			{
				if (pos1 < complete_webpage.size())
				{
					data_map.push_back(cata);
					catalogue_index++;
					data_map[catalogue_index].set_name(complete_webpage, pos1);
					data_map[catalogue_index].make_folder(year_folder);

					pos2 = complete_webpage.find(L'/', pos1);
					pos3 = complete_webpage.find(L'"', pos2);
					temp1 = complete_webpage.substr(pos2, pos3 - pos2);
					url_redir.push_back(temp1);
				}
			}
		}
		for (int ii = 0; ii < url_redir.size(); ii++)
		{
			navigator(hconnect, server, url_redir[ii], data_map, ii, year);
		}
	}
}

// Downloads all .csv files (matching search criteria) for a given year and root URL. 
// Folders are organized by root -> year -> catalogue, and .csv files are named as 
// [catalogue] [(GID)] [region name].csv
// After downloading all CSVs for the given year, it will check those files for known errors,
// and if necessary it will attempt to replace bad files using "plan B". If "plan B" 
// fails, then that CSV file is added to the graveyard and it is ignored henceforth.
void yearly_downloader(wstring year)
{
	vector<CATALOGUE> data_map;
	wstring cata_name;
	HINTERNET hint = NULL;
	HINTERNET hconnect = NULL;
	wstring agent = L"Retriever";
	INTERNET_STATUS_CALLBACK isc;
	BOOL yesno = 0;

	wstring server = get_server(root);
	wstring object = get_object(root);
	object.append(L"?Temporal=" + year);
	DWORD context = 1;
	wstring year_folder = local_directory + L"\\" + year;

	hint = InternetOpenW(agent.c_str(), INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
	if (hint)
	{
		InternetSetStatusCallbackW(hint, (INTERNET_STATUS_CALLBACK)call);
		hconnect = InternetConnectW(hint, server.c_str(), INTERNET_DEFAULT_HTTP_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, context);
	}
	else { err(L"InternetOpen"); }
	if (hconnect)
	{
		navigator(hconnect, server, object, data_map, -1, year);
	}
	else { err(L"InternetConnect"); }
	wcout << L"Navigation complete!" << endl;

	if (debug)
	{
		remove_folder(year, data_map);
	}

	progress[1] = data_map.size();
	int result = 0;
	for (int ii = data_map.size() - 1; ii >= 0; ii--)
	{
		if (go)
		{
			data_map[ii].download_CSVs(year_folder);
			result = data_map[ii].consistency_check();
			if (result)
			{
				wcout << L"Catalogue " << data_map[ii].get_name() << L" had " << result << L" consistency errors." << endl;
			}
			data_map[ii].purge_CSVs();
			cata_name = data_map[ii].get_name();
			if (cata_cleanup(data_map, ii)) 
			{ 
				wcout << L"Catalogue " << cata_name << L" has been archived." << endl;
			}
		}
	}

	if (hconnect) { InternetCloseHandle(hconnect); }
	if (hint) { InternetCloseHandle(hint); }
}

// Perform a variety of tasks before the main work begins.
void initialize(wstring year)
{
	wstring year_folder = local_directory + L"\\" + year;
	if (!CreateDirectoryW(year_folder.c_str(), NULL))
	{
		if (GetLastError() != ERROR_ALREADY_EXISTS) { err(L"CreateDirectory-initialize"); }
	}
	wstring gy_folder = year_folder + L"\\" + year + L".Graveyard";
	if (!CreateDirectoryW(gy_folder.c_str(), NULL))
	{
		if (GetLastError() != ERROR_ALREADY_EXISTS) { err(L"CreateDirectory-initialize"); }
	}

	wstring file_name = year_folder + L"\\" + year + L" default URLs.bin";
	wstring cata_name;

	negative_search_query.push_back(L"Sortation");           // These exclusion criteria are hardcoded because
	negative_search_query.push_back(L"Dissemination");       // their regions are named only by numeric code, 
	negative_search_query.push_back(L"Tract");               // for which definitions are inaccessible. 
	negative_search_query.push_back(L"Enumeration");

	file_name = year_folder + L"\\" + year + L" archive.bin";
	HANDLE hfile = CreateFileW(file_name.c_str(), (GENERIC_READ | GENERIC_WRITE), (FILE_SHARE_READ | FILE_SHARE_WRITE), NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hfile == INVALID_HANDLE_VALUE) { warn(L"CreateFile-initialize"); }
	wstring wfile = bin_memory(hfile);
	size_t pos1 = wfile.find(L'[', 0);
	size_t pos2;
	while (pos1 < wfile.size())
	{
		pos2 = wfile.find(L']', pos1);
		cata_name = wfile.substr(pos1 + 1, pos2 - pos1 - 1);
		existing_archives.push_back(cata_name);
		pos1 = wfile.find(L'[', pos2);
	}
	if (!CloseHandle(hfile)) { warn(L"CloseHandle-initialize"); }
}

int main()
{
	signal(SIGINT, halt);
	wstring year;
	wcout << L"Specify year to download (";
	for (int ii = 0; ii < years.size(); ii++)
	{
		wcout << years[ii]; 
		if (ii < years.size() - 1) { wcout << L", "; }
	}
	wcout << L"):" << endl;
	wcin >> year;

	initialize(year);
	yearly_downloader(year);
	//download(L"www12.statcan.gc.ca/English/census91/data/tables/Rp-eng.cfm?TABID=2&LANG=E&APATH=3&DETAIL=1&DIM=0&FL=A&FREE=1&GC=0&GID=2&GK=0&GRP=1&PID=120&PRID=0&PTYPE=4&S=0&SHOWALL=No&SUB=0&Temporal=1991&THEME=114&VID=0&VNAMEE=&VNAMEF=&D1=0&D2=0&D3=0&D4=0&D5=0&D6=0", L"F:", L"1991_Newf_yescsv.txt");	
	return 0;
}
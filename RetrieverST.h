using namespace std;

wstring local_directory = L"F:";
vector<wstring> search_query = { L"\0" };  // If no search term is desired, define as null wstring. 
vector<wstring> negative_search_query = { L"Agglomeration", L"20%" };  // If no search term (to be avoided) is desired, define as null wstring.
wstring root = L"www12.statcan.gc.ca/datasets/Index-eng.cfm";
vector<wstring> domains = { L".com", L".net", L".org", L".edu", L".ca" };
vector<int> years = { 1981, 1986, 1991, 1996, 2001, 2006, 2011, 2013, 2016, 2017 };
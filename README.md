# Retriever
Program used to download large quantities of spreadsheet files from Statistics Canada. 

# Log
[2020/11/28]
Began storing the "data analysis" project on GitHub. Currently, the Retriever program is capable of downloading 
all spreadsheets (for a given year) from Statistics Canada, subject to hardcoded positive/negative search 
criteria. Retriever will download an HTML error page if Stats Canada's CSV download URL is broken.

[2020/11/30]
Retriever now has a "plan B" function which can attempt to construct a CSV file (using Stats Canada's formatting)
from the HTML data table for a given region, rather than using the CSV download URL. This method is slower than 
the CSV download, because it has to painstakingly assemble the CSV from bits of HTML - and it must produce a CSV
file which is indistinguishable from Stats Canada's standard CSV. However, this function has allowed Retriever
to reliably store all the desired data for later processing into a SQL database. 

Preliminary output suggests somewhere between 0% - 15% of Stats Canada's archived CSV files had faulty hosting 
which would prevent a download from working, even when doing so manually through a web browser.
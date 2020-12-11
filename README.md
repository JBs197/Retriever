# SCDA
SCDA (Statistics Canada Data Analysis) is a project aimed at computing correlations and extrapolations in real-time, using large quantities of census data from Statistics Canada. 

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

[2020/12/06]
Retriever will no longer hold CSV objects in memory after that CSV has gone through a consistency check. The 
memory cost of a CSV object was minimal, but the cumulative burden was substantial after several large catalogues. 

"Plan B" now has a "cauterization" failsafe whereby it will add a specific CSV file to a saved graveyard list, 
in the event that the table data is inaccessible from both the CSV download link and the HTML table. The graveyard
list is checked prior to making any attempt at acquiring that CSV data, and if a CSV file is in the graveyard, it 
will be ignored. 

Removed the hardcoded inputs (root local directory, positive/negative search criteria, root Stats Canada URL,
common domain names, years for which a national census was taken) from the source .cpp file and into a separate .h 
header file. The header file must be in the same folder as the source code file.

[2020/12/07]
Retriever has a new "named region only" filter in place while it is searching for all the CSV files within a catalogue. 
This filter causes the program to completely ignore regions which have only a number as its name. These numbered regions
are subsubdivisions of subdivisions of federal electoral districts. A typical census subdivision has over one hundred 
of these subsubdivisions, and Stats Canada does not offer a map or a list of names through which they can be defined. 
An examination of the census appendix documents reveals that it was once possible to order paper maps of these 
subsubdivisions directly from Stats Canada via mail-order (!) for a price. Because these subsubdivisions cannot be 
defined by reasonable means in the current era, there is no point in downloading or organizing these CSV files. 

Note that, beginning with the 2011 census, Stats Canada began to host its own online map of census subsubdivisions. 
This offers the potential to utilize the amazing articulation of the subsubdivision data, however that functionality
is left for a future time after all the core SCDA functionality is online.

[2020/12/11]
Improved Retriever's memory management by having it delete its catalogue objects after they have been attended to
(downloaded, checked for consistency errors, archived). When a catalogue is archived, its key data (name + template 
URL + backup template URL) is saved in a .bin file outside of its CSV folder. All catalogues in one year are given
one line each within a shared .bin file. 


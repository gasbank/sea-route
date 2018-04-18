# parser.py
from bs4 import BeautifulSoup
import re
import io
import string
from urllib.parse import urljoin
import os.path
import glob
import struct

latlngpat = re.compile(r'(\d+)([N|S])\s+(\d+)([E|W])')
max_locode_len = 0
max_nameascii_len = 0
with open('seaports.dat', 'wb') as fout:
	for fn in glob.glob("*.htm"):
		with open(fn, 'r') as f:
			print('Parsing %s...' % fn)
			soup = BeautifulSoup(f.read(), 'html.parser')
			for r in soup.select('tr')[4:]:
				dlist = r.select('td')
				locode = re.sub(r'\s+', '', dlist[1].text)
				name = dlist[2].text
				nameascii = dlist[3].text.strip()
				subdiv = dlist[4].text
				function = dlist[5].text
				status = dlist[6].text
				date = dlist[7].text
				iata = dlist[8].text
				coords = dlist[9].text.strip()
				latlnglist = latlngpat.findall(coords)
				if latlnglist:
					latlng = latlnglist[0]
					latdegmin = int(latlng[0])
					lat = (latdegmin // 100) + (latdegmin % 100) / 60.0
					if latlng[1] == 'S':
						lat = -lat
					lngdegmin = int(latlng[2])
					lng = (lngdegmin // 100) + (lngdegmin % 100) / 60.0
					if latlng[3] == 'W':
						lng = -lng
					print(locode, nameascii, coords, lat, lng)
					fout.write(struct.pack('80s8sff', bytes(nameascii, encoding='utf-8'), bytes(locode, encoding='utf-8'), lat, lng))
					if max_locode_len < len(locode):
						max_locode_len = len(locode)
					if max_nameascii_len < len(nameascii):
						max_nameascii_len = len(nameascii)

print('Max LOCODE len:', max_locode_len)
print('Max Name Ascii len:', max_nameascii_len)

# parser.py
import requests
from bs4 import BeautifulSoup
import re
import io
import string
from urllib.parse import urljoin
import os.path

# HTTP GET Request
base_url = 'http://www.unece.org/cefact/locode/service/location'
html = requests.get(base_url).text
soup = BeautifulSoup(html, 'html.parser')
for a in soup.select('table > thead > tr > td > a'):
	url = urljoin(base_url, a.get('href'))
	fname = url[url.rfind('/')+1:]
	print('%s --> %s...' % (url, fname))
	if not os.path.isfile(fname):
		with io.open(fname, "w", encoding='utf8') as f:
			html = requests.get(url).text
			f.write(html)

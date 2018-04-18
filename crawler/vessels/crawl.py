# parser.py
import requests
from bs4 import BeautifulSoup
import re
import io
import string

# HTTP GET Request
for page_index in string.ascii_lowercase[1:]:
    base_url = 'http://www.containership-info.com/'
    req = requests.get(base_url + 'page_names_%s.html' % page_index)
    html = req.text
    soup = BeautifulSoup(html, 'html.parser')
    vessels = soup.select('td > a')
    with io.open("vessels_%s.txt" % page_index, "a", encoding='utf8') as myfile:
        for v in vessels:
            try:
                v_href = v.get('href')
                req = requests.get(base_url + v_href)
                html = req.text
                soup = BeautifulSoup(html, 'html.parser')
                details = soup.select('td > table')
                print(v_href)
                lines = [line.strip() for line in details[0].getText().split('\n') if line.strip() != '']
                # print("\n".join(lines))
                myfile.write("\n".join(lines) + "\n")
            except IOError as e:
                print("IOError", e)
            except e:
                print(e)

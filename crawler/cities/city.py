import csv
import struct

# Country,City,AccentCity,Region,Population,Latitude,Longitude
with open('cities.dat', 'wb') as fout:
	with open('cities.txt') as csvfile:
		reader = csv.DictReader(csvfile)
		c = 0
		max_city_name_len = 0
		for i, row in enumerate(reader):
			try:
				population = int(row['Population'])
				longitude = float(row['Longitude'])
				latitude = float(row['Latitude'])
			except:
				continue
			if population > 100000:
				country = row['Country']
				city = row['City']
				region = row['Region']
				city_len = len(city)
				print(country, city, population, longitude, latitude)
				if max_city_name_len < city_len:
					max_city_name_len = city_len
				c = c + 1
				fout.write(struct.pack('32s2siff', bytes(city, encoding='utf-8'), bytes(country, encoding='utf-8'), population, longitude, latitude))
				if c >= 5000:
					break
		print('%d cities listed.' % c)
		print('Max city name length: %d' % max_city_name_len)
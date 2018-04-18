#!/usr/bin/python

import glob
import binascii
import struct

width_map = {}
height_map = {}

TIFF_TAG_WIDTH = 256
TIFF_TAG_HEIGHT = 257
TIFF_TAG_STRIP_BYTE_COUNT = 279
TIFF_TAG_STRIP_OFFSET = 273

for filename in glob.glob('tif/MOD44W_Water_2000_*.tif')[:1]:
	print(filename)
	with open(filename, mode='rb') as file:
		coord = filename[len('tif\MOD44W_Water_2000_'):-4]
		lat = coord[1]
		lng = int(coord[2:4])
		#print(lat, lng)
		header = file.read(4)
		#print('header', binascii.hexlify(header))
		tagoffset = file.read(4)
		#print(binascii.hexlify(tagoffset))
		seekpos = struct.unpack("<L", tagoffset)[0]
		file.seek(seekpos)
		tagcount = struct.unpack("<H", file.read(2))[0]
		print('tagcount', tagcount)
		tags = {}
		for i in range(tagcount):
			tiftag = struct.unpack("<HHLL", file.read(2+2+4+4))
			print(tiftag)
			tags[tiftag[0]] = (tiftag[3], tiftag[2]) # offset & count
			if tiftag[0] == TIFF_TAG_WIDTH:
				w = tiftag[3]
			if tiftag[0] == TIFF_TAG_HEIGHT:
				h = tiftag[3]
		#print('width', tags[256][0])
		#print('height', tags[257][0])
		#print('Rows per strip', tags[278][0])
		# Seek to StripByteCounts
		stripByteCounts = []
		#print('Strip Byte Counts')
		if tags[TIFF_TAG_STRIP_BYTE_COUNT][1] == 1:
			stripByteCount = tags[TIFF_TAG_STRIP_BYTE_COUNT][0]
			print(stripByteCount, '(single)')
			stripByteCounts.append(stripByteCount)
		else:
			file.seek(tags[TIFF_TAG_STRIP_BYTE_COUNT][0])
			for i in range(tags[TIFF_TAG_STRIP_BYTE_COUNT][1]):
				stripByteCount = struct.unpack("<L", file.read(4))[0]
				stripByteCounts.append(stripByteCount)
				#print(stripByteCount)
		# Seek to StripOffsets
		stripOffsets = []
		#print('Strip Offsets')
		if tags[TIFF_TAG_STRIP_OFFSET][1] == 1:
			stripOffset = tags[TIFF_TAG_STRIP_OFFSET][0]
			print(stripOffset, '(single)')
			stripOffsets.append(stripOffset)
		else:
			file.seek(tags[TIFF_TAG_STRIP_OFFSET][0])
			for i in range(tags[TIFF_TAG_STRIP_OFFSET][1]):
				stripOffset = struct.unpack("<L", file.read(4))[0]
				stripOffsets.append(stripOffset)
				#print(stripOffset)
		
		for count, offset in zip(stripByteCounts, stripOffsets):
			file.seek(offset)
			#print('R', binascii.hexlify(file.read(count)))
			
		
		if lng in width_map:
			width_map[lng].add(w)
		else:
			width_map[lng] = set([w])
		if lat in height_map:
			height_map[lat].add(h)
		else:
			height_map[lat] = set([h])
		#print(binascii.hexlify(file.read(4)))
		#print(binascii.hexlify(file.read(4)))

#print(width_map)
#print(height_map)

# 172,824 (w) x 86,412 (h)
# A, C, Y rows 5,121 pixels height

height_map['A'] = set([5121])
height_map['C'] = set([5121])
height_map['Y'] = set([5121])

w_total = 0
h_total = 0
w_array = []
h_array = []
for kv in width_map:
	w = next(iter(width_map[kv]))
	w_total = w_total + w
	w_array.append((kv, w))
	#print(kv, w)
for kv in height_map:
	h = next(iter(height_map[kv]))
	h_total = h_total + h
	h_array.append((kv, h))
	#print(kv, h)

print(sorted(w_array, key=lambda tup: tup[0]))
print(sorted(h_array, key=lambda tup: tup[0]))
print('w_total', w_total)
print('h_total', h_total)
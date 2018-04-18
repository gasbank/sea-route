#!/usr/bin/python
import struct
from rectangle import Rectangle

def read_tif(filename, expected_bytes):
	ret = bytearray(expected_bytes)
	ret_cursor = 0
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
			if tiftag[0] == 256:
				w = tiftag[3]
			if tiftag[0] == 257:
				h = tiftag[3]
		#print('width', tags[256][0])
		#print('height', tags[257][0])
		#print('Rows per strip', tags[278][0])
		# Seek to StripByteCounts
		stripByteCounts = []
		#print('Strip Byte Counts')
		if tags[279][1] == 1:
			stripByteCount = tags[279][0]
			print(stripByteCount, '(single)')
			stripByteCounts.append(stripByteCount)
		else:
			file.seek(tags[279][0])
			for i in range(tags[279][1]):
				stripByteCount = struct.unpack("<L", file.read(4))[0]
				stripByteCounts.append(stripByteCount)
				#print(stripByteCount)
		# Seek to StripOffsets
		stripOffsets = []
		#print('Strip Offsets')
		if tags[273][1] == 1:
			stripOffset = tags[273][0]
			print(stripOffset, '(single)')
			stripOffsets.append(stripOffset)
		else:
			file.seek(tags[273][0])
			for i in range(tags[273][1]):
				stripOffset = struct.unpack("<L", file.read(4))[0]
				stripOffsets.append(stripOffset)
				#print(stripOffset)
		
		if expected_bytes != sum(stripByteCounts):
			raise RuntimeError
			
		for count, offset in zip(stripByteCounts, stripOffsets):
			file.seek(offset)
			ret[(ret_cursor):(ret_cursor+count)] = file.read(count)
			ret_cursor = ret_cursor + count
			
	if len(ret) != sum(stripByteCounts):
		raise RuntimeError
		
	return ret, w

def copy_pixels_to_slice(tif_pixels, tif_width, tif_height, intersectrect_tif, slice, slice_width, slice_height, intersectrect_slice):
	if intersectrect_tif.area() != intersectrect_slice.area():
		raise RuntimeError
	if intersectrect_tif.width() != intersectrect_slice.width():
		raise RuntimeError
	w = intersectrect_tif.width()
	tif_x1 = intersectrect_tif.x1
	slice_x1 = intersectrect_slice.x1
	for yt, ys in zip(range(tif_height - intersectrect_tif.y2, tif_height - intersectrect_tif.y1), range(slice_height - intersectrect_slice.y2, slice_height - intersectrect_slice.y1)):
		slice[(slice_x1 + ys * slice_width):(slice_x1 + ys * slice_width + w)] = tif_pixels[(tif_x1 + yt * tif_width):(tif_x1 + yt * tif_width + w)]
	
w_array = [(1, 5761), (3, 5761), (5, 5761), (7, 5761), (9, 5760), (11, 5761), (13, 5761), (15, 5761), (17, 5761), (19, 5760), (21, 5761), (23, 5761), (25, 5761), (27, 5761), (29, 5760), (31, 5761), (33, 5761), (35, 5761), (37, 5761), (39, 5760), (41, 5761), (43, 5761), (45, 5761), (47, 5761), (49, 5760), (51, 5761), (53, 5761), (55, 5761), (57, 5761), (59, 5760)]
h_array = [('BA', 7372), ('DC', 5121), ('FE', 7681), ('HG', 7681), ('KJ', 7681), ('ML', 7681), ('PN', 7681), ('RQ', 7681), ('TS', 7681), ('VU', 7681), ('XW', 9601), ('ZY', 2870)]
w_accum_array = []
h_accum_array = []
w_map = {}
w_accum = 0
for w in w_array:
	w_map[w[0]] = w_accum
	w_accum_array.append((w[0], w_accum))
	w_accum = w_accum + w[1]
print(w_map)

h_map = {}
h_accum = 0
for h in h_array:
	h_map[h[0]] = h_accum
	h_accum_array.append((h[0], h_accum))
	h_accum = h_accum + h[1]
print(h_map)

print('merged W', w_accum)
print('merged H', h_accum)
print('Total bytes', w_accum // 8 * h_accum)
slice_size = h_accum // 1
print('Slice size', slice_size)
w_slice_count = w_accum // slice_size
h_slice_count = h_accum // slice_size
print('W slices', w_slice_count)
print('H slices', h_slice_count)

for hslice in range(h_slice_count):
	for wslice in range(w_slice_count):
#for hslice in [0]:
#	for wslice in [0]:
		w_range = (slice_size * wslice, slice_size * (wslice + 1))
		h_range = (slice_size * hslice, slice_size * (hslice + 1))
		print('Slice %d, %d' % (wslice, hslice), w_range, h_range)
		
		# slice rect in global coordinates
		slicerect = Rectangle(slice_size * wslice, slice_size * hslice, slice_size * (wslice + 1), slice_size * (hslice + 1))
		out_filename = 'd:/bin/w%d-h%d.dat' % (wslice, hslice)
		with open(out_filename, mode='wb') as out_file:
			slice = bytearray(b'\x01' * slice_size * slice_size)
			
			for hi, h in enumerate(h_accum_array):
				for wi, w in enumerate(w_accum_array):
					wsize = w_array[wi][1]
					hsize = h_array[hi][1]

					# tif rect in global coordinates
					tifrect = Rectangle(w[1], h[1], w[1] + wsize, h[1] + hsize)
					
					# intersection rect in global coordinates
					intersectrect = slicerect & tifrect
					
					if intersectrect:
						print('  File %s %d' % (h[0], w[0]), 'intersect', intersectrect)
						ws = w_array[wi][0]
						hs = h_array[hi][0]
						filename = 'tif/MOD44W_Water_2000_%s%02d%02d.tif' % (hs, ws, ws + 1)
						try:
							expected_bytes = wsize * hsize
							tif_pixels, tif_width = read_tif(filename, expected_bytes)
							tif_height = len(tif_pixels) // tif_width
							#print('w', wsize)
							#print('h', hsize)
							#print(len(pixels))
							
							intersectrect_tif = intersectrect - tifrect
							intersectrect_slice = intersectrect - slicerect
							
							print('intersectrect_tif', intersectrect_tif)
							print('intersectrect_slice', intersectrect_slice)
							
							copy_pixels_to_slice(tif_pixels, tif_width, tif_height, intersectrect_tif, slice, slice_size, slice_size, intersectrect_slice)
							
							if len(tif_pixels) != expected_bytes:
								raise RuntimeError
								
							if len(slice) != slice_size * slice_size:
								raise RuntimeError
						except IOError:
							pass
			
			# Write to disk
			out_file.write(slice)
			del slice
for h in h_array:
	for w in w_array:
		wi = w[0]
		hi = h[0]
		filename = 'tif/MOD44W_Water_2000_%s%02d%02d.tif' % (hi, wi, wi + 1)
		#print(filename)
		try:
			with open(filename, mode='rb') as file:
				pass
		except IOError:
			pass
		
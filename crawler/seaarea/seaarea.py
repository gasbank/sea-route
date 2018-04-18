import shapefile
import struct
sf = shapefile.Reader('World_Seas_IHO_v2/World_Seas_IHO_v2')
shapeRecs = sf.shapeRecords()
print(len(shapeRecs))
maxNameLen = max([len(sr.record[0]) for sr in shapeRecs])
print('Max Name Length', maxNameLen)
with open('seaareas.dat', 'wb') as fout:
	for i, sr in enumerate(shapeRecs):
		name = sr.record[0]
		partsLen = len(sr.shape.parts)
		pointsLen = len(sr.shape.points)
		print(i, name, partsLen, pointsLen)
		# name
		fout.write(struct.pack('128s', bytes(name, encoding='utf-8')))
		# bounding box (min_x, min_y, max_x, max_y)
		fout.write(struct.pack('4f', *sr.shape.bbox))
		# partsLen
		fout.write(struct.pack('i', partsLen))
		# pointsLen
		fout.write(struct.pack('i', pointsLen))
		# parts array
		for p in sr.shape.parts:
			fout.write(struct.pack('i', p))
		# longitude array
		for lng, _ in sr.shape.points:
			fout.write(struct.pack('f', lng))
		# latitude array
		for _, lat in sr.shape.points:
			fout.write(struct.pack('f', lat))
		
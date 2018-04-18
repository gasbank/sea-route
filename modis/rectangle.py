#!/usr/bin/python

class Rectangle:
	def intersection(self, other):
		a, b = self, other
		x1 = max(min(a.x1, a.x2), min(b.x1, b.x2))
		y1 = max(min(a.y1, a.y2), min(b.y1, b.y2))
		x2 = min(max(a.x1, a.x2), max(b.x1, b.x2))
		y2 = min(max(a.y1, a.y2), max(b.y1, b.y2))
		if x1<x2 and y1<y2:
			return Rectangle(x1, y1, x2, y2)
	__and__ = intersection

	def __init__(self, x1, y1, x2, y2):
		if x1>x2 or y1>y2:
			raise ValueError("Coordinates are invalid")
		self.x1, self.y1, self.x2, self.y2 = x1, y1, x2, y2

	def __iter__(self):
		yield self.x1
		yield self.y1
		yield self.x2
		yield self.y2

	def __eq__(self, other):
		return isinstance(other, Rectangle) and tuple(self)==tuple(other)
	def __ne__(self, other):
		return not (self==other)

	def __repr__(self):
		return Rectangle.__name__+repr(tuple(self))
		
	def __sub__(self, other):
		return Rectangle(self.x1 - other.x1, self.y1 - other.y1, self.x2 - other.x1, self.y2 - other.y1)
		
	def area(self):
		return (self.x2 - self.x1) * (self.y2 - self.y1)
		
	def width(self):
		return self.x2 - self.x1
	def height(self):
		return self.y2 - self.y1
		
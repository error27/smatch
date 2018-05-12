#!/usr/bin/env python
# SPDX_License-Identifier: MIT
#
# Copyright (C) 2018 Luc Van Oostenryck <luc.vanoostenryck@gmail.com>
#

"""
///
// Sparse source files may contain documentation inside block-comments
// specifically formatted::
//
// 	///
// 	// Here is some doc
// 	// and here is some more.
//
// More precisely, a doc-block begins with a line containing only ``///``
// and continues with lines beginning by ``//`` followed by either a space,
// a tab or nothing, the first space after ``//`` is ignored.
//
// For functions, some additional syntax must be respected inside the
// block-comment::
//
// 	///
// 	// <mandatory short one-line description>
// 	// <optional blank line>
// 	// @<1st parameter's name>: <description>
// 	// @<2nd parameter's name>: ...
// 	// @return: <description> (absent for void functions)
// 	// <optional blank line>
// 	// <optional long multi-line description>
// 	int somefunction(void *ptr, int count);
//
// Inside the description fields, parameter's names can be referenced
// by using ``@<parameter name>``. A function doc-block must directly precede
// the function it documents. This function can span multiple lines and
// can either be a function prototype (ending with ``;``) or a
// function definition.
//
// Some future versions will also allow to document structures, unions,
// enums, typedefs and variables.
//

"""

import re

class Lines:
	def __init__(self, lines):
		# type: (Iterable[str]) -> None
		self.index = 0
		self.lines = lines
		self.last = None
		self.back = False

	def __iter__(self):
		# type: () -> Lines
		return self

	def memo(self):
		# type: () -> Tuple[int, str]
		return (self.index, self.last)

	def __next__(self):
		# type: () -> Tuple[int, str]
		if not self.back:
			self.last = next(self.lines).rstrip()
			self.index += 1
		else:
			self.back = False
		return self.memo()
	def next(self):
		return self.__next__()

	def undo(self):
		# type: () -> None
		self.back = True

def readline_delim(lines, delim):
	# type: (Lines, Tuple[str, str]) -> Tuple[int, str]
	try:
		(lineno, line) = next(lines)
		if line == '':
			raise StopIteration
		while line[-1] not in delim:
			(n, l) = next(lines)
			line += ' ' + l.lstrip()
	except:
		line = ''
	return (lineno, line)


def process_block(lines):
	# type: (Lines) -> Dict[str, Any]
	info = { }
	tags = []
	desc = []
	state = 'START'

	(n, l) = lines.memo()
	#print('processing line ' + str(n) + ': ' + l)

	## is it a single line comment ?
	m = re.match(r"^///\s+(.+)$", l)	# /// ...
	if m:
		info['type'] = 'single'
		info['desc'] = (n, m.group(1).rstrip())
		return info

	## read the multi line comment
	for (n, l) in lines:
		#print('state %d: %4d: %s' % (state, n, l))
		if l.startswith('// '):
			l = l[3:]					## strip leading '// '
		elif l.startswith('//\t') or l == '//':
			l = l[2:]					## strip leading '//'
		else:
			lines.undo()				## end of doc-block
			break

		if state == 'START':			## one-line short description
			info['short'] = (n ,l)
			state = 'PRE-TAGS'
		elif state == 'PRE-TAGS':		## ignore empty line
			if l != '':
				lines.undo()
				state = 'TAGS'
		elif state == 'TAGS':			## match the '@tagnames'
			m = re.match(r"^@([\w-]*)(:?\s*)(.*)", l)
			if m:
				tag = m.group(1)
				sep = m.group(2)
				## FIXME/ warn if sep != ': '
				l = m.group(3)
				## FIXME: try multi-line ???
				tags.append((n, tag, l))
			else:
				lines.undo()
				state = 'PRE-DESC'
		elif state == 'PRE-DESC':		## ignore the first empty lines
			if l != '':					## or first line of description
				desc = [n, l]
				state = 'DESC'
		elif state == 'DESC':			## remaining lines -> description
			desc.append(l)
		else:
			pass

	## fill the info
	if len(tags):
		info['tags'] = tags
	if len(desc):
		info['desc'] = desc

	## read the item (function only for now)
	(n, line) = readline_delim(lines, (')', ';'))
	if len(line):
		line = line.rstrip(';')
		#print('function: %4d: %s' % (n, line))
		info['type'] = 'func'
		info['func'] = (n, line)
	else:
		info['type'] = 'bloc'

	return info

def process_file(f):
	# type: (TextIOWrapper) -> List[Dict[str, Any]]
	docs = []
	lines = Lines(f)
	for (n, l) in lines:
		#print("%4d: %s" % (n, l))
		if l.startswith('///'):
			info = process_block(lines)
			docs.append(info)

	return docs


if __name__ == '__main__':
	""" extract the doc from stdin """
	import sys

	res = process_file(sys.stdin)
	for info in res:
		print('###');
		print('type: %s' % (info.get('type', '???')))
		val = info.get('short', None)
		if val:
			print('short:%4d: %s' % val)
		for val in info.get('tags', []):
			print('tags: %4d: @%s: %s' % val)
		val = info.get('desc', None)
		if val:
			n = val[0]
			print('desc: %4d:\n\t%s' % (n, '\n\t'.join(val[1:])))

# vim: tabstop=4

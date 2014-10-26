Frames
======

(W) = Header
---------------

	- char[2]: "WB"
	- int16: version
	- int32: timestamp
	- int64: padding

(P) = PHP source
----------------

	- char: "P"
	- int32: file counter
	- int32: length
	- string: gzipped source code

(I) = Function Entry
--------------------

	- char: "I"
	- char: type of function:
	    - value from XFUNC_* in xdebug_private.h
			- XFUNC_UNKNOWN        0x00
			- XFUNC_NORMAL         0x01
			- XFUNC_STATIC_MEMBER  0x02
			- XFUNC_MEMBER         0x03
			- XFUNC_INCLUDES       0x10
			- XFUNC_EVAL           0x10
			- XFUNC_INCLUDE        0x11
			- XFUNC_INCLUDE_ONCE   0x12
			- XFUNC_REQUIRE        0x13
			- XFUNC_REQUIRE_ONCE   0x14
	- timeindex: time since start of script
	- int32: memory usage
	- int32: file counter
	- int32: start line of function source
	- int32: end line of function source
	- int32: line number of call location
	- int32: function counter
	- int16: number of arguments
	- string_ref: classname
	- string_ref: function/method name/closure_ref
	- argument[]

(O) = Function Exit
-------------------

	- char: "O"
	- timeindex: time since start of script
	- int32: memory usage
	- int32: function counter
	- argument: return value

(S) = String ref table
----------------------

	- char: "S"
	- int16: page_nr
	- gzip(string[]) (max 2^16 per page)

($) = Symbol table
------------------

	- char: "$"
	- timeindex: time since start of script
	- int32: function counter
	- int16: number of entries
	- argument[]

(M) = Symbol modification
-------------------------

	- char: "M"
	- char: operation and flags:

		- 0: old value present
		- 1: new value present

	- timeindex: time since start of script
	- int32: function counter (implies file index too)
	- int32: line number of modification location
	- string: name of symbol
	- value: new_value

	If old value present (flag 0):

	- value: old_value

	If new value present (flag 1):

	- value: old_value

	**TODO:** If no old value, infer it's the first set

	**TODO:** If no new value, infer it's an unset

Types
=====

<argument>
----------

	- char: flags
		- 0: named
		- 1: default value
		- 2: not sent
	- string_ref: variable name

	When value present:

	- value: The argument's value

	**TODO:** Need to check variadics

<string>
--------

	- int32: length
	- char[length]: contents of string
	- char: \0

<string_ref>
------------

	- char: type:

		- 0: reference
		- 1: inline
		- 2: null

	When reference:

	- char: padding
	- int16: page_nr
	- int16: string_nr in page

	When inline:

	- <string>: the string

	When null:

	- nothing

	Inline is used for strings <= 16 bytes, Reference for longer.

<timeindex>
-----------

	- int32: in msec since start of script

<value>
-------

	- like a zval

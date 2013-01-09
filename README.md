dwarfprofile
============

Profile binary sizes using dwarf info

Invocation
==========

dwarfprofile -e <path/to/binary/or/dso> # analyse a single object

dwarfprofile -p <pid> # profile running process

Dependencies
============

openSUSE:
	sudo zypper in libdw-devel libelf-devel

Fedora:
	sudo yum install elfutils-libelf-devel elfutils-libelf elfutils-libs elfutils-devel


Difi
====

Difi is a disk filter driver (in Windows DDK terminology) which can redirect disk sector writes to a "hidden" area on disk and consequently return these sectors back for reads. Difi was written as proof of concenpt for bigger virtualization research project.


Design overview
===============

Difi is an upper disk filter driver, it intercepts both reads and writes. If activated, Difi redirects writes to a pre-allocated space on disk, and creates a map of redirected sectors.

How disk space is allocated
---------------------------

Disk space should be allocated in advance using difi-cli utility, which simply creates hidden/system file on disk, obtains its extents (big thanks to Mark Roddy for filterExtents source code!) and sends the information to the driver. Not very smart approach, but it was good enough for the prototype.

How sector mappings are stored
-------------------------------

The disk remapper works with extents. An extent is represented by an inteval of a starting sector and a length. When a write is intercepted, Difi looks in the list of free extents for free one, and creates a mapping. Originally I was going 
to use interval tree, but abandoned the idea after playing around with prototype as too complex and not very suitable for real time environment. Instead, Difi uses simple hash map where I store sector-to-sector mapping. It's probably less memory efficient, but very simple, fast, and can be easily optimized if required.  


Code overview
=============

- Modular kernel mode driver: code split into main driver and several static
  libraries. Libraries are portable, and are used in both driver and user mode unit test 
- Unit tests for C code base using CUtest
- Linux-kernel coding style in Windows


How to build
==============================================================

To compile all projects, you need:
- Microsoft Windows DDK 7.1
- Microsoft Visual C++ Express 2010 or above

To build kernel mode components, first start "x86 checked mode environment" command
prompt and run build.bat in it.

How to run
==============================================================

DO NOT RUN DIFI ON YOUR PHYSICAL COMPUTER, USE A VIRTUAL MACHINE! 

Difi is destructive by nature, and I would hate it if you ruin your hard drive. So if you're really want to give it a test drive, use a virtual machine. You'll also have to turn driver sign verification off.

To install and run Difi drivers, you'll need several command line utilities:

addfilter.exe
psshutdown.exe


Run support/deploy.bat to deploy all Difi components to the target computer. Then run instdififilt.bat there, it will copy the driver to windows system directory and reboot. After reboot, the driver is ready to use.


(more is coming)


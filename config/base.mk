SHELL:=/bin/sh
CPPFLAGS:=
CC:=gcc
CFLAGS:=
CXX:=g++
CXXFLAGS:=
LD:=g++
LDFLAGS:=
AR:=ar
ARFLAGS:=rv
RANLIB:=ranlib
CP:=cp -pv
RM:=rm -fv
MKDIR:=mkdir -pv
RMDIR:=rm -rfv
SED:=sed
FIND:=find
SCRUB:=$(FIND) . -type f -name "*~" -o -name "\#*" | xargs $(RM)

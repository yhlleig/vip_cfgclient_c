#!/bin/bash
cp libcfgclient.a libcfgclient.a.bak
OBJ_PATH="tmp"
mkdir $OBJ_PATH
cd $OBJ_PATH
ar -x ../../../3rd/lib/libzookeeper_mt.a
cd ..
ar -q libcfgclient.a $OBJ_PATH/*.o


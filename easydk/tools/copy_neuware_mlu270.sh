#!/bin/bash
#$1 -- neuware_home
#$2 -- dst_header_dir
#$3 -- dst_lib_dir 
#$4 -- with_cnplugin ON/OFF

INCLUDE_DIR=$1/include
LIB_DIR=$1/lib64

cd $INCLUDE_DIR
for file in cn_*
do
    ln -sf $INCLUDE_DIR/$file  $2/$file
done
cd -

ln -sf $INCLUDE_DIR/cnrt.h $2/cnrt.h

if [ $4 = "ON" ]; then
    ln -sf $INCLUDE_DIR/cnml.h $2/cnml.h
    ln -sf $INCLUDE_DIR/cnplugin.h $2/cnplugin.h
fi

cd $LIB_DIR
for file in libcnrt.*
do
    ln -sf $LIB_DIR/$file  $3/$file
done

for file in libcncodec.*
do
    ln -sf $LIB_DIR/$file  $3/$file
done

for file in libcndrv.*
do
    ln -sf $LIB_DIR/$file  $3/$file
done

if [ $4 = "ON" ]; then
    for file in libcnml.*
    do
        ln -sf $LIB_DIR/$file  $3/$file
    done

    for file in libcnplugin.*
    do
        ln -sf $LIB_DIR/$file  $3/$file
    done
fi

cd -

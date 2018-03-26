#!/bin/bash
rmmod wcb4xxp
modprobe wcb4xxp master=1 companding=ulaw pcmbus_devs=1 hw_dtmf=1
dahdi_cfg
#sleep 1
#echo "0x90909090" > /sys/module/wcb4xxp/parameters/dbgval1
#sleep 2
#echo "0x80808080" > /sys/module/wcb4xxp/parameters/dbgval1

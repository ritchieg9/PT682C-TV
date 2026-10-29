/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PT682C_RESET_OPEN_H
#define PT682C_RESET_OPEN_H
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <libusb.h>
static libusb_device_handle *pt682c_open_with_reset(libusb_context *ctx,uint16_t vid,uint16_t pid){
    libusb_device_handle *h=libusb_open_device_with_vid_pid(ctx,vid,pid);
    if(!h)return NULL;
    if(vid!=0x1f4d||pid!=0xe691)return h;
    puts("STARTUP USB RESET: once before claiming interface; not a firmware reload.");
    int rc=libusb_reset_device(h);printf("USB reset: %s (%d)\n",libusb_error_name(rc),rc);
    if(rc==LIBUSB_ERROR_NOT_FOUND||rc==LIBUSB_ERROR_NO_DEVICE){
        libusb_close(h);h=NULL;
        for(int i=0;i<30&&!h;i++){usleep(250000);h=libusb_open_device_with_vid_pid(ctx,vid,pid);}
        if(!h)return NULL;
    }else if(rc<0){libusb_close(h);return NULL;}
    usleep(500000);return h;
}
#define libusb_open_device_with_vid_pid pt682c_open_with_reset
#endif

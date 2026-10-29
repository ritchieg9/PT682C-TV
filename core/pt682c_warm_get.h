/* SPDX-License-Identifier: GPL-2.0
 * Experimental GET-only mailbox synchronization for a warm PT682C.
 * Insert after the original GET function, before firmware segment definitions.
 */
static int getop(unsigned char opcode,unsigned char *payload,size_t n) {
    unsigned char tx[10]={0xfe,8},message[8]={0},rx[64];
    unsigned char request[2]={0xfd,0};
    if(n>48)return -1;
    unsigned char thisseq=seq++;if(!seq)seq=1;
    message[0]=opcode;message[1]=thisseq;
    putbe(message+4,checksum(message,8));swap4(message,8);memcpy(tx+2,message,8);
    printf("GET sync: request opcode=%u seq=%u expected=%zu\n",opcode,thisseq,n);
    if(iw(tx,10)<0)return -1;
    int reissued=0;
    for(int attempt=0;attempt<40;attempt++) {
        memset(rx,0,sizeof(rx));
        if(iw(request,2)<0 || ir(rx,4)<0 || ir(rx+4,4)<0)return -1;
        swap4(rx,8);
        if(rx[1]==0 && be32(rx+4)==0){usleep(2000);continue;}
        if(rx[2]>48){fprintf(stderr,"Mailbox payload too large: %u; stopping\n",rx[2]);return -1;}
        size_t total=(8+(size_t)rx[2]+3)&~(size_t)3;
        for(size_t i=8;i<total;i+=4)if(ir(rx+i,4)<0)return -1;
        if(total>8)swap4(rx+8,total-8);
        uint32_t received=be32(rx+4);memset(rx+4,0,4);
        if(checksum(rx,8+rx[2])!=received){
            fprintf(stderr,"Mailbox response checksum mismatch. No firmware writes attempted.\n");return -1;
        }
        printf("Mailbox: opcode=%u seq=%u payload=%u status=%u checksum=OK\n",rx[0],rx[1],rx[2],rx[3]);
        if(rx[0]!=opcode || rx[1]!=thisseq || rx[2]!=n){
            puts("Discarding complete response that does not match this GET request.");
            if(!reissued){
                puts("Reissuing this read-only GET once to synchronize mailbox.");
                if(iw(tx,10)<0){return -1;}
                reissued=1;
            }
            usleep(2000);continue;
        }
        if(rx[3]){fprintf(stderr,"Matching GET returned firmware status=%u\n",rx[3]);return -1;}
        memcpy(payload,rx+8,n);return 0;
    }
    fprintf(stderr,"No matching GET response after bounded polling.\n");return -1;
}

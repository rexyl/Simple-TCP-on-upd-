//receiver
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string>
#include <fstream>
#include <vector>
#include <iostream>
#include <algorithm>

typedef std::string string;
typedef unsigned char byte;
typedef std::pair<int, byte*> out_order_buffer_unit;

#define BUFSIZE 512
#define ACK 16
#define END 1
#define DATA 0
string local;
FILE *logfs;
bool lock;
//struct tcp_packet{
//    long sequence_num;
//    long ack_num;
//    char flag;
//    size_t len;
//    char buf[BUFSIZE];
//};
const std::string writelog(time_t now,string source, string dest, int seq,int ack,short flag,long rtt) {
    struct tm  tstruct;
    char       buf[80];
    tstruct = *localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%d.%X", &tstruct);
    string blank = " ",enter = "\n";
    while(lock){}
    lock=1;
    fwrite(buf, 1,strlen(buf), logfs);
    fwrite(blank.c_str(), 1, blank.size(), logfs);
    fwrite(source.c_str(), 1, source.size(), logfs);
    fwrite(blank.c_str(), 1, blank.size(), logfs);
    fwrite(dest.c_str(), 1, source.size(), logfs);
    fwrite(blank.c_str(), 1, blank.size(), logfs);
    string tmp = std::to_string(seq).c_str();
    fwrite(tmp.c_str(), 1, tmp.size(), logfs);
    fwrite(blank.c_str(), 1, blank.size(), logfs);
    tmp = std::to_string(ack).c_str();
    fwrite(tmp.c_str(), 1, tmp.size(), logfs);
    fwrite(blank.c_str(), 1, blank.size(), logfs);
    tmp = std::to_string(flag).c_str();
    fwrite(tmp.c_str(), 1, tmp.size(), logfs);
    fwrite(blank.c_str(), 1, blank.size(), logfs);
    if (rtt>=0) {
        tmp = std::to_string(rtt).c_str();
        fwrite(tmp.c_str(), 1, tmp.size(), logfs);
    }
    fwrite(enter.c_str(), 1, enter.size(), logfs);
    lock=0;
    return buf;
}

short ch_sum(byte* tcp_packet,int len){
    int ch = 0,start=0;
    short tmp;
    while(start<len){
        memcpy(&tmp,tcp_packet+start,sizeof(short));
        start = start+sizeof(short);
        //std::cout<<tmp<<"\n";
        ch += tmp;
        if(start==len-1){
            byte last = tcp_packet[start];
            ch+= (int)last*16;
        }
    }
    short remainder = ch % 65536;
    short checksum = (ch >> 16) + remainder;
    return checksum;
}

int parse_packet(byte *tcp_packet,int *seq,int *acknum,short *flag,short *checksum)
{
    
    memcpy( flag,tcp_packet+12, sizeof(short));
    memcpy(seq,tcp_packet+4, sizeof(int));
    memcpy(acknum,tcp_packet+8,  sizeof(int));
    //flag = DATA;
    if (*flag == ACK || *flag==END) {
        return 0;
    }
    memcpy(checksum, tcp_packet+16, sizeof(short));
    short tmp;
    //memcpy(&tmp, tcp_packet+16, sizeof(short));
    memset(tcp_packet+16, 0, sizeof(short));
    tmp = ch_sum(tcp_packet, 20+BUFSIZE);
    if (tmp!=*checksum) {
        std::cout<<"Corrupt! #"<<*seq<<"\n";
        std::cout<<*checksum<<" vs "<<tmp<<"\n";
        return -1;
    }
    short len;
    memcpy(&len ,tcp_packet+18, sizeof(short));
    return len;
}

int make_packet(byte *tcp_packet,int *seq,int *acknum,short *flag,short *checksum,FILE *ifs)
{
    memcpy(tcp_packet+4, seq, sizeof(int));
    memcpy(tcp_packet+8, acknum, sizeof(int));
    //flag = DATA;
    memcpy(tcp_packet+12, flag, sizeof(short));
    if (*flag == ACK || *flag == END) {
        return 0;
    }
    //memcpy(tcp_packet+16, checksum, sizeof(short));
    memset(tcp_packet+16, 0, sizeof(short));
    short len = fread(tcp_packet+20, 1, BUFSIZE, ifs);
    memcpy(tcp_packet+18, &len, sizeof(short));
    *checksum = ch_sum(tcp_packet, 20+BUFSIZE);
    memcpy(tcp_packet+16, checksum, sizeof(short));
    
    return len;
}

bool cmp(out_order_buffer_unit first,out_order_buffer_unit second){
    return first.first<second.first;
}

int main(int argc, char **argv)
{
    if (argc<6) {
        fprintf(stderr,"ERROR, not enough argument\n");
        exit(1);
    }
    std::string filename(argv[1]);
    int SERVICE_PORT =atoi(argv[2]);
    std::string s_add(argv[3]);
    int portno = atoi(argv[4]);
    std::string logname(argv[5]);
    
    char cCurrentPath[FILENAME_MAX];
    if (!getcwd(cCurrentPath, sizeof(cCurrentPath)))
    {
        return errno;
    }
    std::string pathname(cCurrentPath);
    filename = pathname + "/" + filename;
    
    if (logname=="stdout") {
        logfs = stdout;
    }
    else{
        logname = pathname + "/" + logname;
        logfs = fopen(logname.c_str(), "w");
    }
    FILE *ofs = fopen(filename.c_str(), "w");
    //FILE *ofs = fopen("/Users/Rex/Desktop/cn_second/cn_second/o.txt", "w");
    
    
    struct sockaddr_in myaddr;  /* our address */
    struct sockaddr_in remaddr; /* remote address */
    socklen_t addrlen = sizeof(remaddr);        /* length of addresses */
    int recvlen;            /* # bytes received */
    int fd;             /* our socket */
    
    
    /* create a UDP socket */
    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("cannot create socket\n");
        return 0;
    }
    
    /* bind the socket to any valid IP address and a specific port */
    
    memset((char *)&myaddr, 0, sizeof(myaddr));
    myaddr.sin_family = AF_INET;
    myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    myaddr.sin_port = htons(SERVICE_PORT);
    
    if (bind(fd, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0) {
        perror("bind failed");
        return 0;
    }
    
    /////intial ack related sender tcp//////
    int n,sockfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        printf("ERROR opening socket");
    server = gethostbyname(s_add.c_str());
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr,
          (char *)&serv_addr.sin_addr.s_addr,
          server->h_length);
    serv_addr.sin_port = htons(portno);
    if (connect(sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0)
        printf("ERROR connecting");
    int ws;
    char addtmp[256];
    read(sockfd, &ws, sizeof(int));
    read(sockfd, addtmp, 255);
    write(sockfd,s_add.c_str() , s_add.size() );
    local = string(addtmp,strlen(addtmp));

    //////////
    std::vector<out_order_buffer_unit> out_order_buf;
    
    for(int i = 0; i<ws; i ++){
        byte *temp = new byte[BUFSIZE+20];
        out_order_buffer_unit tmp(-1,temp);
        out_order_buf.push_back(tmp);
    }
    
    /* now loop, receiving data and printing what we received */
    //printf("waiting on port %d\n", SERVICE_PORT);
    byte *tcp_packet = new byte[20+BUFSIZE];
    short flag,checksum,trash_short;
    short len = BUFSIZE;
    int totalbyte = 0,countretrans=0;
    int seq = 0,acknum = 0,rcv_base = 0,trash_int = 0,hehe;
    
    while(1){
        recvlen = recvfrom(fd, tcp_packet, 20+BUFSIZE, 0, (struct sockaddr *)&serv_addr, &addrlen);
        len = parse_packet(tcp_packet, &seq, &acknum, &flag, &checksum);
        if(len==-1) continue;

        writelog(time(0), s_add, local, seq*BUFSIZE, seq*BUFSIZE+1, flag, -1);
        if (flag == END) {
            break;
        }
        if (seq==rcv_base) {
            totalbyte += 20+len;
            //std::cout<<"Write seq # "<<seq<<" with len = "<<len<<"\n";
            fwrite(tcp_packet+20, 1, len, ofs);
            
            //greedy check buffer
            int target = seq+1;
            rcv_base++;
            int len_t;
            for(int i = 0; i<ws; i++){
                if (out_order_buf[i].first == target){
                    len_t = parse_packet(out_order_buf[i].second, &hehe, &trash_int, &trash_short, &trash_short);
                    fwrite( (out_order_buf[i].second)+ 20, 1, len_t, ofs);
                    totalbyte += 20+len_t;
                    rcv_base++;
                    target++;
                    out_order_buf[i].first = -1;
                }
            }
            acknum = seq+1;
            flag = ACK;
            
            make_packet(tcp_packet, &seq, &acknum, &flag, &checksum, ofs);
            n = write(sockfd, tcp_packet, 20);
            if (n < 0)
                printf("ERROR writing to socket");
            writelog(time(0), local, s_add, seq*BUFSIZE, seq*BUFSIZE+1, flag, -1);
            
        }
        else  //out of order, buffer it
        {
            for(int i = 0; i<ws; i++){
                if (out_order_buf[i].first == -1){
                    memcpy(out_order_buf[i].second, tcp_packet, 20+BUFSIZE);
                    out_order_buf[i].first = seq;
                    sort(out_order_buf.begin(),out_order_buf.end());
                    break;
                }
            }
            
            acknum = seq + 1;
            flag = ACK;
            checksum = 0;
            make_packet(tcp_packet, &seq, &acknum, &flag, &checksum, ofs);
            n = write(sockfd, tcp_packet, 20);
            if (n < 0)
                printf("ERROR writing to socket");
            writelog(time(0), local, s_add, seq*BUFSIZE, seq*BUFSIZE+1, flag, -1);
        }
        //printf("seq is %d\n",seq);
    }
    string summary = "Transmission was successful!\nTotal bytes sent is "+std::to_string(totalbyte)+
    "\nNumber of sent segment is "+std::to_string(seq)+"\n";
    fwrite(summary.c_str(), 1, summary.size(), logfs);
    fclose(logfs);
    fclose(ofs);
    free(tcp_packet);
    for(int i = 0; i<ws; i ++){
        free(out_order_buf[i].second);
    }
    return 0;
}

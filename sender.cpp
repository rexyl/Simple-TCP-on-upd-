// sender
#include <iostream>     // std::cout
#include <fstream>      // std::ifstream
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <thread>
#include <time.h>
#include <sys/time.h>
#include <vector>
#include <algorithm>

typedef unsigned char byte;
typedef std::string string;

#define BUFSIZE 512
#define ACK 16
#define END 1
#define DATA 0
#ifndef TIMEOUT
#define TIMEOUT 5
#endif
int fd,WINDOW_SIZE,countretrans=0;
struct sockaddr_in myaddr, remaddr;
int slen=sizeof(remaddr);
int window_base = 0 , packet_fly = 0 ;
short flag,checksum;
bool lock=0;
int seq = 0,acknum = 0;
long rtt = 0,totalbyte=0,totalbyteresend=0;
byte *tcp_packet_r;
FILE *logfs;
int portno,sockfd,ack_port,newsockfd,clilen;
struct sockaddr_in serv_addr,cli_addr;
string local,server;

struct resend_buf
{
    int seq;
    byte *buffer;
    timeval timer;
    bool acked;
    resend_buf(int s,byte* c,timeval t):seq(s),buffer(c),timer(t),acked(0) {}
};
std::vector<resend_buf> resend_buffer_v;

const std::string writelog(time_t now,string source, string dest, int seq,int ack,short flag,long rtt) {
    struct tm  tstruct;
    char       buf[80];
    tstruct = *localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%d.%X", &tstruct);
    string blank = " ",enter = "\n";
    while (lock) {}
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

long diff_ms(timeval t1, timeval t2)
{
    return (((t1.tv_sec - t2.tv_sec) * 1000000) +
            (t1.tv_usec - t2.tv_usec))/1000;
}

short ch_sum(byte* tcp_packet,int len){
    int ch = 0,start=0;
    short tmp;
    while(start<len){
        memcpy(&tmp,tcp_packet+start,sizeof(short));
        start = start+sizeof(short);
        ch += tmp;
        //std::cout<<tmp<<"\n";
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
    memset(tcp_packet+16, 0, sizeof(short));
    //memcpy(&tmp, tcp_packet+16, sizeof(short));
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
    memset(tcp_packet+16, 0, sizeof(short));
    //memcpy(tcp_packet+16, checksum, sizeof(short));
    short len = fread(tcp_packet+20, 1, BUFSIZE, ifs);
    memcpy(tcp_packet+18, &len, sizeof(short));
    *checksum = ch_sum(tcp_packet, 20+BUFSIZE);
    memcpy(tcp_packet+16, checksum, sizeof(short));
    
    return len;
}

void receiver(){
    tcp_packet_r = new byte[20+BUFSIZE];
    int recvlen,seq,acknum,n;
    short flag,checksum;
    while (1) {
        int n = read(newsockfd, tcp_packet_r , 20);
        if (n < 0)
            printf("ERROR reading from socket");
        parse_packet(tcp_packet_r, &seq, &acknum, &flag, &checksum);
        writelog(time(0), server, local, seq*BUFSIZE, seq*BUFSIZE+1, flag, rtt);
        //std::cout<<"receive ack "<<acknum<<"\n";
        //packet_fly -= acknum - window_base;
        if (window_base == acknum-1) {         //receive a in ordered packet
            //forward send window base to last unacked
            
            for(int i=0; i<WINDOW_SIZE; i++){
                if (resend_buffer_v[i].seq == window_base)
                {
                    timeval now;
                    gettimeofday(&now, NULL);
                    rtt = diff_ms(now, resend_buffer_v[i].timer);
                    resend_buffer_v[i].acked = 1;
                    break;
                }
            }
            
            for (int time = 0; time<WINDOW_SIZE; time++){
                for (int i = 0; i<WINDOW_SIZE; i++) {
                    if (resend_buffer_v[i].seq == window_base && resend_buffer_v[i].acked) {
                        window_base++;
                        packet_fly--;
                        resend_buffer_v[i].seq = -1;
                        resend_buffer_v[i].acked = 0;
                        break;
                    }
                }
            }
        }
        
        else{       //receive a out of order packet
            for(int i=0; i<WINDOW_SIZE; i++){
                if (resend_buffer_v[i].seq == acknum-1)
                {
                    timeval now;
                    gettimeofday(&now, NULL);
                    rtt = diff_ms(now, resend_buffer_v[i].timer);
                    resend_buffer_v[i].acked = 1;
                    break;
                }
            }
        }
        //window_base = acknum;
    }
    return;
}

void counter(){
    sleep(TIMEOUT);
    while (1) {
        int sleeptime = TIMEOUT;
        for (int i = 0; i < WINDOW_SIZE; ++i)
        {
            if (resend_buffer_v[i].seq!=-1 && !resend_buffer_v[i].acked)
            {
                timeval now;
                gettimeofday(&now, NULL);
                long sec = diff_ms(now, resend_buffer_v[i].timer);   //from last send time to now
                if (sec > TIMEOUT*1000)
                 {
                    //resend packet seq
                     countretrans++;
                     //std::cout<<"resend pkg "<<resend_buffer_v[i].seq<<"\n";
                     if (sendto(fd, resend_buffer_v[i].buffer, 20+BUFSIZE, 0, (struct sockaddr *)&remaddr, slen)==-1)
                        perror("sendto");
                     resend_buffer_v[i].timer = now;
                     flag =DATA;
                     writelog(time(0), local, server, resend_buffer_v[i].seq, (acknum-1)*BUFSIZE+1, flag, rtt);
                 }
                if (TIMEOUT - sec < sleeptime) {
                    sleeptime = (double)(TIMEOUT -sec);
                }
            }
        }
        //sleep(sleeptime);
    }
    return;
}

int main(int argc, char *argv[])
{
    if (argc < 7) {
        fprintf(stderr,"ERROR, not enough argument\n");
        exit(1);
    }
    std::string filename(argv[1]);
    server = string(argv[2]);
    int SERVICE_PORT = atoi(argv[3]);
    ack_port = atoi(argv[4]);
    std::string logname(argv[5]);
    WINDOW_SIZE = atoi(argv[6]);
    
    //int SERVICE_PORT = 4119;
    //char server[] = "127.0.0.1";  /* change this to use a different server */
    char cCurrentPath[FILENAME_MAX];
    if (!getcwd(cCurrentPath, sizeof(cCurrentPath)))
    {
        return errno;
    }
    std::string pathname(cCurrentPath);
    
    filename = pathname + "/" + filename;
    
    //FILE *ifs = fopen("/Users/Rex/Desktop/cn_second/cn_second/test.txt", "r");
    
    FILE *ifs = fopen(filename.c_str(), "r");
    if (logname=="stdout") {
        logfs = stdout;
    }
    else{
        logname = pathname + "/" + logname;
        //logfs = fopen("/Users/Rex/Desktop/cn_second/cn_second/log.txt", "w");
        logfs = fopen(logname.c_str(), "w");
    }
    if (!ifs) {
        fwrite("File not found", 1, 14, logfs);
        fclose(logfs);
        exit(1);
    }
    /* create a socket */
    if ((fd=socket(AF_INET, SOCK_DGRAM, 0))==-1)
        printf("socket created\n");
    
    /* bind it to all local addresses and pick any port number */
    
    memset((char *)&myaddr, 0, sizeof(myaddr));
    myaddr.sin_family = AF_INET;
    myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    myaddr.sin_port = htons(0);
    
    if (bind(fd, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0) {
        perror("bind failed");
        return 0;
    }
    
    /* now define remaddr, the address to whom we want to send messages */
    /* For convenience, the host address is expressed as a numeric IP address */
    /* that we will convert to a binary format via inet_aton */
    
    memset((char *) &remaddr, 0, sizeof(remaddr));
    remaddr.sin_family = AF_INET;
    remaddr.sin_port = htons(SERVICE_PORT);
    if (inet_aton(server.c_str(), &remaddr.sin_addr)==0) {
        fprintf(stderr, "inet_aton() failed\n");
        exit(1);
    }
    
    //initial ack related tcp socket
    tcp_packet_r = new byte[20+BUFSIZE];
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        printf("ERROR opening socket");
    bzero((char *) &serv_addr, sizeof(serv_addr));
    portno = ack_port;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;                     //define for ipv4
    serv_addr.sin_port = htons(portno);                         //convert small endian(PC machine) to big endien(net work byte order)
    if (bind(sockfd, (struct sockaddr *) &serv_addr,            //sockfd now stands for serv_addr
             sizeof(serv_addr)) < 0)
        printf("ERROR on binding");
    clilen = sizeof(cli_addr);
    int recvlen;
    listen(sockfd,5);
    newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, (socklen_t *) &clilen);
    int ws = WINDOW_SIZE;
    char addtmp[256];
    write(newsockfd, &ws ,sizeof(int));
    write(newsockfd, server.c_str(),sizeof(server));
    read(newsockfd, addtmp, 255);
    local = string(addtmp,strlen(addtmp));
    ////////////////////////////
    sleep(1); //wait for receiver start recvfrom function

    
    /*initial buffer*/
    for(int i=0; i<WINDOW_SIZE; i++){
        byte *b = new byte[BUFSIZE+20];
        timeval *t = new timeval();
        resend_buf rb(-1,b,*t);
        resend_buffer_v.push_back(rb);
    }

    std::thread rev(receiver);
    std::thread count(counter);
    rev.detach();
    count.detach();
   
    /* now let's send the messages */
    byte *tcp_packet = new byte[20+BUFSIZE];
    short len = BUFSIZE;
    

    while(len==BUFSIZE){
        while(packet_fly >= WINDOW_SIZE){}
        flag = DATA;
        checksum = 0;
        len = make_packet(tcp_packet,&seq,&acknum,&flag, &checksum,ifs);
        totalbyte += len+20;
        // find a place in resend_buffer_v
        for (int i = 0; i < WINDOW_SIZE; ++i)
        {
            if (resend_buffer_v[i].seq == -1)
            {
                gettimeofday(&resend_buffer_v[i].timer, NULL);
                resend_buffer_v[i].seq = seq;
                memcpy(resend_buffer_v[i].buffer,tcp_packet,BUFSIZE+20);
                break;
            }
        }
//        srand (time(NULL));
//        int rn = rand()%3;
//        sleep(rn);
//        if (seq == 3 || seq == 10) { //half loss
//            seq++;
//            packet_fly++;
//            continue;
//        }
        
        if (sendto(fd, tcp_packet, 20+BUFSIZE, 0, (struct sockaddr *)&remaddr, slen)==-1)
            perror("sendto");
        writelog(time(0), local, server, seq*BUFSIZE, seq*BUFSIZE+1, flag, rtt);
        packet_fly++;
        seq = seq + 1;
    }
    
    while (1) {
        bool isend = 1;
        for (int i=0; i<WINDOW_SIZE; i++) {
            if (resend_buffer_v[i].seq !=-1 && !resend_buffer_v[i].acked) {
                isend = 0;
            }
        }
        if (isend)
        {
            flag = END;
            make_packet(tcp_packet, &seq, &acknum, &flag, &checksum, ifs);
            if (sendto(fd, tcp_packet, 20+BUFSIZE, 0, (struct sockaddr *)&remaddr, slen)==-1)
                perror("sendto");
            writelog(time(0), local, server, seq*BUFSIZE,seq*BUFSIZE+1 , flag, rtt);
            break;
        }
    }
    string summary = "Transmission was successful!\nTotal bytes sent is "+std::to_string(totalbyte)+
    "\nNumber of sent segment is "+std::to_string(seq)+
    "\nNumber of retransmitted segment is  "+std::to_string(countretrans)+"\n";
    fwrite(summary.c_str(), 1, summary.size(), logfs);
    fclose(ifs);
    fclose(logfs);
    //std::cout<<"File transfer finished\n";
    for(int i=0; i<WINDOW_SIZE; i++){
        free(resend_buffer_v[i].buffer);
    }
    free(tcp_packet);
    free(tcp_packet_r);
    return 0;
}

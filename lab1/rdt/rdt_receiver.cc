/*
 * @Description: 
 * @Version: 1.0
 * @Author: Zhang AO
 * @studentID: 518021910368
 * @School: SJTU
 * @Date: 2021-03-05 13:44:31
 * @LastEditors: Seven
 * @LastEditTime: 2021-03-11 09:38:36
 */
/*
 * FILE: rdt_receiver.cc
 * DESCRIPTION: Reliable data transfer receiver.
 * NOTE: This implementation assumes there is no packet loss, corruption, or 
 *       reordering.  You will need to enhance it to deal with all these 
 *       situations.  In this implementation, the packet format is laid out as 
 *       the following:
 *       
 *       |<-  1 byte  ->|<-             the rest            ->|
 *       | payload size |<-             payload             ->|
 *
 *       The first byte of each packet indicates the size of the payload
 *       (excluding this single-byte header)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <list>
#include <cassert>
#include <iostream>

#include "rdt_struct.h"
#include "rdt_receiver.h"
using namespace std;

#define MAX_WINDOW_SIZE 10
#define HEADER_SIZE 4
#define MAX_SEQ 60
#define MAX_PAYLOAD_SIZE (RDT_PKTSIZE - HEADER_SIZE)

struct RecievePacket
{
    bool recieved = false;
    char *data = NULL;
    int size = 0;
    bool if_head = false; //是否是一个msg的头
    bool if_tail = false; //是否是一个msg的尾
    RecievePacket() {}
    ~RecievePacket()
    {
        if (data)
        {
            free(data);
            data = NULL;
        }
    }
};

static int window_cursor;
static RecievePacket windows[MAX_WINDOW_SIZE]; //packet 缓存，窗口大小与sender一致
static message cur_Msg;                        //当前正在合并的message信息

static void flush_Msg()
{
    Receiver_ToUpperLayer(&cur_Msg);
    if (cur_Msg.data)
    {
        free(cur_Msg.data);
        cur_Msg.data = NULL;
        cur_Msg.size = 0;
    }
}

short checksum(struct packet *p)
{
    printf("reciever checksum\n");
    unsigned long c_sum = 0;
    int size = p->data[2];       //有效载荷大小
    if (size >= RDT_PKTSIZE - 4) //size大小出错，返回0
        return 0;
    for (int i = 2; i < size + 4; i += 2) //除去checksum位的数据，其他位的数据每两个byte求和
    {
        c_sum += *(short *)(&p->data[i]);
    }
    while (c_sum >> 16) //高位byte与地位byte相加
        c_sum = (c_sum >> 16) + (c_sum & 0xffff);
    return ~c_sum;
}

/* receiver initialization, called once at the very beginning */
void Receiver_Init()
{
    window_cursor = 0;
    fprintf(stdout, "At %.2fs: receiver initializing ...\n", GetSimulationTime());
}

/* receiver finalization, called once at the very end.
   you may find that you don't need it, in which case you can leave it blank.
   in certain cases, you might want to use this opportunity to release some 
   memory you allocated in Receiver_init(). */
void Receiver_Final()
{
    printf("reciever finally\n");

    for (int i = 0; i < MAX_WINDOW_SIZE; i++)
    {
        if (windows[i].data)
        {
            free(windows[i].data);
            windows[i].data = NULL;
        }
    }
    fprintf(stdout, "At %.2fs: receiver finalizing ...\n", GetSimulationTime());
}

//移动windows时，将packet合并成message信息并向上抛出
void Reciever_AddtoMsg(struct RecievePacket *p)
{
    printf("reciever add to msg\n");

    if (p->if_head) //message头部
    {
        assert(cur_Msg.data == NULL && cur_Msg.size == 0); //之前的那个message包已经上抛了；
        int new_size = p->size;
        cur_Msg.data = (char *)malloc(new_size);
        memcpy(cur_Msg.data, p->data, new_size);
        cur_Msg.size = new_size;
        if (p->if_tail)
        { //同时也是尾部，上抛并重置msg
            flush_Msg();
        }
    }
    else if (p->if_tail) //尾部
    {
        assert(cur_Msg.data && cur_Msg.size > 0);

        //拼接message，准备上抛
        message new_message;
        int total_size;
        new_message.data = (char *)malloc(total_size)
                               cur_Msg.data = (char *)realloc(cur_Msg.data, new_size);
        memcpy(cur_Msg.data + cur_Msg.size, p->data, p->size);
    }
}

//此处seq只含有低6位bit
void Reicever_SendACK(char seq)
{
    printf("reciever send ack seq=%d\n", seq);
    assert(seq >> 6 == 0);
    packet ack;
    ack.data[2] = 0;
    ack.data[3] = seq;
    *(short *)ack.data = checksum(&ack);
    Receiver_ToLowerLayer(&ack);
}

//清空message缓存
void Reciever_SendMsg()
{
    printf("reciever send msg to upper\n");
    MsgBuffer *msg_b = buffers.front();
    while (msg_b && msg_b->if_combined)
    {
        message *m = (message *)malloc(sizeof(message));
        m->size = msg_b->size;
        m->data = (char *)malloc(m->size);
        // m->data = msg_b->data; //选择共用地址，因为会malloc一个相同的地址
        memcpy(m->data, msg_b->data, m->size);
        // assert(m->data != msg_b->data);
        //似乎在传上去之后，m->data就会被free
        Receiver_ToUpperLayer(m);

        if (msg_b->data)
        {
            free(msg_b->data);
            msg_b->data = NULL;
        }
        if (msg_b)
        {
            free(msg_b);
            msg_b = NULL;
        }
        if (m->data)
        {
            free(m->data);
            m->data = NULL;
        }
        if (m)
        {
            free(m);
            m = NULL;
        }
        buffers.pop_front();
        msg_b = buffers.front();
    }
}

/* event handler, called when a packet is passed from the lower layer at the 
   receiver */
void Receiver_FromLowerLayer(struct packet *pkt)
{

    printf("reciever from lower\n");
    //检验校验码
    short sum = *(short *)(pkt->data);
    if (sum != checksum(pkt))
        return;
    char seq = pkt->data[3] & 63; //取低位6个bit
    char size = pkt->data[2];
    printf("recieve from low——get pkt seq=%d\n", seq);
    // printf("recieve from low——get pkt size=%d\n", size);
    //看看是否是一个msg的头或者尾
    bool if_head = pkt->data[3] >> 7;
    bool if_tail = (pkt->data[3] >> 6) & 1;

    //校验码通过，发送ack
    Reicever_SendACK(seq);

    //如果已经接收过了
    if (windows[seq % MAX_WINDOW_SIZE].recieved)
    {
        Reciever_SendMsg();
        return;
    }

    RecievePacket *p = (RecievePacket *)malloc(sizeof(RecievePacket));
    p->if_head = if_head;
    p->if_tail = if_tail;
    p->recieved = true;
    p->data = (char *)malloc(size);
    memcpy(p->data, pkt->data + HEADER_SIZE, size);
    p->size = size;
    //如果收到了window中的第一个packet，可以移动window，并加入message
    if (seq % MAX_WINDOW_SIZE == window_cursor)
    {
        //将可以合并的packet加入windows中
        Reciever_AddtoMsg(p); //p已经被free了
        int i = 1;
        //只需要顺序往后，碰到没有recieved的包就可以直接中断。
        while (i < MAX_WINDOW_SIZE)
        {
            p = &windows[(i + window_cursor) % MAX_WINDOW_SIZE];
            if (p->recieved)
            {
                Reciever_AddtoMsg(p);
                i++;
                continue;
            }
            break;
        }
        //移动window指针
        window_cursor = (window_cursor + i) % MAX_WINDOW_SIZE;
    }
    //没有收到过，但也不是windows中的首位,直接将当前packet加入windows
    else
    {
        windows[seq % MAX_WINDOW_SIZE] = *p;
    }
    Reciever_SendMsg();
}
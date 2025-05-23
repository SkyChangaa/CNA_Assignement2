#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "sr.h"

#define SR_WINDOW_SIZE 6
#define SR_SEQSPACE 12
#define RTT 15.0

static struct pkt window[SR_SEQSPACE];

static bool acked[SR_SEQSPACE];
static bool in_use[SR_SEQSPACE];

static float timer_start[SR_SEQSPACE];
static int base = 0;
static int nextseqnum = 0;

static struct msg msg_buffer[SR_SEQSPACE];

static int expectedseqnum_B = 0;
static struct pkt recv_buffer[SR_SEQSPACE];
static bool received[SR_SEQSPACE];


/* ******************************************************************
   Go Back N protocol.  Adapted from J.F.Kurose
   ALTERNATING BIT AND GO-BACK-N NETWORK EMULATOR: VERSION 1.2  

   Network properties:
   - one way network delay averages five time units (longer if there
   are other messages in the channel for GBN), but can be larger
   - packets can be corrupted (either the header or the data portion)
   or lost, according to user-defined probabilities
   - packets will be delivered in the order in which they were sent
   (although some can be lost).

   Modifications: 
   - removed bidirectional GBN code and other code not used by prac. 
   - fixed C style to adhere to current programming style
   - added GBN implementation
**********************************************************************/

#define RTT  16.0       /* round trip time.  MUST BE SET TO 16.0 when submitting assignment */
#define WINDOWSIZE 6    /* the maximum number of buffered unacked packet */
#define SEQSPACE 7      /* the min sequence space for GBN must be at least windowsize + 1 */
#define NOTINUSE (-1)   /* used to fill header fields that are not being used */

/* generic procedure to compute the checksum of a packet.  Used by both sender and receiver  
   the simulator will overwrite part of your packet with 'z's.  It will not overwrite your 
   original checksum.  This procedure must generate a different checksum to the original if
   the packet is corrupted.
*/
int ComputeChecksum(struct pkt packet)
{
  int checksum = 0;
  int i;

  checksum = packet.seqnum;
  checksum += packet.acknum;
  for ( i=0; i<20; i++ ) 
    checksum += (int)(packet.payload[i]);

  return checksum;
}

bool IsCorrupted(struct pkt packet)
{
  if (packet.checksum == ComputeChecksum(packet))
    return (false);
  else
    return (true);
}


/********* Sender (A) variables and functions ************/

static struct pkt buffer[WINDOWSIZE];  /* array for storing packets waiting for ACK */
static int windowfirst, windowlast;    /* array indexes of the first/last packet awaiting ACK */
static int windowcount;                /* the number of packets currently awaiting an ACK */
static int A_nextseqnum;               /* the next sequence number to be used by the sender */

/* called from layer 5 (application layer), passed the message to be sent to other side */
void SR_A_output(struct msg message)
{
  struct pkt sendpkt;
  int i;

  /* if not blocked waiting on ACK */
  if ( windowcount < WINDOWSIZE) {
    if (TRACE > 1)
      printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

    /* create packet */
    sendpkt.seqnum = A_nextseqnum;
    sendpkt.acknum = NOTINUSE;
    for ( i=0; i<20 ; i++ ) 
      sendpkt.payload[i] = message.data[i];
    sendpkt.checksum = ComputeChecksum(sendpkt); 

    /* put packet in window buffer */
    /* windowlast will always be 0 for alternating bit; but not for GoBackN */
    windowlast = (windowlast + 1) % WINDOWSIZE; 
    buffer[windowlast] = sendpkt;
    windowcount++;

    /* send out packet */
    if (TRACE > 0)
      printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
    tolayer3 (A, sendpkt);

    /* start timer if first packet in window */
    if (windowcount == 1)
      starttimer(A,RTT);

    /* get next sequence number, wrap back to 0 */
    A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;  
  }
  /* if blocked,  window is full */
  else {
    if (TRACE > 0)
      printf("----A: New message arrives, send window is full\n");
    window_full++;
  }
}


/* called from layer 3, when a packet arrives for layer 4 
   In this practical this will always be an ACK as B never sends data.
*/
void SR_A_input(struct pkt packet)
{
  int ackcount = 0;
  int i;

  /* if received ACK is not corrupted */ 
  if (!IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----A: uncorrupted ACK %d is received\n",packet.acknum);
    total_ACKs_received++;

    /* check if new ACK or duplicate */
    if (windowcount != 0) {
          int seqfirst = buffer[windowfirst].seqnum;
          int seqlast = buffer[windowlast].seqnum;
          /* check case when seqnum has and hasn't wrapped */
          if (((seqfirst <= seqlast) && (packet.acknum >= seqfirst && packet.acknum <= seqlast)) ||
              ((seqfirst > seqlast) && (packet.acknum >= seqfirst || packet.acknum <= seqlast))) {

            /* packet is a new ACK */
            if (TRACE > 0)
              printf("----A: ACK %d is not a duplicate\n",packet.acknum);
            new_ACKs++;

            /* cumulative acknowledgement - determine how many packets are ACKed */
            if (packet.acknum >= seqfirst)
              ackcount = packet.acknum + 1 - seqfirst;
            else
              ackcount = SEQSPACE - seqfirst + packet.acknum;

	    /* slide window by the number of packets ACKed */
            windowfirst = (windowfirst + ackcount) % WINDOWSIZE;

            /* delete the acked packets from window buffer */
            for (i=0; i<ackcount; i++)
              windowcount--;

	    /* start timer again if there are still more unacked packets in window */
            stoptimer(A);
            if (windowcount > 0)
              starttimer(A, RTT);

          }
        }
        else
          if (TRACE > 0)
        printf ("----A: duplicate ACK received, do nothing!\n");
  }
  else 
    if (TRACE > 0)
      printf ("----A: corrupted ACK is received, do nothing!\n");
}

/* called when A's timer goes off */
void SR_A_timerinterrupt(void)
{
  int i;

  if (TRACE > 0)
    printf("----A: time out,resend packets!\n");

  for(i=0; i<windowcount; i++) {

    if (TRACE > 0)
      printf ("---A: resending packet %d\n", (buffer[(windowfirst+i) % WINDOWSIZE]).seqnum);

    tolayer3(A,buffer[(windowfirst+i) % WINDOWSIZE]);
    packets_resent++;
    if (i==0) starttimer(A,RTT);
  }
}       



/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void SR_A_init(void)
{
  /* initialise A's window, buffer and sequence number */
  A_nextseqnum = 0;  /* A starts with seq num 0, do not change this */
  windowfirst = 0;
  windowlast = -1;   /* windowlast is where the last packet sent is stored.  
		     new packets are placed in winlast + 1 
		     so initially this is set to -1
		   */
  windowcount = 0;
}



/********* Receiver (B)  variables and procedures ************/

static int expectedseqnum; /* the sequence number expected next by the receiver */
static int B_nextseqnum;   /* the sequence number for the next packets sent by B */


/* called from layer 3, when a packet arrives for layer 4 at B*/
void SR_B_input(struct pkt packet)
{
  struct pkt sendpkt;
  int i;

  /* if not corrupted and received packet is in order */
  if  ( (!IsCorrupted(packet))  && (packet.seqnum == expectedseqnum) ) {
    if (TRACE > 0)
      printf("----B: packet %d is correctly received, send ACK!\n",packet.seqnum);
    packets_received++;

    /* deliver to receiving application */
    tolayer5(B, packet.payload);

    /* send an ACK for the received packet */
    sendpkt.acknum = expectedseqnum;

    /* update state variables */
    expectedseqnum = (expectedseqnum + 1) % SEQSPACE;        
  }
  else {
    /* packet is corrupted or out of order resend last ACK */
    if (TRACE > 0) 
      printf("----B: packet corrupted or not expected sequence number, resend ACK!\n");
    if (expectedseqnum == 0)
      sendpkt.acknum = SEQSPACE - 1;
    else
      sendpkt.acknum = expectedseqnum - 1;
  }

  /* create packet */
  sendpkt.seqnum = B_nextseqnum;
  B_nextseqnum = (B_nextseqnum + 1) % 2;
    
  /* we don't have any data to send.  fill payload with 0's */
  for ( i=0; i<20 ; i++ ) 
    sendpkt.payload[i] = '0';  

  /* computer checksum */
  sendpkt.checksum = ComputeChecksum(sendpkt); 

  /* send out packet */
  tolayer3 (B, sendpkt);
}

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void SR_B_init(void)
{
  expectedseqnum = 0;
  B_nextseqnum = 1;
}

/******************************************************************************
 * The following functions need be completed only for bi-directional messages *
 *****************************************************************************/

/* Note that with simplex transfer from a-to-B, there is no B_output() */
void B_output(struct msg message)  
{
}

/* called when B's timer goes off */
void B_timerinterrupt(void)
{
}

void SR_A_init(void) 
{
  base = 0;
  nextseqnum = 0;
  for (int i = 0; i < SR_SEQSPACE; i++) {
    acked[i] = false;
    in_use[i] = false;
    timer_start[i] = 0;
  }
}

void SR_A_output(struct msg message)
{
  int window_count = (nextseqnum - base + SR_SEQSPACE) % SR_SEQSPACE;
  if (window_count >= SR_WINDOW_SIZE) {
      printf("SR_A_output: Window full, dropping message\n");
      return;
  }
  struct pkt packet;
  packet.seqnum = nextseqnum;
  packet.acknum = -1;
  memcpy(packet.payload, message.data, 20);
  packet.checksum = 0;
  packet.checksum = compute_checksum(packet);


  window[nextseqnum] = packet;
  acked[nextseqnum] = false;
  in_use[nextseqnum] = true;

  tolayer3(A, packet);
  starttimer(A, RTT);
  timer_start[nextseqnum] = get_sim_time();

  printf("SR_A_output: Sent packet %d\n", nextseqnum);

  nextseqnum = (nextseqnum + 1) % SR_SEQSPACE;
}

void SR_A_input(struct pkt packet) 
{
  int checksum = compute_checksum(packet);
  if (checksum != packet.checksum) {
      printf("SR_A_input: Corrupted ACK received, ignored\n");
      return;
  }
  int acknum = packet.acknum;
  if (!in_use[acknum]) {
    printf("SR_A_input: ACK for unused seq %d, ignoring\n", acknum);
    return;
  }
  acked[acknum] = true;
  in_use[acknum] = false;
  stoptimer(A);  
  printf("SR_A_input: ACK received for packet %d\n", acknum);

}

void SR_A_timerinterrupt(void) 
{int oldest = -1;
  float oldest_time = get_sim_time();
  
  for (int i = 0; i < SR_SEQSPACE; i++) {
      if (in_use[i] && !acked[i] && timer_start[i] < oldest_time) {
          oldest = i;
          oldest_time = timer_start[i];
      }
  }
  if (oldest != -1) {
    tolayer3(A, window[oldest]);
    starttimer(A, RTT);
    timer_start[oldest] = get_sim_time();
    printf("SR_A_timerinterrupt: Timeout for packet %d, retransmitted\n", oldest);
  }

}

void SR_B_input(struct pkt packet) 
{ int checksum = compute_checksum(packet);
  if (checksum != packet.checksum) {
      printf("SR_B_input: Corrupted packet %d, discarded\n", packet.seqnum);
      return;
  }
  struct pkt ackpkt;
  ackpkt.seqnum = 0;
  ackpkt.acknum = packet.seqnum;
  memcpy(ackpkt.payload, "ACK", 4);
  ackpkt.checksum = 0;
  ackpkt.checksum = compute_checksum(ackpkt);

  tolayer3(B, ackpkt);
  printf("SR_B_input: Sent ACK for %d\n", packet.seqnum);

  if (!received[packet.seqnum]) {
    recv_buffer[packet.seqnum] = packet;
    received[packet.seqnum] = true;
    printf("SR_B_input: Buffered packet %d\n", packet.seqnum);
  }

  while (received[expectedseqnum_B]) {
    tolayer5(B, recv_buffer[expectedseqnum_B].payload);
    printf("SR_B_input: Delivered packet %d to layer5\n", expectedseqnum_B);
    received[expectedseqnum_B] = false;
    expectedseqnum_B = (expectedseqnum_B + 1) % SR_SEQSPACE;
  }
}

void SR_B_init(void) 
{expectedseqnum_B = 0;
  for (int i = 0; i < SR_SEQSPACE; i++) {
      received[i] = false;
  } 
}

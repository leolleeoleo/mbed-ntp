/* NTPClient.cpp */
/*
Copyright (C) 2012 ARM Limited.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#define __DEBUG__ 0 //Disabled
#ifndef __MODULE__
#define __MODULE__ "NTPClient.cpp"
#endif

#include "core/fwk.h"

#include "NTPClient.h"

#include "UDPSocket.h"

#include "mbed.h" //time() and set_time()

#define NTP_PORT 123
#define NTP_CLIENT_PORT 0 //Random port
#define NTP_REQUEST_TIMEOUT 15000
#define NTP_TIMESTAMP_DELTA 2208988800ull //Diff btw a UNIX timestamp (Starting Jan, 1st 1970) and a NTP timestamp (Starting Jan, 1st 1900)

NTPClient::NTPClient()
{


}

int NTPClient::setTime(const char* host, uint16_t port, uint32_t timeout)
{
#if __DEBUG__ >= 3
  time_t ctTime;
  ctTime = time(NULL);
  INFO("Time is set to (UTC): %s", ctime(&ctTime));
#endif

  //Create & bind socket
  DBG("Creating socket");
  UDPSocket sock;
  sock.bind(0); //Bind to a random port

  struct NTPPacket pkt;

  //Now ping the server and wait for response
  DBG("Ping");
  //Prepare NTP Packet:
  pkt.li = 0; //Leap Indicator : No warning
  pkt.vn = 4; //Version Number : 4
  pkt.mode = 3; //Client mode
  pkt.stratum = 0; //Not relevant here
  pkt.poll = 0; //Not significant as well
  pkt.precision = 0; //Neither this one is

  pkt.rootDelay = 0; //Or this one
  pkt.rootDispersion = 0; //Or that one
  pkt.refId = 0; //...

  pkt.refTm_s = 0;
  pkt.origTm_s = 0;
  pkt.rxTm_s = 0;
  pkt.txTm_s = htonl( NTP_TIMESTAMP_DELTA + time(NULL) ); //WARN: We are in LE format, network byte order is BE

  pkt.refTm_f = pkt.origTm_f = pkt.rxTm_f = pkt.txTm_f = 0;

  //Set timeout, non-blocking and wait using select
  if( sock.sendTo( (char*)&pkt, sizeof(NTPPacket), host, port, NTP_REQUEST_TIMEOUT ) < 0 )
  {
    ERR("Could not send packet");
    sock.close();
    return NET_CONN;
  }

  //Read response
  DBG("Pong");
  char* inHost;
  int inPort;
  do
  {
    ret = sock.receiveFrom( (char*)&pkt, sizeof(NTPPacket), &inHost, inPort); //FIXME need a DNS Resolver to actually compare the incoming address with the DNS name
    if(ret < 0)
    {
      ERR("Could not receive packet");
      sock.close();
      return NET_CONN;
    }
  } while( respAddr.sin_addr.s_addr != serverAddr.sin_addr.s_addr);

  if(ret < sizeof(NTPPacket)) //TODO: Accept chunks
  {
    ERR("Receive packet size does not match");
    sock.close();
    return NET_PROTOCOL;
  }

  if( pkt.stratum == 0)  //Kiss of death message : Not good !
  {
    ERR("Kissed to death!");
    sock.close();
    return NTP_PORT;
  }

  //Correct Endianness
  pkt.refTm_s = ntohl( pkt.refTm_s );
  pkt.refTm_f = ntohl( pkt.refTm_f );
  pkt.origTm_s = ntohl( pkt.origTm_s );
  pkt.origTm_f = ntohl( pkt.origTm_f );
  pkt.rxTm_s = ntohl( pkt.rxTm_s );
  pkt.rxTm_f = ntohl( pkt.rxTm_f );
  pkt.txTm_s = ntohl( pkt.txTm_s );
  pkt.txTm_f = ntohl( pkt.txTm_f );

  //Compute offset, see RFC 4330 p.13
  uint32_t destTm_s = (NTP_TIMESTAMP_DELTA + time(NULL));
  int64_t offset = ( (int64_t)( pkt.rxTm_s - pkt.origTm_s ) + (int64_t) ( pkt.txTm_s - destTm_s ) ) / 2; //Avoid overflow
  DBG("Sent @%ul", pkt.txTm_s);
  DBG("Offset: %ul", offset);
  //Set time accordingly
  set_time( time(NULL) + offset );

#if __DEBUG__ >= 3
  ctTime = time(NULL);
  INFO("Time is now (UTC): %s", ctime(&ctTime));
#endif

  sock.close();

  return OK;
}


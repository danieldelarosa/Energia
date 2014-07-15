/* TO DO:
 *   1) Add a way to check if the connection has been dropped by the server
 * X 2) Make this an extension of the print class in print.cpp
 *   3) Figure out what status is supposed to do
 *   4) Prevent the wrong methods from being called based on server or client side
 *
 */

/*
 WiFiClient.cpp - Adaptation of Arduino WiFi library for Energia and CC3200 launchpad
 Author: Noah Luskey | LuskeyNoah@gmail.com
 
 WiFiClient objects suffer from a bit of an existential crisis, where really
 the same class serves two separate purposes. Client instances can exist server
 side (as a wrapper for a port to send messages to), or client side, as the 
 object attempting to make a connection. Only certain calls should be made in 
 each instance, and effort has been made to prevent the user from being able
 to mess things up with the wrong function calls.
 
 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.
 
 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.
 
 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

extern "C" {
  #include "utility/wl_definitions.h"
  #include "SimpleLink/Socket.h"
}

#include "WiFi.h"
#include "WiFiClient.h"
#include "WiFiServer.h"

//--tested, working--//
//--client side--//
WiFiClient::WiFiClient()
{
    //
    //initialize to empty buffer and no socket assigned yet
    //
    rx_currentIndex = 0;
    rx_fillLevel = 0;
    _socketIndex = NO_SOCKET_AVAIL;
    
}

//--tested, working--//
//--server side--//
WiFiClient::WiFiClient(uint8_t socketIndex)
{
    //
    //this is called by the server class. Initialize with the assigned socket index
    //
    rx_currentIndex = 0;
    rx_fillLevel = 0;
    _socketIndex = socketIndex;
}

//WiFiClient::~WiFiClient()
//{
//    //
//    //don't do anything if the socket was never set up
//    //
//    if (_socketIndex == NO_SOCKET_AVAIL) {
//        return;
//    }
//    
//    //
//    //close the socket
//    //
//    int socketHandle = WiFiClass::_handleArray[_socketIndex];
//    sl_Close(socketHandle);
//    
//    //
//    //clear the tracking stuff from the WiFiClass arrays
//    //
//    WiFiClass::_handleArray[_socketIndex] = -1;
//    WiFiClass::_portArray[_socketIndex] = -1;
//    WiFiClass::_typeArray[_socketIndex] = -1;
//    
//}

//--tested, working--//
//--client side--//
int WiFiClient::connect(const char* host, uint16_t port)
{
    //
    //get the host ip address
    //
    IPAddress hostIP(0,0,0,0);
    int success = WiFi.hostByName((char*)host, hostIP);
    if (!success) {
        return false;
    }
    
    return connect(hostIP, port);
}

//--tested, working--//
//--client side--//
int WiFiClient::connect(IPAddress ip, uint16_t port)
{
    //
    //this function should only be called once and only on the client side
    //
    if (_socketIndex != NO_SOCKET_AVAIL) {
        return false;
    }
    
    
    //
    //get a socket index and attempt to create a socket
    //note that the socket is intentionally left as BLOCKING. This allows an
    //abusive user to send as many requests as they want as fast as they can try
    //and it won't overload simplelink.
    //
    int socketIndex = WiFiClass::getSocket();
    if (socketIndex == NO_SOCKET_AVAIL) {
        return false;
    }
    int socketHandle = sl_Socket(SL_AF_INET, SL_SOCK_STREAM, SL_IPPROTO_TCP);
    if (socketHandle < 0) {
        return false;
    }
    
    //
    //connect the socket to the requested IP address and port. Check for success
    //
    SlSockAddrIn_t server = {0};
    server.sin_family = SL_AF_INET;
    server.sin_port = sl_Htons(port);
    server.sin_addr.s_addr = ip;
    int iRet = sl_Connect(socketHandle, (SlSockAddr_t*)&server, sizeof(SlSockAddrIn_t));
    if (iRet < 0) {
        sl_Close(socketHandle);
        return false;
    }

    //
    //we've successfully created a socket and connected, so store the
    //information in the arrays provided by WiFiClass
    //
    _socketIndex = socketIndex;
    WiFiClass::_handleArray[socketIndex] = socketHandle;
    WiFiClass::_typeArray[socketIndex] = TYPE_TCP_CLIENT;
    WiFiClass::_portArray[socketIndex] = port;
    return true;
}

//--tested, working--//
//--client and server side--//
size_t WiFiClient::write(uint8_t b)
{
    //
    //don't do anything if not properly set up
    //
    if (_socketIndex == NO_SOCKET_AVAIL) {
        return 0;
    }
    
    //
    //write the single byte to the server the client is connected to
    //
    return write(&b, 1);
}

//--tested, working--//
//--client and server side--//
size_t WiFiClient::write(const uint8_t *buffer, size_t size)
{
    //
    //don't do anything if not properly set up
    //
    if (_socketIndex == NO_SOCKET_AVAIL) {
        return 0;
    }
    
    //
    //write the bufer to the socket
    //
    int iRet = sl_Send(WiFiClass::_handleArray[_socketIndex], buffer, size, NULL);
    if ((iRet < 0) || (iRet != size)) {
        //
        //if an error occured or the socket has died, call stop()
        //to make the object aware that it's dead
        //
        stop();
        return 0;
    } else {
        return iRet;
    }
}

//--tested, working--//
//--client and server side--//
int WiFiClient::available()
{
    //
    //don't do anything if not properly set up
    //
    if (_socketIndex == NO_SOCKET_AVAIL) {
        return 0;
    }
    
    //
    //if the buffer doesn't have any data in it or we've read everything
    //then receive some data
    //
    int bytesLeft = rx_fillLevel - rx_currentIndex;
    if (bytesLeft <= 0) {
        //
        //Receive any pending information into the buffer
        //if the connection has died, call stop() to make the object aware it's dead
        //
        int iRet = sl_Recv(WiFiClass::_handleArray[_socketIndex], rx_buffer, TCP_RX_BUFF_MAX_SIZE, NULL);
        if ((iRet < 0)  &&  (iRet != SL_EAGAIN)) {
            stop();
            memset(rx_buffer, 0, TCP_RX_BUFF_MAX_SIZE);
            return 0;
        }
        
        //
        //receive successful. Reset rx index pointer and set buffer fill level indicator
        //(if SL_EAGAIN was received, the actual number of bytes received was zero, not -11)
        //
        rx_currentIndex = 0;
        rx_fillLevel = iRet != SL_EAGAIN ? iRet : 0;
        bytesLeft = rx_currentIndex - rx_fillLevel;
    }
    
    //
    //limit bytes left to >= 0 (although it *shouldn't* ever become negative)
    //
    if (bytesLeft < 0) {
        bytesLeft = 0;
    }
    return bytesLeft;
}

//--tested, working--//
int WiFiClient::read()
{
    //
    //return a single byte from the rx buffer (or zero if past the end)
    //
    if (rx_currentIndex < rx_fillLevel) {
        return rx_buffer[rx_currentIndex++];
    } else {
        return 0;
    }
}

//--tested, working--//
int WiFiClient::read(uint8_t* buf, size_t size)
{
    //
    //read the requested number of bytes into the buffer
    //this won't read past the end of the data since read() handles that
    //
    int i;
    for (i = 0; i < size; i++) {
        buf[i] = read();
    }
    return i;
}

//--tested, working--//
int WiFiClient::peek()
{
    //
    //return the next byte in the buffer or zero if we're past the end of the data
    //
    if (rx_currentIndex < rx_fillLevel) {
        return rx_buffer[rx_currentIndex+1];
    } else {
        return 0;
    }
}

//--tested, working--//
void WiFiClient::flush()
{
    //
    //clear out the buffer and reset all the buffer indicators
    //
    memset(rx_buffer, 0, TCP_RX_BUFF_MAX_SIZE);
    rx_fillLevel = 0;
    rx_currentIndex = 0;
}

//--tested, working--//
void WiFiClient::stop()
{
    //
    //don't do anything if not properly set up
    //
    if (_socketIndex == NO_SOCKET_AVAIL) {
        return;
    }
    
    //
    //disconnect, destroy the socket, and reset the socket tracking variables
    //in WiFiClass, but don't destroy any of the received data
    //
    int iRet = sl_Close(WiFiClass::_handleArray[_socketIndex]);
    if (iRet < 0) {
        return;
    }
    
    //
    //since no error occurred while closing the socket, reset WiFiClass variables
    //
    WiFiClass::_portArray[_socketIndex] = -1;
    WiFiClass::_handleArray[_socketIndex] = -1;
    WiFiClass::_typeArray[_socketIndex] = -1;
    _socketIndex = NO_SOCKET_AVAIL;
    
}

//!! works, sort of, dependent on status(), which needs work !!//
uint8_t WiFiClient::connected()
{
    //
    //as described by the arduino api, this will return true even if the client has
    //disconnected as long as there is data left in the buffer
    //
    if ( status() ) {
        return true;
    } else if (rx_currentIndex < rx_fillLevel) {
        return true;
    } else {
        return false;
    }
    
}

//!! works, sort of, needs to actually test connection !!//
uint8_t WiFiClient::status()
{
    //
    //This basically just checks if stop() has been called
    //!! is there a way to test the tcp connection without a write?
    //
    return _socketIndex != NO_SOCKET_AVAIL ? true : false;
}

//--tested, working--//
WiFiClient::operator bool()
{
    //
    //a "fake" client instance with index==255 should evaluate to false
    //
    return _socketIndex != 255;
}




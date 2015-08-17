/*
  WiFiClient.cpp - TCP/IP client for esp8266, mostly compatible
                   with Arduino WiFi shield library

  Copyright (c) 2014 Ivan Grokhotkov. All rights reserved.
  This file is part of the esp8266 core for Arduino environment.

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

#define LWIP_INTERNAL

extern "C"
{
    #include "include/wl_definitions.h"
    #include "osapi.h"
    #include "ets_sys.h"
}

#include "debug.h"
#include "ESP8266WiFi.h"
#include "WiFiClient.h"
#include "WiFiServer.h"
#include "lwip/opt.h"
#include "lwip/ip.h"
#include "lwip/tcp.h"
#include "lwip/inet.h"
#include "lwip/netif.h"
#include "cbuf.h"
#include "include/ClientContext.h"
#include "c_types.h"

uint16_t WiFiClient::_localPort = 0;

template<>
WiFiClient* SList<WiFiClient>::_s_first = 0;


WiFiClient::WiFiClient()
: _client(0)
{
    WiFiClient::_add(this);
}

WiFiClient::WiFiClient(ClientContext* client) : _client(client)
{
    _client->ref();
    WiFiClient::_add(this);
}

WiFiClient::~WiFiClient()
{
    WiFiClient::_remove(this);
    if (_client)
        _client->unref();
}

WiFiClient::WiFiClient(const WiFiClient& other)
{
    _client = other._client;
    if (_client)
        _client->ref();
    WiFiClient::_add(this);
}

WiFiClient& WiFiClient::operator=(const WiFiClient& other)
{
   if (_client)
        _client->unref();
    _client = other._client;
    if (_client)
        _client->ref();
    return *this;
}


int WiFiClient::connectex(const char* host, uint16_t port,bool block)
{
    IPAddress remote_addr;
    if (WiFi.hostByName(host, remote_addr))
    {
        return connectex(remote_addr, port,block);
    }
    return 0;
}

int WiFiClient::connectex(IPAddress ip, uint16_t port,bool block)
{
    ip_addr_t addr;
    addr.addr = ip;

    if (_client)
        stop();

    // if the default interface is down, tcp_connect exits early without
    // ever calling tcp_err
    // http://lists.gnu.org/archive/html/lwip-devel/2010-05/msg00001.html
    netif* interface = ip_route(&addr);
    if (!interface) {
        DEBUGV("no route to host\r\n");
        return 0;
    }

    tcp_pcb* pcb = tcp_new();
    if (!pcb)
        return 0;

    if (_localPort > 0) {
        pcb->local_port = _localPort++;
    }

    tcp_arg(pcb, this);
    tcp_err(pcb, &WiFiClient::_s_err);
    if (block) {
        tcp_connect(pcb, &addr, port, reinterpret_cast<tcp_connected_fn>(&WiFiClient::_s_connected));

        esp_yield();
        if (_client)
            return 1;
        //  if tcp_error was called, pcb has already been destroyed.
        // tcp_abort(pcb);
        return 0;
    } else {
        tcp_connect(pcb, &addr, port, reinterpret_cast<tcp_connected_fn>(&WiFiClient::_s_connected_nb));
        return -1;
    }
}

int8_t WiFiClient::_connected(void* pcb, int8_t err)
{
    tcp_pcb* tpcb = reinterpret_cast<tcp_pcb*>(pcb);
    _client = new ClientContext(tpcb, 0, 0);
    _client->ref();
    esp_schedule();
    return ERR_OK;
}

int8_t WiFiClient::_connected_nb(void* pcb, int8_t err)
{
    tcp_pcb* tpcb = reinterpret_cast<tcp_pcb*>(pcb);
    _client = new ClientContext(tpcb, 0, 0);
    _client->ref();
    return ERR_OK;
}

void WiFiClient::_err(int8_t err)
{
    DEBUGV(":err %d\r\n", err);
    esp_schedule();
}


void WiFiClient::setNoDelay(bool nodelay) {
    if (!_client)
        return;
    _client->setNoDelay(nodelay);
}

bool WiFiClient::getNoDelay() {
    if (!_client)
        return false;
    return _client->getNoDelay();
}

size_t WiFiClient::write(uint8_t b)
{
    return write(&b, 1);
}

size_t WiFiClient::write(const uint8_t *buf, size_t size)
{
    if (!_client || !size)
    {
        return 0;
    }

    return _client->write(reinterpret_cast<const char*>(buf), size);
}

int WiFiClient::available()
{
    if (!_client)
        return false;

    int result = _client->getSize();

    if (!result) {
        optimistic_yield(100);
    }
    return result;
}

int WiFiClient::read()
{
    if (!available())
        return -1;

    return _client->read();
}


int WiFiClient::read(uint8_t* buf, size_t size)
{
    return (int) _client->read(reinterpret_cast<char*>(buf), size);
}

int WiFiClient::peek()
{
    if (!available())
        return -1;

    return _client->peek();
}

void WiFiClient::flush()
{
    if (_client)
        _client->flush();
}

void WiFiClient::stop()
{
    if (!_client)
        return;

    _client->unref();
    _client = 0;
}

uint8_t WiFiClient::connected()
{
    if (!_client)
        return 0;

    return _client->state() == ESTABLISHED || available();
}

uint8_t WiFiClient::status()
{
    if (!_client)
        return CLOSED;
    return _client->state();
}

 WiFiClient::operator bool()
{
    return _client != 0;
}

IPAddress WiFiClient::remoteIP()
{
    if (!_client)
        return IPAddress(0U);

    return IPAddress(_client->getRemoteAddress());
}

uint16_t WiFiClient::remotePort()
{
    if (!_client)
        return 0;

    return _client->getRemotePort();
}

int8_t WiFiClient::_s_connected(void* arg, void* tpcb, int8_t err)
{
    return reinterpret_cast<WiFiClient*>(arg)->_connected(tpcb, err);
}

int8_t WiFiClient::_s_connected_nb(void* arg, void* tpcb, int8_t err)
{
    return reinterpret_cast<WiFiClient*>(arg)->_connected_nb(tpcb, err);
}

void WiFiClient::_s_err(void* arg, int8_t err)
{
    reinterpret_cast<WiFiClient*>(arg)->_err(err);
}

void WiFiClient::stopAll()
{
    for (WiFiClient* it = _s_first; it; it = it->_next) {
        ClientContext* c = it->_client;
        if (c) {
            c->abort();
            c->unref();
            it->_client = 0;
        }
    }
}

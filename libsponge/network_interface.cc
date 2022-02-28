#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`


using namespace std;



//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//send an arp message within an ethernet frame
void NetworkInterface::send_arp(const uint16_t &opcode, const uint32_t &target_ip, const EthernetAddress &target_eth={0,0,0,0,0,0})
{
    EthernetFrame new_frame;
    new_frame.header().type=EthernetHeader::TYPE_ARP;
    new_frame.header().src=_ethernet_address;
    if(opcode==ARPMessage::OPCODE_REQUEST)
        new_frame.header().dst=ETHERNET_BROADCAST;
    else
        new_frame.header().dst=target_eth;
    ARPMessage new_request;
    new_request.opcode=opcode;
    new_request.sender_ethernet_address=_ethernet_address;
    new_request.sender_ip_address=_ip_address.ipv4_numeric();
    //For an arp request message, the target eth addr is of no significance
    if(opcode==ARPMessage::OPCODE_REPLY)
        new_request.target_ethernet_address=target_eth;
    new_request.target_ip_address=target_ip;
    new_frame.payload()=new_request.serialize();
    _frames_out.push(new_frame);
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    if(_cached_mappings.find(next_hop_ip)!=_cached_mappings.end())//Found in cache
    {
        EthernetFrame new_frame;
        new_frame.header().type=EthernetHeader::TYPE_IPv4;
        new_frame.header().src=_ethernet_address;
        new_frame.header().dst=_cached_mappings[next_hop_ip];
        new_frame.payload()=dgram.serialize();
        _frames_out.push(new_frame);
        return;
    }
    else//Not found in cache, may need to send an arp request
    {
        bool found=false;
        //Check if this arp request have been sent recently
        for(auto& it:_arp_send_timer)
        {
            //this ip have recently been sent
            if(it.first==next_hop_ip)
            {
                found=true;
                if(it.second>5000)//It have been at least 5sec since last sent
                {
                    //store the dgram into queue, resend arp request
                    it.second=0;
                    _datagrams_waiting.push_back({dgram, next_hop});
                    //resend an arp request after this loop
                    break;
                }
                else//store the dgram into queue, wait for arp reply
                {
                    _datagrams_waiting.push_back({dgram, next_hop});
                    //return and wait for reply
                    return;
                }
            }
        }
        //if not found, add dgram into the queue and add the ip into timer list
        if(!found)
        {
            _arp_send_timer.push_back({next_hop_ip, 0});
            _datagrams_waiting.push_back({dgram, next_hop});
        }
        //Send an arp request
        send_arp(ARPMessage::OPCODE_REQUEST, next_hop_ip);
        
        return;
    }


}

//Update mapping entries and entry timer, remove invalid term of send timer
void NetworkInterface::update_mappings(const uint32_t &new_ip, const EthernetAddress &new_eth)
{
    //Refresh mapping and mapping timer
    bool found=false;
    _cached_mappings[new_ip]=new_eth;
    for(auto& it:_arp_mapping_timer)
    {
        if(it.first==new_ip)
        {
            it.second=0;
            found=true;
        }
    }
    if(!found)
    {
        _arp_mapping_timer.push_back({new_ip, 0});
    }
    //Remove send timer for that ip
    auto it=_arp_send_timer.begin();
    while(it!=_arp_send_timer.end())
    {
        if(it->first==new_ip)
        {
            auto previous=it++;
            _arp_send_timer.erase(previous);
        }
        else
            it++;
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    if(frame.header().dst!=_ethernet_address&&frame.header().dst!=ETHERNET_BROADCAST)
        return {};
    if(frame.header().type==EthernetHeader::TYPE_IPv4)
    {
        InternetDatagram new_dgram;
        if(new_dgram.parse(frame.payload())==ParseResult::NoError)
            return new_dgram;
    }
    else if(frame.header().type==EthernetHeader::TYPE_ARP)
    {
        ARPMessage new_message;
        if(new_message.parse(frame.payload())==ParseResult::NoError)
        {
            if(new_message.opcode==ARPMessage::OPCODE_REPLY)
            {
                //update mappings with arp reply info
                update_mappings(new_message.sender_ip_address, new_message.sender_ethernet_address);
            }
            else if(new_message.opcode==ARPMessage::OPCODE_REQUEST)
            {
                //also update mappings with arp request info
                update_mappings(new_message.sender_ip_address, new_message.sender_ethernet_address);
                //if this request is targeted towards us, then send proper reply
                if(new_message.target_ip_address==_ip_address.ipv4_numeric())
                    send_arp(ARPMessage::OPCODE_REPLY, new_message.sender_ip_address, new_message.sender_ethernet_address);
            }
            //check datagrams in queue, if the mapping of their next hop addr is now availiable then send them
            auto it=_datagrams_waiting.begin();
            while(it!=_datagrams_waiting.end())
            {
                if(_cached_mappings.find(it->second.ipv4_numeric())!=_cached_mappings.end())
                {
                    auto previous=it++;
                    send_datagram(previous->first, previous->second);
                    _datagrams_waiting.erase(previous);
                }
                else
                {
                    it++;
                }
            }
        }  
    }
    return {};
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) { 
    //Increment timer
    for(auto& it:_arp_send_timer)
    {
        it.second+=ms_since_last_tick;
    }
    for(auto& it:_arp_mapping_timer)
    {
        it.second+=ms_since_last_tick;
    }
    //Check expired entries and remove it from cache
    auto it=_arp_mapping_timer.begin();
    while(it!=_arp_mapping_timer.end())
    {
        //mapping entries timeout is 30sec
        if(it->second>30000)
        {
            auto previous=it++;
            //remove relevant entry from mapping cache and entry timer
            _cached_mappings.erase(previous->first);
            _arp_mapping_timer.erase(previous);
        }
        else
            it++;
    }
    return;
 }

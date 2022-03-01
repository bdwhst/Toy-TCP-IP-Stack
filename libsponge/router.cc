#include "router.hh"

#include <iostream>

using namespace std;

// Dummy implementation of an IP router

// Given an incoming Internet datagram, the router decides
// (1) which interface to send it out on, and
// (2) what next hop address to send it to.

// For Lab 6, please replace with a real implementation that passes the
// automated checks run by `make check_lab6`.

// You will need to add private members to the class declaration in `router.hh`

uint32_t Router::get_mask(const size_t index) const
{
    uint8_t prefix_length=_route_table[index].prefix_length;
    if(prefix_length==0)
        return 0;
    int32_t mask=1;
    return mask<<31>>(prefix_length-1);
}

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num << "\n";
    _route_table.push_back({route_prefix, prefix_length, next_hop, interface_num});
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    if(!dgram.header().ttl||!(--dgram.header().ttl))
        return;
    size_t max_match_entry=0;
    uint8_t max_match_prefix_len=0;
    uint32_t dst=dgram.header().dst;
    bool found=false;
    for(size_t i=0;i<_route_table.size();i++)
    {
        uint32_t mask=get_mask(i);
        if(((dst&mask)==(_route_table[i].route_prefix&mask)))
        {
            found=true;
            if(_route_table[i].prefix_length>=max_match_prefix_len)
            {
                max_match_entry=i;
            }
        }
    }
    if(!found)
        return;
    Address dst_ip=_route_table[max_match_entry].next_hop.value_or(Address::from_ipv4_numeric(dgram.header().dst));
    interface(_route_table[max_match_entry].interface_num).send_datagram(dgram, dst_ip);
}

void Router::route() {
    // Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
    for (auto &interface : _interfaces) {
        auto &queue = interface.datagrams_out();
        while (not queue.empty()) {
            route_one_datagram(queue.front());
            queue.pop();
        }
    }
}

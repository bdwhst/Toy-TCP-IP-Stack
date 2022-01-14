#include "wrapping_integers.hh"

// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.


using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    uint32_t value=static_cast<uint32_t>(n+isn.raw_value());
    return WrappingInt32{value};
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    uint32_t value=n-isn;
    uint64_t res1=checkpoint>>32;
    uint64_t res2=res1+1;
    uint64_t res3=res1-1;
    res1=(res1<<32)+value;
    res2=(res2<<32)+value;
    res3=(res3<<32)+value;
    
    if(res1>checkpoint)
    {
        //choose the closest solution between little and middle seqno
        if(res1-checkpoint<checkpoint-res3||checkpoint<0x100000000ul)
        {
            return res1;
        }
        else
        {
            return res3;
        }
    }
    else
    {
        //choose the closest solution between big and middle seqno
        if(checkpoint-res1<res2-checkpoint||checkpoint>0xFFFFFFFF00000000ul)
        {
            return res1;
        }
        else
        {
            return res2;
        }
    }

    
}

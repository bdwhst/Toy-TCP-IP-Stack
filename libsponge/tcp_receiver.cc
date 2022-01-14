#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.



using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    if(seg.header().syn)
    {
        synced=true;
        isn=seg.header().seqno;
    }
    if(synced)
    {
        //reference to number of bytes_written
        //if PREVIOUSLY synced then subtract one from unwrapped seqno (syn will make seqno increment by one)
        uint64_t stream_index=unwrap(seg.header().seqno, isn, _reassembler.stream_out().bytes_written())-!seg.header().syn;
        _reassembler.push_substring(seg.payload().copy(), stream_index, seg.header().fin);
    }
        
    
}

optional<WrappingInt32> TCPReceiver::ackno() const { 
    if(synced)
        //both sync and fin will make the sequence no increment by one
        //use input_ended() to check if the reassembler has reassembled streams to fin
        return wrap(_reassembler.stream_out().bytes_written(), isn)+synced+_reassembler.stream_out().input_ended(); 
    else
        return {};
}

size_t TCPReceiver::window_size() const { 
    return _capacity-_reassembler.stream_out().buffer_size(); 
}

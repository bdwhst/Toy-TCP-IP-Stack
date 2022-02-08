#include "tcp_connection.hh"

#include <iostream>


using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { 
    return _sender.stream_in().remaining_capacity();
 }

size_t TCPConnection::bytes_in_flight() const { 
    return _sender.bytes_in_flight(); 
}

size_t TCPConnection::unassembled_bytes() const { 
    return _receiver.unassembled_bytes(); 
}

size_t TCPConnection::time_since_last_segment_received() const { return _timer_since_last_received; }

void TCPConnection::abort_connection()
{
    _sender.stream_in().set_error();
    _receiver.stream_out().set_error();
    _is_active=false;
}

//pop and send a segment from the sender's queue
void TCPConnection::send_a_segment_with_ack()
{
    if(_sender.segments_out().empty())
        return;
    TCPSegment& seg_to_send=_sender.segments_out().front();
    if(_receiver.ackno().has_value())
    {
        seg_to_send.header().ack=true;
        seg_to_send.header().ackno=_receiver.ackno().value();
    }
    seg_to_send.header().win=_receiver.window_size();
    _segments_out.push(seg_to_send);
    _sender.segments_out().pop();
}

void TCPConnection::segment_received(const TCPSegment &seg) { 
    //std::cout<<"Segment received!"<<std::endl;
    if(!_is_active)
        return;
    _timer_since_last_received=0;
    if(seg.header().rst)
    {
        abort_connection();
        return;
    }
    if(!_receiver.stream_out().input_ended())
        _receiver.segment_received(seg);
    if(_receiver.stream_out().input_ended()&&(_sender.next_seqno_absolute()<_sender.stream_in().bytes_written()+2))
        _linger_after_streams_finish=false;
    if(seg.header().ack&&_sender.next_seqno_absolute()!=0)
    {
        _sender.ack_received(seg.header().ackno, seg.header().win);
        _sender.fill_window();
    }
    if(seg.header().syn)
    {
        _sender.fill_window();
    }

    if(!_sender.segments_out().empty())
    {
        do
        {
            send_a_segment_with_ack();
        } while (!_sender.segments_out().empty());
    }
    else if(seg.length_in_sequence_space()>0||
    (_receiver.ackno().has_value()&&(seg.length_in_sequence_space()==0)
    &&seg.header().seqno==_receiver.ackno().value()-1))
    {
        _sender.send_empty_segment();
        send_a_segment_with_ack();
    }

    

}

void TCPConnection::send_a_rst_segment()
{
    _sender.fill_window();
    if(_sender.segments_out().empty())
        _sender.send_empty_segment();
    TCPSegment& seg_to_send=_sender.segments_out().front();
    seg_to_send.header().rst=true;
    _segments_out.push(seg_to_send);
    _sender.segments_out().pop();
}

bool TCPConnection::active() const { return _is_active; }

size_t TCPConnection::write(const string &data) {
    size_t num_data=_sender.stream_in().write(data);
    _sender.fill_window();
    
    while (!_sender.segments_out().empty())
    {
        send_a_segment_with_ack();
    } 
    return num_data;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) { 
    if(!_is_active)
        return;
    _timer_since_last_received+=ms_since_last_tick;
    _sender.tick(ms_since_last_tick);
    if(_sender.consecutive_retransmissions()>TCPConfig::MAX_RETX_ATTEMPTS)
    {
        send_a_rst_segment();
        abort_connection();
        return;
    }
    else
        send_a_segment_with_ack();
    

    if(_receiver.stream_out().input_ended()&&_receiver.unassembled_bytes()==0
    &&_sender.stream_in().eof()&&(_sender.next_seqno_absolute()==_sender.stream_in().bytes_written()+2)
    &&_sender.bytes_in_flight()==0)
    {
        if(!_linger_after_streams_finish||_timer_since_last_received>=10*_cfg.rt_timeout)
            _is_active=false;
    }


}

void TCPConnection::end_input_stream() {
    //cout<<"Ending outbound stream!"<<endl;
    _sender.stream_in().end_input();
    _sender.fill_window();
    while (!_sender.segments_out().empty())
    {
        send_a_segment_with_ack();
    } 
}

void TCPConnection::connect() {
    if(_sender.next_seqno_absolute()==0)
    {
        _sender.fill_window();
        send_a_segment_with_ack();
    }

}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            send_a_rst_segment();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

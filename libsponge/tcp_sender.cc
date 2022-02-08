#include "tcp_sender.hh"
#include <iostream>
#include "tcp_config.hh"

#include <random>



using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _curr_retransmission_timeout(retx_timeout)
    , _stream(capacity)
    , _timer(retx_timeout) { }

uint64_t TCPSender::bytes_in_flight() const { 
    size_t count=0;
    for(const auto& it:_segments_in_flight)
    {
        count+=it.length_in_sequence_space();
    }
    return count; 
}

//Send a non-empty segment(non-empty in sequence space)
void TCPSender::send_a_segment(uint16_t window_size) {
    //std::cout<<"Sending a segment"<<std::endl;
    //If window_size is 0 or the sender finished sending, do nothing
    if(!window_size||_sender_finished)
        return;
    TCPSegment new_seg;
    //Determine whether to mark syn flag
    if(_next_seqno==0&&_stream.bytes_read()==0)
    {
        //modify the segment to send
        new_seg.header().seqno=wrap(0, _isn);
        new_seg.header().syn=true;
        _next_seqno+=1;
        _sender_win_size-=1;
    }
    else
    {
        //If the buffer is empty and the read stream is not ended, then do nothing
        if(_stream.buffer_empty()&&!_stream.eof())
            return;
        //modify the segment to send
        new_seg.header().seqno=wrap(_next_seqno, _isn);
        new_seg.payload()=_stream.read(window_size);
        _next_seqno+=new_seg.payload().size();
        _sender_win_size-=new_seg.payload().size();
    }
    //Determine whether to mark fin flag
    //check _sender_win_size instead of win_size
    //because we can't know if the input stream is ended untill we actually read to EOF
    //Under such circumstances, the window_size (equals to MAX_PAYLOAD_SIZE) may be used up
    //But we can still append a fin byte in sequence space
    if(_stream.eof()&&_sender_win_size)
    {
        //add fin flag
        new_seg.header().fin=true;
        _next_seqno+=1;
        _sender_win_size-=1;
        _sender_finished=true;
    }
    //push new segment to output queue and in_flight list
    _segments_out.push(new_seg);
    _segments_in_flight.push_back(new_seg);
    
    if(!_timer.is_started())
        _timer.restart(_curr_retransmission_timeout);
}

//Fill the sender window
void TCPSender::fill_window() {
    //If the sender window is less than or equal to 1452 then send a segment directly
    if(_sender_win_size<=TCPConfig::MAX_PAYLOAD_SIZE)
    {
        send_a_segment(_sender_win_size);
    }
    else //Split the sender window into several segments
    {
        uint16_t remaining_win_size=_sender_win_size;
        //Try to send segments whose payload is as large as possible
        while(remaining_win_size>TCPConfig::MAX_PAYLOAD_SIZE)
        {
            uint16_t segment_size=TCPConfig::MAX_PAYLOAD_SIZE;
            if(_next_seqno==0)
                segment_size+=1;
            send_a_segment(segment_size);
            remaining_win_size-=segment_size;
        }
        if(remaining_win_size)
            send_a_segment(remaining_win_size);
    }
}

//Update receiver and sender window size
void TCPSender::update_window(const WrappingInt32 ackno, const uint16_t window_size)
{
    uint64_t abs_ackno=unwrap(ackno, _isn, _next_seqno);
    _receiver_win_size=window_size;
    //Recalculate sender window size
    _sender_win_size=abs_ackno+_receiver_win_size-_next_seqno;
    if(_receiver_win_size<1)
        _sender_win_size+=1;
}

//Reset timeout, timer and # of consecutive retransmissions
void TCPSender::reset_retrans_parameters(bool timer_start)
{
    _curr_retransmission_timeout=_initial_retransmission_timeout;
    _timer.restart(_curr_retransmission_timeout);
    if(!timer_start)
        _timer.stop();
    _num_consecutive_retrans=0;
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) { 
    //std::cout<<"ACK received"<<std::endl;
    //Ignore invalid ack
    if(unwrap(ackno, _isn, _next_seqno)>_next_seqno)
        return;
    //Update two windows
    update_window(ackno, window_size);
    //If there are no segments in flight
    if(_segments_in_flight.empty())
    {
        reset_retrans_parameters(false);
        return;
    }
    //Compare abs_ackno to the last byte of the oldest segment in flight
    uint16_t num_segments_in_flight=_segments_in_flight.size();
    uint64_t front_seq_start_num=unwrap(_segments_in_flight.front().header().seqno, _isn, _next_seqno);
    uint64_t front_seq_end_num=front_seq_start_num+_segments_in_flight.front().length_in_sequence_space()-1;
    uint64_t abs_ackno=unwrap(ackno, _isn, _next_seqno);
    while(abs_ackno>front_seq_end_num)
    {
        //If this last byte has been acked then remove the segment from the temp storage
        _segments_in_flight.pop_front();
        if(_segments_in_flight.empty())
            break;
        front_seq_start_num=unwrap(_segments_in_flight.front().header().seqno, _isn, _next_seqno);
        front_seq_end_num=front_seq_start_num+_segments_in_flight.front().length_in_sequence_space()-1;
    }
    //If new ack received(segments in flight changed)
    if(num_segments_in_flight!=_segments_in_flight.size())
    {
        if(!_segments_in_flight.empty())
            reset_retrans_parameters(true);
        else
            reset_retrans_parameters(false);
    }
    return;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) { 
    //Ignore if timer hasn't started
    if(!_timer.is_started())
        return;
    _timer.tick(ms_since_last_tick);
    //if timer expired
    if(_timer.expired())
    {
        //Delete the acked segment from list
        _segments_out.push(_segments_in_flight.front());
        //If the receive window size is not zero, then do exponential backoff and increment the counter
        //On the contrary, if the receive window IS ZERO
        //this means the sender may be very eager to know when the receiver's window is free
        //So don't use exp backoff and counter timeout to stop frequent retransmission
        if(_receiver_win_size)
        {
            _num_consecutive_retrans++;
            _curr_retransmission_timeout*=2;
        }
        _timer.restart(_curr_retransmission_timeout);
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _num_consecutive_retrans; }

void TCPSender::send_empty_segment() { 
    TCPSegment new_seg;
    new_seg.header().seqno=wrap(_next_seqno, _isn);
    _segments_out.push(new_seg);
    return;
 }

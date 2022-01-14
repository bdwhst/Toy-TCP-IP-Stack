#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity), reassemble_buffer(), expected_index(0), buffer_end_index(0), eof_index(0xFFFFFFFFFFFFFFFF) {
    reassemble_buffer.resize(_capacity);
    for(auto& it:reassemble_buffer)
    {
        it.valid=false;
    }
}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    buffer_end_index=_capacity-_output.buffer_size();
    uint64_t segment_end_index=data.length();
    uint64_t starti,startj;
    if(index<expected_index)
    {
        starti=0;
        startj=expected_index-index;
    }
    else
    {
        startj=0;
        starti=index-expected_index;
    }
    size_t i, j;
    for(i=starti, j=startj;i<buffer_end_index&&j<segment_end_index;i++,j++)
    {
        reassemble_buffer[i].c=data[j];
        reassemble_buffer[i].valid=true;
    }

    if(eof){
        //Mark the position following the last byte of the stream to be eof
        eof_index=expected_index+starti+segment_end_index-startj;
    }

    write_to_stream();
    

    return;
}

//! write to output stream if possible
void StreamReassembler::write_to_stream()
{
    if(!reassemble_buffer[0].valid)
    {
        //Deal with the case when a empty string with eof has been pushed in
        if(expected_index==eof_index)
            _output.end_input();
        return;
    }
    size_t i=0;
    string temp_out;
    while (reassemble_buffer[i].valid&&i<buffer_end_index)
    {
        temp_out+=reassemble_buffer[i].c;
        i++;
    }
    _output.write(temp_out);
    size_t j;
    for(j=0;i<buffer_end_index;j++,i++)
    {
        reassemble_buffer[j]=reassemble_buffer[i];
        reassemble_buffer[i].valid=false;
    }
    //Update the remaining buffer size and expected index
    buffer_end_index-=(i-j);
    expected_index+=(i-j);
    //If this position has been marked as eof then end input
    if(expected_index==eof_index)
        _output.end_input();

    return;
}

size_t StreamReassembler::unassembled_bytes() const { 
    size_t count=0;
    for(size_t i=0;i<buffer_end_index;i++)
    {
        if(reassemble_buffer[i].valid)
            count++;
    }
    return count; 
}

bool StreamReassembler::empty() const { return buffer_end_index==0; }

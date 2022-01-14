#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`



using namespace std;

size_t ByteStream::write(const string &data) {
    size_t count=0;
    while(!is_full()&&(count!=data.length()))
    {
        buffer[rear]=data[count];
        rear=(rear+1)%capacity;
        count++;
        write_count++;
    }
    return count;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    string peek_data;
    size_t t_front=front;
    size_t len_count=len;
    while(t_front!=rear&&len_count)
    {
        peek_data+=buffer[t_front];
        t_front=(t_front+1)%capacity;
        len_count--;
    }
    return peek_data;
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) { 
    size_t len_count=len;
    while(len_count&&!is_empty())
    {
        front=(front+1)%capacity;
        len_count--;
        read_count++;
    }
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    string data;
    size_t len_count=len;
    while(!is_empty()&&len_count)
    {
        data+=buffer[front];
        front=(front+1)%capacity;
        len_count--;
        read_count++;
    }
    return data;
}

void ByteStream::end_input() { eof_flag=1; }

bool ByteStream::input_ended() const { return eof_flag; }

size_t ByteStream::buffer_size() const { return (rear+capacity-front)%capacity; }

bool ByteStream::buffer_empty() const { return is_empty(); }

bool ByteStream::eof() const { return eof_flag&&is_empty(); }

size_t ByteStream::bytes_written() const { return write_count; }

size_t ByteStream::bytes_read() const { return read_count; }

size_t ByteStream::remaining_capacity() const { return capacity-(rear+capacity-front)%capacity-1; }

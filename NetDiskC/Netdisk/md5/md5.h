#pragma once
#ifndef MD5_H  
#define MD5_H  
  
#include <string>  
#include <fstream>  
  
/* 类型定义 */  
typedef unsigned char byte;  
typedef unsigned long ulong;  
  
using std::string;  
using std::ifstream;  
  
/* MD5 类声明。 */  
class MD5 {  
public:  
    MD5();  
    MD5(const void *input, size_t length);  
    MD5(const string &str);  
    MD5(ifstream &in);  
    void update(const void *input, size_t length);  
    void update(const string &str);  
    void update(ifstream &in);  
    const byte* digest();  
    string toString();  
    void reset();  
private:  
    void update(const byte *input, size_t length);  
    void final();  
    void transform(const byte block[64]);  
    void encode(const ulong *input, byte *output, size_t length);  
    void decode(const byte *input, ulong *output, size_t length);  
    string bytesToHexString(const byte *input, size_t length);  
  
    /* 禁止拷贝的类 */  
    MD5(const MD5&);  
    MD5& operator=(const MD5&);  
private:  
    ulong _state[4];    /* 状态值（ABCD） */  
    ulong _count[2];    /* number of bits, modulo 2^64 (low-order word first) */  
    byte _buffer[64];   /* 输入缓冲区 */  
    byte _digest[16];   /* 消息摘要 */  
    bool _finished;     /* 是否已计算完成？ */  
  
    static const byte PADDING[64];  /* padding for calculate */  
    static const char HEX[16];  
    static const size_t BUFFER_SIZE = 1024;  
};  
  
#endif/*MD5_H*/

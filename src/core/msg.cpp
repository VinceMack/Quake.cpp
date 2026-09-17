// msg.cpp -- Network buffer and message serialization primitives
#include "quakedef.hpp"
#include "core/msg.hpp"
#include "ui/console.hpp"

namespace Common {

void MSG_WriteChar(sizebuf_t* sb, int c) {
    static_cast<byte*>(SZ_GetSpace(sb, 1))[0] = static_cast<byte>(c);
}

void MSG_WriteByte(sizebuf_t* sb, int c) {
    static_cast<byte*>(SZ_GetSpace(sb, 1))[0] = static_cast<byte>(c);
}

void MSG_WriteShort(sizebuf_t* sb, int c) {
    auto* b = static_cast<byte*>(SZ_GetSpace(sb, 2));
    b[0] = static_cast<byte>(c & 0xff);
    b[1] = static_cast<byte>((c >> 8) & 0xff);
}

void MSG_WriteLong(sizebuf_t* sb, int c) {
    auto* b = static_cast<byte*>(SZ_GetSpace(sb, 4));
    b[0] = static_cast<byte>(c & 0xff);
    b[1] = static_cast<byte>((c >> 8) & 0xff);
    b[2] = static_cast<byte>((c >> 16) & 0xff);
    b[3] = static_cast<byte>((c >> 24) & 0xff);
}

void MSG_WriteFloat(sizebuf_t* sb, float f) {
    int swapped = LittleLong(std::bit_cast<int>(f));
    SZ_Write(sb, &swapped, sizeof(swapped));
}

void MSG_WriteString(sizebuf_t* sb, const char* s) {
    SZ_Write(sb, s ? s : "", s ? Q_strlen(s) + 1 : 1);
}

int msg_readcount = 0;
bool msg_badread = false;

void MSG_BeginReading(void) {
    msg_readcount = 0;
    msg_badread = false;
}

int MSG_ReadChar(void) {
    if (msg_readcount + 1 > Net::net_message.cursize) {
        msg_badread = true;
        return -1;
    }
    return static_cast<int8_t>(Net::net_message.data[msg_readcount++]);
}

int MSG_ReadByte(void) {
    if (msg_readcount + 1 > Net::net_message.cursize) {
        msg_badread = true;
        return -1;
    }
    return Net::net_message.data[msg_readcount++];
}

int MSG_ReadShort(void) {
    if (msg_readcount + 2 > Net::net_message.cursize) {
        msg_badread = true;
        return -1;
    }
    int c = static_cast<int16_t>(Net::net_message.data[msg_readcount] | (Net::net_message.data[msg_readcount + 1] << 8));
    msg_readcount += 2;
    return c;
}

int MSG_ReadLong(void) {
    if (msg_readcount + 4 > Net::net_message.cursize) {
        msg_badread = true;
        return -1;
    }
    int c = static_cast<int>(Net::net_message.data[msg_readcount] | (Net::net_message.data[msg_readcount + 1] << 8) |
                            (Net::net_message.data[msg_readcount + 2] << 16) | (Net::net_message.data[msg_readcount + 3] << 24));
    msg_readcount += 4;
    return c;
}

float MSG_ReadFloat(void) {
    if (msg_readcount + 4 > Net::net_message.cursize) {
        msg_badread = true;
        return -1.0f;
    }
    uint32_t val = 0;
    std::memcpy(&val, &Net::net_message.data[msg_readcount], sizeof(val));
    msg_readcount += 4;
    return std::bit_cast<float>(static_cast<uint32_t>(LittleLong(static_cast<int>(val))));
}

char* MSG_ReadString(void) {
    static char string[2048];
    int l = 0;
    do {
        int c = MSG_ReadChar();
        if (c == -1 || c == 0) break;
        string[l++] = static_cast<char>(c);
    } while (l < static_cast<int>(sizeof(string) - 1));
    string[l] = '\0';
    return string;
}

void SZ_Clear(sizebuf_t* buf) {
    buf->cursize = 0;
}

void* SZ_GetSpace(sizebuf_t* buf, int length) {
    if (buf->cursize + length > buf->maxsize) {
        if (!buf->allowoverflow) Sys_Error("SZ_GetSpace: overflow without allowoverflow set");
        if (length > buf->maxsize) Sys_Error("SZ_GetSpace: %i is > full buffer size", length);
        buf->overflowed = true;
        Console::Con_Printf("SZ_GetSpace: overflow");
        SZ_Clear(buf);
    }
    void* data = buf->data + buf->cursize;
    buf->cursize += length;
    return data;
}

void SZ_Print(sizebuf_t* buf, const char* data) {
    int len = Q_strlen(data) + 1;
    if (buf->data[buf->cursize - 1]) {
        Q_memcpy(SZ_GetSpace(buf, len), data, len);
    } else {
        Q_memcpy(static_cast<byte*>(SZ_GetSpace(buf, len - 1)) - 1, data, len);
    }
}

} // namespace Common

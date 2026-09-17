// msg.hpp -- Network serialization buffers and bitstream read/write primitives
#pragma once

#include "core/types.hpp"
#include "core/string_utils.hpp"
#include "core/endian.hpp"
#include <span>

struct sizebuf_t {
    bool allowoverflow = false;
    bool overflowed = false;
    byte* data = nullptr;
    int maxsize = 0;
    int cursize = 0;

    [[nodiscard]] constexpr std::span<byte> as_span() noexcept { return {data, static_cast<size_t>(cursize)}; }
    [[nodiscard]] constexpr std::span<const byte> as_span() const noexcept { return {data, static_cast<size_t>(cursize)}; }
    [[nodiscard]] constexpr int remaining() const noexcept { return maxsize - cursize; }
    [[nodiscard]] constexpr bool has_overflowed() const noexcept { return overflowed; }
};

namespace Common {

inline void SZ_Init(sizebuf_t* buf, std::span<byte> storage) {
    buf->data = storage.data();
    buf->maxsize = static_cast<int>(storage.size());
    buf->cursize = 0;
}
void SZ_Clear(sizebuf_t* buf);
void* SZ_GetSpace(sizebuf_t* buf, int length);
void SZ_Print(sizebuf_t* buf, const char* data);
inline void SZ_Write(sizebuf_t* buf, const void* data, int length) { Q_memcpy(SZ_GetSpace(buf, length), data, length); }

void MSG_WriteChar(sizebuf_t* sb, int c);
void MSG_WriteByte(sizebuf_t* sb, int c);
void MSG_WriteShort(sizebuf_t* sb, int c);
void MSG_WriteLong(sizebuf_t* sb, int c);
void MSG_WriteFloat(sizebuf_t* sb, float f);
void MSG_WriteString(sizebuf_t* sb, const char* s);
inline void MSG_WriteCoord(sizebuf_t* sb, float f) { MSG_WriteShort(sb, static_cast<int>(f * 8)); }
inline void MSG_WriteAngle(sizebuf_t* sb, float f) { MSG_WriteByte(sb, (static_cast<int>(f) * 256 / 360) & 255); }

extern int msg_readcount;
extern bool msg_badread;

void MSG_BeginReading(void);
int MSG_ReadChar(void);
int MSG_ReadByte(void);
int MSG_ReadShort(void);
int MSG_ReadLong(void);
float MSG_ReadFloat(void);
char* MSG_ReadString(void);

inline float MSG_ReadCoord(void) { return MSG_ReadShort() * (1.0f / 8); }
inline float MSG_ReadAngle(void) { return MSG_ReadChar() * (360.0f / 256); }

} // namespace Common

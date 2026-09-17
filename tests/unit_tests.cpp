// unit_tests.cpp -- Dependency-free unit tests for the engine's core foundation layer
#include "core/endian.hpp"
#include "core/filesystem.hpp"
#include "core/math.hpp"
#include "core/msg.hpp"
#include "core/string_utils.hpp"
#include "network/socket.hpp"

#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(condition)) {                                                  \
            ++g_failures;                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        }                                                                    \
    } while (0)

bool NearlyEqual(float a, float b, float epsilon = 1e-5f) { return std::fabs(a - b) <= epsilon; }

// COM_Init normally selects these based on the host byte order, but it also
// mounts the game data, which the unit tests do not need.
void SelectEndianFunctions() {
    using namespace Common;
    if constexpr (std::endian::native == std::endian::little) {
        bigendien = false;
        BigShort = ShortSwap;
        LittleShort = ShortNoSwap;
        BigLong = LongSwap;
        LittleLong = LongNoSwap;
        BigFloat = FloatSwap;
        LittleFloat = FloatNoSwap;
    } else {
        bigendien = true;
        BigShort = ShortNoSwap;
        LittleShort = ShortSwap;
        BigLong = LongNoSwap;
        LittleLong = LongSwap;
        BigFloat = FloatNoSwap;
        LittleFloat = FloatSwap;
    }
}

void TestByteSwapping() {
    using namespace Common;
    CHECK(ShortSwap(0x1234) == 0x3412);
    CHECK(LongSwap(0x12345678) == 0x78563412);
    CHECK(FloatSwap(FloatSwap(3.25f)) == 3.25f);
    CHECK(std::bit_cast<uint32_t>(FloatSwap(1.0f)) == 0x0000803Fu);
}

void TestMessageRoundTrip() {
    using namespace Common;
    byte buffer[128] = {};
    sizebuf_t sb{};
    sb.data = buffer;
    sb.maxsize = sizeof(buffer);

    MSG_WriteByte(&sb, 200);
    MSG_WriteChar(&sb, -5);
    MSG_WriteShort(&sb, -1234);
    MSG_WriteLong(&sb, 0x12345678);
    MSG_WriteFloat(&sb, 1.5f);
    MSG_WriteString(&sb, "hi");
    MSG_WriteCoord(&sb, 12.5f);
    MSG_WriteAngle(&sb, 90.0f);

    // Wire format is little-endian and byte-exact; this is what other Quake clients read.
    CHECK(buffer[0] == 200);
    CHECK(buffer[1] == static_cast<byte>(-5));
    CHECK(buffer[2] == 0x2E && buffer[3] == 0xFB); // -1234 = 0xFB2E
    CHECK(buffer[4] == 0x78 && buffer[5] == 0x56 && buffer[6] == 0x34 && buffer[7] == 0x12);
    CHECK(sb.cursize == 1 + 1 + 2 + 4 + 4 + 3 + 2 + 1);

    Net::net_message = sb;
    MSG_BeginReading();
    CHECK(MSG_ReadByte() == 200);
    CHECK(MSG_ReadChar() == -5);
    CHECK(MSG_ReadShort() == -1234);
    CHECK(MSG_ReadLong() == 0x12345678);
    CHECK(MSG_ReadFloat() == 1.5f);
    CHECK(std::strcmp(MSG_ReadString(), "hi") == 0);
    CHECK(MSG_ReadCoord() == 12.5f);
    CHECK(MSG_ReadAngle() == 90.0f);
    CHECK(!msg_badread);
    MSG_ReadByte();
    CHECK(msg_badread);
}

void TestParser() {
    using namespace Common;
    const char* text = "{ \"classname\" \"worldspawn\" // trailing comment\n light 300 }";
    const char* expected[] = { "{", "classname", "worldspawn", "light", "300", "}" };
    for (const char* token : expected) {
        text = COM_Parse(text);
        CHECK(text != nullptr);
        CHECK(std::strcmp(com_token, token) == 0);
    }
    CHECK(COM_Parse(text) == nullptr);
}

void TestNumberParsing() {
    using namespace Common;
    CHECK(Q_atoi("-42") == -42);
    CHECK(Q_atoi("0x1F") == 31);
    CHECK(Q_atoi("'A'") == 65);
    CHECK(Q_atoi("12abc") == 12);
    CHECK(Q_atoi("") == 0);
    CHECK(NearlyEqual(Q_atof("3.5"), 3.5f));
    CHECK(NearlyEqual(Q_atof("-0.25"), -0.25f));
    CHECK(NearlyEqual(Q_atof("0x10"), 16.0f));
    CHECK(NearlyEqual(Q_atof("800"), 800.0f));
}

void TestCaseInsensitiveCompare() {
    using namespace Common;
    CHECK(Q_strcasecmp("Quake", "qUAKE") == 0);
    CHECK(Q_strcasecmp("abc", "abd") < 0);
    CHECK(Q_strncasecmp("abcdef", "ABCxyz", 3) == 0);
    CHECK(Q_strcasecmp(eastl::string_view("e1m1"), eastl::string_view("E1M1")) == 0);
}

void TestCrc() {
    using namespace Common;
    std::uint16_t crc = 0;
    CRC_Init(crc);
    for (const char* p = "123456789"; *p; ++p) {
        CRC_ProcessByte(crc, static_cast<byte>(*p));
    }
    CHECK(crc == 0x29B1); // CRC-16/CCITT-FALSE check value
}

void TestVector3() {
    using namespace Math;
    Vector3 a{ 1.0f, 2.0f, 3.0f };
    Vector3 b{ 4.0f, 5.0f, 6.0f };
    CHECK(a.dot(b) == 32.0f);
    CHECK(a.cross(b) == Vector3(-3.0f, 6.0f, -3.0f));
    CHECK((a + b) == Vector3(5.0f, 7.0f, 9.0f));
    CHECK((b - a) == Vector3(3.0f, 3.0f, 3.0f));
    CHECK((a * 2.0f) == Vector3(2.0f, 4.0f, 6.0f));
    Vector3 n{ 0.0f, 3.0f, 4.0f };
    CHECK(n.normalize() == 5.0f);
    CHECK(NearlyEqual(n.length(), 1.0f));

    Vector3 forward, right, up;
    AngleVectors(Vector3(0.0f, 0.0f, 0.0f), forward, right, up);
    CHECK(NearlyEqual(forward.x, 1.0f) && NearlyEqual(forward.y, 0.0f) && NearlyEqual(forward.z, 0.0f));
    CHECK(NearlyEqual(right.y, -1.0f));
    CHECK(NearlyEqual(up.z, 1.0f));

    CHECK(anglemod(370.0f) < 10.01f && anglemod(370.0f) > 9.99f);
    CHECK(anglemod(-10.0f) > 349.99f);
}

} // namespace

int main() {
    SelectEndianFunctions();
    TestByteSwapping();
    TestMessageRoundTrip();
    TestParser();
    TestNumberParsing();
    TestCaseInsensitiveCompare();
    TestCrc();
    TestVector3();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

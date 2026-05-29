#pragma once

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

struct QrMatrix {
    int size = 0;
    std::vector<uint8_t> modules;

    bool empty() const {
        return size <= 0 || modules.empty();
    }

    bool at(int x, int y) const {
        return x >= 0 && y >= 0 && x < size && y < size &&
            modules[static_cast<size_t>(y) * size + x] != 0;
    }
};

namespace qr_detail {

static void AppendBits(std::vector<bool>& bits, uint32_t value, int count) {
    for (int i = count - 1; i >= 0; --i) {
        bits.push_back(((value >> i) & 1) != 0);
    }
}

static uint8_t GfMul(uint8_t x, uint8_t y) {
    int z = 0;
    for (int i = 7; i >= 0; --i) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        if (((y >> i) & 1) != 0) {
            z ^= x;
        }
    }
    return static_cast<uint8_t>(z);
}

static uint8_t GfPow2(int power) {
    uint8_t value = 1;
    for (int i = 0; i < power; ++i) {
        value = GfMul(value, 2);
    }
    return value;
}

static std::vector<uint8_t> ReedSolomonGenerator(int degree) {
    std::vector<uint8_t> result = { 1 };
    for (int i = 0; i < degree; ++i) {
        const uint8_t root = GfPow2(i);
        std::vector<uint8_t> next(result.size() + 1, 0);
        for (size_t j = 0; j < result.size(); ++j) {
            next[j] ^= GfMul(result[j], 1);
            next[j + 1] ^= GfMul(result[j], root);
        }
        result = next;
    }
    return result;
}

static std::vector<uint8_t> ReedSolomonRemainder(
    const std::vector<uint8_t>& data,
    const std::vector<uint8_t>& generator)
{
    const int degree = static_cast<int>(generator.size()) - 1;
    std::vector<uint8_t> result(static_cast<size_t>(degree), 0);
    for (uint8_t b : data) {
        const uint8_t factor = static_cast<uint8_t>(b ^ result[0]);
        std::move(result.begin() + 1, result.end(), result.begin());
        result.back() = 0;
        for (int i = 0; i < degree; ++i) {
            result[static_cast<size_t>(i)] ^= GfMul(generator[static_cast<size_t>(i + 1)], factor);
        }
    }
    return result;
}

static std::vector<uint8_t> MakeVersion3LowCodewords(const std::string& text) {
    constexpr int kDataCodewords = 55;
    constexpr int kEccCodewords = 15;
    constexpr int kCapacityBits = kDataCodewords * 8;

    if (text.size() > 53) {
        return {};
    }

    std::vector<bool> bits;
    AppendBits(bits, 0x4, 4);
    AppendBits(bits, static_cast<uint32_t>(text.size()), 8);
    for (unsigned char c : text) {
        AppendBits(bits, c, 8);
    }

    const int terminator = (std::min)(4, kCapacityBits - static_cast<int>(bits.size()));
    for (int i = 0; i < terminator; ++i) {
        bits.push_back(false);
    }
    while ((bits.size() % 8) != 0) {
        bits.push_back(false);
    }

    std::vector<uint8_t> data;
    data.reserve(kDataCodewords);
    for (size_t i = 0; i < bits.size(); i += 8) {
        uint8_t value = 0;
        for (int j = 0; j < 8; ++j) {
            value = static_cast<uint8_t>((value << 1) | (bits[i + j] ? 1 : 0));
        }
        data.push_back(value);
    }
    for (uint8_t pad = 0xEC; data.size() < kDataCodewords; pad ^= 0xEC ^ 0x11) {
        data.push_back(pad);
    }

    std::vector<uint8_t> result = data;
    const std::vector<uint8_t> generator = ReedSolomonGenerator(kEccCodewords);
    const std::vector<uint8_t> ecc = ReedSolomonRemainder(data, generator);
    result.insert(result.end(), ecc.begin(), ecc.end());
    return result;
}

class QrBuilder {
public:
    QrBuilder() : modules_(kSize * kSize, 0), reserved_(kSize * kSize, 0) {}

    QrMatrix build(const std::string& text) {
        const std::vector<uint8_t> codewords = MakeVersion3LowCodewords(text);
        if (codewords.empty()) {
            return {};
        }

        drawFunctionPatterns();
        drawCodewords(codewords);

        QrMatrix matrix;
        matrix.size = kSize;
        matrix.modules = modules_;
        return matrix;
    }

private:
    static constexpr int kVersion = 3;
    static constexpr int kSize = 4 * kVersion + 17;
    static constexpr int kMask = 0;

    std::vector<uint8_t> modules_;
    std::vector<uint8_t> reserved_;

    void setFunction(int x, int y, bool dark) {
        if (x < 0 || y < 0 || x >= kSize || y >= kSize) {
            return;
        }
        const size_t index = static_cast<size_t>(y) * kSize + x;
        modules_[index] = dark ? 1 : 0;
        reserved_[index] = 1;
    }

    void setData(int x, int y, bool dark) {
        modules_[static_cast<size_t>(y) * kSize + x] = dark ? 1 : 0;
    }

    bool isReserved(int x, int y) const {
        return reserved_[static_cast<size_t>(y) * kSize + x] != 0;
    }

    static bool maskBit(int x, int y) {
        return ((x + y) & 1) == 0;
    }

    void drawFinder(int cx, int cy) {
        for (int y = -4; y <= 4; ++y) {
            for (int x = -4; x <= 4; ++x) {
                const int xx = cx + x;
                const int yy = cy + y;
                const int dist = (std::max)(std::abs(x), std::abs(y));
                setFunction(xx, yy, dist != 2 && dist != 4);
            }
        }
    }

    void drawAlignment(int cx, int cy) {
        for (int y = -2; y <= 2; ++y) {
            for (int x = -2; x <= 2; ++x) {
                const int dist = (std::max)(std::abs(x), std::abs(y));
                setFunction(cx + x, cy + y, dist != 1);
            }
        }
    }

    static int formatBits() {
        constexpr int kErrorCorrectionLow = 1;
        int data = (kErrorCorrectionLow << 3) | kMask;
        int rem = data << 10;
        for (int i = 14; i >= 10; --i) {
            if (((rem >> i) & 1) != 0) {
                rem ^= 0x537 << (i - 10);
            }
        }
        return ((data << 10) | rem) ^ 0x5412;
    }

    void drawFormatBits() {
        const int bits = formatBits();
        auto bit = [&](int i) { return ((bits >> i) & 1) != 0; };

        for (int i = 0; i <= 5; ++i) setFunction(8, i, bit(i));
        setFunction(8, 7, bit(6));
        setFunction(8, 8, bit(7));
        setFunction(7, 8, bit(8));
        for (int i = 9; i < 15; ++i) setFunction(14 - i, 8, bit(i));

        for (int i = 0; i < 8; ++i) setFunction(kSize - 1 - i, 8, bit(i));
        for (int i = 8; i < 15; ++i) setFunction(8, kSize - 15 + i, bit(i));
        setFunction(8, kSize - 8, true);
    }

    void drawFunctionPatterns() {
        drawFinder(3, 3);
        drawFinder(kSize - 4, 3);
        drawFinder(3, kSize - 4);

        for (int i = 8; i < kSize - 8; ++i) {
            const bool dark = (i % 2) == 0;
            setFunction(6, i, dark);
            setFunction(i, 6, dark);
        }

        drawAlignment(22, 22);
        drawFormatBits();
    }

    void drawCodewords(const std::vector<uint8_t>& codewords) {
        size_t bitIndex = 0;
        const size_t bitCount = codewords.size() * 8;
        bool upward = true;

        for (int right = kSize - 1; right >= 1; right -= 2) {
            if (right == 6) {
                --right;
            }
            for (int vert = 0; vert < kSize; ++vert) {
                const int y = upward ? (kSize - 1 - vert) : vert;
                for (int j = 0; j < 2; ++j) {
                    const int x = right - j;
                    if (isReserved(x, y)) {
                        continue;
                    }

                    bool dark = false;
                    if (bitIndex < bitCount) {
                        dark = ((codewords[bitIndex >> 3] >> (7 - (bitIndex & 7))) & 1) != 0;
                        ++bitIndex;
                    }
                    if (maskBit(x, y)) {
                        dark = !dark;
                    }
                    setData(x, y, dark);
                }
            }
            upward = !upward;
        }
    }
};

} // namespace qr_detail

static QrMatrix GenerateLoginQrMatrix(const std::string& text) {
    return qr_detail::QrBuilder().build(text);
}

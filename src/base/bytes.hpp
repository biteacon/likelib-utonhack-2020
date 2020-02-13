#pragma once

#include "types.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace base
{


class Bytes
{
  public:
    //==============
    Bytes();
    explicit Bytes(std::size_t size);
    explicit Bytes(const std::vector<Byte>& bytes);
    explicit Bytes(const std::string& s);
    Bytes(const Byte* bytes, std::size_t length);
    Bytes(std::initializer_list<Byte> l);
    Bytes(const Bytes&) = default;
    Bytes(Bytes&&) = default;
    Bytes& operator=(const Bytes&) = default;
    Bytes& operator=(Bytes&&) = default;
    ~Bytes() = default;
    //==============

    template<typename I>
    Bytes(I begin, I end);
    //==============
    Byte& operator[](std::size_t index);
    const Byte& operator[](std::size_t index) const;
    //==============
    [[nodiscard]] Bytes takePart(std::size_t begin_index, std::size_t one_past_end_index) const;
    //==============
    Bytes& append(Byte byte);
    Bytes& append(const Byte* byte, std::size_t length);

    Bytes& append(const Bytes& bytes);
    //==============
    [[nodiscard]] std::size_t size() const noexcept;
    //==============
    void clear();
    void resize(std::size_t new_size);
    void reserve(std::size_t reserve_size);
    [[nodiscard]] std::size_t capacity() const;
    void shrinkToFit();
    [[nodiscard]] bool isEmpty() const noexcept;
    //==============
    [[nodiscard]] const Byte* toArray() const;
    [[nodiscard]] Byte* toArray();
    //==============
    [[nodiscard]] std::vector<Byte>& toVector() noexcept;
    [[nodiscard]] const std::vector<Byte>& toVector() const noexcept;
    //==============

    [[nodiscard]] std::string toHex() const;

    [[nodiscard]] std::string toString() const;
    //==============
    [[nodiscard]] static Bytes fromHex(const std::string_view& hex_view);
    //==============
    [[nodiscard]] bool operator==(const Bytes& another) const;
    [[nodiscard]] bool operator!=(const Bytes& another) const;

    // lexicographical compare
    [[nodiscard]] bool operator<(const Bytes& another) const;
    [[nodiscard]] bool operator>(const Bytes& another) const;
    [[nodiscard]] bool operator<=(const Bytes& another) const;
    [[nodiscard]] bool operator>=(const Bytes& another) const;
    //==============

  private:
    std::vector<Byte> _raw;
};

base::Bytes operator+(const base::Bytes& a, const base::Bytes& b);

std::ostream& operator<<(std::ostream& os, const Bytes& bytes);

} // namespace base

namespace std
{
template<>
struct hash<base::Bytes>
{
    std::size_t operator()(const base::Bytes& k) const;
};
} // namespace std

#include "bytes.tpp"

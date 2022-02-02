// Copyright 2021-2022 Stefan Karschti
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef INPUTOSM_SPAN_H
#define INPUTOSM_SPAN_H

#include <cstdint>
#include <cstddef>
#include <type_traits>

#include <cassert>

namespace input_osm {

template <typename T>
class span_t
{
public:
    using value_type = T;
    using size_type = size_t;
    using difference_type = int64_t;
    using const_reference = const value_type&;
    using reference = value_type&;
    using const_pointer = const value_type*;
    using pointer = value_type*;
    using const_iterator = const_pointer;
    using iterator = const_pointer;

    span_t() = default;

    span_t(const_pointer data, size_type size)
    : mData{data}
    , mSize{size}
    {
    }

    template <int64_t N>
    span_t(const value_type (&arr)[N])
        : mData{arr}
        , mSize{static_cast<size_type>(N)}
    {
    }

    template <typename Container>
    span_t(const Container& c)
        : mData{c.data()}
        , mSize{static_cast<size_type>(c.size())}
    {
    }

    auto begin() const -> const_iterator
    {
        return cbegin();
    }

    auto end() const -> const_iterator
    {
        return cend();
    }

    auto cbegin() const -> const_iterator
    {
        return mData;
    }

    auto cend() const -> const_iterator
    {
        return mData + mSize;
    }

    auto operator[](size_type index) const -> const_reference
    {
        assert(index < size() && "Span access out of bounds");
        return mData[index];
    }

    auto data() const -> const_pointer
    {
        return mData;
    }

    auto size() const -> size_type
    {
        return mSize;
    }

    bool empty() const
    {
        return 0 == mSize;
    }

private:
    const_pointer mData = nullptr;
    size_type mSize = 0;
};

template <typename T, typename S = typename span_t<T>::size_type>
std::enable_if_t<std::is_integral_v<S>, span_t<T>> makeSpan(const T* data, S size)
{
    return span_t<T>(data, static_cast<typename span_t<T>::size_type>(size));
}

} // namespace inputosm

#endif // INPUTOSM_SPAN_H

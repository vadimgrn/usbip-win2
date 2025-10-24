/*
* Copyright (c) 2022-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
*/

#pragma once

template<typename T>
inline void swap(T &a, T &b)
{
        T tmp(a); // std::move is not available
        a = b;
        b = tmp;
}

template<typename T1, typename T2>
struct pair
{
        using first_type = T1;
        using second_type = T2;

        constexpr pair() = default;

        constexpr pair(const T1 &x, const T2 &y) : first(x), second(y) {}

        template<typename X, typename Y>
        constexpr pair(const X &x, const Y &y) : first(x), second(y) {}

        pair(const pair&) = default;
        pair& operator =(const pair&) = default;

        pair(pair&&) = default;
        pair& operator =(pair&&) = default;

        template<typename X, typename Y>
        constexpr pair(const pair<X, Y> &p) : first(p.first), second(p.second) {}

        template<typename X, typename Y>
        constexpr pair(pair<X, Y>&& p) : first(p.first), second(p.second) {}

        template<typename X, typename Y>
        auto& operator =(const pair<X, Y> &p)
        {
                first = p.first;
                second = p.second;
                return *this;
        }

        template<typename X, typename Y>
        auto& operator =(pair<X, Y>&& p)
        {
                first = p.first;
                second = p.second;
                return *this;
        }

        void swap(pair &p)
        {
                ::swap(first, p.first);
                ::swap(second, p.second);
        }

        template<typename X, typename Y>
        void swap(pair<X, Y> &p)
        {
                ::swap(first, p.first);
                ::swap(second, p.second);
        }

        T1 first{};
        T2 second{};
};

template<typename T1, typename T2, typename U1, typename U2>
inline auto operator ==(const pair<T1, T2> &a, const pair<U1, U2> &b)
{
        return a.first == b.first && a.second == b.second;
}

template<typename T1, typename T2, typename U1, typename U2>
inline auto operator !=(const pair<T1, T2> &a, const pair<U1, U2> &b)
{
        return !(a == b);
}

template<typename T1, typename T2, typename U1, typename U2>
inline void swap(pair<T1, T2> &a, pair<U1, U2> &b)
{
        a.swap(b);
}

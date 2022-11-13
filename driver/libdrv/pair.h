/*
* Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
*/

#pragma once

template<typename T1, typename T2>
struct pair
{
        pair() = default;

        pair(const T1 &x, const T2 &y) : first(x), second(y) {}

        template<typename X, typename Y>
        pair(const X &x, const Y &y) : first(x), second(y) {}

        pair(const pair &p) : first(p.first), second(p.second) {}

        template<typename X, typename Y>
        pair(const pair<X, Y> &p) : first(p.first), second(p.second) {}

        auto& operator =(const pair &p)
        {
                first = p.first;
                second = p.second;
                return *this;
        }

        template<typename X, typename Y>
        auto& operator =(const pair<X, Y> &p)
        {
                first = p.first;
                second = p.second;
                return *this;
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
